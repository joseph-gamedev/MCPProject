// Platformer play: a playable character with a side camera, touch events, moving objects, hide/show and solidity.
#include "LogicTuning.h"
#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsCore.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"

namespace
{
	double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, double Default)
	{
		double V = Default;
		if (J.IsValid())
		{
			J->TryGetNumberField(Field, V);
		}
		return V;
	}

	FString Str(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Default = FString())
	{
		FString V = Default;
		if (J.IsValid())
		{
			J->TryGetStringField(Field, V);
		}
		return V;
	}

	FVector Vec(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FVector& Default)
	{
		FVector V = Default;
		if (J.IsValid())
		{
			RuntimeJson::ToVector(J->TryGetField(Field), V);
		}
		return V;
	}

	FRuntimeSceneGraph* GraphOf(UWorld* W)
	{
		return W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	}

	void ForSubtreeActors(FRuntimeSceneGraph& Graph, const FString& Id, TFunctionRef<void(AActor*)> Fn)
	{
		for (const FString& Sub : Graph.CollectSubtree(Id))
		{
			if (const FRuntimeSceneNode* N = Graph.Find(Sub))
			{
				if (AActor* A = N->Actor.Get())
				{
					Fn(A);
				}
			}
		}
	}

	TSharedPtr<FJsonValue> Part(double S, const TCHAR* Shape, std::initializer_list<double> Size, std::initializer_list<double> Loc,
		const TCHAR* Material, std::initializer_list<double> Color)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("shape"), Shape);
		TArray<TSharedPtr<FJsonValue>> Sz, Lc, Cl;
		for (double V : Size) Sz.Add(MakeShared<FJsonValueNumber>(V * S));
		for (double V : Loc) Lc.Add(MakeShared<FJsonValueNumber>(V * S));
		for (double V : Color) Cl.Add(MakeShared<FJsonValueNumber>(V));
		P->SetArrayField(TEXT("size"), Sz);
		P->SetArrayField(TEXT("location"), Lc);
		P->SetArrayField(TEXT("color"), Cl);
		P->SetStringField(TEXT("material"), Material);
		return MakeShared<FJsonValueObject>(P);
	}

	TSharedRef<FJsonObject> Assembly(const FString& Name, TArray<TSharedPtr<FJsonValue>> Parts)
	{
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), Name);
		Args->SetArrayField(TEXT("parts"), Parts);
		Args->SetNumberField(TEXT("roughness"), 0.6);
		Args->SetBoolField(TEXT("repair_coplanar"), false);
		return Args;
	}

	/** The default hero in 5 pieces (torso+head, legs and arms pivoting at hips/shoulders). Feet at z=0, facing +X. */
	TArray<TSharedRef<FJsonObject>> DefaultBody(double Height)
	{
		const double S = Height / 180.0;
		const std::initializer_list<double> Red = {0.75, 0.05, 0.04}, Blue = {0.05, 0.12, 0.6}, Skin = {0.85, 0.6, 0.45}, Brown = {0.15, 0.07, 0.03};
		TArray<TSharedRef<FJsonObject>> Out;
		Out.Add(Assembly(TEXT("PlayerBody"), {
			Part(S, TEXT("cylinder"), {60, 56, 52}, {0, 0, 50}, TEXT("plaster"), Blue),
			Part(S, TEXT("box"), {8, 10, 40}, {22, -14, 76}, TEXT("plaster"), Blue),
			Part(S, TEXT("box"), {8, 10, 40}, {22, 14, 76}, TEXT("plaster"), Blue),
			Part(S, TEXT("sphere"), {8, 8, 8}, {28, -14, 100}, TEXT("gold"), {1, 0.8, 0.2}),
			Part(S, TEXT("sphere"), {8, 8, 8}, {28, 14, 100}, TEXT("gold"), {1, 0.8, 0.2}),
			Part(S, TEXT("cylinder"), {58, 54, 26}, {0, 0, 98}, TEXT("plaster"), Red),
			Part(S, TEXT("sphere"), {52, 52, 52}, {0, 0, 120}, TEXT("plaster"), Skin),
			Part(S, TEXT("sphere"), {18, 18, 16}, {25, 0, 136}, TEXT("plaster"), {0.85, 0.5, 0.38}),
			Part(S, TEXT("box"), {6, 26, 6}, {24, 0, 128}, TEXT("black"), {0.04, 0.02, 0.01}),
			Part(S, TEXT("sphere"), {9, 9, 11}, {22, -9, 145}, TEXT("black"), {0.02, 0.02, 0.05}),
			Part(S, TEXT("sphere"), {9, 9, 11}, {22, 9, 145}, TEXT("black"), {0.02, 0.02, 0.05}),
			Part(S, TEXT("dome"), {58, 58, 28}, {0, 0, 150}, TEXT("plaster"), Red),
			Part(S, TEXT("box"), {28, 44, 6}, {24, 0, 152}, TEXT("plaster"), Red),
		}));
		for (const double Y : {-14.0, 14.0})
		{
			Out.Add(Assembly(Y < 0 ? TEXT("PlayerLegL") : TEXT("PlayerLegR"), {
				Part(S, TEXT("box"), {24, 22, 42}, {0, 0, -42}, TEXT("plaster"), Blue),
				Part(S, TEXT("box"), {36, 26, 16}, {6, 0, -55}, TEXT("wood"), Brown),
			}));
		}
		for (const double Y : {-36.0, 36.0})
		{
			Out.Add(Assembly(Y < 0 ? TEXT("PlayerArmL") : TEXT("PlayerArmR"), {
				Part(S, TEXT("cylinder"), {18, 18, 36}, {0, 0, -36}, TEXT("plaster"), Red),
				Part(S, TEXT("sphere"), {22, 22, 22}, {0, 0, -52}, TEXT("white"), {0.92, 0.92, 0.92}),
			}));
		}
		return Out;
	}
}

bool FLogicWorld::HasPlayer() const
{
	return Player.IsValid();
}

ACharacter* FLogicWorld::GetPlayer() const
{
	return Player.Get();
}

bool FLogicWorld::DirectMatch(const FRuntimeSceneNode& Node, const FString& Target, const FRuntimeSceneGraph& Graph)
{
	if (Target.StartsWith(TEXT("tag:")))
	{
		return Node.Tags.Contains(FName(*Target.Mid(4)));
	}
	return Node.Id == Target || Node.Name == Target || Graph.GetPath(Node.Id) == Target;
}

bool FLogicWorld::ResolveNodes(const TArray<FString>& Targets, TArray<FString>& OutIds, FString& OutError) const
{
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	if (!Graph)
	{
		OutError = TEXT("no scene");
		return false;
	}
	for (const FString& T : Targets)
	{
		int32 Found = 0;
		for (const TPair<FString, FRuntimeSceneNode>& P : Graph->GetNodes())
		{
			if (!P.Value.bSoftDeleted && DirectMatch(P.Value, T, *Graph))
			{
				OutIds.AddUnique(P.Key);
				++Found;
				if (!T.StartsWith(TEXT("tag:")))
				{
					break;
				}
			}
		}
		if (Found == 0)
		{
			OutError = FString::Printf(TEXT("no node matches '%s' (use an id, a path like Level/coin_3, a name, or tag:coin)"), *T);
			return false;
		}
	}
	return true;
}

bool FLogicWorld::IsHidden(const FString& NodeId) const
{
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	FString Id = NodeId;
	for (int32 Guard = 0; Graph && !Id.IsEmpty() && Guard < 64; ++Guard)
	{
		if (Hidden.Contains(Id))
		{
			return true;
		}
		const FRuntimeSceneNode* N = Graph->Find(Id);
		Id = N ? N->ParentId : FString();
	}
	return false;
}

bool FLogicWorld::HideNode(const FString& Id, bool bHide)
{
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	if (!Graph || !Graph->Find(Id))
	{
		return false;
	}
	if (bHide)
	{
		Hidden.Add(Id);
	}
	else
	{
		Hidden.Remove(Id);
	}
	for (const FString& Sub : Graph->CollectSubtree(Id))
	{
		const FRuntimeSceneNode* N = Graph->Find(Sub);
		AActor* A = N ? N->Actor.Get() : nullptr;
		if (!A)
		{
			continue;
		}
		A->SetActorHiddenInGame(bHide);
		const bool* bSolid = Solid.Find(Sub);
		A->SetActorEnableCollision(!bHide && (!bSolid || *bSolid));
	}
	Touching.Remove(Id);
	return true;
}

void FLogicWorld::ApplySolid()
{
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	if (!Graph)
	{
		return;
	}
	for (const TPair<FString, bool>& P : Solid)
	{
		if (Graph->Find(P.Key) && !IsHidden(P.Key))
		{
			ForSubtreeActors(*Graph, P.Key, [&P](AActor* A) { A->SetActorEnableCollision(P.Value); });
		}
	}
}

bool FLogicWorld::SpawnPlayer(FString& OutError)
{
	UWorld* W = World.Get();
	if (!W || !PD().IsValid())
	{
		OutError = TEXT("no player definition");
		return false;
	}
	RemovePlayer();
	const double Radius = Num(PD(), TEXT("radius"), 38.0);
	const double Height = Num(PD(), TEXT("height"), 180.0);
	const double Half = Height / 2.0;
	SpawnPoint = Vec(PD(), TEXT("location"), FVector::ZeroVector);
	Checkpoint = SpawnPoint;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ACharacter* C = W->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FRotator::ZeroRotator, SpawnPoint + FVector(0, 0, Half + 2.0)), Params);
	if (!C)
	{
		OutError = TEXT("could not spawn the player character");
		return false;
	}
	C->GetCapsuleComponent()->SetCapsuleSize(Radius, Half);
	C->bUseControllerRotationYaw = false;
	UCharacterMovementComponent* M = C->GetCharacterMovement();
	M->MaxWalkSpeed = Num(PD(), TEXT("speed"), 650.0);
	M->JumpZVelocity = Num(PD(), TEXT("jump"), 1250.0);
	M->GravityScale = Num(PD(), TEXT("gravity"), 2.8);
	M->AirControl = Num(PD(), TEXT("air_control"), 0.85);
	M->BrakingDecelerationWalking = 2600.f;
	M->GroundFriction = 9.f;
	M->MaxAcceleration = 3200.f;
	M->MaxStepHeight = Radius;
	const bool bTop = IsTopDown();
	M->bOrientRotationToMovement = !bTop;
	M->RotationRate = FRotator(0.f, 1500.f, 0.f);
	M->SetPlaneConstraintEnabled(!bTop);
	M->SetPlaneConstraintNormal(FVector(0.f, 1.f, 0.f));
	C->JumpMaxHoldTime = Num(PD(), TEXT("jump_hold"), 0.22);
	C->SetActorRotation(FRotator(0.f, Num(PD(), TEXT("yaw"), 0.0), 0.f));
	if (IsFlight())
	{
		C->GetCapsuleComponent()->SetCapsuleSize(Radius, Radius);
		M->SetMovementMode(MOVE_None);
		M->GravityScale = 0.f;
		C->SetActorLocation(SpawnPoint);
		FlightSpeed = 0.0;
	}
	Player = C;
	Facing = 1.f;

	// Body: the agent's node, or a default hero.
	FRuntimeSceneGraph* Graph = GraphOf(W);
	FString BodyRef;
	PD()->TryGetStringField(TEXT("body"), BodyRef);
	AActor* Body = nullptr;
	if (!BodyRef.IsEmpty() && Graph)
	{
		TArray<FString> Ids;
		FString Err;
		if (ResolveNodes({BodyRef}, Ids, Err) && Ids.Num() > 0)
		{
			PlayerBodyNode = Ids[0];
			Body = Graph->Find(Ids[0])->Actor.Get();
		}
	}
	Limbs.Reset();
	PlayerBodyNodes.Reset();
	if (!Body)
	{
		FToolCallerContext Caller;
		Caller.SessionId = FLogicManager::Session();
		Caller.ClientName = TEXT("logic");
		const double S = Height / 180.0;
		const FVector Joints[] = {FVector::ZeroVector, FVector(0, -14 * S, 55 * S), FVector(0, 14 * S, 55 * S), FVector(0, -36 * S, 110 * S), FVector(0, 36 * S, 110 * S)};
		const TArray<TSharedRef<FJsonObject>> Pieces = DefaultBody(Height);
		for (int32 i = 0; i < Pieces.Num(); ++i)
		{
			const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("build.create_assembly"), Pieces[i], Caller, 1);
			FString Id;
			if (!R.IsOk() || !R.Payload.IsValid() || !R.Payload->TryGetStringField(TEXT("id"), Id) || !Graph || !Graph->Find(Id))
			{
				continue;
			}
			AActor* PieceActor = Graph->Find(Id)->Actor.Get();
			PlayerBodyNodes.Add(Id);
			PieceActor->SetActorEnableCollision(false);
			if (i == 0)
			{
				PlayerBodyNode = Id;
				Body = PieceActor;
				bOwnBody = true;
			}
			else if (Body)
			{
				PieceActor->AttachToActor(Body, FAttachmentTransformRules::KeepWorldTransform);
				PieceActor->SetActorRelativeLocation(Joints[i]);
				PieceActor->SetActorRelativeRotation(FRotator::ZeroRotator);
				FLimb L;
				L.Actor = PieceActor;
				L.Kind = i - 1;
				Limbs.Add(L);
			}
		}
	}
	if (Body)
	{
		Body->SetActorEnableCollision(false);
		TArray<AActor*> Shown = {Body};
		Body->GetAttachedActors(Shown, false, true);
		for (AActor* X : Shown)
		{
			X->SetActorHiddenInGame(false);
		}
		Body->AttachToActor(C, FAttachmentTransformRules::KeepWorldTransform);
		Body->SetActorRelativeLocation(IsFlight() ? FVector::ZeroVector : FVector(0, 0, -Half));
		Body->SetActorRelativeRotation(FRotator::ZeroRotator);
		PlayerBody = Body;
	}

	ACameraActor* Cam = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(SpawnPoint), Params);
	if (Cam)
	{
		Cam->GetCameraComponent()->SetFieldOfView(static_cast<float>(Num(PD(), TEXT("fov"), 55.0)));
		Cam->GetCameraComponent()->bConstrainAspectRatio = false;
		PlayerCamera = Cam;
	}
	if (APlayerController* PC = W->GetFirstPlayerController())
	{
		PreviousPawn = PC->GetPawn();
		PC->Possess(C);
		ApplyPlayerInputMode(PC);
		if (Cam)
		{
			PC->SetViewTarget(Cam);
		}
	}
	TickCamera(0.0, true);
	return true;
}

void FLogicWorld::RemovePlayer()
{
	UWorld* W = World.Get();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (ACharacter* C = Player.Get())
	{
		if (PC && PC->GetPawn() == C)
		{
			if (APawn* Prev = PreviousPawn.Get())
			{
				PC->Possess(Prev);
			}
			else
			{
				PC->UnPossess();
			}
		}
		if (AActor* Body = PlayerBody.Get())
		{
			Body->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			if (!bOwnBody)
			{
				// A level model used as the body: hide it again (it is shown when attached).
				TArray<AActor*> Parts = {Body};
				Body->GetAttachedActors(Parts, false, true);
				for (AActor* X : Parts)
				{
					X->SetActorHiddenInGame(true);
				}
				Body->SetActorRelativeScale3D(FVector::OneVector);
			}
			else
			{
				Body->SetActorEnableCollision(true);
			}
		}
		C->Destroy();
	}
	if (bOwnBody && W)
	{
		FToolCallerContext Caller;
		Caller.SessionId = FLogicManager::Session();
		for (const FString& Id : PlayerBodyNodes)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("target"), Id);
			IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("scene.delete_node"), Args, Caller, 1);
		}
	}
	PlayerBodyNodes.Reset();
	Limbs.Reset();
	GunNode.Reset();
	if (ACameraActor* Cam = PlayerCamera.Get())
	{
		Cam->Destroy();
	}
	Player.Reset();
	PlayerBody.Reset();
	PlayerCamera.Reset();
	PlayerBodyNode.Reset();
	bOwnBody = false;
}

void FLogicWorld::RespawnPlayer()
{
	ACharacter* C = Player.Get();
	if (!C)
	{
		return;
	}
	const double Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	C->GetCharacterMovement()->StopMovementImmediately();
	if (IsFlight())
	{
		C->SetActorLocationAndRotation(Checkpoint, FRotator(0, Num(PD(), TEXT("yaw"), 0.0), 0), false, nullptr, ETeleportType::TeleportPhysics);
		FlightSpeed = 0.0;
		FlightVelocity = FVector::ZeroVector;
	}
	else
	{
		C->SetActorLocation(Checkpoint + FVector(0, 0, Half + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
		C->SetActorRotation(FRotator::ZeroRotator);
		C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
	Facing = 1.f;
	bFell = false;
	Invulnerable = bRunning ? LogicTuning::Get(TEXT("combat.respawn_invulnerable")) : 0.0;
	TickCamera(0.0, true);
}

void FLogicWorld::ResetPlay()
{
	PlayerLive = PlayerDef.IsValid() ? RuntimeJson::Parse(RuntimeJson::Write(PlayerDef.ToSharedRef())) : nullptr;
	ResetExtras();
	ResetTweens();
	ResetPools();
	ResetCombat();
	FloatTexts.Reset();
	PendingFeedback.Reset();
	HitFlash = 0.0;
	ShakeTime = 0.0;
	if (SlowmoLeft > 0.0)
	{
		SlowmoLeft = 0.0;
		SlowmoScale = 1.0;
		if (UWorld* W = World.Get())
		{
			W->GetWorldSettings()->SetTimeDilation(1.f);
		}
	}
	for (const FString& Id : Hidden.Array())
	{
		HideNode(Id, false);
	}
	Hidden.Reset();
	Touching.Reset();
	LastTouch.Reset();
	PlayTime = 0.0;
	bFrozen = false;
	// Moving objects back to where they started.
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : Motions)
	{
		const FRuntimeSceneNode* N = Graph ? Graph->Find(P.Key) : nullptr;
		if (AActor* A = N ? N->Actor.Get() : nullptr)
		{
			FRotator R = FRotator::ZeroRotator;
			RuntimeJson::ToRotator(P.Value->TryGetField(TEXT("origin_rot")), R);
			A->SetActorLocationAndRotation(Vec(P.Value, TEXT("origin"), A->GetActorLocation()), R);
		}
	}
	ApplySolid();
	ResetBrains();
	bool bAutoSpawn = true;
	if (PD().IsValid())
	{
		PD()->TryGetBoolField(TEXT("auto_spawn"), bAutoSpawn);
	}
	if (PD().IsValid() && !bAutoSpawn)
	{
		RemovePlayer();
	}
	else if (PD().IsValid())
	{
		FString Err;
		if (!Player.IsValid())
		{
			SpawnPlayer(Err);
		}
		Checkpoint = SpawnPoint;
		RespawnPlayer();
		if (UWorld* W = World.Get())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (PC->GetPawn() != Player.Get())
				{
					PC->Possess(Player.Get());
				}
				if (ACameraActor* Cam = PlayerCamera.Get())
				{
					PC->SetViewTarget(Cam);
				}
				ApplyPlayerInputMode(PC);
			}
		}
	}
}

void FLogicWorld::ApplyPlayerInputMode(APlayerController* PC)
{
	PC->ResetIgnoreInputFlags();
	if (IsTopDown())
	{
		// Aim with the mouse: visible cursor, clicks still reach the game.
		PC->SetShowMouseCursor(true);
		PC->DefaultMouseCursor = EMouseCursor::Crosshairs;
		PC->CurrentMouseCursor = EMouseCursor::Crosshairs;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void FLogicWorld::TickPlayerInput(APlayerController* PC)
{
	ACharacter* C = Player.Get();
	if (!C || !PC || PC->GetPawn() != C || bFrozen)
	{
		return;
	}
	const bool bRunKey = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
	// Shooting (both modes): left mouse / F / right trigger; hold for automatic weapons.
	if (!EquippedWeapon(TEXT("player")).IsEmpty())
	{
		const TSharedPtr<FJsonObject>* W = WeaponDefs.Find(EquippedWeapon(TEXT("player")));
		bool bAuto = false;
		if (W)
		{
			(*W)->TryGetBoolField(TEXT("auto"), bAuto);
		}
		const bool bDown = PC->IsInputKeyDown(EKeys::LeftMouseButton) || PC->IsInputKeyDown(EKeys::F) || PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > 0.5f;
		const bool bPressed = PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) || PC->WasInputKeyJustPressed(EKeys::F) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightTrigger);
		if (bAuto ? bDown : bPressed)
		{
			FString Err;
			FireWeapon(TEXT("player"), FString(), nullptr, Err);
		}
		FString ReloadKey;
		PD()->TryGetStringField(TEXT("reload_key"), ReloadKey);
		if (!ReloadKey.IsEmpty() && PC->WasInputKeyJustPressed(FKey(*ReloadKey)) && W)
		{
			StartReload(TEXT("player"), *W, WeaponStates.FindOrAdd(TEXT("player")));
		}
	}
	// Secondary weapon (missiles): right mouse / left trigger.
	if (const FString Secondary = Str(PD(), TEXT("secondary")); !Secondary.IsEmpty())
	{
		if (PC->WasInputKeyJustPressed(EKeys::RightMouseButton) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftTrigger) || PC->WasInputKeyJustPressed(EKeys::G))
		{
			FString Err;
			FireWeapon(TEXT("player"), Secondary, nullptr, Err);
		}
	}
	if (IsFlight())
	{
		float MX = 0.f, MY = 0.f;
		PC->GetInputMouseDelta(MX, MY);
		const double Sens = Num(PD(), TEXT("mouse_sensitivity"), 1.0);
		FlightPitchIn = MY * LogicTuning::Get(TEXT("flight.mouse_scale")) * Sens;
		FlightYawIn = MX * LogicTuning::Get(TEXT("flight.mouse_scale")) * Sens;
		if (PC->IsInputKeyDown(EKeys::Up)) FlightPitchIn += 1.0;
		if (PC->IsInputKeyDown(EKeys::Down)) FlightPitchIn -= 1.0;
		if (PC->IsInputKeyDown(EKeys::Left) || PC->IsInputKeyDown(EKeys::Q)) FlightYawIn -= 1.0;
		if (PC->IsInputKeyDown(EKeys::Right) || PC->IsInputKeyDown(EKeys::E)) FlightYawIn += 1.0;
		FlightRollIn = 0.0;
		if (PC->IsInputKeyDown(EKeys::D)) FlightRollIn += 1.0;
		if (PC->IsInputKeyDown(EKeys::A)) FlightRollIn -= 1.0;
		const float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		const float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		if (FMath::Abs(LX) > 0.15f) FlightRollIn = LX;
		if (FMath::Abs(LY) > 0.15f) FlightPitchIn = -LY;
		if (FMath::Abs(RX) > 0.15f) FlightYawIn = RX;
		const double ThrottleStep = LogicTuning::Get(TEXT("flight.throttle_rate")) * FMath::Max(0.001, static_cast<double>(PC->GetWorld()->GetDeltaSeconds()));
		if (PC->IsInputKeyDown(EKeys::W)) FlightThrottle = FMath::Min(1.0, FlightThrottle + ThrottleStep);
		if (PC->IsInputKeyDown(EKeys::S)) FlightThrottle = FMath::Max(0.0, FlightThrottle - ThrottleStep);
		bFlightBoost = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder);
		return;
	}
	const TSharedPtr<FJsonObject>* DashDef = nullptr;
	const bool bHasDash = PD()->TryGetObjectField(TEXT("dash"), DashDef);
	auto TryDash = [&](FVector Dir)
	{
		if (!bHasDash || PlayTime < DashReadyAt || Dir.IsNearlyZero())
		{
			return;
		}
		const FKey Key(*Str(*DashDef, TEXT("key"), IsTopDown() ? TEXT("SpaceBar") : TEXT("LeftControl")));
		if (!PC->WasInputKeyJustPressed(Key) && !PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right))
		{
			return;
		}
		const double Dur = Num(*DashDef, TEXT("duration"), 0.18);
		C->LaunchCharacter(Dir.GetSafeNormal() * Num(*DashDef, TEXT("speed"), 2200.0), true, false);
		DashUntil = PlayTime + Dur;
		DashReadyAt = PlayTime + Num(*DashDef, TEXT("cooldown"), 0.8);
		bool bInv = true;
		(*DashDef)->TryGetBoolField(TEXT("invulnerable"), bInv);
		if (bInv)
		{
			Invulnerable = FMath::Max(Invulnerable, Dur);
		}
		FLogicEvent E;
		E.On = TEXT("dash");
		Queue.Add(E);
	};
	if (IsTopDown())
	{
		FVector Move = FVector::ZeroVector;
		if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) Move.X += 1;
		if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) Move.X -= 1;
		if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) Move.Y += 1;
		if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) Move.Y -= 1;
		const float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		if (FMath::Abs(LX) > 0.2f || FMath::Abs(LY) > 0.2f)
		{
			Move = FVector(LY, LX, 0);
		}
		C->GetCharacterMovement()->MaxWalkSpeed = static_cast<float>(Num(PD(), TEXT("speed"), 650.0) * (bRunKey ? 1.5 : 1.0));
		TryDash(Move.IsNearlyZero() ? AimDir : Move);
		if (IsGrid())
		{
			// Cell-by-cell movement: one step per press (repeats while held), blocked by walls.
			const double Cell = Num(PD(), TEXT("grid"), 100.0);
			if (GridT >= 1.0 && !Move.IsNearlyZero() && PlayTime >= GridNextInput)
			{
				const FVector Dir = FMath::Abs(Move.X) >= FMath::Abs(Move.Y) ? FVector(FMath::Sign(Move.X), 0, 0) : FVector(0, FMath::Sign(Move.Y), 0);
				FVector From = C->GetActorLocation();
				From.X = SpawnPoint.X + FMath::RoundToDouble((From.X - SpawnPoint.X) / Cell) * Cell;
				From.Y = SpawnPoint.Y + FMath::RoundToDouble((From.Y - SpawnPoint.Y) / Cell) * Cell;
				const FVector To = From + Dir * Cell;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(LogicGrid), false, C);
				TArray<AActor*> Ignore;
				C->GetAttachedActors(Ignore, false, true);
				Q.AddIgnoredActors(Ignore);
				UWorld* Wd = C->GetWorld();
				if (!Wd || !Wd->SweepTestByChannel(From, To, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(static_cast<float>(Cell * 0.3)), Q))
				{
					GridFrom = From;
					GridTo = To;
					GridT = 0.0;
					GridNextInput = PlayTime + Num(PD(), TEXT("step_time"), 0.14);
					C->SetActorRotation(FRotator(0.0, Dir.Rotation().Yaw, 0.0));
				}
			}
			return;
		}
		if (!Move.IsNearlyZero())
		{
			C->AddMovementInput(Move.GetClampedToMaxSize(1.0), 1.f, true);
		}
		const FVector P = C->GetActorLocation();
		FVector WL, WD;
		const float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), RY = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
		if (FMath::Abs(RX) > 0.3f || FMath::Abs(RY) > 0.3f)
		{
			AimPoint = P + FVector(RY, RX, 0).GetSafeNormal() * 600.0;
		}
		else if (PC->DeprojectMousePositionToWorld(WL, WD) && FMath::Abs(WD.Z) > 1e-3)
		{
			const double T = (P.Z - WL.Z) / WD.Z;
			if (T > 0)
			{
				AimPoint = WL + WD * T;
			}
		}
		const FVector Dir = (AimPoint - P).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			AimDir = Dir;
			C->SetActorRotation(FRotator(0.0, Dir.Rotation().Yaw, 0.0));
		}
		return;
	}
	TryDash(FVector(Facing, 0, 0));
	float Axis = 0.f;
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right))
	{
		Axis += 1.f;
	}
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left))
	{
		Axis -= 1.f;
	}
	const float Stick = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	if (FMath::Abs(Stick) > 0.25f)
	{
		Axis = FMath::Clamp(Axis + Stick, -1.f, 1.f);
	}
	const bool bRun = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
	C->GetCharacterMovement()->MaxWalkSpeed = static_cast<float>(Num(PD(), TEXT("speed"), 650.0) * (bRun ? 1.6 : 1.0));
	if (Axis != 0.f)
	{
		Facing = Axis > 0.f ? 1.f : -1.f;
		C->AddMovementInput(FVector(1.f, 0.f, 0.f), Axis, true);
	}
	const FKey JumpKeys[] = {EKeys::SpaceBar, EKeys::W, EKeys::Up, EKeys::Gamepad_FaceButton_Bottom};
	for (const FKey& K : JumpKeys)
	{
		if (PC->WasInputKeyJustPressed(K))
		{
			C->Jump();
		}
		if (PC->WasInputKeyJustReleased(K))
		{
			C->StopJumping();
		}
	}
}

void FLogicWorld::TickMotion()
{
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	if (!Graph)
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : Motions)
	{
		const FRuntimeSceneNode* N = Graph->Find(P.Key);
		AActor* A = N ? N->Actor.Get() : nullptr;
		if (!A || N->bSoftDeleted || IsHidden(P.Key) || IsTweening(P.Key) || A->IsHidden() || Agents.Contains(P.Key))
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& J = P.Value;
		FString Type = TEXT("none");
		J->TryGetStringField(TEXT("type"), Type);
		const FVector Origin = Vec(J, TEXT("origin"), A->GetActorLocation());
		FRotator OriginRot = FRotator::ZeroRotator;
		RuntimeJson::ToRotator(J->TryGetField(TEXT("origin_rot")), OriginRot);
		const double T = PlayTime + Num(J, TEXT("offset"), 0.0);
		const double Speed = FMath::Max(1.0, Num(J, TEXT("speed"), 150.0));
		FVector Pos = Origin;
		FRotator Rot = OriginRot;
		double Dir = 1.0;
		if (Type == TEXT("patrol"))
		{
			const FVector Axis = Vec(J, TEXT("axis"), FVector(1, 0, 0)).GetSafeNormal();
			const double Dist = FMath::Max(1.0, Num(J, TEXT("distance"), 400.0));
			const double Period = 2.0 * Dist / Speed;
			const double Phase = FMath::Fmod(T, Period);
			const double S = Phase < Period / 2 ? Phase * Speed : (Period - Phase) * Speed;
			Dir = Phase < Period / 2 ? 1.0 : -1.0;
			Pos = Origin + Axis * S;
		}
		else if (Type == TEXT("path"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
			if (J->TryGetArrayField(TEXT("points"), Points) && Points->Num() >= 2)
			{
				TArray<FVector> Pts;
				for (const TSharedPtr<FJsonValue>& V : *Points)
				{
					FVector Pt;
					if (RuntimeJson::ToVector(V, Pt))
					{
						Pts.Add(Origin + Pt);
					}
				}
				bool bLoop = false;
				J->TryGetBoolField(TEXT("loop"), bLoop);
				if (bLoop)
				{
					Pts.Add(Pts[0]);
				}
				double Total = 0.0;
				for (int32 i = 1; i < Pts.Num(); ++i)
				{
					Total += FVector::Dist(Pts[i - 1], Pts[i]);
				}
				if (Total > 1.0)
				{
					double D = FMath::Fmod(T * Speed, bLoop ? Total : 2.0 * Total);
					if (!bLoop && D > Total)
					{
						D = 2.0 * Total - D;
						Dir = -1.0;
					}
					for (int32 i = 1; i < Pts.Num(); ++i)
					{
						const double Seg = FVector::Dist(Pts[i - 1], Pts[i]);
						if (D <= Seg || i == Pts.Num() - 1)
						{
							Pos = FMath::Lerp(Pts[i - 1], Pts[i], Seg > 0 ? FMath::Clamp(D / Seg, 0.0, 1.0) : 0.0);
							break;
						}
						D -= Seg;
					}
				}
			}
		}
		else if (Type == TEXT("bob"))
		{
			Pos = Origin + FVector(0, 0, Num(J, TEXT("amplitude"), 30.0) * FMath::Sin(2.0 * PI * T / FMath::Max(0.1, Num(J, TEXT("period"), 2.0))));
		}
		FVector Spin = FVector::ZeroVector;
		if (RuntimeJson::ToVector(J->TryGetField(TEXT("spin")), Spin))
		{
			Rot = OriginRot + FRotator(Spin.X * T, Spin.Y * T, Spin.Z * T);
		}
		const TArray<TSharedPtr<FJsonValue>>* Waddle = nullptr;
		if (J->TryGetArrayField(TEXT("waddle"), Waddle) && Waddle->Num() == 2)
		{
			Rot.Roll += (*Waddle)[0]->AsNumber() * FMath::Sin(2.0 * PI * (*Waddle)[1]->AsNumber() * T);
		}
		bool bFace = false;
		J->TryGetBoolField(TEXT("face"), bFace);
		if (bFace && Dir < 0)
		{
			Rot.Yaw += 180.0;
		}
		A->SetActorLocationAndRotation(Pos, Rot);
	}
}

bool FLogicWorld::MakeTouchEvent(const FString& NodeId, bool bFromAbove, bool bFromBelow, FLogicEvent& Out) const
{
	if (!MakeClickEventForNode(NodeId, false, Out))
	{
		return false;
	}
	Out.On = TEXT("touch");
	FLogicValue TouchData = Out.Data.M->FindRef(TEXT("data")).DeepCopy();
	if (TouchData.Type != FLogicValue::EType::Map)
	{
		TouchData = FLogicValue::MakeMap();
	}
	for (FLogicValue* Target : {Out.Data.M.Get() ? &Out.Data : nullptr, &TouchData})
	{
		Target->M->Add(TEXT("from_above"), FLogicValue::MakeBool(bFromAbove));
		Target->M->Add(TEXT("from_below"), FLogicValue::MakeBool(bFromBelow));
		Target->M->Add(TEXT("side"), FLogicValue::MakeBool(!bFromAbove && !bFromBelow));
	}
	Out.Data.M->Add(TEXT("data"), TouchData);
	return true;
}

void FLogicWorld::TickTouch()
{
	ACharacter* C = Player.Get();
	FRuntimeSceneGraph* Graph = GraphOf(World.Get());
	TArray<const FLogicRule*> TouchRules;
	for (const FLogicRule& R : Rules)
	{
		if (R.bEnabled && R.On == TEXT("touch"))
		{
			TouchRules.Add(&R);
		}
	}
	if (!C || !Graph || TouchRules.Num() == 0)
	{
		Touching.Reset();
		return;
	}
	const FBox PB = C->GetCapsuleComponent()->Bounds.GetBox().ExpandBy(4.0);
	const double H = PB.GetSize().Z;
	const double Vz = C->GetVelocity().Z;
	TSet<FString> Now;
	for (const TPair<FString, FRuntimeSceneNode>& P : Graph->GetNodes())
	{
		const FRuntimeSceneNode& N = P.Value;
		if (N.bSoftDeleted || !N.Actor.IsValid() || PlayerBodyNodes.Contains(N.Id) || N.Id == PlayerBodyNode || IsHidden(N.Id) || N.Actor->IsHidden())
		{
			continue;
		}
		bool bMatch = false;
		for (const FLogicRule* R : TouchRules)
		{
			bMatch |= !R->Target.IsEmpty() && DirectMatch(N, R->Target, *Graph);
		}
		if (!bMatch)
		{
			continue;
		}
		const FBox NB = GameBox(N.Actor.Get());
		if (!NB.IsValid || !NB.Intersect(PB))
		{
			continue;
		}
		Now.Add(N.Id);
		if (Touching.Contains(N.Id))
		{
			continue;
		}
		if (const double* Last = LastTouch.Find(N.Id); Last && PlayTime - *Last < LogicTuning::Get(TEXT("touch.retouch_cooldown")))
		{
			continue;
		}
		LastTouch.Add(N.Id, PlayTime);
		// Above: the feet were over the top last frame (fast falls), or are in its top part while not rising.
		const bool bAbove = (PrevFeetZ >= NB.Max.Z - LogicTuning::Get(TEXT("touch.stomp_tolerance")) && Vz <= 200.0) || (PB.Min.Z >= NB.Max.Z - 0.4 * H && Vz <= 60.0);
		const bool bBelow = !bAbove && PB.Max.Z <= NB.Min.Z + 0.4 * H && Vz >= -60.0;
		FLogicEvent E;
		if (MakeTouchEvent(N.Id, bAbove, bBelow, E))
		{
			Queue.Add(MoveTemp(E));
		}
	}
	Touching = MoveTemp(Now);
	PrevFeetZ = PB.Min.Z;
}

void FLogicWorld::TickCamera(double Dt, bool bSnap)
{
	ACharacter* C = Player.Get();
	ACameraActor* Cam = PlayerCamera.Get();
	if (!C || !Cam)
	{
		return;
	}
	const FVector P = C->GetActorLocation();
	if (IsFlight())
	{
		const FQuat Q = C->GetActorQuat();
		const double Dist = Num(PD(), TEXT("camera_distance"), 1300.0);
		const double Up = Num(PD(), TEXT("camera_height"), 330.0);
		const FVector Goal = P - Q.GetForwardVector() * Dist + Q.GetUpVector() * Up;
		CamPos = bSnap || Dt <= 0.0 ? Goal : FMath::VInterpTo(CamPos, Goal, Dt, 7.0);
		FRotator CR = (P + Q.GetForwardVector() * 1500.0 - CamPos).Rotation();
		CR.Roll = Q.Rotator().Roll * 0.35;
		Cam->SetActorLocationAndRotation(CamPos + ShakeOffset(), CR);
		const double BaseFov = Num(PD(), TEXT("fov"), 70.0);
		Cam->GetCameraComponent()->SetFieldOfView(static_cast<float>(FMath::FInterpTo(Cam->GetCameraComponent()->FieldOfView, BaseFov + (bFlightBoost ? LogicTuning::Get(TEXT("flight.boost_fov")) : 0.0), Dt, 3.0)));
		return;
	}
	if (IsTopDown())
	{
		const double Pitch = -FMath::Abs(Num(PD(), TEXT("camera_pitch"), 60.0));
		const double Dist = Num(PD(), TEXT("camera_distance"), 1500.0);
		const FVector Lead = AimDir * Num(PD(), TEXT("camera_lead"), 150.0);
		const FVector Goal = P + Lead + FVector(-Dist * FMath::Cos(FMath::DegreesToRadians(-Pitch)), 0.0, Dist * FMath::Sin(FMath::DegreesToRadians(-Pitch)));
		CamPos = bSnap || Dt <= 0.0 ? Goal : FMath::VInterpTo(CamPos, Goal, Dt, 6.0);
		Cam->SetActorLocationAndRotation(CamPos + ShakeOffset(), FRotator(Pitch, 0.0, 0.0));
		return;
	}
	const FVector Target = P + FVector(Facing * Num(PD(), TEXT("camera_lead"), 200.0), Num(PD(), TEXT("camera_distance"), 1700.0),
		Num(PD(), TEXT("camera_height"), 250.0));
	if (bSnap || Dt <= 0.0)
	{
		CamPos = Target;
	}
	else
	{
		CamPos.X = FMath::FInterpTo(CamPos.X, Target.X, Dt, 5.0);
		CamPos.Y = Target.Y;
		CamPos.Z = FMath::FInterpTo(CamPos.Z, Target.Z, Dt, 3.0);
	}
	Cam->SetActorLocationAndRotation(CamPos + ShakeOffset(), FRotator(Num(PD(), TEXT("camera_pitch"), -6.0), -90.0, 0.0));
}

void FLogicWorld::TickPlay(double Dt)
{
	PlayTime += Dt;
	TickTweens(Dt);
	TickFlight(Dt);
	if (IsGrid() && GridT < 1.0)
	{
		GridT = FMath::Min(1.0, GridT + Dt / FMath::Max(0.01, Num(PD(), TEXT("step_time"), 0.14)));
		if (ACharacter* GC = Player.Get())
		{
			const FVector L = FMath::Lerp(GridFrom, GridTo, GridT);
			GC->SetActorLocation(FVector(L.X, L.Y, GC->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			AnimPhase += Dt * 12.0;
		}
	}
	TickMotion();
	TickSpawners(Dt);
	TickInteract();
	TickBrains(Dt);
	TickCombat(Dt);
	TickFeel(Dt);
	if (ACharacter* C = Player.Get())
	{
		const double KillZ = Num(PD(), TEXT("kill_z"), SpawnPoint.Z - 1500.0);
		if (!bFell && C->GetActorLocation().Z < KillZ)
		{
			bFell = true;
			if (Rules.ContainsByPredicate([](const FLogicRule& R) { return R.bEnabled && R.On == TEXT("fall"); }))
			{
				FLogicEvent E;
				E.On = TEXT("fall");
				Queue.Add(MoveTemp(E));
			}
			else
			{
				RespawnPlayer();
			}
		}
		TickTouch();
		TickPlayerAnim(Dt);
		TickCamera(Dt, false);
	}
}

TSharedRef<FJsonObject> FLogicWorld::PlayerJson() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	ACharacter* C = Player.Get();
	J->SetBoolField(TEXT("exists"), C != nullptr);
	if (C)
	{
		const FVector L = C->GetActorLocation() - FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		J->SetField(TEXT("location"), RuntimeJson::Vec(L));
		J->SetField(TEXT("velocity"), RuntimeJson::Vec(C->GetVelocity()));
		J->SetBoolField(TEXT("grounded"), C->GetCharacterMovement()->IsMovingOnGround());
		J->SetBoolField(TEXT("frozen"), bFrozen);
		J->SetBoolField(TEXT("invulnerable"), Invulnerable > 0.0);
		J->SetNumberField(TEXT("speed"), RuntimeJson::Round(IsFlight() ? FlightSpeed : C->GetVelocity().Size()));
		J->SetNumberField(TEXT("altitude"), RuntimeJson::Round(C->GetActorLocation().Z));
		J->SetNumberField(TEXT("throttle"), RuntimeJson::Round(FlightThrottle));
		J->SetBoolField(TEXT("boost"), bFlightBoost);
		J->SetBoolField(TEXT("dash_ready"), PlayTime >= DashReadyAt);
		if (const FString Secondary = Str(PD(), TEXT("secondary")); !Secondary.IsEmpty())
		{
			if (const TSharedPtr<FJsonObject>* W = WeaponDefs.Find(Secondary); W && Num(*W, TEXT("homing"), 0.0) > 0.0)
			{
				const FVector Fwd = IsFlight() ? C->GetActorForwardVector() : AimDir;
				J->SetStringField(TEXT("lock"), LockTarget(TEXT("player"), C->GetActorLocation(), Fwd, Num(*W, TEXT("lock_range"), 12000.0),
					Num(*W, TEXT("lock_angle"), 25.0), Str(*W, TEXT("hits"))));
			}
		}
		J->SetField(TEXT("checkpoint"), RuntimeJson::Vec(Checkpoint));
		UWorld* W = World.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		J->SetBoolField(TEXT("controlled"), PC && PC->GetPawn() == C);
	}
	return J;
}
