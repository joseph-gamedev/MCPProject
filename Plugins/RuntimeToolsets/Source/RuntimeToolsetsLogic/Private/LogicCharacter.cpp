// Generic character (character.create): movement, key bindings, climbing and the camera rig are all data, no genre preset.
// Also generic contact events between any objects (logic.watch_contacts).

#include "LogicRuntime.h"
#include "LogicJsonHelpers.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace LogicCharacterPrivate
{
	FVector CharVec(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FVector& Default)
	{
		FVector V = Default;
		if (J.IsValid())
		{
			RuntimeJson::ToVector(J->TryGetField(Field), V);
		}
		return V;
	}

	TSharedPtr<FJsonObject> CharObj(const TSharedPtr<FJsonObject>& J, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return J.IsValid() && J->TryGetObjectField(Field, O) ? *O : nullptr;
	}

	/** Box of a scene node's colliding parts (or all parts when nothing collides). */
	bool CharNodeBox(UWorld* W, const FString& Id, FBox& Out)
	{
		FRuntimeSceneGraph* G = W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
		const FRuntimeSceneNode* N = G ? G->Find(Id) : nullptr;
		AActor* A = N ? N->Actor.Get() : nullptr;
		if (!A || A->IsHidden())
		{
			return false;
		}
		Out = A->GetComponentsBoundingBox(true, true);
		return Out.IsValid != 0;
	}
}

using namespace LogicCharacterPrivate;

bool FLogicWorld::IsCustom() const
{
	return PD().IsValid() && Str(PD(), TEXT("mode")) == TEXT("custom");
}

void FLogicWorld::ApplyCustomMovement(ACharacter* C)
{
	UCharacterMovementComponent* M = C->GetCharacterMovement();
	const TSharedPtr<FJsonObject> Mv = CharObj(PD(), TEXT("movement"));
	M->MaxWalkSpeed = Num(Mv, TEXT("walk_speed"), 600.0);
	M->MaxAcceleration = Num(Mv, TEXT("acceleration"), 3000.0);
	M->BrakingDecelerationWalking = Num(Mv, TEXT("braking"), 2600.0);
	M->GroundFriction = 9.f;
	M->JumpZVelocity = Num(Mv, TEXT("jump_velocity"), 900.0);
	M->GravityScale = Num(Mv, TEXT("gravity_scale"), 2.0);
	M->AirControl = Num(Mv, TEXT("air_control"), 0.8);
	M->MaxStepHeight = Num(Mv, TEXT("max_step"), 40.0);
	M->SetWalkableFloorAngle(Num(Mv, TEXT("max_slope"), 46.0));
	C->JumpMaxHoldTime = Num(Mv, TEXT("jump_hold"), 0.2);
	const FString FacingMode = Str(PD(), TEXT("facing"), TEXT("movement"));
	M->bOrientRotationToMovement = FacingMode == TEXT("movement");
	M->RotationRate = FRotator(0.f, Num(Mv, TEXT("turn_rate"), 1500.0), 0.f);
	const FVector Plane = CharVec(PD(), TEXT("plane_normal"), FVector::ZeroVector);
	M->SetPlaneConstraintEnabled(!Plane.IsNearlyZero());
	if (!Plane.IsNearlyZero())
	{
		M->SetPlaneConstraintNormal(Plane.GetSafeNormal());
	}
	bClimbing = false;
	ClimbNode.Reset();
}

void FLogicWorld::StopClimbing(ACharacter* C, bool bJump)
{
	if (!bClimbing || !C)
	{
		return;
	}
	bClimbing = false;
	ClimbNode.Reset();
	UCapsuleComponent* Cap = C->GetCapsuleComponent();
	Cap->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Cap->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	C->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	if (bJump)
	{
		C->Jump();
	}
	FLogicEvent E;
	E.On = TEXT("climb");
	E.Name = TEXT("end");
	Queue.Add(MoveTemp(E));
}

void FLogicWorld::TickCustomInput(APlayerController* PC, ACharacter* C)
{
	UCharacterMovementComponent* M = C->GetCharacterMovement();
	const TSharedPtr<FJsonObject> Mv = CharObj(PD(), TEXT("movement"));
	FVector Move = FVector::ZeroVector;
	double Climb = 0.0;
	bool bRun = false, bJumpPressed = false, bJumpReleased = false;
	const TArray<TSharedPtr<FJsonValue>>* Controls = nullptr;
	if (PD()->TryGetArrayField(TEXT("controls"), Controls))
	{
		for (const TSharedPtr<FJsonValue>& V : *Controls)
		{
			const TSharedPtr<FJsonObject> Ctl = V->AsObject();
			if (!Ctl.IsValid())
			{
				continue;
			}
			const FKey Key(*Str(Ctl, TEXT("key")));
			if (!Key.IsValid())
			{
				continue;
			}
			const FString Action = Str(Ctl, TEXT("action")).ToLower();
			const bool bDown = Key.IsAnalog() ? FMath::Abs(PC->GetInputAnalogKeyState(Key)) > 0.25f : PC->IsInputKeyDown(Key);
			const FVector Dir = CharVec(Ctl, TEXT("direction"), FVector::ZeroVector);
			if (Action == TEXT("move") && bDown)
			{
				Move += Key.IsAnalog() ? Dir * PC->GetInputAnalogKeyState(Key) : Dir;
			}
			else if (Action == TEXT("climb") && bDown)
			{
				Climb += Dir.Z != 0.0 ? FMath::Sign(Dir.Z) : 1.0;
			}
			else if (Action == TEXT("run"))
			{
				bRun |= bDown;
			}
			else if (Action == TEXT("jump"))
			{
				bJumpPressed |= PC->WasInputKeyJustPressed(Key);
				bJumpReleased |= PC->WasInputKeyJustReleased(Key);
			}
			else if (Action == TEXT("fire") || Action == TEXT("fire_secondary"))
			{
				const FString Weapon = Action == TEXT("fire") ? EquippedWeapon(TEXT("player")) : Str(PD(), TEXT("secondary"));
				const TSharedPtr<FJsonObject>* WDef = Weapon.IsEmpty() ? nullptr : WeaponDefs.Find(Weapon);
				bool bAuto = false;
				if (WDef)
				{
					(*WDef)->TryGetBoolField(TEXT("auto"), bAuto);
				}
				if (WDef && (bAuto ? bDown : PC->WasInputKeyJustPressed(Key)))
				{
					FString Err;
					FireWeapon(TEXT("player"), Action == TEXT("fire") ? FString() : Weapon, nullptr, Err);
				}
			}
		}
	}

	// Climbing: inside a climbable object, climb input moves along Z with collision off (so the hero passes through the
	// floor it climbs to) and snaps to the climbable's center; leaving either end puts the hero back on its feet.
	const double Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const double Radius = C->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector P = C->GetActorLocation();
	const double Feet = P.Z - Half;
	FString InId;
	FBox InBox(ForceInit);
	const TArray<TSharedPtr<FJsonValue>>* Climbables = nullptr;
	if (PD()->TryGetArrayField(TEXT("climbables"), Climbables))
	{
		for (const TSharedPtr<FJsonValue>& V : *Climbables)
		{
			FBox B;
			if (CharNodeBox(World.Get(), V->AsString(), B) && FMath::Abs(P.X - B.GetCenter().X) <= B.GetExtent().X + Radius * 0.6
				&& FMath::Abs(P.Y - B.GetCenter().Y) <= B.GetExtent().Y + Radius && Feet >= B.Min.Z - 25.0 && Feet <= B.Max.Z + 25.0)
			{
				InId = V->AsString();
				InBox = B;
				break;
			}
		}
	}
	const double ClimbSpeed = Num(Mv, TEXT("climb_speed"), 300.0);
	if (!bClimbing && !InId.IsEmpty() && Climb != 0.0 && (Climb > 0.0 ? Feet < InBox.Max.Z - 10.0 : Feet > InBox.Min.Z + 10.0))
	{
		bClimbing = true;
		ClimbNode = InId;
		UCapsuleComponent* Cap = C->GetCapsuleComponent();
		Cap->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
		Cap->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
		M->SetMovementMode(MOVE_Flying);
		FLogicEvent E;
		E.On = TEXT("climb");
		E.Name = TEXT("start");
		E.Target = InId;
		Queue.Add(MoveTemp(E));
	}
	if (bClimbing)
	{
		FBox B;
		if (!CharNodeBox(World.Get(), ClimbNode, B))
		{
			StopClimbing(C, false);
			return;
		}
		if (bJumpPressed)
		{
			StopClimbing(C, true);
			return;
		}
		const FVector Center = B.GetCenter();
		const double NewFeet = FMath::Clamp(Feet + Climb * ClimbSpeed * FMath::Max(0.0, static_cast<double>(PC->GetWorld()->GetDeltaSeconds())), B.Min.Z, B.Max.Z);
		const double Snap = FMath::Min(1.0, PC->GetWorld()->GetDeltaSeconds() * 14.0);
		const bool bThinX = B.GetExtent().X <= B.GetExtent().Y;
		FVector L(bThinX ? FMath::Lerp(P.X, Center.X, Snap) : P.X, bThinX ? P.Y : FMath::Lerp(P.Y, Center.Y, Snap), NewFeet + Half);
		C->SetActorLocation(L, false, nullptr, ETeleportType::TeleportPhysics);
		M->Velocity = FVector::ZeroVector;
		AnimPhase += Climb != 0.0 ? PC->GetWorld()->GetDeltaSeconds() * 10.0 : 0.0;
		if ((Climb > 0.0 && NewFeet >= B.Max.Z - 0.5) || (Climb < 0.0 && NewFeet <= B.Min.Z + 0.5))
		{
			C->SetActorLocation(FVector(L.X, L.Y, NewFeet + Half + 3.0), false, nullptr, ETeleportType::TeleportPhysics);
			StopClimbing(C, false);
		}
		return;
	}

	const double Walk = Num(Mv, TEXT("walk_speed"), 600.0);
	M->MaxWalkSpeed = static_cast<float>(bRun ? Num(Mv, TEXT("run_speed"), Walk * 1.6) : Walk);
	const FString FacingMode = Str(PD(), TEXT("facing"), TEXT("movement"));
	if (FacingMode == TEXT("mouse"))
	{
		FVector WL, WD;
		if (PC->DeprojectMousePositionToWorld(WL, WD) && FMath::Abs(WD.Z) > 1e-3)
		{
			const double T = (P.Z - WL.Z) / WD.Z;
			if (T > 0)
			{
				AimPoint = WL + WD * T;
				const FVector D = (AimPoint - P).GetSafeNormal2D();
				if (!D.IsNearlyZero())
				{
					C->SetActorRotation(FRotator(0.0, D.Rotation().Yaw, 0.0));
				}
			}
		}
	}
	else if (FacingMode == TEXT("fixed"))
	{
		C->SetActorRotation(FRotator(0.0, Num(PD(), TEXT("yaw"), 0.0), 0.0));
	}
	if (!Move.IsNearlyZero())
	{
		C->AddMovementInput(Move.GetClampedToMaxSize(1.0), 1.f, true);
	}
	AimDir = C->GetActorForwardVector();
	Facing = AimDir.X >= 0.0 ? 1.f : -1.f;
	if (bJumpPressed)
	{
		C->Jump();
	}
	if (bJumpReleased)
	{
		C->StopJumping();
	}
}

void FLogicWorld::TickCustomCamera(double Dt, bool bSnap)
{
	ACharacter* C = Player.Get();
	ACameraActor* Cam = PlayerCamera.Get();
	if (!C || !Cam)
	{
		return;
	}
	const TSharedPtr<FJsonObject> Rig = CharObj(PD(), TEXT("camera"));
	const FString Mode = Str(Rig, TEXT("mode"), TEXT("follow")).ToLower();
	const FVector P = C->GetActorLocation();
	const FVector Offset = CharVec(Rig, TEXT("offset"), FVector(0.0, -1600.0, 300.0));
	const FVector Fixed = CharVec(Rig, TEXT("location"), SpawnPoint + Offset);
	FVector Goal = Mode == TEXT("fixed") ? Fixed : P + Offset;
	const TArray<TSharedPtr<FJsonValue>>* Lock = nullptr;
	if (Mode != TEXT("fixed") && Rig.IsValid() && Rig->TryGetArrayField(TEXT("lock_axes"), Lock))
	{
		for (const TSharedPtr<FJsonValue>& A : *Lock)
		{
			const FString Axis = A->AsString().ToLower();
			if (Axis == TEXT("x")) Goal.X = Fixed.X;
			if (Axis == TEXT("y")) Goal.Y = Fixed.Y;
			if (Axis == TEXT("z")) Goal.Z = Fixed.Z;
		}
	}
	const double Lag = Num(Rig, TEXT("lag"), 0.12);
	CamPos = bSnap || Dt <= 0.0 || Lag <= 0.0 ? Goal : FMath::VInterpTo(CamPos, Goal, Dt, 1.0 / Lag);
	FRotator Rot;
	bool bLookAt = true;
	if (Rig.IsValid())
	{
		Rig->TryGetBoolField(TEXT("look_at"), bLookAt);
	}
	if (!bLookAt && Rig.IsValid() && RuntimeJson::ToRotator(Rig->TryGetField(TEXT("rotation")), Rot))
	{
	}
	else
	{
		const FVector LookTarget = (Mode == TEXT("fixed") ? CharVec(Rig, TEXT("look_at_point"), P) : P) + CharVec(Rig, TEXT("look_offset"), FVector::ZeroVector);
		Rot = (LookTarget - CamPos).Rotation();
	}
	Cam->SetActorLocationAndRotation(CamPos + ShakeOffset(), Rot);
	UCameraComponent* CC = Cam->GetCameraComponent();
	const double Ortho = Num(Rig, TEXT("ortho_width"), 0.0);
	if (Ortho > 0.0)
	{
		CC->SetProjectionMode(ECameraProjectionMode::Orthographic);
		CC->SetOrthoWidth(static_cast<float>(Ortho));
	}
	else
	{
		CC->SetProjectionMode(ECameraProjectionMode::Perspective);
		CC->SetFieldOfView(static_cast<float>(Num(Rig, TEXT("fov"), 55.0)));
	}
}

void FLogicWorld::TickContacts()
{
	UWorld* W = World.Get();
	if (!W || ContactDefs.Num() == 0)
	{
		return;
	}
	auto Boxes = [this, W](const TSharedPtr<FJsonObject>& Def, const TCHAR* Field, TArray<TPair<FString, FBox>>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Specs = nullptr;
		if (!Def->TryGetArrayField(Field, Specs))
		{
			return;
		}
		TArray<FString> Targets;
		for (const TSharedPtr<FJsonValue>& V : *Specs)
		{
			const FString S = V->AsString();
			if (S == TEXT("player"))
			{
				if (ACharacter* C = Player.Get())
				{
					Out.Emplace(TEXT("player"), C->GetCapsuleComponent()->Bounds.GetBox());
				}
			}
			else
			{
				Targets.Add(S);
			}
		}
		TArray<FString> Ids;
		FString Err;
		if (Targets.Num() > 0 && ResolveNodes(Targets, Ids, Err))
		{
			for (const FString& Id : Ids)
			{
				FBox B;
				if (!IsHidden(Id) && CharNodeBox(W, Id, B))
				{
					Out.Emplace(Id, B);
				}
			}
		}
	};
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : ContactDefs)
	{
		TArray<TPair<FString, FBox>> A, B;
		Boxes(P.Value, TEXT("a"), A);
		Boxes(P.Value, TEXT("b"), B);
		const double Margin = Num(P.Value, TEXT("margin"), 0.0);
		TSet<FString>& Active = ContactActive.FindOrAdd(P.Key);
		TSet<FString> Now;
		for (const TPair<FString, FBox>& X : A)
		{
			for (const TPair<FString, FBox>& Y : B)
			{
				if (X.Key != Y.Key && X.Value.ExpandBy(Margin).Intersect(Y.Value))
				{
					const FString Pair = X.Key + TEXT("|") + Y.Key;
					Now.Add(Pair);
					if (!Active.Contains(Pair))
					{
						FLogicEvent E;
						E.On = TEXT("contact");
						E.Name = P.Key;
						E.Target = X.Key == TEXT("player") ? FString() : X.Key;
						E.Data.M->Add(TEXT("other"), FLogicValue::MakeString(Y.Key));
						E.Data.M->Add(TEXT("self"), FLogicValue::MakeString(X.Key));
						const FVector D = Y.Value.GetCenter() - X.Value.GetCenter();
						E.Data.M->Add(TEXT("from_above"), FLogicValue::MakeBool(D.Z < -(X.Value.GetExtent().Z + Y.Value.GetExtent().Z) * 0.5));
						Queue.Add(MoveTemp(E));
					}
				}
			}
		}
		Active = MoveTemp(Now);
	}
}
