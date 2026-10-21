#include "RuntimePhysicsToolset.h"
#include "RuntimeToolHelpers.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	const TCHAR* PhysicsMobilityName(EComponentMobility::Type M)
	{
		return M == EComponentMobility::Static ? TEXT("Static") : M == EComponentMobility::Stationary ? TEXT("Stationary") : TEXT("Movable");
	}

	FString PhysicsNormalize(const FString& In)
	{
		FString Out = In.ToLower().Replace(TEXT("_"), TEXT("")).Replace(TEXT(" "), TEXT(""));
		Out.RemoveFromStart(TEXT("ecc"));
		return Out;
	}

	/** Channels that are configured as trace types or object types (unused custom channels are skipped). */
	TArray<FPhysicsToolChannelInfo> PhysicsKnownChannels(TArray<ECollisionChannel>* OutChannels = nullptr)
	{
		TArray<FPhysicsToolChannelInfo> Out;
		const UCollisionProfile* Profile = UCollisionProfile::Get();
		for (int32 i = 0; i < static_cast<int32>(ECC_OverlapAll_Deprecated); ++i)
		{
			const ECollisionChannel C = static_cast<ECollisionChannel>(i);
			const bool bTrace = Profile->ConvertToTraceType(C) != TraceTypeQuery_MAX;
			const bool bObject = Profile->ConvertToObjectType(C) != ObjectTypeQuery_MAX;
			if (!bTrace && !bObject)
			{
				continue;
			}
			FPhysicsToolChannelInfo& I = Out.AddDefaulted_GetRef();
			I.Name = Profile->ReturnChannelNameFromContainerIndex(i).ToString();
			I.bTrace = bTrace;
			I.bObjectType = bObject;
			if (OutChannels)
			{
				OutChannels->Add(C);
			}
		}
		return Out;
	}

	/** Resolves a channel name ("Visibility", "world_static", "ECC_Pawn"); fails the call with the known names. */
	bool PhysicsResolveChannel(const FString& Name, ECollisionChannel& Out)
	{
		TArray<ECollisionChannel> Channels;
		const TArray<FPhysicsToolChannelInfo> Infos = PhysicsKnownChannels(&Channels);
		const FString Want = PhysicsNormalize(Name);
		TArray<FString> Names;
		for (int32 i = 0; i < Infos.Num(); ++i)
		{
			if (PhysicsNormalize(Infos[i].Name) == Want)
			{
				Out = Channels[i];
				return true;
			}
			Names.Add(Infos[i].Name);
		}
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown collision channel '%s'"), *Name),
			TEXT("Channels: ") + FString::Join(Names, TEXT(", ")) + TEXT(" (see physics.get_collision_channels)"));
		return false;
	}

	FPhysicsToolHit PhysicsMakeHit(const FHitResult& Hit)
	{
		FPhysicsToolHit J;
		if (AActor* A = Hit.GetActor())
		{
			J.Actor = RuntimeToolCall::MakeReference(A);
		}
		if (const UPrimitiveComponent* C = Hit.GetComponent())
		{
			J.Component = C->GetName();
		}
		J.Location = Hit.ImpactPoint;
		J.Normal = Hit.ImpactNormal;
		J.Distance = static_cast<float>(RuntimeJson::Round(Hit.Distance));
		return J;
	}

	FString PhysicsOwnerRef(const UPrimitiveComponent* P)
	{
		return RuntimeToolCall::MakeReference(P->GetOwner() ? static_cast<const UObject*>(P->GetOwner()) : P);
	}

	/** Turns simulation on/off. Undo restores the flag, mobility and puts the actor back where it was (re-attached). */
	class FSimulateCommand final : public IRuntimeCommand
	{
	public:
		FSimulateCommand(UPrimitiveComponent* InPrim, bool bInEnable, const FString& InRef) : Prim(InPrim), bEnable(bInEnable), Ref(InRef) {}

		virtual FString GetLabel() const override { return FString::Printf(TEXT("%s physics %s"), bEnable ? TEXT("simulate") : TEXT("stop"), *Ref); }

		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override
		{
			UPrimitiveComponent* P = Prim.Get();
			AActor* Owner = P ? P->GetOwner() : nullptr;
			if (!P || !Owner)
			{
				OutError = TEXT("component no longer exists");
				return false;
			}
			bWasSimulating = P->IsSimulatingPhysics();
			OldParent = Owner->GetAttachParentActor();
			OldRelative = P->GetRelativeTransform();
			OldWorld = P->GetComponentTransform();
			OldMobility = P->Mobility;
			return Apply(Context, bEnable);
		}

		virtual bool Undo(FRuntimeCommandContext& Context) override
		{
			UPrimitiveComponent* P = Prim.Get();
			AActor* Owner = P ? P->GetOwner() : nullptr;
			if (!P || !Owner)
			{
				return false;
			}
			P->SetSimulatePhysics(bWasSimulating);
			if (!bWasSimulating)
			{
				P->SetPhysicsLinearVelocity(FVector::ZeroVector);
				P->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				if (P == Owner->GetRootComponent())
				{
					if (AActor* Parent = OldParent.Get(); Parent && Owner->GetAttachParentActor() != Parent)
					{
						Owner->AttachToActor(Parent, FAttachmentTransformRules::KeepRelativeTransform);
					}
					P->SetRelativeTransform(OldRelative, false, nullptr, ETeleportType::ResetPhysics);
					if (!OldParent.IsValid())
					{
						P->SetWorldTransform(OldWorld, false, nullptr, ETeleportType::ResetPhysics);
					}
				}
				else
				{
					P->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
					P->SetRelativeTransform(OldRelative, false, nullptr, ETeleportType::ResetPhysics);
				}
				P->SetMobility(OldMobility);
			}
			Sync(Context, Owner);
			return true;
		}

		virtual bool Redo(FRuntimeCommandContext& Context) override { return Apply(Context, bEnable); }

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("target"), Ref);
			D->SetBoolField(TEXT("simulating"), bForUndo ? bWasSimulating : bEnable);
			return D;
		}

	private:
		bool Apply(FRuntimeCommandContext& Context, bool bValue)
		{
			UPrimitiveComponent* P = Prim.Get();
			if (!P)
			{
				return false;
			}
			if (bValue)
			{
				P->SetMobility(EComponentMobility::Movable);
			}
			P->SetSimulatePhysics(bValue);
			Sync(Context, P->GetOwner());
			return true;
		}

		static void Sync(FRuntimeCommandContext& Context, AActor* Owner)
		{
			if (Context.Graph && Owner)
			{
				if (const FRuntimeSceneNode* Node = Context.Graph->FindByActor(Owner))
				{
					Context.Graph->SyncParentFromAttachment(Node->Id);
				}
			}
		}

		TWeakObjectPtr<UPrimitiveComponent> Prim;
		bool bEnable;
		FString Ref;
		bool bWasSimulating = false;
		TWeakObjectPtr<AActor> OldParent;
		FTransform OldRelative;
		FTransform OldWorld;
		TEnumAsByte<EComponentMobility::Type> OldMobility = EComponentMobility::Movable;
	};
}

FPhysicsToolCollisionChannels URuntimePhysicsToolset::GetCollisionChannels()
{
	FPhysicsToolCollisionChannels Out;
	Out.Channels = PhysicsKnownChannels();
	TArray<TSharedPtr<FName>> Names;
	UCollisionProfile::GetProfileNames(Names);
	for (const TSharedPtr<FName>& N : Names)
	{
		if (N.IsValid())
		{
			Out.Profiles.Add(N->ToString());
		}
	}
	Out.Profiles.Sort();
	return Out;
}

FPhysicsToolTraceResult URuntimePhysicsToolset::LineTrace(const FVector& Start, const FVector& End, const FVector& Direction, const TArray<AActor*>& Ignore,
	float Distance, const FString& Channel, bool bMulti, bool bComplex, int32 Limit)
{
	FPhysicsToolTraceResult Out;
	FToolInvocation* Inv = RuntimeToolCall::Current();
	FVector To = End;
	if (!Inv->Has(TEXT("end")))
	{
		if (!Inv->Has(TEXT("direction")) || Direction.IsNearlyZero())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give end, or a non-zero direction (+ distance)"),
				TEXT("e.g. {\"start\":[0,0,500],\"direction\":[0,0,-1]}"));
			return Out;
		}
		To = Start + Direction.GetSafeNormal() * Distance;
	}
	ECollisionChannel C = ECC_Visibility;
	if (!Channel.IsEmpty() && !PhysicsResolveChannel(Channel, C))
	{
		return Out;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsTrace), bComplex);
	for (AActor* A : Ignore)
	{
		if (A)
		{
			Params.AddIgnoredActor(A);
		}
	}
	UWorld* World = RuntimeToolCall::GetWorld();
	if (bMulti)
	{
		TArray<FHitResult> Results;
		World->LineTraceMultiByChannel(Results, Start, To, C, Params);
		const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
		for (const FHitResult& H : Results)
		{
			if (Out.Hits.Num() >= Max)
			{
				break;
			}
			Out.Hits.Add(PhysicsMakeHit(H));
		}
	}
	else
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, To, C, Params))
		{
			Out.Hits.Add(PhysicsMakeHit(Hit));
		}
	}
	Out.bHit = Out.Hits.Num() > 0;
	return Out;
}

FPhysicsToolOverlapResult URuntimePhysicsToolset::OverlapTest(const FVector& Center, const FVector& Size, EPhysicsToolOverlapShape Shape, float Radius,
	const FString& Channel, int32 Limit)
{
	FPhysicsToolOverlapResult Out;
	FCollisionShape CollisionShape;
	if (Shape == EPhysicsToolOverlapShape::Box)
	{
		if (!RuntimeToolCall::Current()->Has(TEXT("size")) || Size.GetMin() <= 0.0)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("a box needs size [x,y,z] > 0"), TEXT("e.g. {\"shape\":\"Box\",\"size\":[200,200,200]}"));
			return Out;
		}
		CollisionShape = FCollisionShape::MakeBox(Size * 0.5);
	}
	else
	{
		if (Radius <= 0.f)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("radius %g must be > 0"), Radius));
			return Out;
		}
		CollisionShape = FCollisionShape::MakeSphere(Radius);
	}
	TArray<FOverlapResult> Results;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsOverlap), false);
	UWorld* World = RuntimeToolCall::GetWorld();
	if (Channel.IsEmpty())
	{
		World->OverlapMultiByObjectType(Results, Center, FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), CollisionShape, Params);
	}
	else
	{
		ECollisionChannel C = ECC_Visibility;
		if (!PhysicsResolveChannel(Channel, C))
		{
			return Out;
		}
		World->OverlapMultiByChannel(Results, Center, FQuat::Identity, C, CollisionShape, Params);
	}
	TArray<FString> Actors;
	for (const FOverlapResult& R : Results)
	{
		if (AActor* A = R.GetActor())
		{
			Actors.AddUnique(RuntimeToolCall::MakeReference(A));
		}
	}
	Out.Total = Actors.Num();
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	if (Actors.Num() > Max)
	{
		Actors.SetNum(Max);
		Out.bTruncated = true;
	}
	Out.Actors = MoveTemp(Actors);
	return Out;
}

FPhysicsToolState URuntimePhysicsToolset::GetPhysics(UPrimitiveComponent* Target)
{
	FPhysicsToolState Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, path, actor:Name or actor#Component"));
		return Out;
	}
	UPrimitiveComponent* P = Target;
	Out.Component = P->GetName();
	Out.bSimulating = P->IsSimulatingPhysics();
	Out.bGravity = P->IsGravityEnabled();
	Out.CollisionProfile = P->GetCollisionProfileName().ToString();
	Out.bCollision = P->IsCollisionEnabled();
	Out.Mobility = PhysicsMobilityName(P->Mobility);
	if (const FBodyInstance* Body = P->GetBodyInstance(); Body && Body->IsValidBodyInstance())
	{
		Out.MassKg = static_cast<float>(RuntimeJson::Round(Body->GetBodyMass()));
	}
	if (Out.bSimulating)
	{
		Out.Velocity = P->GetPhysicsLinearVelocity();
		Out.AngularVelocity = P->GetPhysicsAngularVelocityInDegrees();
	}
	if (const USkeletalMeshComponent* SK = Cast<USkeletalMeshComponent>(P))
	{
		for (const FBodyInstance* B : SK->Bodies)
		{
			const UBodySetup* Setup = B ? B->GetBodySetup() : nullptr;
			if (!Setup)
			{
				continue;
			}
			FPhysicsToolBodyInfo& I = Out.Bodies.AddDefaulted_GetRef();
			I.Bone = Setup->BoneName.ToString();
			I.bSimulating = B->IsInstanceSimulatingPhysics();
			I.MassKg = B->IsValidBodyInstance() ? static_cast<float>(RuntimeJson::Round(B->GetBodyMass())) : 0.f;
		}
	}
	return Out;
}

FPhysicsToolSimulateResult URuntimePhysicsToolset::SetSimulate(UPrimitiveComponent* Target, bool bEnabled)
{
	FPhysicsToolSimulateResult Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, path, actor:Name or actor#Component"));
		return Out;
	}
	if (bEnabled && !Target->IsCollisionEnabled())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s has no collision, so physics cannot simulate it"), *Target->GetName()),
			TEXT("Enable collision on the component first (e.g. reflection: SetCollisionEnabled QueryAndPhysics), or target another component with actor#Component"));
		return Out;
	}
	const EComponentMobility::Type OldMobility = Target->Mobility;
	const FString Ref = PhysicsOwnerRef(Target);
	if (RuntimeToolCall::RunCommand(MakeUnique<FSimulateCommand>(Target, bEnabled, Ref)))
	{
		Out.Target = Ref;
		Out.bSimulating = Target->IsSimulatingPhysics();
		Out.bMobilityChanged = bEnabled && OldMobility != EComponentMobility::Movable;
		if (Out.bMobilityChanged)
		{
			Out.Note = FString::Printf(TEXT("mobility changed from %s to Movable so it can simulate; undo restores it"), PhysicsMobilityName(OldMobility));
		}
	}
	return Out;
}

FPhysicsToolImpulseResult URuntimePhysicsToolset::AddImpulse(UPrimitiveComponent* Target, const FVector& Impulse, const FVector& At, bool bVelocityChange)
{
	FPhysicsToolImpulseResult Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, path, actor:Name or actor#Component"));
		return Out;
	}
	if (!Target->IsSimulatingPhysics())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s is not simulating physics"), *Target->GetName()),
			TEXT("Call physics.set_simulate first"));
		return Out;
	}
	if (RuntimeToolCall::Current()->Has(TEXT("at")))
	{
		Target->AddImpulseAtLocation(bVelocityChange ? Impulse * Target->GetMass() : Impulse, At);
	}
	else
	{
		Target->AddImpulse(Impulse, NAME_None, bVelocityChange);
	}
	Out.Target = PhysicsOwnerRef(Target);
	RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("impulse on %s"), *Out.Target));
	Out.Velocity = Target->GetPhysicsLinearVelocity();
	return Out;
}

FPhysicsToolRagdollResult URuntimePhysicsToolset::Ragdoll(AActor* Target, bool bEnabled)
{
	FPhysicsToolRagdollResult Out;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, path, actor:Name or actor#Component"));
		return Out;
	}
	USkeletalMeshComponent* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh || !Mesh->GetPhysicsAsset())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			FString::Printf(TEXT("%s has no skeletal mesh with a physics asset"), *RuntimeToolCall::MakeReference(Target)),
			TEXT("Ragdolls need a character or skeletal mesh actor; use physics.set_simulate for other objects"));
		return Out;
	}
	TWeakObjectPtr<USkeletalMeshComponent> Weak = Mesh;
	const FName OldProfile = Mesh->GetCollisionProfileName();
	const bool bWasSimulating = Mesh->IsSimulatingPhysics();
	const FTransform OldRelative = Mesh->GetRelativeTransform();
	TWeakObjectPtr<USceneComponent> OldParent = Mesh->GetAttachParent();
	const FString Ref = RuntimeToolCall::MakeReference(Target);
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("target"), Ref);
	Delta->SetBoolField(TEXT("ragdoll"), bEnabled);
	auto DoFn = [Weak, bEnabled](FRuntimeCommandContext&, FString& OutError)
	{
		if (!Weak.IsValid())
		{
			OutError = TEXT("mesh no longer exists");
			return false;
		}
		if (bEnabled)
		{
			Weak->SetCollisionProfileName(FName(TEXT("Ragdoll")));
		}
		Weak->SetAllBodiesSimulatePhysics(bEnabled);
		Weak->WakeAllRigidBodies();
		return true;
	};
	auto UndoFn = [Weak, OldProfile, bWasSimulating, OldRelative, OldParent](FRuntimeCommandContext&)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetAllBodiesSimulatePhysics(bWasSimulating);
		Weak->SetCollisionProfileName(OldProfile);
		if (!bWasSimulating && OldParent.IsValid())
		{
			Weak->AttachToComponent(OldParent.Get(), FAttachmentTransformRules::KeepRelativeTransform);
			Weak->SetRelativeTransform(OldRelative, false, nullptr, ETeleportType::ResetPhysics);
		}
		return true;
	};
	if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(bEnabled ? TEXT("ragdoll") : TEXT("end ragdoll"), DoFn, UndoFn, Delta)))
	{
		Out.Target = Ref;
		Out.bRagdoll = bEnabled;
		Out.Bodies = Mesh->Bodies.Num();
	}
	return Out;
}
