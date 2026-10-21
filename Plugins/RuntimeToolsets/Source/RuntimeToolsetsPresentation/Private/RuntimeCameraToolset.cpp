#include "RuntimeCameraToolset.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsActivity.h"
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
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "ShaderCore.h"

namespace RtCam
{
	using namespace PresentationTool;

	const FName ToolCameraTag(TEXT("RuntimeToolsetsCamera"));
	const FName FlyPawnTag(TEXT("RuntimeToolsetsFlyPawn"));
	const TCHAR* FlyControls = TEXT("player keeps control: WASD/QE to fly, mouse to look");

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

	APlayerController* RequirePlayer()
	{
		UWorld* World = RuntimeToolCall::GetWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC || !PC->PlayerCameraManager)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no local player controller with a camera manager"),
				TEXT("Camera tools need a running game or PIE session with a player"));
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

	/** The view as a result struct. */
	FRuntimeCameraView MakeView(const APlayerController* PC, const FPose& Pose)
	{
		FRuntimeCameraView V;
		V.Location = Pose.Location;
		V.Rotation = Pose.Rotation;
		V.Fov = Pose.Fov;
		AActor* ViewTarget = PC->GetViewTarget();
		V.ViewTarget = Ref(ViewTarget);
		V.bToolCamera = ViewTarget && ViewTarget->ActorHasTag(ToolCameraTag);
		V.Pawn = Ref(PC->GetPawn());
		if (V.bToolCamera)
		{
			V.Mode = ERuntimeCameraMode::ToolCamera;
			V.Controls = TEXT("locked tool camera: camera.reset_camera gives the view back");
		}
		else if (PC->GetPawn() && ViewTarget == PC->GetPawn() && CanFly(PC->GetPawn()))
		{
			V.Mode = ERuntimeCameraMode::PlayerPawn;
			V.Controls = FlyControls;
		}
		else
		{
			V.Mode = ERuntimeCameraMode::ViewTarget;
		}
		return V;
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
				if (IsActive(P))
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
	FRuntimeCameraView RunPose(APlayerController* PC, const FPose& Pose, bool bSetFov, float BlendTime, const TCHAR* Label)
	{
		TUniquePtr<IRuntimeCommand> Cmd;
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
			Cmd = MakeUnique<FCameraPoseCommand>(PC, Pose, BlendTime, Label);
		}
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return FRuntimeCameraView();
		}
		return MakeView(PC, Pose);
	}

	bool CheckFov(float Fov)
	{
		if (Fov < 5.f || Fov > 170.f)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("fov %g is outside 5-170 degrees"), Fov));
			return false;
		}
		return true;
	}

	EViewModeIndex ToEngine(ERuntimeViewMode Mode)
	{
		switch (Mode)
		{
		case ERuntimeViewMode::Lit: return VMI_Lit;
		case ERuntimeViewMode::Unlit: return VMI_Unlit;
		case ERuntimeViewMode::Wireframe: return VMI_BrushWireframe;
		case ERuntimeViewMode::LightingOnly: return VMI_LightingOnly;
		case ERuntimeViewMode::Reflections: return VMI_ReflectionOverride;
		case ERuntimeViewMode::ShaderComplexity: return VMI_ShaderComplexity;
		case ERuntimeViewMode::CollisionVisibility: return VMI_CollisionVisibility;
		case ERuntimeViewMode::CollisionPawn: return VMI_CollisionPawn;
		case ERuntimeViewMode::LodColoration: return VMI_LODColoration;
		}
		return VMI_Lit;
	}

	ERuntimeViewMode FromEngine(int32 Index)
	{
		for (int32 i = 0; i <= static_cast<int32>(ERuntimeViewMode::LodColoration); ++i)
		{
			if (ToEngine(static_cast<ERuntimeViewMode>(i)) == Index)
			{
				return static_cast<ERuntimeViewMode>(i);
			}
		}
		return ERuntimeViewMode::Lit;
	}
}

FRuntimeCameraView URuntimeCameraToolset::GetCamera()
{
	using namespace RtCam;
	APlayerController* PC = RequirePlayer();
	return PC ? MakeView(PC, CurrentPose(PC)) : FRuntimeCameraView();
}

FRuntimeCameraView URuntimeCameraToolset::SetCamera(const FVector& Location, const FRotator& Rotation, const FVector& LookAt, const float& Fov, float BlendTime)
{
	using namespace RtCam;
	APlayerController* PC = RequirePlayer();
	if (!PC)
	{
		return {};
	}
	FPose Pose = CurrentPose(PC);
	if (Passed(TEXT("location")))
	{
		Pose.Location = Location;
	}
	if (Passed(TEXT("rotation")))
	{
		Pose.Rotation = Rotation;
	}
	if (Passed(TEXT("look_at")))
	{
		if (LookAt.Equals(Pose.Location, 0.1))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("look_at equals the camera location"), TEXT("Move the camera away from the point or drop look_at"));
			return {};
		}
		Pose.Rotation = (LookAt - Pose.Location).Rotation();
	}
	const bool bFov = Passed(TEXT("fov"));
	if (bFov)
	{
		if (!CheckFov(Fov))
		{
			return {};
		}
		Pose.Fov = Fov;
	}
	return RunPose(PC, Pose, bFov, BlendTime, TEXT("set camera"));
}

FRuntimeCameraView URuntimeCameraToolset::SetFov(float Fov, float BlendTime)
{
	using namespace RtCam;
	APlayerController* PC = RequirePlayer();
	if (!PC || !CheckFov(Fov))
	{
		return {};
	}
	FPose Pose = CurrentPose(PC);
	Pose.Fov = Fov;
	return RunPose(PC, Pose, true, BlendTime, TEXT("set fov"));
}

FRuntimeCameraView URuntimeCameraToolset::FrameNodes(const TArray<AActor*>& Targets, const FVector& Direction, const float& Fov, float Padding, float BlendTime)
{
	using namespace RtCam;
	APlayerController* PC = RequirePlayer();
	if (!PC)
	{
		return {};
	}
	FBox Box(ForceInit);
	for (AActor* A : Targets)
	{
		if (A)
		{
			Box += RuntimeTools::ActorBox(A);
		}
	}
	if (Targets.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("targets is empty"), TEXT("Pass node ids, e.g. [\"n3\"] (scene.find_nodes lists them)"));
		return {};
	}
	if (!Box.IsValid)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("targets have no bounds"));
		return {};
	}
	FPose Pose = CurrentPose(PC);
	const bool bFov = Passed(TEXT("fov"));
	if (bFov)
	{
		if (!CheckFov(Fov))
		{
			return {};
		}
		Pose.Fov = Fov;
	}
	FVector Dir = Passed(TEXT("direction")) ? Direction : FVector::ZeroVector;
	if (Dir.IsNearlyZero())
	{
		Dir = Pose.Rotation.Vector();
	}
	Dir.Normalize();
	const double Radius = FMath::Max(Box.GetExtent().Size(), 10.0) * FMath::Clamp(Padding, 0.5f, 5.f);
	const double HalfFov = FMath::DegreesToRadians(FMath::Clamp(Pose.Fov, 5.f, 170.f) * 0.5);
	const double Distance = Radius / FMath::Sin(HalfFov);
	Pose.Location = Box.GetCenter() - Dir * Distance;
	Pose.Rotation = Dir.Rotation();
	FRuntimeCameraView View = RunPose(PC, Pose, bFov, BlendTime, TEXT("frame nodes"));
	View.Center = Box.GetCenter();
	View.Distance = RuntimeJson::Round(Distance);
	return View;
}

FRuntimeCameraView URuntimeCameraToolset::Orbit(const FVector& Center, const float& Distance, AActor* Target, float Yaw, float Pitch, float BlendTime)
{
	using namespace RtCam;
	APlayerController* PC = RequirePlayer();
	if (!PC)
	{
		return {};
	}
	FVector Pivot;
	if (Target)
	{
		Pivot = RuntimeTools::ActorBox(Target).GetCenter();
	}
	else if (Passed(TEXT("center")))
	{
		Pivot = Center;
	}
	else
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give target or center"), TEXT("e.g. {\"target\":\"n3\",\"yaw\":45}"));
		return {};
	}
	FPose Pose = CurrentPose(PC);
	FVector Offset = Pose.Location - Pivot;
	double Dist = Offset.Size();
	if (Dist < 1.0)
	{
		Offset = -Pose.Rotation.Vector() * 500.0;
		Dist = 500.0;
	}
	if (Passed(TEXT("distance")))
	{
		if (Distance <= 0.f)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("distance must be positive"));
			return {};
		}
		Dist = Distance;
	}
	// Spherical step: yaw around world Z, pitch clamped to avoid flipping over the pole.
	FRotator Dir = Offset.Rotation();
	Dir.Yaw += Yaw;
	Dir.Pitch = FMath::Clamp(Dir.Pitch + Pitch, -89.0, 89.0);
	Pose.Location = Pivot + Dir.Vector() * Dist;
	Pose.Rotation = (Pivot - Pose.Location).Rotation();
	FRuntimeCameraView View = RunPose(PC, Pose, false, BlendTime, TEXT("orbit"));
	View.Center = Pivot;
	View.Distance = RuntimeJson::Round(Dist);
	return View;
}

FRuntimeCameraView URuntimeCameraToolset::ResetCamera(float BlendTime)
{
	using namespace RtCam;
	APlayerController* PC = RequirePlayer();
	if (!PC)
	{
		return {};
	}
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	TWeakObjectPtr<AActor> Old = PC->GetViewTarget();
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	auto DoFn = [WeakPC, BlendTime](FRuntimeCommandContext&, FString& Err)
	{
		if (!WeakPC.IsValid())
		{
			Err = TEXT("player controller no longer exists");
			return false;
		}
		AActor* NewTarget = WeakPC->GetPawn() ? static_cast<AActor*>(WeakPC->GetPawn()) : static_cast<AActor*>(WeakPC.Get());
		WeakPC->SetViewTargetWithBlend(NewTarget, BlendTime);
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
	}
	Cmd->Add(MakeUnique<FLambdaCommand>(TEXT("reset camera"), DoFn, UndoFn, Delta));
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	FRuntimeCameraView View = MakeView(PC, CurrentPose(PC));
	View.Controls = PC->GetPawn() ? TEXT("back to the player's pawn") : TEXT("view on the player controller (no pawn)");
	return View;
}

FRuntimeFlyModeInfo URuntimeCameraToolset::SetFlyMode(bool bEnabled)
{
	using namespace RtCam;
	FRuntimeFlyModeInfo Out;
	APlayerController* PC = RequirePlayer();
	if (!PC)
	{
		return Out;
	}
	if (bEnabled && CanFly(PC->GetPawn()))
	{
		// Already a free-flying pawn: just make sure the view is the pawn's.
		if (PC->GetViewTarget() != PC->GetPawn())
		{
			PC->SetViewTarget(PC->GetPawn());
		}
		PC->SetInputMode(FInputModeGameOnly());
	}
	else if (!RuntimeToolCall::RunCommand(MakeUnique<FFlyModeCommand>(PC, bEnabled)))
	{
		return Out;
	}
	Out.bFlyMode = bEnabled;
	Out.Controls = bEnabled ? TEXT("WASD/QE to fly, mouse to look") : TEXT("back to the player's pawn");
	return Out;
}

FRuntimeFlySpeedInfo URuntimeCameraToolset::SetFlySpeed(const float& Speed, const float& Multiplier, const float& MinSpeed, const float& MaxSpeed, ERuntimeFlySpeedMode Mode)
{
	using namespace RtCam;
	FRuntimeFlySpeedInfo Out;
	for (const TPair<const TCHAR*, float>& P : {TPair<const TCHAR*, float>(TEXT("speed"), Speed), TPair<const TCHAR*, float>(TEXT("multiplier"), Multiplier),
			 TPair<const TCHAR*, float>(TEXT("min_speed"), MinSpeed), TPair<const TCHAR*, float>(TEXT("max_speed"), MaxSpeed)})
	{
		if (Passed(P.Key) && P.Value <= 0.f)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s must be positive (got %g)"), P.Key, P.Value));
			return Out;
		}
	}
	FRuntimeToolsetsActivity::FFlySpeed& Fly = FRuntimeToolsetsActivity::Get().FlySpeed();
	if (Mode != ERuntimeFlySpeedMode::Unchanged)
	{
		Fly.bAuto = Mode == ERuntimeFlySpeedMode::Auto;
	}
	if (Passed(TEXT("speed")))
	{
		Fly.FixedSpeed = Speed;
		Fly.bAuto = Mode == ERuntimeFlySpeedMode::Unchanged ? false : Fly.bAuto;
	}
	if (Passed(TEXT("multiplier")))
	{
		Fly.Multiplier = Multiplier;
	}
	if (Passed(TEXT("min_speed")))
	{
		Fly.MinSpeed = MinSpeed;
	}
	if (Passed(TEXT("max_speed")))
	{
		Fly.MaxSpeed = MaxSpeed;
	}
	Fly.MaxSpeed = FMath::Max(Fly.MinSpeed, Fly.MaxSpeed);
	Out.Mode = Fly.bAuto ? ERuntimeFlySpeedMode::Auto : ERuntimeFlySpeedMode::Fixed;
	Out.Multiplier = Fly.Multiplier;
	Out.MinSpeed = Fly.MinSpeed;
	Out.MaxSpeed = Fly.MaxSpeed;
	Out.FixedSpeed = Fly.FixedSpeed;
	Out.CurrentSpeed = FMath::RoundToFloat(FRuntimeToolsetsActivity::Get().GetCurrentFlySpeed());
	return Out;
}

FRuntimePossessInfo URuntimeCameraToolset::Possess(AActor* Target)
{
	using namespace RtCam;
	FRuntimePossessInfo Out;
	APlayerController* PC = RequirePlayer();
	if (!PC)
	{
		return Out;
	}
	APawn* Pawn = Cast<APawn>(Target);
	if (!Pawn)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is a %s, not a Pawn"), *Ref(Target), Target ? *Target->GetClass()->GetName() : TEXT("nothing")),
			TEXT("Spawn a pawn, e.g. actors.spawn_actor class /Script/Engine.DefaultPawn"));
		return Out;
	}
	if (Pawn->GetController() && Pawn->GetController() != PC)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("pawn is controlled by another controller"));
		return Out;
	}
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	TWeakObjectPtr<APawn> New = Pawn;
	TWeakObjectPtr<APawn> Old = PC->GetPawn();
	Out.Possessed = Ref(Pawn);
	Out.Previous = Ref(PC->GetPawn());
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("possessed"), Out.Possessed);
	auto DoFn = [WeakPC, New](FRuntimeCommandContext&, FString& Err)
	{
		if (!WeakPC.IsValid() || !New.IsValid())
		{
			Err = TEXT("pawn or controller no longer exists");
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
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("possess"), DoFn, UndoFn, Delta)))
	{
		return FRuntimePossessInfo();
	}
	return Out;
}

FRuntimeViewModeInfo URuntimeCameraToolset::SetViewMode(ERuntimeViewMode Mode)
{
	using namespace RtCam;
	FRuntimeViewModeInfo Out;
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
	if (!Viewport)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("no game viewport"), TEXT("View modes need a game window (not a headless or server run)"));
		return Out;
	}
	const EViewModeIndex New = ToEngine(Mode);
	const FString ModeName = StaticEnum<ERuntimeViewMode>()->GetNameStringByValue(static_cast<int64>(Mode));
	if (New != VMI_Lit && !AllowDebugViewmodes())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("view mode '%s' is not available in this build (debug view modes were not cooked)"), *ModeName),
			TEXT("Set r.ForceDebugViewModes=1 under [SystemSettings] in DefaultEngine.ini and re-cook, or use Lit"));
		return Out;
	}
	const EViewModeIndex Old = static_cast<EViewModeIndex>(Viewport->ViewModeIndex);
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("view_mode"), ModeName);
	auto DoFn = [New](FRuntimeCommandContext&, FString& Err)
	{
		if (!GEngine || !GEngine->GameViewport)
		{
			Err = TEXT("no game viewport");
			return false;
		}
		GEngine->GameViewport->SetViewMode(New);
		if (GEngine->GameViewport->ViewModeIndex != New)
		{
			Err = TEXT("the engine rejected this view mode");
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
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("view mode %s"), *ModeName), DoFn, UndoFn, Delta)))
	{
		return Out;
	}
	Out.ViewMode = Mode;
	Out.Previous = FromEngine(Old);
	return Out;
}
