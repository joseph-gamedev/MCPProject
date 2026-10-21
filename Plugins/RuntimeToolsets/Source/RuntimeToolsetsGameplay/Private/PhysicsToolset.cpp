#include "PhysicsToolset.h"
#include "RuntimeToolHelpers.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

using namespace RuntimeTools;

namespace
{
	ECollisionChannel ReadChannel(const FToolInvocation& Inv)
	{
		const FString C = Inv.GetString(TEXT("channel"), TEXT("visibility"));
		return C == TEXT("camera") ? ECC_Camera : C == TEXT("world_static") ? ECC_WorldStatic : C == TEXT("pawn") ? ECC_Pawn : ECC_Visibility;
	}

	/** Resolves "target" to a primitive component (the actor's root primitive, or target#Component). */
	UPrimitiveComponent* ResolvePrimitive(FToolInvocation& Inv)
	{
		FResolvedTarget T;
		if (!Inv.ResolveTarget(TEXT("target"), T))
		{
			return nullptr;
		}
		UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(T.Component);
		if (!Prim && T.Actor)
		{
			Prim = Cast<UPrimitiveComponent>(T.Actor->GetRootComponent());
		}
		if (!Prim)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target has no primitive root component"), TEXT("Use target#ComponentName"));
		}
		return Prim;
	}

	TSharedRef<FJsonObject> HitJson(FToolInvocation& Inv, const FHitResult& Hit)
	{
		TSharedRef<FJsonObject> J = Obj();
		if (AActor* A = Hit.GetActor())
		{
			J->SetStringField(TEXT("actor"), Ref(Inv, A));
		}
		if (const UPrimitiveComponent* C = Hit.GetComponent())
		{
			J->SetStringField(TEXT("component"), C->GetName());
		}
		J->SetField(TEXT("location"), RuntimeJson::Vec(Hit.ImpactPoint));
		J->SetField(TEXT("normal"), RuntimeJson::Vec(Hit.ImpactNormal));
		J->SetNumberField(TEXT("distance"), RuntimeJson::Round(Hit.Distance));
		return J;
	}

	/** Turns simulation on/off. Undo restores the flag and puts the actor back where it was (re-attached). */
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
			if (bEnable && !P->IsCollisionEnabled())
			{
				OutError = TEXT("component has no collision; physics cannot simulate");
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

static void LineTrace(FToolInvocation& Inv)
{
	FVector Start, End, Direction;
	if (!Inv.GetVector(TEXT("start"), Start))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("start is required"));
		return;
	}
	if (!Inv.GetVector(TEXT("end"), End))
	{
		if (!Inv.GetVector(TEXT("direction"), Direction) || Direction.IsNearlyZero())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give end, or direction (+ distance)"));
			return;
		}
		End = Start + Direction.GetSafeNormal() * Inv.GetNumber(TEXT("distance"), 100000.0);
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsTrace), Inv.GetBool(TEXT("complex"), false));
	for (const FString& Target : Inv.GetStringArray(TEXT("ignore")))
	{
		ERuntimeToolError Error;
		FString Message;
		if (AActor* A = RuntimeObjectResolver::ResolveActor(Target, Inv.GetWorld(), Inv.GetGraph(), Error, Message))
		{
			Params.AddIgnoredActor(A);
		}
	}
	const ECollisionChannel Channel = ReadChannel(Inv);
	TArray<TSharedPtr<FJsonValue>> Hits;
	if (Inv.GetBool(TEXT("multi"), false))
	{
		TArray<FHitResult> Results;
		Inv.GetWorld()->LineTraceMultiByChannel(Results, Start, End, Channel, Params);
		const int32 Limit = Inv.GetLimit();
		for (const FHitResult& H : Results)
		{
			if (Hits.Num() >= Limit)
			{
				break;
			}
			Hits.Add(MakeShared<FJsonValueObject>(HitJson(Inv, H)));
		}
	}
	else
	{
		FHitResult Hit;
		if (Inv.GetWorld()->LineTraceSingleByChannel(Hit, Start, End, Channel, Params))
		{
			Hits.Add(MakeShared<FJsonValueObject>(HitJson(Inv, Hit)));
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetBoolField(TEXT("hit"), Hits.Num() > 0);
	Out->SetArrayField(TEXT("hits"), Hits);
	Inv.Succeed(Out);
}

static void OverlapTest(FToolInvocation& Inv)
{
	FVector Center;
	if (!Inv.GetVector(TEXT("center"), Center))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("center is required"));
		return;
	}
	FCollisionShape Shape;
	if (Inv.GetString(TEXT("shape"), TEXT("sphere")) == TEXT("box"))
	{
		FVector Size;
		if (!Inv.GetVector(TEXT("size"), Size) || Size.GetMin() <= 0.0)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("box needs size [x,y,z] > 0"));
			return;
		}
		Shape = FCollisionShape::MakeBox(Size * 0.5);
	}
	else
	{
		Shape = FCollisionShape::MakeSphere(static_cast<float>(Inv.GetNumber(TEXT("radius"), 100.0)));
	}
	TArray<FOverlapResult> Results;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsOverlap), false);
	Inv.GetWorld()->OverlapMultiByObjectType(Results, Center, FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Shape, Params);
	TArray<FString> Actors;
	for (const FOverlapResult& R : Results)
	{
		if (AActor* A = R.GetActor())
		{
			Actors.AddUnique(Ref(Inv, A));
		}
	}
	const int32 Limit = Inv.GetLimit();
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Actors.Num());
	if (Actors.Num() > Limit)
	{
		Actors.SetNum(Limit);
		Out->SetBoolField(TEXT("truncated"), true);
	}
	Out->SetField(TEXT("actors"), RuntimeJson::Strings(Actors));
	Inv.Succeed(Out);
}

static void GetPhysics(FToolInvocation& Inv)
{
	UPrimitiveComponent* P = ResolvePrimitive(Inv);
	if (!P)
	{
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("component"), P->GetName());
	Out->SetBoolField(TEXT("simulating"), P->IsSimulatingPhysics());
	Out->SetBoolField(TEXT("gravity"), P->IsGravityEnabled());
	Out->SetStringField(TEXT("collision_profile"), P->GetCollisionProfileName().ToString());
	Out->SetBoolField(TEXT("collision"), P->IsCollisionEnabled());
	if (P->IsSimulatingPhysics())
	{
		Out->SetNumberField(TEXT("mass_kg"), RuntimeJson::Round(P->GetMass()));
		Out->SetField(TEXT("velocity"), RuntimeJson::Vec(P->GetPhysicsLinearVelocity()));
		Out->SetField(TEXT("angular_velocity"), RuntimeJson::Vec(P->GetPhysicsAngularVelocityInDegrees()));
	}
	Inv.Succeed(Out);
}

static void SetSimulate(FToolInvocation& Inv)
{
	UPrimitiveComponent* P = ResolvePrimitive(Inv);
	if (!P)
	{
		return;
	}
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MakeUnique<FSimulateCommand>(P, Inv.GetBool(TEXT("enabled"), true), Ref(Inv, P->GetOwner())), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void AddImpulse(FToolInvocation& Inv)
{
	UPrimitiveComponent* P = ResolvePrimitive(Inv);
	if (!P)
	{
		return;
	}
	if (!P->IsSimulatingPhysics())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("target is not simulating physics"), TEXT("Call physics.set_simulate first"));
		return;
	}
	FVector Impulse;
	if (!Inv.GetVector(TEXT("impulse"), Impulse))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("impulse is required"));
		return;
	}
	const bool bVelChange = Inv.GetBool(TEXT("velocity_change"), true);
	FVector At;
	if (Inv.GetVector(TEXT("at"), At))
	{
		P->AddImpulseAtLocation(bVelChange ? Impulse * P->GetMass() : Impulse, At);
	}
	else
	{
		P->AddImpulse(Impulse, NAME_None, bVelChange);
	}
	Inv.RecordIrreversible(FString::Printf(TEXT("impulse on %s"), *Ref(Inv, P->GetOwner())));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("target"), Ref(Inv, P->GetOwner()));
	Out->SetBoolField(TEXT("undoable"), false);
	Inv.Succeed(Out);
}

static void Ragdoll(FToolInvocation& Inv)
{
	AActor* Actor = Inv.ResolveActor(TEXT("target"));
	if (!Actor)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh || !Mesh->GetPhysicsAsset())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target has no skeletal mesh with a physics asset"));
		return;
	}
	const bool bEnable = Inv.GetBool(TEXT("enabled"), true);
	TWeakObjectPtr<USkeletalMeshComponent> Weak = Mesh;
	const FName OldProfile = Mesh->GetCollisionProfileName();
	const bool bWasSimulating = Mesh->IsSimulatingPhysics();
	const FTransform OldRelative = Mesh->GetRelativeTransform();
	TWeakObjectPtr<USceneComponent> OldParent = Mesh->GetAttachParent();
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Ref(Inv, Actor));
	Delta->SetBoolField(TEXT("ragdoll"), bEnable);
	auto DoFn = [Weak, bEnable](FRuntimeCommandContext&, FString& Error)
	{
		if (!Weak.IsValid())
		{
			Error = TEXT("mesh no longer exists");
			return false;
		}
		if (bEnable)
		{
			Weak->SetCollisionProfileName(TEXT("Ragdoll"));
		}
		Weak->SetAllBodiesSimulatePhysics(bEnable);
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
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(bEnable ? TEXT("ragdoll") : TEXT("end ragdoll"), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

void FPhysicsToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Channel = []() { return FToolParam::Enum(TEXT("Trace channel"), {TEXT("visibility"), TEXT("camera"), TEXT("world_static"), TEXT("pawn")}).Default(TEXT("visibility")); };

	B.Tool(TEXT("line_trace"), TEXT("Trace a ray; returns hit actor, point, normal, distance."))
		.Args(FToolSchema()
			.Add(TEXT("start"), FToolParam::Vec3(TEXT("Start [x,y,z]")).Required())
			.Add(TEXT("end"), FToolParam::Vec3(TEXT("End [x,y,z]")))
			.Add(TEXT("direction"), FToolParam::Vec3(TEXT("Or a direction")))
			.Add(TEXT("distance"), FToolParam::Number(TEXT("Length with direction cm"), 1.0).Default(100000.0))
			.Add(TEXT("channel"), Channel())
			.Add(TEXT("multi"), FToolParam::Boolean(TEXT("Return every hit")).Default(false))
			.Add(TEXT("complex"), FToolParam::Boolean(TEXT("Trace complex (per-triangle) collision")).Default(false))
			.Add(TEXT("ignore"), FToolParam::StringArray(TEXT("Targets to ignore"), 64))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&LineTrace);

	B.Tool(TEXT("overlap_test"), TEXT("Actors overlapping a sphere or box."))
		.Args(FToolSchema()
			.Add(TEXT("center"), FToolParam::Vec3(TEXT("Centre [x,y,z]")).Required())
			.Add(TEXT("shape"), FToolParam::Enum(TEXT("Shape"), {TEXT("sphere"), TEXT("box")}).Default(TEXT("sphere")))
			.Add(TEXT("radius"), FToolParam::Number(TEXT("Sphere radius cm"), 0.1).Default(100.0))
			.Add(TEXT("size"), FToolParam::Vec3(TEXT("Box size [x,y,z] cm")))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&OverlapTest);

	B.Tool(TEXT("get_physics"), TEXT("Simulation state, mass, velocity and collision of a target."))
		.Args(FToolSchema().Add(TEXT("target"), FToolParam::Target().Required()))
		.RequiresWorld()
		.Handle(&GetPhysics);

	B.Tool(TEXT("set_simulate"), TEXT("Turn physics simulation on/off. Undo puts the object back."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("enabled"), FToolParam::Boolean(TEXT("Simulate")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&SetSimulate);

	B.Tool(TEXT("add_impulse"), TEXT("Push a simulating object. velocity_change ignores mass. Not undoable."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("impulse"), FToolParam::Vec3(TEXT("[x,y,z] (cm/s with velocity_change)")).Required())
			.Add(TEXT("at"), FToolParam::Vec3(TEXT("Apply at this world point (adds spin)")))
			.Add(TEXT("velocity_change"), FToolParam::Boolean(TEXT("Treat impulse as a velocity change")).Default(true)))
		.Irreversible().RequiresWorld()
		.Handle(&AddImpulse);

	B.Tool(TEXT("ragdoll"), TEXT("Switch a skeletal mesh to ragdoll physics (or back)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Actor with a skeletal mesh")).Required())
			.Add(TEXT("enabled"), FToolParam::Boolean(TEXT("Ragdoll on")).Default(true)))
		.Undoable().RequiresWorld()
		.Handle(&Ragdoll);
}
