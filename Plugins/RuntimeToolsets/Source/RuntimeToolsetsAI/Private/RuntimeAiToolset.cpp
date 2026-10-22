#include "RuntimeAiToolset.h"
#include "RuntimeCommand.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolReflection.h"
#include "AIController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"

namespace RtAi
{
	// AddNavigationBoundsUpdateRequest is protected but exported: reach it through a pointer to member of a derived
	// accessor, so navigation bounds can be registered at runtime without an editor-built NavMeshBoundsVolume brush.
	struct FNavSysAccess : public UNavigationSystemV1
	{
		using UNavigationSystemV1::PerformNavigationBoundsUpdate;
	};

	TMap<TWeakObjectPtr<UWorld>, FBox>& RegisteredBounds()
	{
		static TMap<TWeakObjectPtr<UWorld>, FBox> Map;
		return Map;
	}

	UNavigationSystemV1* Nav()
	{
		return FNavigationSystem::GetCurrent<UNavigationSystemV1>(RuntimeToolCall::GetWorld());
	}

	APawn* Agent(AActor* Ref)
	{
		APawn* P = RuntimeAgents::FindAgent(Ref);
		if (!P)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is not an AI agent"), Ref ? *Ref->GetName() : TEXT("target")),
				TEXT("Make it one with ai.create_agent"));
		}
		return P;
	}

	AAIController* Controller(AActor* Ref)
	{
		APawn* P = Agent(Ref);
		AAIController* C = P ? Cast<AAIController>(P->GetController()) : nullptr;
		if (P && !C)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("the agent has no AI controller"), TEXT("ai.create_agent gives it one"));
		}
		return C;
	}

	FRuntimeNavigationStatus Status(UWorld* W)
	{
		FRuntimeNavigationStatus S;
		UNavigationSystemV1* N = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
		if (!N)
		{
			S.Hint = TEXT("This world has no navigation system (WorldSettings > Navigation System Config).");
			return S;
		}
		const ANavigationData* D = N->GetDefaultNavDataInstance();
		S.bHasNavMesh = D != nullptr;
		S.bBuilding = N->IsNavigationBuildInProgress();
		S.bDynamic = D && D->GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic;
		FBox All(ForceInit);
		for (const FNavigationBounds& B : N->GetNavigationBounds())
		{
			All += B.AreaBox;
		}
		if (All.IsValid)
		{
			S.BoundsMin = All.Min;
			S.BoundsMax = All.Max;
		}
		if (!S.bHasNavMesh)
		{
			S.Hint = TEXT("No navmesh yet: call ai.build_navigation after building the walkable geometry.");
		}
		else if (!S.bDynamic)
		{
			S.Hint = TEXT("The navmesh is static and will not rebuild at runtime. Set [/Script/NavigationSystem.RecastNavMesh] RuntimeGeneration=Dynamic in DefaultEngine.ini.");
		}
		return S;
	}

	FString MovementName(const AAIController* C)
	{
		switch (C ? C->GetMoveStatus() : EPathFollowingStatus::Idle)
		{
		case EPathFollowingStatus::Waiting: return TEXT("Waiting");
		case EPathFollowingStatus::Paused: return TEXT("Paused");
		case EPathFollowingStatus::Moving: return TEXT("Moving");
		default: return TEXT("Idle");
		}
	}

	FRuntimeAgentInfo Info(APawn* P)
	{
		FRuntimeAgentInfo I;
		I.Agent = P;
		I.Location = P->GetActorLocation();
		const AAIController* C = Cast<AAIController>(P->GetController());
		I.Movement = MovementName(C);
		if (const ARuntimeAgentCharacter* RC = Cast<ARuntimeAgentCharacter>(P))
		{
			I.Body = RC->Body.Get();
			I.Settings = RC->Settings;
		}
		if (const FRuntimeSceneNode* N = RuntimeToolCall::NodeOf(I.Body ? I.Body.Get() : P))
		{
			I.Id = N->Id;
		}
		if (const UBehaviorTreeComponent* BT = C ? Cast<UBehaviorTreeComponent>(C->GetBrainComponent()) : nullptr; BT && BT->GetRootTree())
		{
			I.BehaviorTree = BT->GetRootTree()->GetPathName();
		}
		return I;
	}

	FString KeyTypeName(const UClass* K)
	{
		FString N = K ? K->GetName() : FString(TEXT("None"));
		N.RemoveFromStart(TEXT("BlackboardKeyType_"));
		return N;
	}

	TArray<FRuntimeBlackboardEntry> Keys(const UBlackboardData* Data, const UBlackboardComponent* Values)
	{
		TArray<FRuntimeBlackboardEntry> Out;
		for (const UBlackboardData* D = Data; D; D = D->Parent)
		{
			for (const FBlackboardEntry& E : D->Keys)
			{
				FRuntimeBlackboardEntry& R = Out.AddDefaulted_GetRef();
				R.Key = E.EntryName.ToString();
				R.Type = KeyTypeName(E.KeyType ? E.KeyType->GetClass() : nullptr);
				if (Values)
				{
					R.Value = Values->DescribeKeyValue(E.EntryName, EBlackboardDescription::OnlyValue);
				}
			}
		}
		return Out;
	}

	FRuntimeBrainState Brain(AAIController* C)
	{
		FRuntimeBrainState S;
		const UBehaviorTreeComponent* BT = Cast<UBehaviorTreeComponent>(C->GetBrainComponent());
		if (BT && BT->GetRootTree())
		{
			S.bRunning = BT->IsRunning();
			S.BehaviorTree = BT->GetRootTree()->GetPathName();
			if (const UBTNode* N = BT->GetActiveNode())
			{
				S.ActiveNode = N->GetNodeName();
			}
		}
		if (const UBlackboardComponent* BB = C->GetBlackboardComponent(); BB && BB->GetBlackboardAsset())
		{
			S.Blackboard = Keys(BB->GetBlackboardAsset(), BB);
		}
		return S;
	}

	void AddNode(TArray<FRuntimeBtNode>& Out, const UBTNode* N, int32 Depth, const TCHAR* Kind)
	{
		if (!N)
		{
			return;
		}
		FRuntimeBtNode& R = Out.AddDefaulted_GetRef();
		R.Index = Out.Num() - 1;
		R.Depth = Depth;
		R.Name = N->GetNodeName();
		R.Kind = Kind;
		FString C = N->GetClass()->GetName();
		C.RemoveFromEnd(TEXT("_C"));
		R.Class = C;
	}

	void Walk(TArray<FRuntimeBtNode>& Out, const UBTCompositeNode* Node, int32 Depth)
	{
		if (!Node)
		{
			return;
		}
		AddNode(Out, Node, Depth, TEXT("Composite"));
		for (const UBTService* S : Node->Services)
		{
			AddNode(Out, S, Depth + 1, TEXT("Service"));
		}
		for (const FBTCompositeChild& Child : Node->Children)
		{
			for (const UBTDecorator* D : Child.Decorators)
			{
				AddNode(Out, D, Depth + 1, TEXT("Decorator"));
			}
			if (Child.ChildComposite)
			{
				Walk(Out, Child.ChildComposite, Depth + 1);
			}
			else
			{
				AddNode(Out, Child.ChildTask, Depth + 1, TEXT("Task"));
				if (Child.ChildTask)
				{
					for (const UBTService* S : Child.ChildTask->Services)
					{
						AddNode(Out, S, Depth + 2, TEXT("Service"));
					}
				}
			}
		}
	}
}

// ---- navigation -------------------------------------------------------------------------------------------------------

URuntimeNavBuildAsyncResult* URuntimeAiToolset::BuildNavigation(const FBox& Bounds, float Margin, float TimeoutSeconds)
{
	URuntimeNavBuildAsyncResult* R = NewObject<URuntimeNavBuildAsyncResult>();
	UWorld* W = RuntimeToolCall::GetWorld();
	UNavigationSystemV1* N = RtAi::Nav();
	if (!N)
	{
		R->SetError(TEXT("this world has no navigation system"), ERuntimeToolError::WrongState);
		return R;
	}
	FBox Box(ForceInit);
	if (RuntimeToolCall::Current()->Has(TEXT("bounds")))
	{
		Box = FBox(Bounds.Min, Bounds.Max);
		if (!(Box.Max.X > Box.Min.X && Box.Max.Y > Box.Min.Y && Box.Max.Z > Box.Min.Z))
		{
			R->SetError(TEXT("bounds needs min < max on every axis"), ERuntimeToolError::InvalidArguments);
			return R;
		}
	}
	else
	{
		// What tools built (scene nodes), plus a margin. Level backdrops (oceans, sky domes, huge planes) are left out:
		// covering them would ask for millions of navmesh tiles.
		if (FRuntimeSceneGraph* G = RuntimeToolCall::GetGraph())
		{
			for (const FString& Root : G->GetRootIds())
			{
				for (const FString& Id : G->CollectSubtree(Root))
				{
					const FRuntimeSceneNode* Node = G->Find(Id);
					AActor* A = Node && !Node->bSoftDeleted ? Node->Actor.Get() : nullptr;
					if (A && !A->IsA<APawn>() && A->GetActorEnableCollision() && !A->IsHidden())
					{
						const FBox B = A->GetComponentsBoundingBox(false);
						if (B.IsValid && B.Max.Z > -50000.0) // templates parked far below the ground do not count
						{
							Box += B;
						}
					}
				}
			}
		}
		if (!Box.IsValid)
		{
			// No tool-built geometry: the level's own colliding actors of sensible size.
			for (TActorIterator<AActor> It(W); It; ++It)
			{
				if (It->IsA<APawn>() || It->IsA<AController>() || !It->GetActorEnableCollision())
				{
					continue;
				}
				const FBox B = It->GetComponentsBoundingBox(false);
				if (B.IsValid && B.GetExtent().GetMax() < 50000.0)
				{
					Box += B;
				}
			}
		}
		if (!Box.IsValid)
		{
			R->SetError(TEXT("nothing to build navigation on"), ERuntimeToolError::WrongState);
			return R;
		}
		Box = Box.ExpandBy(FVector(Margin, Margin, Margin + 500.f));
	}
	// Tile budget: keep the area within 4 km x 4 km (pass explicit bounds for a specific part of a bigger world).
	constexpr double MaxSide = 400000.0;
	if (Box.GetSize().X > MaxSide || Box.GetSize().Y > MaxSide)
	{
		R->SetError(FString::Printf(TEXT("the area is %.0f x %.0f m, more than %.0f m on a side"), Box.GetSize().X / 100.0, Box.GetSize().Y / 100.0, MaxSide / 100.0),
			ERuntimeToolError::InvalidArguments);
		return R;
	}

	// Register the area as navigation bounds (replacing what this tool registered before in this world).
	FNavigationBoundsUpdateRequest Req;
	Req.NavBounds.UniqueID = 0x5254AB00;
	Req.NavBounds.AreaBox = Box;
	Req.NavBounds.Level = W->PersistentLevel;
	// Applied now (not queued for the next navigation tick) so the navmesh below is built with these bounds.
	auto Perform = &RtAi::FNavSysAccess::PerformNavigationBoundsUpdate;
	Req.UpdateRequest = RtAi::RegisteredBounds().Contains(W) ? FNavigationBoundsUpdateRequest::Updated : FNavigationBoundsUpdateRequest::Added;
	(N->*Perform)(TArray<FNavigationBoundsUpdateRequest>{Req});
	RtAi::RegisteredBounds().Add(W, Box);

	ANavigationData* Data = N->GetDefaultNavDataInstance(FNavigationSystem::Create);
	if (!Data)
	{
		R->SetError(TEXT("could not create a navmesh"), ERuntimeToolError::Internal);
		return R;
	}
	Data->RebuildAll();

	TWeakObjectPtr<URuntimeNavBuildAsyncResult> Weak = R;
	TWeakObjectPtr<UWorld> WeakW = W;
	const double End = FPlatformTime::Seconds() + TimeoutSeconds;
	const double Start = FPlatformTime::Seconds();
	TSharedRef<double> LastBusy = MakeShared<double>(Start);
	RuntimeToolCall::SetTimeout(TimeoutSeconds + 5.0);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, WeakW, End, Start, LastBusy](float)
	{
		URuntimeNavBuildAsyncResult* Res = Weak.Get();
		if (!Res || Res->bIsComplete || !WeakW.IsValid())
		{
			return false;
		}
		UNavigationSystemV1* Sys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(WeakW.Get());
		// Generation starts a few frames after RebuildAll and runs in tasks: done once it has been quiet for half a
		// second (and at least a second has passed).
		const double Now = FPlatformTime::Seconds();
		if (!Sys || Sys->IsNavigationBuildInProgress())
		{
			*LastBusy = Now;
		}
		const bool bDone = Sys && Now - Start > 1.0 && Now - *LastBusy > 0.5;
		if (bDone || FPlatformTime::Seconds() > End)
		{
			FRuntimeNavigationStatus S = RtAi::Status(WeakW.Get());
			if (!bDone)
			{
				S.Hint = TEXT("Still building after the timeout; check again with ai.get_navigation_status.");
			}
			Res->SetValue(S);
			return false;
		}
		return true;
	}), 0.1f);
	return R;
}

FRuntimeNavigationStatus URuntimeAiToolset::GetNavigationStatus()
{
	return RtAi::Status(RuntimeToolCall::GetWorld());
}

FRuntimeNavPath URuntimeAiToolset::FindPath(const FVector& Start, const FVector& End)
{
	FRuntimeNavPath P;
	UNavigationSystemV1* N = RtAi::Nav();
	if (!N || !N->GetDefaultNavDataInstance())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no navmesh"), TEXT("Call ai.build_navigation first"));
		return P;
	}
	// Points inside obstacles or slightly above the floor are snapped to the nearest walkable spot first.
	auto Snap = [N](const FVector& In)
	{
		FNavLocation Out;
		return N->ProjectPointToNavigation(In, Out, FVector(300.f, 300.f, 500.f)) ? Out.Location : In;
	};
	UNavigationPath* Path = N->FindPathToLocationSynchronously(RuntimeToolCall::GetWorld(), Snap(Start), Snap(End));
	if (!Path || !Path->IsValid())
	{
		return P;
	}
	P.bFound = true;
	P.bPartial = Path->IsPartial();
	P.Length = Path->GetPathLength();
	P.Points = Path->PathPoints;
	return P;
}

FVector URuntimeAiToolset::ProjectToNavigation(const FVector& Location, float SearchExtent)
{
	UNavigationSystemV1* N = RtAi::Nav();
	if (!N || !N->GetDefaultNavDataInstance())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no navmesh"), TEXT("Call ai.build_navigation first"));
		return Location;
	}
	FNavLocation Out;
	if (!N->ProjectPointToNavigation(Location, Out, FVector(SearchExtent)))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("nothing walkable within %.0f cm"), SearchExtent), TEXT("Increase search_extent or build_navigation over this area"));
		return Location;
	}
	return Out.Location;
}

// ---- agents -----------------------------------------------------------------------------------------------------------

FRuntimeAgentInfo URuntimeAiToolset::CreateAgent(AActor* Target, const FRuntimeAgentSettings& Settings)
{
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return {};
	}
	const bool bWasAgent = RuntimeAgents::FindAgent(Target) != nullptr;
	TWeakObjectPtr<AActor> WeakT = Target;
	TSharedPtr<TWeakObjectPtr<APawn>> Made = MakeShared<TWeakObjectPtr<APawn>>();
	auto Do = [WeakT, Settings, Made](FRuntimeCommandContext&, FString& Err)
	{
		APawn* P = WeakT.IsValid() ? RuntimeAgents::MakeAgent(WeakT.Get(), Settings, Err) : nullptr;
		*Made = P;
		return P != nullptr;
	};
	auto Undo = [Made, bWasAgent](FRuntimeCommandContext&)
	{
		if (!bWasAgent && Made->IsValid())
		{
			RuntimeAgents::RemoveAgent(Made->Get());
		}
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("create AI agent"), Do, Undo)))
	{
		return {};
	}
	return RtAi::Info(Made->Get());
}

bool URuntimeAiToolset::RemoveAgent(AActor* Agent)
{
	ARuntimeAgentCharacter* C = Cast<ARuntimeAgentCharacter>(RtAi::Agent(Agent));
	if (!C)
	{
		if (!RuntimeToolCall::HasFailed())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("only agents made with ai.create_agent can be removed"), TEXT("Use ai.stop_behavior_tree / stop_movement for other pawns"));
		}
		return false;
	}
	TWeakObjectPtr<AActor> Body = C->Body;
	const FRuntimeAgentSettings Settings = C->Settings;
	TSharedPtr<TWeakObjectPtr<APawn>> Current = MakeShared<TWeakObjectPtr<APawn>>(C);
	auto Do = [Current](FRuntimeCommandContext&, FString&)
	{
		if (Current->IsValid())
		{
			RuntimeAgents::RemoveAgent(Current->Get());
		}
		return true;
	};
	auto Undo = [Body, Settings, Current](FRuntimeCommandContext&)
	{
		FString Err;
		*Current = Body.IsValid() ? RuntimeAgents::MakeAgent(Body.Get(), Settings, Err) : nullptr;
		return Current->IsValid();
	};
	return RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("remove AI agent"), Do, Undo));
}

TArray<FRuntimeAgentInfo> URuntimeAiToolset::ListAgents(int32 Limit)
{
	TArray<FRuntimeAgentInfo> Out;
	for (TActorIterator<APawn> It(RuntimeToolCall::GetWorld()); It; ++It)
	{
		if (Cast<AAIController>(It->GetController()))
		{
			Out.Add(RtAi::Info(*It));
		}
	}
	Out.Sort([](const FRuntimeAgentInfo& A, const FRuntimeAgentInfo& B) { return A.Agent->GetName() < B.Agent->GetName(); });
	if (Out.Num() > Limit)
	{
		Out.SetNum(FMath::Max(0, Limit));
	}
	return Out;
}

URuntimeMoveAsyncResult* URuntimeAiToolset::MoveTo(AActor* Agent, const FVector& Location, AActor* TargetActor, float AcceptanceRadius, float TimeoutSeconds)
{
	URuntimeMoveAsyncResult* R = NewObject<URuntimeMoveAsyncResult>();
	AAIController* C = RtAi::Controller(Agent);
	if (!C)
	{
		R->SetError(TEXT("not an AI agent (ai.create_agent first)"), ERuntimeToolError::InvalidArguments);
		return R;
	}
	if (!TargetActor && !RuntimeToolCall::Current()->Has(TEXT("location")))
	{
		R->SetError(TEXT("give a location or a target_actor"), ERuntimeToolError::InvalidArguments);
		return R;
	}
	const EPathFollowingRequestResult::Type Request = TargetActor
		? C->MoveToActor(TargetActor, AcceptanceRadius, true, true, true, nullptr, true)
		: C->MoveToLocation(Location, AcceptanceRadius, true, true, true, true, nullptr, true);
	const double Start = FPlatformTime::Seconds();
	auto Outcome = [C, Start](ERuntimeMoveResult Result)
	{
		FRuntimeMoveOutcome O;
		O.Result = Result;
		O.Location = C->GetPawn() ? C->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		O.Seconds = FPlatformTime::Seconds() - Start;
		return O;
	};
	if (Request == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		R->SetValue(Outcome(ERuntimeMoveResult::Success));
		return R;
	}
	if (Request == EPathFollowingRequestResult::Failed)
	{
		R->SetValue(Outcome(ERuntimeMoveResult::Unreachable));
		return R;
	}
	UPathFollowingComponent* PF = C->GetPathFollowingComponent();
	const FAIRequestID Id = PF ? PF->GetCurrentRequestId() : FAIRequestID::InvalidRequest;
	TWeakObjectPtr<URuntimeMoveAsyncResult> Weak = R;
	TWeakObjectPtr<AAIController> WeakC = C;
	TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
	TSharedRef<FTSTicker::FDelegateHandle> Timer = MakeShared<FTSTicker::FDelegateHandle>();
	auto Finish = [Weak, WeakC, Handle, Timer, Start](ERuntimeMoveResult Result)
	{
		if (AAIController* Ctrl = WeakC.Get())
		{
			if (UPathFollowingComponent* P = Ctrl->GetPathFollowingComponent())
			{
				P->OnRequestFinished.Remove(*Handle);
			}
		}
		FTSTicker::GetCoreTicker().RemoveTicker(*Timer);
		if (URuntimeMoveAsyncResult* Res = Weak.Get(); Res && !Res->bIsComplete)
		{
			FRuntimeMoveOutcome O;
			O.Result = Result;
			O.Location = WeakC.IsValid() && WeakC->GetPawn() ? WeakC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
			O.Seconds = FPlatformTime::Seconds() - Start;
			Res->SetValue(O);
		}
	};
	if (PF)
	{
		*Handle = PF->OnRequestFinished.AddLambda([Finish, Id](FAIRequestID Req, const FPathFollowingResult& Result)
		{
			if (Req != Id)
			{
				return;
			}
			Finish(Result.IsSuccess() ? ERuntimeMoveResult::Success
				: Result.HasFlag(FPathFollowingResultFlags::Blocked) ? ERuntimeMoveResult::Blocked
				: Result.HasFlag(FPathFollowingResultFlags::InvalidPath) ? ERuntimeMoveResult::Unreachable
				: ERuntimeMoveResult::Aborted);
		});
	}
	const double End = Start + TimeoutSeconds;
	*Timer = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Finish, End, Weak](float)
	{
		if (!Weak.IsValid() || Weak->bIsComplete)
		{
			return false;
		}
		if (FPlatformTime::Seconds() > End)
		{
			Finish(ERuntimeMoveResult::TimedOut);
			return false;
		}
		return true;
	}), 0.1f);
	R->OnCancelled = [Finish]() { Finish(ERuntimeMoveResult::Aborted); };
	RuntimeToolCall::SetTimeout(TimeoutSeconds + 5.0);
	return R;
}

bool URuntimeAiToolset::StopMovement(AActor* Agent)
{
	AAIController* C = RtAi::Controller(Agent);
	if (!C)
	{
		return false;
	}
	C->StopMovement();
	return true;
}

// ---- perception -------------------------------------------------------------------------------------------------------

TArray<FRuntimePerceivedActor> URuntimeAiToolset::GetPerception(AActor* Agent)
{
	TArray<FRuntimePerceivedActor> Out;
	AAIController* C = RtAi::Controller(Agent);
	UAIPerceptionComponent* P = C ? C->GetPerceptionComponent() : nullptr;
	if (!P)
	{
		if (C)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("the agent has no perception"), TEXT("Agents made with ai.create_agent have sight and hearing"));
		}
		return Out;
	}
	TArray<AActor*> Known;
	P->GetKnownPerceivedActors(nullptr, Known);
	UWorld* W = RuntimeToolCall::GetWorld();
	for (AActor* A : Known)
	{
		FActorPerceptionBlueprintInfo Info;
		if (!A || !P->GetActorsPerception(A, Info))
		{
			continue;
		}
		for (const FAIStimulus& S : Info.LastSensedStimuli)
		{
			if (S.GetAge() >= FAIStimulus::NeverHappenedAge)
			{
				continue;
			}
			FRuntimePerceivedActor& R = Out.AddDefaulted_GetRef();
			R.Actor = A;
			const TSubclassOf<UAISense> Sense = UAIPerceptionSystem::GetSenseClassForStimulus(W, S);
			FString N = Sense ? Sense->GetName() : FString(TEXT("Unknown"));
			N.RemoveFromStart(TEXT("AISense_"));
			R.Sense = N;
			R.Location = S.StimulusLocation;
			R.Age = S.GetAge();
			R.bCurrentlySensed = S.IsActive() && S.GetAge() < 0.01f;
			R.Tag = S.Tag.IsNone() ? FString() : S.Tag.ToString();
		}
	}
	Out.Sort([](const FRuntimePerceivedActor& A, const FRuntimePerceivedActor& B) { return A.Age < B.Age; });
	return Out;
}

bool URuntimeAiToolset::MakePerceivable(AActor* Target)
{
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return false;
	}
	TWeakObjectPtr<AActor> Weak = Target;
	TSharedPtr<TWeakObjectPtr<UAIPerceptionStimuliSourceComponent>> Added = MakeShared<TWeakObjectPtr<UAIPerceptionStimuliSourceComponent>>();
	auto Do = [Weak, Added](FRuntimeCommandContext&, FString& Err)
	{
		AActor* A = Weak.Get();
		if (!A)
		{
			Err = TEXT("target is gone");
			return false;
		}
		UAIPerceptionStimuliSourceComponent* S = A->FindComponentByClass<UAIPerceptionStimuliSourceComponent>();
		if (!S)
		{
			S = NewObject<UAIPerceptionStimuliSourceComponent>(A);
			S->RegisterComponent();
			A->AddInstanceComponent(S);
			*Added = S;
		}
		S->RegisterForSense(UAISense_Sight::StaticClass());
		S->RegisterWithPerceptionSystem();
		return true;
	};
	auto Undo = [Added](FRuntimeCommandContext&)
	{
		if (UAIPerceptionStimuliSourceComponent* S = Added->Get())
		{
			S->UnregisterFromPerceptionSystem();
			S->DestroyComponent();
		}
		return true;
	};
	return RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("make perceivable"), Do, Undo));
}

bool URuntimeAiToolset::ReportNoise(const FVector& Location, float Loudness, float MaxRange, AActor* Instigator, const FString& Tag)
{
	if (Loudness <= 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("loudness must be above 0"));
		return false;
	}
	UWorld* W = RuntimeToolCall::GetWorld();
	if (!Instigator)
	{
		// AI hearing ignores noises without an instigator: give it a short-lived invisible source at the spot.
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.ObjectFlags |= RF_Transient;
		Instigator = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), P);
		if (Instigator)
		{
			USceneComponent* Root = NewObject<USceneComponent>(Instigator, TEXT("Root"));
			Instigator->SetRootComponent(Root);
			Root->RegisterComponent();
			Instigator->SetActorLocation(Location);
			Instigator->Tags.Add(TEXT("RTNoiseSource"));
			Instigator->SetLifeSpan(30.f);
		}
	}
	UAISense_Hearing::ReportNoiseEvent(W, Location, Loudness, Instigator, MaxRange, Tag.IsEmpty() ? NAME_None : FName(*Tag));
	return true;
}

// ---- behavior trees ---------------------------------------------------------------------------------------------------

TArray<FString> URuntimeAiToolset::ListBehaviorTrees()
{
	TArray<FString> Out;
	FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AR.Get().GetAssetsByClass(UBehaviorTree::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& A : Assets)
	{
		Out.Add(A.GetObjectPathString());
	}
	Out.Sort();
	return Out;
}

FRuntimeBtDescription URuntimeAiToolset::DescribeBehaviorTree(UBehaviorTree* Tree)
{
	FRuntimeBtDescription D;
	if (!Tree)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("tree is required"), TEXT("See ai.list_behavior_trees"));
		return D;
	}
	D.Name = Tree->GetPathName();
	for (const UBTDecorator* Dec : Tree->RootDecorators)
	{
		RtAi::AddNode(D.Nodes, Dec, 0, TEXT("Decorator"));
	}
	RtAi::Walk(D.Nodes, Tree->RootNode, 0);
	D.BlackboardKeys = RtAi::Keys(Tree->GetBlackboardAsset(), nullptr);
	return D;
}

FRuntimeBrainState URuntimeAiToolset::RunBehaviorTree(AActor* Agent, UBehaviorTree* Tree)
{
	AAIController* C = RtAi::Controller(Agent);
	if (!C)
	{
		return {};
	}
	if (!Tree || !Tree->RootNode)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("the tree is empty or missing"), TEXT("See ai.list_behavior_trees"));
		return {};
	}
	if (!C->RunBehaviorTree(Tree))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, TEXT("the behavior tree did not start (blackboard mismatch?)"));
		return {};
	}
	return RtAi::Brain(C);
}

bool URuntimeAiToolset::StopBehaviorTree(AActor* Agent)
{
	AAIController* C = RtAi::Controller(Agent);
	if (!C)
	{
		return false;
	}
	if (UBrainComponent* B = C->GetBrainComponent())
	{
		B->StopLogic(TEXT("stopped by ai.stop_behavior_tree"));
		return true;
	}
	RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("the agent runs no behavior tree"));
	return false;
}

FRuntimeBrainState URuntimeAiToolset::GetBrainState(AActor* Agent)
{
	AAIController* C = RtAi::Controller(Agent);
	return C ? RtAi::Brain(C) : FRuntimeBrainState();
}

FRuntimeBlackboardEntry URuntimeAiToolset::SetBlackboardValue(AActor* Agent, const FString& Key, const FRuntimeToolJson& Value)
{
	FRuntimeBlackboardEntry E;
	AAIController* C = RtAi::Controller(Agent);
	UBlackboardComponent* BB = C ? C->GetBlackboardComponent() : nullptr;
	if (!BB || !BB->GetBlackboardAsset())
	{
		if (C)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("the agent has no blackboard"), TEXT("Run a behavior tree first (ai.run_behavior_tree)"));
		}
		return E;
	}
	const FName Name(*Key);
	const FBlackboard::FKey Id = BB->GetKeyID(Name);
	if (Id == FBlackboard::InvalidKey)
	{
		TArray<FString> Names;
		for (const FRuntimeBlackboardEntry& K : RtAi::Keys(BB->GetBlackboardAsset(), nullptr))
		{
			Names.Add(K.Key);
		}
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no blackboard key '%s'"), *Key), TEXT("Keys: ") + FString::Join(Names, TEXT(", ")));
		return E;
	}
	const UClass* T = BB->GetKeyType(Id);
	const TSharedPtr<FJsonValue>& V = Value.Value;
	auto Bad = [&](const TCHAR* Expected)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is a %s key: give %s"), *Key, *RtAi::KeyTypeName(T), Expected));
		return E;
	};
	if (!V.IsValid())
	{
		return Bad(TEXT("a value"));
	}
	bool Bool;
	double Num;
	FString Str;
	if (T == UBlackboardKeyType_Bool::StaticClass())
	{
		if (!V->TryGetBool(Bool)) { return Bad(TEXT("true/false")); }
		BB->SetValueAsBool(Name, Bool);
	}
	else if (T == UBlackboardKeyType_Int::StaticClass())
	{
		if (!V->TryGetNumber(Num)) { return Bad(TEXT("a number")); }
		BB->SetValueAsInt(Name, static_cast<int32>(Num));
	}
	else if (T == UBlackboardKeyType_Float::StaticClass())
	{
		if (!V->TryGetNumber(Num)) { return Bad(TEXT("a number")); }
		BB->SetValueAsFloat(Name, static_cast<float>(Num));
	}
	else if (T == UBlackboardKeyType_String::StaticClass() || T == UBlackboardKeyType_Name::StaticClass())
	{
		if (!V->TryGetString(Str)) { return Bad(TEXT("a string")); }
		T == UBlackboardKeyType_String::StaticClass() ? BB->SetValueAsString(Name, Str) : BB->SetValueAsName(Name, FName(*Str));
	}
	else if (T == UBlackboardKeyType_Vector::StaticClass() || T == UBlackboardKeyType_Rotator::StaticClass())
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!V->TryGetArray(A) || A->Num() != 3) { return Bad(TEXT("[x, y, z]")); }
		const FVector X((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		T == UBlackboardKeyType_Vector::StaticClass() ? BB->SetValueAsVector(Name, X) : BB->SetValueAsRotator(Name, FRotator(X.X, X.Y, X.Z));
	}
	else if (T == UBlackboardKeyType_Object::StaticClass())
	{
		if (V->IsNull())
		{
			BB->ClearValue(Name);
		}
		else
		{
			if (!V->TryGetString(Str)) { return Bad(TEXT("an object reference")); }
			UObject* O = RuntimeToolCall::Resolve(Str);
			if (!O) { return E; }
			BB->SetValueAsObject(Name, O);
		}
	}
	else
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("%s keys cannot be set by this tool"), *RtAi::KeyTypeName(T)));
		return E;
	}
	E.Key = Key;
	E.Type = RtAi::KeyTypeName(T);
	E.Value = BB->DescribeKeyValue(Name, EBlackboardDescription::OnlyValue);
	return E;
}
