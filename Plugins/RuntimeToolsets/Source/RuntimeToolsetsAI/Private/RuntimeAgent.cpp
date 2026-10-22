#include "RuntimeAgent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"

ARuntimeAgentController::ARuntimeAgentController()
{
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception")));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing"));
	for (UAISenseConfig* C : {static_cast<UAISenseConfig*>(SightConfig.Get()), static_cast<UAISenseConfig*>(HearingConfig.Get())})
	{
		C->SetMaxAge(5.f);
	}
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	HearingConfig->DetectionByAffiliation = SightConfig->DetectionByAffiliation;
	GetPerceptionComponent()->ConfigureSense(*SightConfig);
	GetPerceptionComponent()->ConfigureSense(*HearingConfig);
	GetPerceptionComponent()->SetDominantSense(SightConfig->GetSenseImplementation());
	bSetControlRotationFromPawnOrientation = true;
}

void ARuntimeAgentController::ApplySettings(const FRuntimeAgentSettings& S)
{
	SightConfig->SightRadius = FMath::Max(0.f, S.SightRadius);
	SightConfig->LoseSightRadius = FMath::Max(SightConfig->SightRadius, S.LoseSightRadius);
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(S.SightHalfAngle, 1.f, 180.f);
	SightConfig->SetMaxAge(S.MemorySeconds);
	SightConfig->SetStartsEnabled(S.SightRadius > 0.f);
	HearingConfig->HearingRange = FMath::Max(0.f, S.HearingRange);
	HearingConfig->SetMaxAge(S.MemorySeconds);
	HearingConfig->SetStartsEnabled(S.HearingRange > 0.f);
	GetPerceptionComponent()->ConfigureSense(*SightConfig);
	GetPerceptionComponent()->ConfigureSense(*HearingConfig);
	GetPerceptionComponent()->RequestStimuliListenerUpdate();
}

ARuntimeAgentCharacter::ARuntimeAgentCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	AIControllerClass = ARuntimeAgentController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 360.f, 0.f);
	Move->bUseRVOAvoidance = true;
	Move->NavAgentProps.bCanCrouch = false;
	GetCapsuleComponent()->SetHiddenInGame(true);
}

namespace RuntimeAgents
{
	APawn* FindAgent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (APawn* P = Cast<APawn>(Actor))
		{
			return P;
		}
		for (AActor* Parent = Actor->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
		{
			if (ARuntimeAgentCharacter* A = Cast<ARuntimeAgentCharacter>(Parent))
			{
				return A;
			}
		}
		return nullptr;
	}

	AAIController* ControllerOf(AActor* Actor)
	{
		APawn* P = FindAgent(Actor);
		return P ? Cast<AAIController>(P->GetController()) : nullptr;
	}

	APawn* MakeAgent(AActor* Actor, const FRuntimeAgentSettings& Settings, FString& OutError)
	{
		if (!Actor)
		{
			OutError = TEXT("no actor");
			return nullptr;
		}
		UWorld* W = Actor->GetWorld();
		APawn* Pawn = FindAgent(Actor);
		if (!Pawn)
		{
			// Wrap the object: a capsule sized to its bounds, standing where it stands.
			FVector Origin, Extent;
			Actor->GetActorBounds(false, Origin, Extent); // all parts: game objects are often non-colliding
			if (Extent.IsNearlyZero())
			{
				Origin = Actor->GetActorLocation();
				Extent = FVector(40.f, 40.f, 90.f);
			}
			const float Radius = FMath::Clamp(static_cast<float>(FMath::Max(Extent.X, Extent.Y)), 20.f, 400.f);
			const float HalfHeight = FMath::Clamp(static_cast<float>(Extent.Z), Radius, 800.f);
			const FVector At(Origin.X, Origin.Y, Origin.Z - Extent.Z + HalfHeight + 2.f);
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			ARuntimeAgentCharacter* C = W->SpawnActor<ARuntimeAgentCharacter>(ARuntimeAgentCharacter::StaticClass(), At, FRotator(0.f, Actor->GetActorRotation().Yaw, 0.f), P);
			if (!C)
			{
				OutError = TEXT("could not spawn the agent body");
				return nullptr;
			}
			C->GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);
			C->Body = Actor;
			// The body rides on the capsule; its own collision would fight the capsule.
			TArray<UPrimitiveComponent*> Prims;
			Actor->GetComponents(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				Prim->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				// The eyes sit inside the body: let sight traces (and those aimed at the capsule) pass through it.
				Prim->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
				Prim->SetCanEverAffectNavigation(false);
			}
			Actor->AttachToActor(C, FAttachmentTransformRules::KeepWorldTransform);
			if (!C->GetController())
			{
				C->SpawnDefaultController();
			}
			Pawn = C;
		}
		if (!Pawn->GetController())
		{
			Pawn->AIControllerClass = Pawn->AIControllerClass ? Pawn->AIControllerClass : TSubclassOf<AController>(ARuntimeAgentController::StaticClass());
			Pawn->SpawnDefaultController();
		}
		if (ARuntimeAgentCharacter* C = Cast<ARuntimeAgentCharacter>(Pawn))
		{
			C->Settings = Settings;
			C->GetCharacterMovement()->MaxWalkSpeed = Settings.MaxSpeed;
			C->GetCharacterMovement()->RotationRate = FRotator(0.f, Settings.TurnRate, 0.f);
		}
		if (ARuntimeAgentController* RC = Cast<ARuntimeAgentController>(Pawn->GetController()))
		{
			RC->ApplySettings(Settings);
		}
		if (!Cast<AAIController>(Pawn->GetController()))
		{
			OutError = TEXT("the pawn is not controlled by an AI controller (a player owns it?)");
			return nullptr;
		}
		return Pawn;
	}

	void RemoveAgent(APawn* Agent)
	{
		ARuntimeAgentCharacter* C = Cast<ARuntimeAgentCharacter>(Agent);
		if (!C)
		{
			return; // plain pawns keep their controller
		}
		if (AActor* Body = C->Body.Get())
		{
			Body->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			TArray<UPrimitiveComponent*> Prims;
			Body->GetComponents(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				Prim->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				Prim->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
				Prim->SetCanEverAffectNavigation(true);
			}
		}
		if (AController* Ctrl = C->GetController())
		{
			Ctrl->UnPossess();
			Ctrl->Destroy();
		}
		C->Destroy();
	}
}
