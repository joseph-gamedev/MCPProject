// AI brains: data-driven state machines with vision cones, hearing, steering movement and shooting.
#include "LogicTuning.h"
#include "LogicJsonHelpers.h"
#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* FxTag = TEXT("logic_fx");

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& J, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return J.IsValid() && J->TryGetObjectField(Field, O) ? *O : nullptr;
	}

	TSharedPtr<FJsonObject> FindState(const TSharedPtr<FJsonObject>& Brain, const FString& Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (Brain.IsValid() && Brain->TryGetArrayField(TEXT("states"), States))
		{
			for (const TSharedPtr<FJsonValue>& V : *States)
			{
				const TSharedPtr<FJsonObject> S = V->AsObject();
				if (S.IsValid() && Str(S, TEXT("name")) == Name)
				{
					return S;
				}
			}
		}
		return nullptr;
	}

	const TArray<FString>& MoveModes()
	{
		static const TArray<FString> M = {TEXT("stop"), TEXT("patrol"), TEXT("chase"), TEXT("flee"), TEXT("to_last_known"), TEXT("home"), TEXT("wander"), TEXT("strafe")};
		return M;
	}

	void SetColor(AActor* A, const FLinearColor& C)
	{
		TArray<UPrimitiveComponent*> Prims;
		A->GetComponents(Prims);
		for (UPrimitiveComponent* P : Prims)
		{
			for (int32 i = 0; i < P->GetNumMaterials(); ++i)
			{
				if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(P->GetMaterial(i)))
				{
					M->SetVectorParameterValue(TEXT("Color"), C);
					M->SetVectorParameterValue(TEXT("Emissive"), C);
				}
			}
		}
	}
}

FBox FLogicWorld::GameBox(const AActor* Actor)
{
	TArray<AActor*> All = {const_cast<AActor*>(Actor)};
	Actor->GetAttachedActors(All, false, true);
	FBox Box(ForceInit);
	for (const AActor* A : All)
	{
		if (A != Actor && (A->IsHidden() || A->Tags.Contains(FName(FxTag))))
		{
			continue;
		}
		if (A == Actor && A->Tags.Contains(FName(FxTag)))
		{
			continue;
		}
		FVector O, E;
		A->GetActorBounds(false, O, E, false);
		if (!E.IsNearlyZero())
		{
			Box += FBox(O - E, O + E);
		}
	}
	return Box;
}

bool FLogicWorld::ValidateBrain(const TSharedPtr<FJsonObject>& Def, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
	if (!Def->TryGetArrayField(TEXT("states"), States) || States->Num() == 0)
	{
		OutError = TEXT("a brain needs states: [{name, move, face, fire, icon, speed, transitions:[{when, to}]}]");
		return false;
	}
	TSet<FString> Names;
	for (const TSharedPtr<FJsonValue>& V : *States)
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		const FString Name = Str(S, TEXT("name"));
		if (Name.IsEmpty() || Names.Contains(Name))
		{
			OutError = FString::Printf(TEXT("state names must be unique and non-empty ('%s')"), *Name);
			return false;
		}
		Names.Add(Name);
		const FString Move = Str(S, TEXT("move"), TEXT("stop"));
		if (!MoveModes().Contains(Move))
		{
			OutError = FString::Printf(TEXT("state '%s': move must be one of %s"), *Name, *FString::Join(MoveModes(), TEXT(", ")));
			return false;
		}
	}
	for (const TSharedPtr<FJsonValue>& V : *States)
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Trans = nullptr;
		if (!S->TryGetArrayField(TEXT("transitions"), Trans))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& T : *Trans)
		{
			const TSharedPtr<FJsonObject> To = T->AsObject();
			FString When = Str(To, TEXT("when")), Target = Str(To, TEXT("to")), Err;
			if (!Names.Contains(Target))
			{
				OutError = FString::Printf(TEXT("state '%s': transition to unknown state '%s'"), *Str(S, TEXT("name")), *Target);
				return false;
			}
			if (When.IsEmpty() || !LogicExpr::Parse(When, Err))
			{
				OutError = FString::Printf(TEXT("state '%s': transition 'when' '%s': %s"), *Str(S, TEXT("name")), *When, Err.IsEmpty() ? TEXT("needs an expression") : *Err);
				return false;
			}
		}
	}
	const FString Start = Str(Def, TEXT("start"), Str((*States)[0]->AsObject(), TEXT("name")));
	if (!Names.Contains(Start))
	{
		OutError = FString::Printf(TEXT("start state '%s' does not exist"), *Start);
		return false;
	}
	return true;
}

bool FLogicWorld::CreateBrainVisuals(const FString& NodeId, const TSharedPtr<FJsonObject>& Brain, const FString& Session, FString& OutError)
{
	const TSharedPtr<FJsonObject> Vision = Obj(Brain, TEXT("vision"));
	bool bCone = Vision.IsValid();
	if (Vision.IsValid())
	{
		Vision->TryGetBoolField(TEXT("show_cone"), bCone);
	}
	if (!bCone)
	{
		return true;
	}
	FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(World.Get());
	if (!G)
	{
		return false;
	}
	// Remove an old cone of this node.
	for (const FString& Child : G->CollectSubtree(NodeId))
	{
		const FRuntimeSceneNode* N = G->Find(Child);
		if (N && N->Actor.IsValid() && N->Actor->Tags.Contains(FName(FxTag)) && N->Name.EndsWith(TEXT("vision")))
		{
			G->RemoveNode(Child, true);
		}
	}
	const double R = Num(Vision, TEXT("radius"), 1200.0);
	const double Ang = Num(Vision, TEXT("angle"), 90.0);
	const int32 Seg = 18;
	TArray<TSharedPtr<FJsonValue>> Verts = {RuntimeJson::Vec(FVector(0, 0, 0))};
	TArray<TSharedPtr<FJsonValue>> Tris;
	for (int32 i = 0; i <= Seg; ++i)
	{
		const double A = FMath::DegreesToRadians(-Ang / 2 + Ang * i / Seg);
		Verts.Add(RuntimeJson::Vec(FVector(R * FMath::Cos(A), R * FMath::Sin(A), 0)));
	}
	for (int32 i = 1; i <= Seg; ++i)
	{
		for (const TArray<int32>& T : {TArray<int32>{0, i, i + 1}, TArray<int32>{0, i + 1, i}})
		{
			TArray<TSharedPtr<FJsonValue>> Idx;
			for (int32 X : T) Idx.Add(MakeShared<FJsonValueNumber>(X));
			Tris.Add(MakeShared<FJsonValueArray>(Idx));
		}
	}
	TSharedRef<FJsonObject> Part = MakeShared<FJsonObject>();
	Part->SetStringField(TEXT("shape"), TEXT("raw"));
	Part->SetArrayField(TEXT("vertices"), Verts);
	Part->SetArrayField(TEXT("triangles"), Tris);
	TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("name"), TEXT("vision"));
	Args->SetStringField(TEXT("parent"), NodeId);
	Args->SetField(TEXT("location"), RuntimeJson::Vec(FVector(0, 0, 4)));
	Args->SetArrayField(TEXT("parts"), {MakeShared<FJsonValueObject>(Part)});
	Args->SetStringField(TEXT("material"), TEXT("glass"));
	Args->SetField(TEXT("color"), RuntimeJson::Vec(FVector(1.0, 0.85, 0.2)));
	Args->SetNumberField(TEXT("opacity"), Num(Vision, TEXT("opacity"), 0.22));
	Args->SetBoolField(TEXT("repair_coplanar"), false);
	FToolCallerContext Caller;
	Caller.SessionId = Session;
	const FToolResult R2 = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("build.create_assembly"), Args, Caller, 1);
	FString Id;
	if (!R2.IsOk() || !R2.Payload->TryGetStringField(TEXT("id"), Id))
	{
		OutError = TEXT("could not create the vision cone: ") + R2.Message;
		return false;
	}
	if (const FRuntimeSceneNode* N = G->Find(Id); N && N->Actor.IsValid())
	{
		N->Actor->Tags.Add(FName(FxTag));
		N->Actor->SetActorEnableCollision(false);
		N->Actor->SetActorRelativeRotation(FRotator::ZeroRotator);
	}
	return true;
}

void FLogicWorld::ResetBrains()
{
	Agents.Reset();
	FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(World.Get());
	if (!G)
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonObject>>& P : BrainAssign)
	{
		const FString BrainName = Str(P.Value, TEXT("brain"));
		const TSharedPtr<FJsonObject>* Brain = BrainDefs.Find(BrainName);
		if (!Brain)
		{
			continue;
		}
		TArray<FString> Ids;
		FString Err;
		ResolveNodes({P.Key}, Ids, Err);
		for (const FString& Id : Ids)
		{
			const FRuntimeSceneNode* N = G->Find(Id);
			if (!N || !N->Actor.IsValid())
			{
				continue;
			}
			FBrainAgent Ag;
			Ag.Node = Id;
			Ag.Brain = BrainName;
			Ag.Home = N->Actor->GetActorLocation();
			Ag.Yaw = N->Actor->GetActorRotation().Yaw;
			Ag.Rot = N->Actor->GetActorQuat();
			const TSharedPtr<FJsonObject> Patrol = Obj(P.Value, TEXT("patrol"));
			Ag.Patrol.Add(Ag.Home);
			const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
			if (Patrol.IsValid() && Patrol->TryGetArrayField(TEXT("points"), Points))
			{
				Ag.Patrol.Reset();
				for (const TSharedPtr<FJsonValue>& V : *Points)
				{
					FVector Pt;
					if (RuntimeJson::ToVector(V, Pt))
					{
						Ag.Patrol.Add(Ag.Home + Pt);
					}
				}
			}
			else if (Patrol.IsValid())
			{
				FVector Axis(1, 0, 0);
				RuntimeJson::ToVector(Patrol->TryGetField(TEXT("axis")), Axis);
				Ag.Patrol.Add(Ag.Home + Axis.GetSafeNormal() * Num(Patrol, TEXT("distance"), 600.0));
			}
			Ag.State = Str(*Brain, TEXT("start"));
			if (Ag.State.IsEmpty())
			{
				const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
				(*Brain)->TryGetArrayField(TEXT("states"), States);
				Ag.State = States && States->Num() > 0 ? Str((*States)[0]->AsObject(), TEXT("name")) : FString();
			}
			Ag.Icon = Str(FindState(*Brain, Ag.State), TEXT("icon"));
			Ag.NextPerceive = Random.FRand() * 0.1;
			Ag.NextDecide = Random.FRand() * 0.15;
			for (const FString& Child : G->CollectSubtree(Id))
			{
				const FRuntimeSceneNode* C = G->Find(Child);
				if (C && C->Actor.IsValid() && C->Actor->Tags.Contains(FName(FxTag)) && C->Name.EndsWith(TEXT("vision")))
				{
					Ag.Cone = Child;
				}
			}
			Agents.Add(Id, Ag);
		}
	}
}

void FLogicWorld::HearNoise(const FVector& At, double Radius)
{
	for (TPair<FString, FBrainAgent>& P : Agents)
	{
		const TSharedPtr<FJsonObject>* Brain = BrainDefs.Find(P.Value.Brain);
		const double Hearing = Num(Obj(Brain ? *Brain : nullptr, TEXT("hearing")), TEXT("radius"), 0.0);
		if (Hearing <= 0.0 || P.Value.bSees)
		{
			continue;
		}
		FVector Loc;
		if (OwnerLocation(P.Key, Loc) && FVector::Dist2D(Loc, At) <= FMath::Min(Hearing, Radius))
		{
			P.Value.bHeard = true;
			P.Value.LastKnown = At;
			P.Value.bHasLastKnown = true;
		}
	}
}

void FLogicWorld::ChangeBrainState(FBrainAgent& Ag, const FString& To)
{
	const FString From = Ag.State;
	Ag.State = To;
	Ag.TimeInState = 0.0;
	Ag.bHeard = false;
	Ag.bAtGoal = false;
	const TSharedPtr<FJsonObject>* Brain = BrainDefs.Find(Ag.Brain);
	Ag.Icon = Str(FindState(Brain ? *Brain : nullptr, To), TEXT("icon"));
	FLogicEvent E;
	E.On = TEXT("state");
	E.Name = To;
	E.Target = Ag.Node;
	E.Data.M->Add(TEXT("from"), FLogicValue::MakeString(From));
	E.Data.M->Add(TEXT("to"), FLogicValue::MakeString(To));
	Queue.Add(E);
}

bool FLogicWorld::BrainSteer(FBrainAgent& Ag, AActor* A, const FVector& Goal, double Speed, double Dt)
{
	const FVector Pos = A->GetActorLocation();
	FVector Dir = Goal - Pos;
	Dir.Z = 0;
	const double Dist = Dir.Size();
	if (Dist < 30.0 || Speed <= 0.0)
	{
		return false;
	}
	Dir /= Dist;
	UWorld* W = World.Get();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(LogicBrainSteer), false);
	TArray<AActor*> Ignore = {A};
	A->GetAttachedActors(Ignore, false, true);
	Q.AddIgnoredActors(Ignore);
	if (ACharacter* C = Player.Get())
	{
		Q.AddIgnoredActor(C);
	}
	const FVector Eye = Pos + FVector(0, 0, 50);
	FVector Chosen = FVector::ZeroVector;
	for (const double Turn : {0.0, 35.0, -35.0, 70.0, -70.0, 110.0, -110.0})
	{
		const FVector D = Dir.RotateAngleAxis(Turn, FVector::UpVector);
		FHitResult Hit;
		if (!W || !W->SweepSingleByChannel(Hit, Eye, Eye + D * LogicTuning::Get(TEXT("brain.steer_probe")), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(static_cast<float>(LogicTuning::Get(TEXT("brain.steer_radius")))), Q))
		{
			Chosen = D;
			break;
		}
	}
	if (Chosen.IsNearlyZero())
	{
		return false;
	}
	const double Step = FMath::Min(Dist, Speed * Dt);
	A->SetActorLocation(FVector(Pos.X + Chosen.X * Step, Pos.Y + Chosen.Y * Step, Ag.Home.Z));
	return true;
}

void FLogicWorld::TickBrains(double Dt)
{
	if (Agents.Num() == 0)
	{
		return;
	}
	FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(World.Get());
	ACharacter* C = Player.Get();
	const FHealthState* PlayerHealth = Healths.Find(TEXT("player"));
	const bool bPlayerAlive = C && (!PlayerHealth || !PlayerHealth->bDead) && !bFrozen;
	const FVector PlayerLoc = C ? C->GetActorLocation() : FVector::ZeroVector;
	UWorld* W = World.Get();
	for (TPair<FString, FBrainAgent>& P : Agents)
	{
		FBrainAgent& Ag = P.Value;
		const FRuntimeSceneNode* N = G ? G->Find(Ag.Node) : nullptr;
		AActor* A = N ? N->Actor.Get() : nullptr;
		const FHealthState* H = Healths.Find(Ag.Node);
		if (!A || A->IsHidden() || IsHidden(Ag.Node) || (H && H->bDead))
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* BrainP = BrainDefs.Find(Ag.Brain);
		if (!BrainP)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& Brain = *BrainP;
		const TSharedPtr<FJsonObject> Vision = Obj(Brain, TEXT("vision"));
		TSharedPtr<FJsonObject> St = FindState(Brain, Ag.State);
		Ag.TimeInState += Dt;

		// ---- perception
		Ag.NextPerceive -= Dt;
		if (Ag.NextPerceive <= 0.0)
		{
			Ag.NextPerceive = LogicTuning::Get(TEXT("brain.perceive_interval"));
			bool bSees = false;
			if (bPlayerAlive && Vision.IsValid())
			{
				const FVector Eye = A->GetActorLocation() + FVector(0, 0, Num(Vision, TEXT("eye_height"), 100.0));
				const FVector To = PlayerLoc - Eye;
				const bool bFly = Obj(Brain, TEXT("flight")).IsValid();
				const double Dist2D = bFly ? To.Size() : FVector(To.X, To.Y, 0).Size();
				const FVector Forward = bFly ? Ag.Rot.GetForwardVector() : FRotator(0, Ag.Yaw, 0).Vector();
				const FVector ToDir = bFly ? To.GetSafeNormal() : FVector(To.X, To.Y, 0).GetSafeNormal();
				const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToDir), -1.0, 1.0)));
				const bool bClose = Dist2D < Num(Vision, TEXT("near"), LogicTuning::Get(TEXT("brain.near_notice")));
				if (bClose || (Dist2D <= Num(Vision, TEXT("radius"), 1200.0) && Angle <= Num(Vision, TEXT("angle"), 90.0) / 2.0))
				{
					FCollisionQueryParams Q(SCENE_QUERY_STAT(LogicVision), false);
					TArray<AActor*> Ignore = {A, C};
					A->GetAttachedActors(Ignore, false, true);
					C->GetAttachedActors(Ignore, false, true);
					Q.AddIgnoredActors(Ignore);
					FHitResult Hit;
					bSees = !W || !W->LineTraceSingleByChannel(Hit, Eye, PlayerLoc, ECC_Visibility, Q);
				}
			}
			Ag.bSees = bSees;
			if (bSees)
			{
				Ag.SinceSeen = 0.0;
				Ag.LastKnown = PlayerLoc;
				Ag.bHasLastKnown = true;
			}
		}
		if (!Ag.bSees)
		{
			Ag.SinceSeen += Dt;
		}

		// ---- decisions
		Ag.NextDecide -= Dt;
		if (Ag.NextDecide <= 0.0 && St.IsValid())
		{
			Ag.NextDecide = Num(Brain, TEXT("decide_every"), LogicTuning::Get(TEXT("brain.decide_interval")));
			const TArray<TSharedPtr<FJsonValue>>* Trans = nullptr;
			if (St->TryGetArrayField(TEXT("transitions"), Trans))
			{
				FFrame Frame;
				FVector Loc = A->GetActorLocation();
				Frame.Locals.Add(TEXT("self"), FLogicValue::MakeString(Ag.Node));
				Frame.Locals.Add(TEXT("sees"), FLogicValue::MakeBool(Ag.bSees));
				Frame.Locals.Add(TEXT("heard"), FLogicValue::MakeBool(Ag.bHeard));
				Frame.Locals.Add(TEXT("distance"), FLogicValue::MakeNumber(C ? FVector::Dist2D(Loc, PlayerLoc) : 1e9));
				Frame.Locals.Add(TEXT("time_in_state"), FLogicValue::MakeNumber(Ag.TimeInState));
				Frame.Locals.Add(TEXT("since_seen"), FLogicValue::MakeNumber(Ag.SinceSeen));
				Frame.Locals.Add(TEXT("at_goal"), FLogicValue::MakeBool(Ag.bAtGoal));
				Frame.Locals.Add(TEXT("at_home"), FLogicValue::MakeBool(FVector::Dist2D(Loc, Ag.Home) < LogicTuning::Get(TEXT("brain.home_reach"))));
				Frame.Locals.Add(TEXT("has_last_known"), FLogicValue::MakeBool(Ag.bHasLastKnown));
				Frame.Locals.Add(TEXT("health"), FLogicValue::MakeNumber(H ? H->Current : 0.0));
				Frame.Locals.Add(TEXT("health_pct"), FLogicValue::MakeNumber(H && H->Max > 0 ? H->Current / H->Max : 1.0));
				Frame.Locals.Add(TEXT("player_alive"), FLogicValue::MakeBool(bPlayerAlive));
				for (const TSharedPtr<FJsonValue>& T : *Trans)
				{
					const TSharedPtr<FJsonObject> To = T->AsObject();
					FLogicScope Scope = MakeScope(Frame);
					const FLogicValue V = LogicExpr::Evaluate(Str(To, TEXT("when")), Scope);
					if (Scope.Error.IsEmpty() && V.Truthy())
					{
						ChangeBrainState(Ag, Str(To, TEXT("to")));
						St = FindState(Brain, Ag.State);
						break;
					}
				}
			}
		}
		if (!St.IsValid())
		{
			continue;
		}
		if (Obj(Brain, TEXT("flight")).IsValid())
		{
			TickFlightAgent(Ag, A, Brain, St, Dt, PlayerLoc, bPlayerAlive);
			continue;
		}

		// ---- movement
		const FString Move = Str(St, TEXT("move"), TEXT("stop"));
		const double Speed = Num(St, TEXT("speed"), Move == TEXT("chase") ? 330.0 : Move == TEXT("patrol") ? 150.0 : 220.0);
		FVector Goal = A->GetActorLocation();
		bool bHasGoal = false;
		if (Move == TEXT("patrol") && Ag.Patrol.Num() > 1)
		{
			Goal = Ag.Patrol[Ag.PatrolIndex];
			bHasGoal = true;
			if (FVector::Dist2D(A->GetActorLocation(), Goal) < LogicTuning::Get(TEXT("brain.waypoint_reach")))
			{
				if (Ag.PatrolIndex + Ag.PatrolDir >= Ag.Patrol.Num() || Ag.PatrolIndex + Ag.PatrolDir < 0)
				{
					Ag.PatrolDir = -Ag.PatrolDir;
				}
				Ag.PatrolIndex += Ag.PatrolDir;
			}
		}
		else if ((Move == TEXT("chase") || Move == TEXT("strafe")) && C)
		{
			const double Keep = Num(St, TEXT("keep_distance"), 450.0);
			const FVector Target = Ag.bSees ? PlayerLoc : Ag.LastKnown;
			const double D = FVector::Dist2D(A->GetActorLocation(), Target);
			if (Move == TEXT("strafe") && D <= Keep * 1.3)
			{
				const FVector Side = FVector::CrossProduct((Target - A->GetActorLocation()).GetSafeNormal2D(), FVector::UpVector) * (FMath::Sin(Ag.TimeInState * 0.8) > 0 ? 1.0 : -1.0);
				Goal = A->GetActorLocation() + Side * 200.0;
				bHasGoal = true;
			}
			else if (D > Keep)
			{
				Goal = Target;
				bHasGoal = true;
			}
		}
		else if (Move == TEXT("flee") && C)
		{
			Goal = A->GetActorLocation() + (A->GetActorLocation() - PlayerLoc).GetSafeNormal2D() * 400.0;
			bHasGoal = true;
		}
		else if (Move == TEXT("to_last_known") && Ag.bHasLastKnown)
		{
			Goal = Ag.LastKnown;
			bHasGoal = true;
			Ag.bAtGoal = FVector::Dist2D(A->GetActorLocation(), Goal) < LogicTuning::Get(TEXT("brain.goal_reach"));
		}
		else if (Move == TEXT("home"))
		{
			Goal = Ag.Home;
			bHasGoal = true;
		}
		else if (Move == TEXT("wander"))
		{
			if (PlayTime >= Ag.NextWander)
			{
				const double R = Num(St, TEXT("radius"), 500.0);
				Ag.Wander = Ag.Home + FVector(Random.FRandRange(-R, R), Random.FRandRange(-R, R), 0);
				Ag.NextWander = PlayTime + Random.FRandRange(2.0, 4.0);
			}
			Goal = Ag.Wander;
			bHasGoal = true;
		}
		const FVector Before = A->GetActorLocation();
		Ag.bMoving = bHasGoal && BrainSteer(Ag, A, Goal, Speed, Dt);
		const FVector Moved = A->GetActorLocation() - Before;
		if (bHasGoal && Move == TEXT("to_last_known") && !Ag.bMoving)
		{
			Ag.bAtGoal = true;
		}

		// ---- facing and animation
		double WantYaw = Ag.Yaw;
		const FString Face = Str(St, TEXT("face"), TEXT("move"));
		if (Face == TEXT("target") && C && (Ag.bSees || Ag.bHasLastKnown))
		{
			WantYaw = ((Ag.bSees ? PlayerLoc : Ag.LastKnown) - A->GetActorLocation()).Rotation().Yaw;
		}
		else if (Face == TEXT("look_around"))
		{
			WantYaw = Ag.Yaw + 60.0 * Dt * (FMath::Sin(Ag.TimeInState * 0.9) > 0 ? 1.0 : -1.0) * 3.0;
		}
		else if (!Moved.IsNearlyZero(0.1))
		{
			WantYaw = Moved.Rotation().Yaw;
		}
		const double Turn = Num(Brain, TEXT("turn_speed"), LogicTuning::Get(TEXT("brain.turn_speed"))) * Dt;
		Ag.Yaw += FMath::Clamp(FRotator::NormalizeAxis(WantYaw - Ag.Yaw), -Turn, Turn);
		if (Ag.bMoving)
		{
			Ag.Phase += Dt * Speed * 0.035;
		}
		Ag.Roll = FMath::FInterpTo(Ag.Roll, Ag.bMoving ? FMath::Sin(Ag.Phase) * LogicTuning::Get(TEXT("brain.waddle_roll")) : 0.0, Dt, 12.0);
		const double Bob = Ag.bMoving ? FMath::Abs(FMath::Sin(Ag.Phase)) * LogicTuning::Get(TEXT("brain.waddle_bob")) : 0.0;
		FVector L = A->GetActorLocation();
		L.Z = Ag.Home.Z + Bob;
		A->SetActorLocationAndRotation(L, FRotator(0, Ag.Yaw, Ag.Roll));

		// ---- shooting
		bool bFire = false;
		St->TryGetBoolField(TEXT("fire"), bFire);
		if (bFire && Ag.bSees && C)
		{
			const double Off = FMath::Abs(FRotator::NormalizeAxis((PlayerLoc - A->GetActorLocation()).Rotation().Yaw - Ag.Yaw));
			if (Off < Num(St, TEXT("fire_angle"), 25.0))
			{
				FString Err;
				FireWeapon(Ag.Node, Str(St, TEXT("weapon")), &PlayerLoc, Err);
			}
		}

		// ---- vision cone colour: yellow calm, orange suspicious, red when seeing the player
		if (!Ag.Cone.IsEmpty())
		{
			const FRuntimeSceneNode* ConeN = G->Find(Ag.Cone);
			if (AActor* ConeA = ConeN ? ConeN->Actor.Get() : nullptr)
			{
				FLinearColor Want = Ag.bSees ? FLinearColor(1.f, 0.12f, 0.05f) : (Ag.Icon == TEXT("?") ? FLinearColor(1.f, 0.55f, 0.1f) : FLinearColor(1.f, 0.85f, 0.2f));
				FVector Custom;
				if (const TSharedPtr<FJsonObject> Cc = Obj(St, TEXT("cone")); Cc.IsValid() && RuntimeJson::ToVector(Cc->TryGetField(TEXT("color")), Custom) && !Ag.bSees)
				{
					Want = FLinearColor(Custom.X, Custom.Y, Custom.Z);
				}
				if (!Want.Equals(Ag.ConeColor))
				{
					Ag.ConeColor = Want;
					SetColor(ConeA, Want);
				}
			}
		}
	}
	if (Queue.Num() > 0 && !bProcessing)
	{
		ProcessQueue(nullptr);
	}
}

void FLogicWorld::TickFeel(double Dt)
{
	HitFlash = FMath::Max(0.0, HitFlash - Dt * 2.5);
	Recoil = FMath::FInterpTo(Recoil, 0.0, Dt, 14.0);
	for (int32 i = FloatTexts.Num() - 1; i >= 0; --i)
	{
		FloatTexts[i].Age += Dt;
		if (FloatTexts[i].Age > LogicTuning::Get(TEXT("hud.number_life")))
		{
			FloatTexts.RemoveAt(i);
		}
	}
}
