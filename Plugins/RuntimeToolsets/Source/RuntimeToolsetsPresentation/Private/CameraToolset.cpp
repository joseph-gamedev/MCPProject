#include "CameraToolset.h"
#include "RuntimeToolHelpers.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "ShaderCore.h"
#include "RuntimeToolsetsActivity.h"

using namespace RuntimeTools;

namespace
{
	const FName ToolCameraTag(TEXT("RuntimeToolsetsCamera"));
	const FName FlyPawnTag(TEXT("RuntimeToolsetsFlyPawn"));

	/** Spectator pawn -> the pawn the player had before fly mode. */
	TMap<TWeakObjectPtr<APawn>, TWeakObjectPtr<APawn>>& FlyOrigins()
	{
		static TMap<TWeakObjectPtr<APawn>, TWeakObjectPtr<APawn>> Map;
		return Map;
	}

	/** The player's pawn can fly freely (WASD + mouse), so camera moves keep the player in control. */
	bool CanFly(const APawn* Pawn)
	{
		return Pawn && Pawn->IsA<ADefaultPawn>();
	}

	APlayerController* RequirePlayer(FToolInvocation& Inv)
	{
		APlayerController* PC = Inv.GetWorld() ? Inv.GetWorld()->GetFirstPlayerController() : nullptr;
		if (!PC || !PC->PlayerCameraManager)
		{
			Inv.Fail(ERuntimeToolError::WrongState, TEXT("no local player controller with a camera manager"));
			return nullptr;
		}
		return PC;
	}

	ACameraActor* FindToolCamera(UWorld* World)
	{
		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(ToolCameraTag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	struct FPose
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Fov = 90.f;
	};

	/** Current view of the player (whatever the view target is). */
	FPose CurrentPose(const APlayerController* PC)
	{
		FPose P;
		// Flying pawn in control: its eye and the control rotation are the view (the camera cache lags a frame).
		if (const APawn* Pawn = PC->GetPawn(); Pawn && PC->GetViewTarget() == Pawn)
		{
			P.Location = Pawn->GetPawnViewLocation();
			P.Rotation = PC->GetControlRotation();
			P.Fov = PC->PlayerCameraManager->GetFOVAngle();
			return P;
		}
		// The camera manager's cached pose lags a frame behind; read the tool camera directly when it is active.
		if (const ACameraActor* Cam = Cast<ACameraActor>(PC->GetViewTarget()); Cam && Cam->ActorHasTag(ToolCameraTag))
		{
			P.Location = Cam->GetActorLocation();
			P.Rotation = Cam->GetActorRotation();
			P.Fov = Cam->GetCameraComponent()->FieldOfView;
			return P;
		}
		P.Location = PC->PlayerCameraManager->GetCameraLocation();
		P.Rotation = PC->PlayerCameraManager->GetCameraRotation();
		P.Fov = PC->PlayerCameraManager->GetFOVAngle();
		return P;
	}

	TSharedRef<FJsonObject> PoseJson(const FPose& P)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetField(TEXT("location"), RuntimeJson::Vec(P.Location));
		J->SetField(TEXT("rotation"), RuntimeJson::Rot(P.Rotation));
		J->SetNumberField(TEXT("fov"), RuntimeJson::Round(P.Fov));
		return J;
	}

	/**
	 * Moves the tool camera (spawned on first use) to a pose and makes it the view target.
	 * Undo restores the previous view target and the camera's previous pose.
	 */
	class FCameraPoseCommand final : public IRuntimeCommand
	{
	public:
		FCameraPoseCommand(APlayerController* InPC, const FPose& InPose, float InBlend, const FString& InLabel)
			: PC(InPC), NewPose(InPose), Blend(InBlend), Label(InLabel)
		{
		}

		virtual FString GetLabel() const override { return Label; }

		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override
		{
			APlayerController* P = PC.Get();
			if (!P || !Context.World)
			{
				OutError = TEXT("player controller no longer exists");
				return false;
			}
			OldViewTarget = P->GetViewTarget();
			ACameraActor* Cam = FindToolCamera(Context.World);
			if (Cam)
			{
				OldCamPose.Location = Cam->GetActorLocation();
				OldCamPose.Rotation = Cam->GetActorRotation();
				OldCamPose.Fov = Cam->GetCameraComponent()->FieldOfView;
			}
			else
			{
				FActorSpawnParameters Params;
				Params.ObjectFlags |= RF_Transient;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Cam = Context.World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), NewPose.Location, NewPose.Rotation, Params);
				if (!Cam)
				{
					OutError = TEXT("could not spawn the tool camera");
					return false;
				}
				Cam->Tags.Add(ToolCameraTag);
				Cam->GetCameraComponent()->bConstrainAspectRatio = false;
				OldCamPose = NewPose;
			}
			Camera = Cam;
			return Apply(NewPose, Cam, Blend);
		}

		virtual bool Undo(FRuntimeCommandContext& Context) override
		{
			APlayerController* P = PC.Get();
			ACameraActor* Cam = Camera.Get();
			if (!P || !Cam)
			{
				return false;
			}
			SetPose(Cam, OldCamPose);
			if (OldViewTarget.IsValid() && OldViewTarget.Get() != Cam)
			{
				P->SetViewTargetWithBlend(OldViewTarget.Get(), 0.f);
			}
			return true;
		}

		virtual bool Redo(FRuntimeCommandContext& Context) override
		{
			ACameraActor* Cam = Camera.Get();
			return Cam && Apply(NewPose, Cam, 0.f);
		}

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = PoseJson(bForUndo ? OldCamPose : NewPose);
			if (bForUndo && OldViewTarget.IsValid())
			{
				D->SetStringField(TEXT("view_target"), OldViewTarget->GetName());
			}
			return D;
		}

	private:
		static void SetPose(ACameraActor* Cam, const FPose& Pose)
		{
			Cam->SetActorLocationAndRotation(Pose.Location, Pose.Rotation);
			Cam->GetCameraComponent()->SetFieldOfView(Pose.Fov);
		}

		bool Apply(const FPose& Pose, ACameraActor* Cam, float BlendTime)
		{
			APlayerController* P = PC.Get();
			if (!P)
			{
				return false;
			}
			SetPose(Cam, Pose);
			if (P->GetViewTarget() != Cam)
			{
				P->SetViewTargetWithBlend(Cam, BlendTime);
			}
			return true;
		}

		TWeakObjectPtr<APlayerController> PC;
		FPose NewPose;
		float Blend;
		FString Label;
		TWeakObjectPtr<AActor> OldViewTarget;
		TWeakObjectPtr<ACameraActor> Camera;
		FPose OldCamPose;
	};


	/**
	 * Puts the player's own (flying) pawn at a pose: the view stays under player control (WASD + mouse).
	 * Undo restores the pawn, control rotation, FOV and view target.
	 */
	class FPawnPoseCommand final : public IRuntimeCommand
	{
	public:
		FPawnPoseCommand(APlayerController* InPC, const FPose& InPose, bool bInSetFov, const FString& InLabel)
			: PC(InPC), NewPose(InPose), bSetFov(bInSetFov), Label(InLabel)
		{
		}

		virtual FString GetLabel() const override { return Label; }

		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override
		{
			APlayerController* P = PC.Get();
			APawn* Pawn = P ? P->GetPawn() : nullptr;
			if (!Pawn)
			{
				OutError = TEXT("the player has no pawn");
				return false;
			}
			MovedPawn = Pawn;
			OldLocation = Pawn->GetActorLocation();
			OldControl = P->GetControlRotation();
			OldViewTarget = P->GetViewTarget();
			OldLockedFov = P->PlayerCameraManager->GetLockedFOV();
			return Apply(NewPose);
		}

		virtual bool Undo(FRuntimeCommandContext& Context) override
		{
			APlayerController* P = PC.Get();
			APawn* Pawn = MovedPawn.Get();
			if (!P || !Pawn)
			{
				return false;
			}
			Pawn->SetActorLocation(OldLocation, false, nullptr, ETeleportType::TeleportPhysics);
			P->SetControlRotation(OldControl);
			if (OldLockedFov > 0.f)
			{
				P->PlayerCameraManager->SetFOV(OldLockedFov);
			}
			else
			{
				P->PlayerCameraManager->UnlockFOV();
			}
			if (OldViewTarget.IsValid() && OldViewTarget.Get() != P->GetViewTarget())
			{
				P->SetViewTarget(OldViewTarget.Get());
			}
			return true;
		}

		virtual bool Redo(FRuntimeCommandContext& Context) override { return Apply(NewPose); }

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = PoseJson(NewPose);
			D->SetStringField(TEXT("mode"), TEXT("player_pawn"));
			D->SetStringField(TEXT("controls"), TEXT("player keeps control: WASD/QE to fly, mouse to look"));
			return D;
		}

	private:
		bool Apply(const FPose& Pose)
		{
			APlayerController* P = PC.Get();
			APawn* Pawn = MovedPawn.IsValid() ? MovedPawn.Get() : (P ? static_cast<APawn*>(P->GetPawn()) : static_cast<APawn*>(nullptr));
			if (!P || !Pawn)
			{
				return false;
			}
			if (P->GetViewTarget() != Pawn)
			{
				P->SetViewTarget(Pawn);
			}
			const FVector EyeOffset = Pawn->GetPawnViewLocation() - Pawn->GetActorLocation();
			Pawn->SetActorLocation(Pose.Location - EyeOffset, false, nullptr, ETeleportType::TeleportPhysics);
			if (UPawnMovementComponent* Move = Pawn->GetMovementComponent())
			{
				Move->StopMovementImmediately();
			}
			P->SetControlRotation(Pose.Rotation);
			if (bSetFov)
			{
				P->PlayerCameraManager->SetFOV(Pose.Fov);
			}
			return true;
		}

		TWeakObjectPtr<APlayerController> PC;
		FPose NewPose;
		bool bSetFov;
		FString Label;
		TWeakObjectPtr<APawn> MovedPawn;
		FVector OldLocation = FVector::ZeroVector;
		FRotator OldControl = FRotator::ZeroRotator;
		TWeakObjectPtr<AActor> OldViewTarget;
		float OldLockedFov = 0.f;
	};

	/** Switches the player into (or out of) a free-flying spectator pawn; undo switches back. */
	class FFlyModeCommand final : public IRuntimeCommand
	{
	public:
		FFlyModeCommand(APlayerController* InPC, bool bInEnable) : PC(InPC), bEnable(bInEnable) {}

		virtual FString GetLabel() const override { return bEnable ? TEXT("fly mode on") : TEXT("fly mode off"); }
		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override { return Set(Context, bEnable, OutError); }
		virtual bool Undo(FRuntimeCommandContext& Context) override { FString E; return Set(Context, !bEnable, E); }
		virtual bool Redo(FRuntimeCommandContext& Context) override { FString E; return Set(Context, bEnable, E); }

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetBoolField(TEXT("fly_mode"), bForUndo ? !bEnable : bEnable);
			return D;
		}

		/** True when the player is in a spectator created by fly mode. */
		static bool IsActive(const APlayerController* P)
		{
			const APawn* Pawn = P ? P->GetPawn() : nullptr;
			return Pawn && Pawn->ActorHasTag(FlyPawnTag);
		}

	private:
		bool Set(FRuntimeCommandContext& Context, bool bOn, FString& OutError)
		{
			APlayerController* P = PC.Get();
			if (!P || !Context.World)
			{
				OutError = TEXT("player controller no longer exists");
				return false;
			}
			if (bOn)
			{
				if (IsActive(P) || (P->GetPawn() == nullptr && false))
				{
					return true;
				}
				const FPose Pose = CurrentPose(P);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Params.ObjectFlags |= RF_Transient;
				ASpectatorPawn* Fly = Context.World->SpawnActor<ASpectatorPawn>(ASpectatorPawn::StaticClass(), Pose.Location, FRotator(0, Pose.Rotation.Yaw, 0), Params);
				if (!Fly)
				{
					OutError = TEXT("could not spawn a spectator pawn");
					return false;
				}
				Fly->Tags.Add(FlyPawnTag);
				FlyOrigins().Add(Fly, P->GetPawn());
				P->Possess(Fly);
				P->SetControlRotation(Pose.Rotation);
				P->SetInputMode(FInputModeGameOnly());
				return true;
			}
			if (!IsActive(P))
			{
				return true;
			}
			APawn* Fly = P->GetPawn();
			TWeakObjectPtr<APawn> Original;
			FlyOrigins().RemoveAndCopyValue(Fly, Original);
			if (Original.IsValid())
			{
				P->Possess(Original.Get());
			}
			else
			{
				P->UnPossess();
			}
			Fly->Destroy();
			return true;
		}

		TWeakObjectPtr<APlayerController> PC;
		bool bEnable;
	};

	/**
	 * Moves the view. The player keeps control: a flying pawn is moved directly; a walking pawn switches to fly
	 * mode first. Without any pawn a locked tool camera is used.
	 */
	void RunPose(FToolInvocation& Inv, APlayerController* PC, const FPose& Pose, const TCHAR* Label, TSharedPtr<FJsonObject> Extra = nullptr)
	{
		TSharedPtr<FJsonObject> Delta;
		TUniquePtr<IRuntimeCommand> Cmd;
		const bool bSetFov = Inv.Has(TEXT("fov"));
		if (CanFly(PC->GetPawn()))
		{
			Cmd = MakeUnique<FPawnPoseCommand>(PC, Pose, bSetFov, Label);
		}
		else if (PC->GetPawn())
		{
			TUniquePtr<FCompositeCommand> Both = MakeUnique<FCompositeCommand>(Label);
			Both->Add(MakeUnique<FFlyModeCommand>(PC, true));
			Both->Add(MakeUnique<FPawnPoseCommand>(PC, Pose, bSetFov, Label));
			Cmd = MoveTemp(Both);
		}
		else
		{
			Cmd = MakeUnique<FCameraPoseCommand>(PC, Pose, static_cast<float>(Inv.GetNumber(TEXT("blend_time"), 0.0)), Label);
		}
		if (Inv.RunCommand(MoveTemp(Cmd), &Delta))
		{
			if (Extra.IsValid())
			{
				for (const FString& Key : RuntimeJson::Keys(Extra))
				{
					Delta->SetField(Key, Extra->TryGetField(Key));
				}
			}
			Inv.Succeed(Delta);
		}
	}

	const TArray<TPair<FString, EViewModeIndex>>& ViewModes()
	{
		static const TArray<TPair<FString, EViewModeIndex>> Modes = {
			{TEXT("lit"), VMI_Lit},
			{TEXT("unlit"), VMI_Unlit},
			{TEXT("wireframe"), VMI_BrushWireframe},
			{TEXT("lighting_only"), VMI_LightingOnly},
			{TEXT("reflections"), VMI_ReflectionOverride},
			{TEXT("shader_complexity"), VMI_ShaderComplexity},
			{TEXT("collision_visibility"), VMI_CollisionVisibility},
			{TEXT("collision_pawn"), VMI_CollisionPawn},
			{TEXT("lod_coloration"), VMI_LODColoration},
		};
		return Modes;
	}
}

static void GetCamera(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	if (!PC)
	{
		return;
	}
	TSharedRef<FJsonObject> Out = PoseJson(CurrentPose(PC));
	AActor* ViewTarget = PC->GetViewTarget();
	Out->SetStringField(TEXT("view_target"), ViewTarget ? Ref(Inv, ViewTarget) : TEXT(""));
	Out->SetBoolField(TEXT("tool_camera"), ViewTarget && ViewTarget->ActorHasTag(ToolCameraTag));
	if (APawn* Pawn = PC->GetPawn())
	{
		Out->SetStringField(TEXT("pawn"), Ref(Inv, Pawn));
	}
	Inv.Succeed(Out);
}

static void SetCamera(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	if (!PC)
	{
		return;
	}
	FPose Pose = CurrentPose(PC);
	Inv.GetVector(TEXT("location"), Pose.Location);
	Inv.GetRotator(TEXT("rotation"), Pose.Rotation);
	FVector LookAt;
	if (Inv.GetVector(TEXT("look_at"), LookAt))
	{
		if (LookAt.Equals(Pose.Location, 0.1))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("look_at equals the camera location"));
			return;
		}
		Pose.Rotation = (LookAt - Pose.Location).Rotation();
	}
	if (Inv.Has(TEXT("fov")))
	{
		Pose.Fov = static_cast<float>(Inv.GetNumber(TEXT("fov")));
	}
	RunPose(Inv, PC, Pose, TEXT("set camera"));
}

static void FrameNodes(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	TArray<AActor*> Actors;
	if (!PC || !ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	FBox Box(ForceInit);
	for (AActor* A : Actors)
	{
		Box += ActorBox(A);
	}
	if (!Box.IsValid)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("targets have no bounds"));
		return;
	}
	FPose Pose = CurrentPose(PC);
	if (Inv.Has(TEXT("fov")))
	{
		Pose.Fov = static_cast<float>(Inv.GetNumber(TEXT("fov")));
	}
	FVector Direction;
	if (!Inv.GetVector(TEXT("direction"), Direction) || Direction.IsNearlyZero())
	{
		Direction = Pose.Rotation.Vector();
	}
	Direction.Normalize();
	const double Radius = FMath::Max(Box.GetExtent().Size(), 10.0) * Inv.GetNumber(TEXT("padding"), 1.15);
	const double HalfFov = FMath::DegreesToRadians(FMath::Clamp(Pose.Fov, 5.f, 170.f) * 0.5);
	const double Distance = Radius / FMath::Sin(HalfFov);
	Pose.Location = Box.GetCenter() - Direction * Distance;
	Pose.Rotation = Direction.Rotation();

	TSharedRef<FJsonObject> Extra = Obj();
	Extra->SetField(TEXT("center"), RuntimeJson::Vec(Box.GetCenter()));
	Extra->SetNumberField(TEXT("distance"), RuntimeJson::Round(Distance));
	RunPose(Inv, PC, Pose, TEXT("frame nodes"), Extra);
}

static void Orbit(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	if (!PC)
	{
		return;
	}
	FVector Center;
	if (!Inv.GetString(TEXT("target")).IsEmpty())
	{
		AActor* Target = Inv.ResolveActor(TEXT("target"));
		if (!Target)
		{
			return;
		}
		Center = ActorBox(Target).GetCenter();
	}
	else if (!Inv.GetVector(TEXT("center"), Center))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give target or center"));
		return;
	}
	FPose Pose = CurrentPose(PC);
	FVector Offset = Pose.Location - Center;
	double Distance = Offset.Size();
	if (Distance < 1.0)
	{
		Offset = -Pose.Rotation.Vector() * 500.0;
		Distance = 500.0;
	}
	if (Inv.Has(TEXT("distance")))
	{
		Distance = Inv.GetNumber(TEXT("distance"));
	}
	// Spherical step: yaw around world Z, pitch clamped to avoid flipping over the pole.
	FRotator Dir = Offset.Rotation();
	Dir.Yaw += Inv.GetNumber(TEXT("yaw"), 0.0);
	Dir.Pitch = FMath::Clamp(Dir.Pitch + Inv.GetNumber(TEXT("pitch"), 0.0), -89.0, 89.0);
	Pose.Location = Center + Dir.Vector() * Distance;
	Pose.Rotation = (Center - Pose.Location).Rotation();
	RunPose(Inv, PC, Pose, TEXT("orbit"));
}

static void ResetCamera(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	if (!PC)
	{
		return;
	}
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	TWeakObjectPtr<AActor> Old = PC->GetViewTarget();
	const float Blend = static_cast<float>(Inv.GetNumber(TEXT("blend_time"), 0.0));
	TSharedRef<FJsonObject> Delta = Obj();
	AActor* NewTarget = PC->GetPawn() ? static_cast<AActor*>(PC->GetPawn()) : static_cast<AActor*>(PC);
	Delta->SetStringField(TEXT("view_target"), Ref(Inv, NewTarget));
	auto DoFn = [WeakPC, Blend](FRuntimeCommandContext&, FString& Error)
	{
		if (!WeakPC.IsValid())
		{
			Error = TEXT("player controller no longer exists");
			return false;
		}
		AActor* Target = WeakPC->GetPawn() ? static_cast<AActor*>(WeakPC->GetPawn()) : static_cast<AActor*>(WeakPC.Get());
		WeakPC->SetViewTargetWithBlend(Target, Blend);
		return true;
	};
	auto UndoFn = [WeakPC, Old](FRuntimeCommandContext&)
	{
		if (!WeakPC.IsValid() || !Old.IsValid())
		{
			return false;
		}
		WeakPC->SetViewTargetWithBlend(Old.Get(), 0.f);
		return true;
	};
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("reset camera"));
	if (FFlyModeCommand::IsActive(PC))
	{
		Cmd->Add(MakeUnique<FFlyModeCommand>(PC, false));
		Delta->SetBoolField(TEXT("left_fly_mode"), true);
	}
	Cmd->Add(MakeUnique<FLambdaCommand>(TEXT("reset camera"), DoFn, UndoFn, Delta));
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetFlyMode(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	if (!PC)
	{
		return;
	}
	const bool bEnable = Inv.GetBool(TEXT("enabled"), true);
	TSharedRef<FJsonObject> Out = Obj();
	if (bEnable && CanFly(PC->GetPawn()))
	{
		// Already a free-flying pawn: just make sure the view is the pawn's.
		if (PC->GetViewTarget() != PC->GetPawn())
		{
			PC->SetViewTarget(PC->GetPawn());
		}
		PC->SetInputMode(FInputModeGameOnly());
		Out->SetBoolField(TEXT("fly_mode"), true);
		Out->SetStringField(TEXT("controls"), TEXT("WASD/QE to fly, mouse to look"));
		Inv.Succeed(Out);
		return;
	}
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MakeUnique<FFlyModeCommand>(PC, bEnable), &Delta))
	{
		Delta->SetStringField(TEXT("controls"), bEnable ? TEXT("WASD/QE to fly, mouse to look") : TEXT("back to the player's pawn"));
		Inv.Succeed(Delta);
	}
}

static void SetFlySpeed(FToolInvocation& Inv)
{
	FRuntimeToolsetsActivity::FFlySpeed& Fly = FRuntimeToolsetsActivity::Get().FlySpeed();
	if (Inv.Has(TEXT("mode")))
	{
		Fly.bAuto = Inv.GetString(TEXT("mode")) == TEXT("auto");
	}
	if (Inv.Has(TEXT("speed")))
	{
		Fly.FixedSpeed = Inv.GetNumber(TEXT("speed"));
		Fly.bAuto = Inv.Has(TEXT("mode")) ? Fly.bAuto : false;
	}
	Fly.Multiplier = Inv.GetNumber(TEXT("multiplier"), Fly.Multiplier);
	Fly.MinSpeed = Inv.GetNumber(TEXT("min_speed"), Fly.MinSpeed);
	Fly.MaxSpeed = FMath::Max(Fly.MinSpeed, Inv.GetNumber(TEXT("max_speed"), Fly.MaxSpeed));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("mode"), Fly.bAuto ? TEXT("auto") : TEXT("fixed"));
	Out->SetNumberField(TEXT("multiplier"), Fly.Multiplier);
	Out->SetNumberField(TEXT("min_speed"), Fly.MinSpeed);
	Out->SetNumberField(TEXT("max_speed"), Fly.MaxSpeed);
	Out->SetNumberField(TEXT("fixed_speed"), Fly.FixedSpeed);
	Out->SetNumberField(TEXT("current_speed"), FMath::RoundToDouble(FRuntimeToolsetsActivity::Get().GetCurrentFlySpeed()));
	Inv.Succeed(Out);
}

static void Possess(FToolInvocation& Inv)
{
	APlayerController* PC = RequirePlayer(Inv);
	if (!PC)
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(Inv.ResolveActor(TEXT("target")));
	if (!Pawn)
	{
		if (!Inv.IsComplete())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a pawn"));
		}
		return;
	}
	if (Pawn->GetController() && Pawn->GetController() != PC)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("pawn is controlled by another controller"));
		return;
	}
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	TWeakObjectPtr<APawn> New = Pawn;
	TWeakObjectPtr<APawn> Old = PC->GetPawn();
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("possessed"), Ref(Inv, Pawn));
	auto DoFn = [WeakPC, New](FRuntimeCommandContext&, FString& Error)
	{
		if (!WeakPC.IsValid() || !New.IsValid())
		{
			Error = TEXT("pawn or controller no longer exists");
			return false;
		}
		WeakPC->Possess(New.Get());
		return true;
	};
	auto UndoFn = [WeakPC, Old](FRuntimeCommandContext&)
	{
		if (!WeakPC.IsValid())
		{
			return false;
		}
		if (Old.IsValid())
		{
			WeakPC->Possess(Old.Get());
		}
		else
		{
			WeakPC->UnPossess();
		}
		return true;
	};
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("possess"), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetViewMode(FToolInvocation& Inv)
{
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
	if (!Viewport)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("no game viewport"));
		return;
	}
	const FString Mode = Inv.GetString(TEXT("mode"));
	const TPair<FString, EViewModeIndex>* Found = ViewModes().FindByPredicate([&Mode](const TPair<FString, EViewModeIndex>& P) { return P.Key == Mode; });
	if (!Found)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown view mode '%s'"), *Mode));
		return;
	}
	const EViewModeIndex New = Found->Value;
	if (New != VMI_Lit && !AllowDebugViewmodes())
	{
		Inv.Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("view mode '%s' is not available in this build (debug view modes were not cooked)"), *Mode),
			TEXT("Set r.ForceDebugViewModes=1 under [SystemSettings] in DefaultEngine.ini and re-cook, or use 'lit'"));
		return;
	}
	const EViewModeIndex Old = static_cast<EViewModeIndex>(Viewport->ViewModeIndex);
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("view_mode"), Mode);
	auto DoFn = [New](FRuntimeCommandContext&, FString& Error)
	{
		if (!GEngine || !GEngine->GameViewport)
		{
			Error = TEXT("no game viewport");
			return false;
		}
		GEngine->GameViewport->SetViewMode(New);
		if (GEngine->GameViewport->ViewModeIndex != New)
		{
			Error = TEXT("the engine rejected this view mode");
			return false;
		}
		return true;
	};
	auto UndoFn = [Old](FRuntimeCommandContext&)
	{
		if (!GEngine || !GEngine->GameViewport)
		{
			return false;
		}
		GEngine->GameViewport->SetViewMode(Old);
		return true;
	};
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("view mode %s"), *Mode), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

void FCameraToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Blend = []() { return FToolParam::Number(TEXT("Blend seconds"), 0.0, 10.0).Default(0.0); };
	const auto Fov = []() { return FToolParam::Number(TEXT("Horizontal FOV degrees"), 5.0, 170.0); };

	B.Tool(TEXT("get_camera"), TEXT("Current player view: location, rotation, fov, view target, pawn."))
		.RequiresWorld()
		.Handle(&GetCamera);

	B.Tool(TEXT("set_camera"), TEXT("Move the player's view to a pose (missing parts keep the current view). The player keeps fly control."))
		.Args(FToolSchema()
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]")))
			.Add(TEXT("look_at"), FToolParam::Vec3(TEXT("Point to look at (overrides rotation)")))
			.Add(TEXT("fov"), Fov())
			.Add(TEXT("blend_time"), Blend()))
		.Undoable().RequiresWorld()
		.Handle(&SetCamera);

	B.Tool(TEXT("set_fov"), TEXT("Change the field of view, keeping the current pose."))
		.Args(FToolSchema().Add(TEXT("fov"), Fov().Required()).Add(TEXT("blend_time"), Blend()))
		.Undoable().RequiresWorld()
		.Handle(&SetCamera);

	B.Tool(TEXT("frame_nodes"), TEXT("Point the view so all targets fit on screen."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Targets to frame"), 256).Required())
			.Add(TEXT("direction"), FToolParam::Vec3(TEXT("View direction (default: current)")))
			.Add(TEXT("padding"), FToolParam::Number(TEXT("Margin factor"), 0.5, 5.0).Default(1.15))
			.Add(TEXT("fov"), Fov())
			.Add(TEXT("blend_time"), Blend()))
		.Undoable().RequiresWorld()
		.Handle(&FrameNodes);

	B.Tool(TEXT("orbit"), TEXT("Orbit the view around a target or point by yaw/pitch degrees."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Orbit around this actor's bounds centre")))
			.Add(TEXT("center"), FToolParam::Vec3(TEXT("Or orbit around this point")))
			.Add(TEXT("yaw"), FToolParam::Number(TEXT("Degrees around Z")).Default(0.0))
			.Add(TEXT("pitch"), FToolParam::Number(TEXT("Degrees up/down")).Default(0.0))
			.Add(TEXT("distance"), FToolParam::Number(TEXT("New distance cm"), 1.0))
			.Add(TEXT("blend_time"), Blend()))
		.Undoable().RequiresWorld()
		.Handle(&Orbit);

	B.Tool(TEXT("reset_camera"), TEXT("Give the view back to the player's pawn and leave fly mode."))
		.Args(FToolSchema().Add(TEXT("blend_time"), Blend()))
		.Undoable().RequiresWorld()
		.Handle(&ResetCamera);

	B.Tool(TEXT("set_fly_mode"), TEXT("Let the player fly freely (WASD/QE + mouse) from the current view, or return to their pawn."))
		.Args(FToolSchema().Add(TEXT("enabled"), FToolParam::Boolean(TEXT("Fly mode on")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&SetFlyMode);

	B.Tool(TEXT("set_fly_speed"), TEXT("Fly speed: 'auto' scales with distance to the nearest surface (fast far away, slow up close) or 'fixed'."))
		.Args(FToolSchema()
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("auto or fixed"), {TEXT("auto"), TEXT("fixed")}))
			.Add(TEXT("speed"), FToolParam::Number(TEXT("Fixed speed cm/s"), 10.0, 1000000.0))
			.Add(TEXT("multiplier"), FToolParam::Number(TEXT("Scales either mode"), 0.01, 100.0))
			.Add(TEXT("min_speed"), FToolParam::Number(TEXT("Auto: slowest cm/s"), 10.0, 1000000.0))
			.Add(TEXT("max_speed"), FToolParam::Number(TEXT("Auto: fastest cm/s"), 10.0, 10000000.0)))
		.Handle(&SetFlySpeed);

	B.Tool(TEXT("possess"), TEXT("Make the local player control a pawn."))
		.Args(FToolSchema().Add(TEXT("target"), FToolParam::Target(TEXT("Pawn")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&Possess);

	TArray<FString> ModeNames;
	for (const TPair<FString, EViewModeIndex>& P : ViewModes())
	{
		ModeNames.Add(P.Key);
	}
	B.Tool(TEXT("set_view_mode"), TEXT("Viewport render mode (lit, unlit, wireframe, ...)."))
		.Args(FToolSchema().Add(TEXT("mode"), FToolParam::Enum(TEXT("View mode"), ModeNames).Required()))
		.Undoable()
		.Handle(&SetViewMode);
}
