#include "RuntimeStateTreeToolset.h"
#include "RuntimeCommand.h"
#include "RuntimeToolReflection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/StateTreeComponent.h"
#include "Components/StateTreeComponentSchema.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeNodeBase.h"
#include "StateTreeReference.h"
#include "StateTreeSchema.h"
#include "StructUtils/InstancedStruct.h"

namespace
{
	template <typename EnumType>
	FString EnumString(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	/** A private UPROPERTY array of UStateTree (compiled data) read through reflection; null if the layout changed. */
	template <typename T>
	const TArray<T>* TreeArray(const UStateTree* Tree, const TCHAR* Name)
	{
		const FArrayProperty* P = FindFProperty<FArrayProperty>(UStateTree::StaticClass(), Name);
		const FStructProperty* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
		if (!Inner || Inner->Struct != T::StaticStruct())
		{
			return nullptr;
		}
		return P->ContainerPtrToValuePtr<TArray<T>>(Tree);
	}

	FRuntimeStateTreeNode Node(const UStateTree* Tree, int32 Index)
	{
		FRuntimeStateTreeNode N;
		if (Index < 0 || Index >= Tree->GetNodes().Num())
		{
			return N;
		}
		const FConstStructView View = Tree->GetNode(Index);
		if (const UScriptStruct* S = View.GetScriptStruct())
		{
			N.Type = S->GetName();
		}
		if (const FStateTreeNodeBase* Base = View.GetPtr<const FStateTreeNodeBase>())
		{
			N.Name = Base->Name.ToString();
		}
		return N;
	}

	TArray<FRuntimeStateTreeNode> Nodes(const UStateTree* Tree, int32 Begin, int32 Num)
	{
		TArray<FRuntimeStateTreeNode> Out;
		for (int32 i = Begin; i < Begin + Num; ++i)
		{
			Out.Add(Node(Tree, i));
		}
		return Out;
	}

	/** Path of a state ("Root/Combat/Attack"); names joined from the root. */
	FString StatePath(const TArray<FCompactStateTreeState>& States, FStateTreeStateHandle Handle)
	{
		TArray<FString> Parts;
		for (int32 Guard = 0; Handle.IsValid() && States.IsValidIndex(Handle.Index) && Guard < 64; ++Guard)
		{
			Parts.Insert(States[Handle.Index].Name.ToString(), 0);
			Handle = States[Handle.Index].Parent;
		}
		return FString::Join(Parts, TEXT("/"));
	}

	FString TargetName(const TArray<FCompactStateTreeState>& States, FStateTreeStateHandle Handle)
	{
		if (Handle == FStateTreeStateHandle::Succeeded)
		{
			return TEXT("Succeeded");
		}
		if (Handle == FStateTreeStateHandle::Failed)
		{
			return TEXT("Failed");
		}
		if (Handle == FStateTreeStateHandle::Stopped)
		{
			return TEXT("Stopped");
		}
		return Handle.IsValid() ? StatePath(States, Handle) : FString();
	}

	FStateTreeInstanceData* InstanceDataOf(UStateTreeComponent* C)
	{
		static const FStructProperty* P = FindFProperty<FStructProperty>(UStateTreeComponent::StaticClass(), TEXT("InstanceData"));
		return P && P->Struct == FStateTreeInstanceData::StaticStruct() ? P->ContainerPtrToValuePtr<FStateTreeInstanceData>(C) : nullptr;
	}

	const UStateTree* TreeOf(const UStateTreeComponent* C)
	{
		static const FStructProperty* P = FindFProperty<FStructProperty>(UStateTreeComponent::StaticClass(), TEXT("StateTreeRef"));
		return P && P->Struct == FStateTreeReference::StaticStruct() ? P->ContainerPtrToValuePtr<FStateTreeReference>(C)->GetStateTree() : nullptr;
	}

	FString ActorLabel(const AActor* A)
	{
		return A ? RuntimeToolCall::MakeReference(A) : TEXT("(none)");
	}

	UStateTreeComponent* FindComponent(AActor* Target)
	{
		if (!Target)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, e.g. \"n12\""));
			return nullptr;
		}
		UStateTreeComponent* C = Target->FindComponentByClass<UStateTreeComponent>();
		if (!C)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s has no StateTree component"), *ActorLabel(Target)),
				TEXT("Start one with statetree.run_state_tree"));
		}
		return C;
	}

	FRuntimeStateTreeStatus MakeStatus(AActor* A, UStateTreeComponent* C)
	{
		FRuntimeStateTreeStatus S;
		S.Id = RuntimeToolCall::MakeReference(A);
		S.Name = A ? A->GetName() : FString();
		if (!C)
		{
			return S;
		}
		const UStateTree* Tree = TreeOf(C);
		S.Tree = Tree ? Tree->GetPathName() : FString();
		S.bRunning = C->IsRunning();
		S.bPaused = C->IsPaused();
		S.Status = EnumString(C->GetStateTreeRunStatus());
		FStateTreeInstanceData* Data = InstanceDataOf(C);
		if (Tree && Data && S.bRunning && Tree->IsReadyToRun() && A)
		{
			const FStateTreeReadOnlyExecutionContext Ctx(A, Tree, *Data);
			for (const FName& N : Ctx.GetActiveStateNames())
			{
				S.ActiveStates.Add(N.ToString());
			}
			for (const FStateTreeExecutionFrame& F : Ctx.GetActiveFrames())
			{
				TArray<FString> Parts;
				for (const FStateTreeStateHandle& H : F.ActiveStates)
				{
					const FCompactStateTreeState* St = F.StateTree ? F.StateTree->GetStateFromHandle(H) : nullptr;
					Parts.Add(St ? St->Name.ToString() : TEXT("?"));
				}
				if (Parts.Num())
				{
					S.ActivePaths.Add(FString::Join(Parts, TEXT("/")));
				}
			}
		}
		return S;
	}

	TArray<FString> CloseTags(const FString& Want)
	{
		FGameplayTagContainer All;
		UGameplayTagsManager::Get().RequestAllGameplayTags(All, false);
		FString Leaf = Want;
		int32 Dot;
		if (Want.FindLastChar(TEXT('.'), Dot))
		{
			Leaf = Want.Mid(Dot + 1);
		}
		TArray<FString> Out;
		for (const FGameplayTag& T : All)
		{
			const FString S = T.ToString();
			if (Out.Num() < 8 && (S.Contains(Want) || (!Leaf.IsEmpty() && S.Contains(Leaf))))
			{
				Out.Add(S);
			}
		}
		return Out;
	}

	bool IsComponentSchema(const UStateTree* Tree)
	{
		const UStateTreeSchema* Schema = Tree ? Tree->GetSchema() : nullptr;
		return Schema && Schema->GetClass()->IsChildOf(UStateTreeComponentSchema::StaticClass());
	}
}

TArray<FRuntimeStateTreeAsset> URuntimeStateTreeToolset::ListStateTrees(const FString& Filter)
{
	TArray<FRuntimeStateTreeAsset> Out;
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByClass(UStateTree::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& A : Assets)
	{
		const FString Path = A.GetObjectPathString();
		if (Filter.IsEmpty() || Path.Contains(Filter))
		{
			FRuntimeStateTreeAsset& I = Out.AddDefaulted_GetRef();
			I.Name = A.AssetName.ToString();
			I.Path = Path;
		}
	}
	Out.Sort([](const FRuntimeStateTreeAsset& A, const FRuntimeStateTreeAsset& B) { return A.Path < B.Path; });
	return Out;
}

FRuntimeStateTreeDescription URuntimeStateTreeToolset::DescribeStateTree(UStateTree* Asset)
{
	FRuntimeStateTreeDescription D;
	if (!Asset)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("asset is required"), TEXT("See statetree.list_state_trees"));
		return D;
	}
	D.Path = Asset->GetPathName();
	D.Schema = Asset->GetSchema() ? Asset->GetSchema()->GetClass()->GetName() : FString();
	D.bReadyToRun = Asset->IsReadyToRun();
	D.Evaluators = Nodes(Asset, Asset->GetGlobalEvaluatorsBegin(), Asset->GetGlobalEvaluatorsNum());
	D.GlobalTasks = Nodes(Asset, Asset->GetGlobalTasksBegin(), Asset->GetGlobalTasksNum());
	const TArray<FCompactStateTreeState>* States = TreeArray<FCompactStateTreeState>(Asset, TEXT("States"));
	const TArray<FCompactStateTransition>* Transitions = TreeArray<FCompactStateTransition>(Asset, TEXT("Transitions"));
	if (!States || !Transitions)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("this engine version's StateTree layout is not readable"));
		return D;
	}
	TSet<FString> Events;
	for (int32 i = 0; i < States->Num(); ++i)
	{
		const FCompactStateTreeState& Src = (*States)[i];
		FRuntimeStateTreeState& S = D.States.AddDefaulted_GetRef();
		S.Name = Src.Name.ToString();
		S.Path = StatePath(*States, FStateTreeStateHandle(static_cast<uint16>(i)));
		S.Type = EnumString(Src.Type);
		S.Selection = EnumString(Src.SelectionBehavior);
		S.Tag = Src.Tag.IsValid() ? Src.Tag.ToString() : FString();
		S.RequiredEvent = Src.RequiredEventToEnter.Tag.IsValid() ? Src.RequiredEventToEnter.Tag.ToString() : FString();
		if (!S.RequiredEvent.IsEmpty())
		{
			Events.Add(S.RequiredEvent);
		}
		S.LinkedAsset = Src.LinkedAsset ? Src.LinkedAsset->GetPathName() : FString();
		S.Tasks = Nodes(Asset, Src.TasksBegin, Src.TasksNum);
		S.EnterConditions = Nodes(Asset, Src.EnterConditionsBegin, Src.EnterConditionsNum);
		for (int32 t = Src.TransitionsBegin; t < Src.TransitionsBegin + Src.TransitionsNum && Transitions->IsValidIndex(t); ++t)
		{
			const FCompactStateTransition& Tr = (*Transitions)[t];
			FRuntimeStateTreeTransition& T = S.Transitions.AddDefaulted_GetRef();
			T.Trigger = StaticEnum<EStateTreeTransitionTrigger>()->GetValueOrBitfieldAsString(static_cast<int64>(Tr.Trigger));
			T.EventTag = Tr.RequiredEvent.Tag.IsValid() ? Tr.RequiredEvent.Tag.ToString() : FString();
			T.PayloadType = Tr.RequiredEvent.PayloadStruct ? Tr.RequiredEvent.PayloadStruct->GetName() : FString();
			T.Target = TargetName(*States, Tr.State);
			T.Priority = EnumString(Tr.Priority);
			T.Delay = Tr.Delay.GetDuration();
			T.bEnabled = Tr.bTransitionEnabled;
			T.Conditions = Nodes(Asset, Tr.ConditionsBegin, Tr.ConditionsNum);
			if (!T.EventTag.IsEmpty())
			{
				Events.Add(T.EventTag);
			}
		}
	}
	D.Events = Events.Array();
	D.Events.Sort();
	return D;
}

FRuntimeStateTreeStatus URuntimeStateTreeToolset::RunStateTree(AActor* Target, UStateTree* Asset)
{
	if (!Target || !Asset)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target and asset are required"), TEXT("See statetree.list_state_trees"));
		return {};
	}
	if (!Asset->IsReadyToRun())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s is not compiled"), *Asset->GetPathName()), TEXT("Compile it in the StateTree editor"));
		return {};
	}
	if (!IsComponentSchema(Asset))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			FString::Printf(TEXT("%s uses schema %s"), *Asset->GetPathName(), Asset->GetSchema() ? *Asset->GetSchema()->GetClass()->GetName() : TEXT("(none)")),
			TEXT("Actors run trees with StateTreeComponentSchema (or a child such as StateTreeAIComponentSchema on AI controllers)"));
		return {};
	}
	struct FState
	{
		TWeakObjectPtr<UStateTreeComponent> Comp;
		TWeakObjectPtr<UStateTree> Previous;
		bool bAdded = false;
		bool bWasRunning = false;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<AActor> WeakActor = Target;
	TWeakObjectPtr<UStateTree> WeakTree = Asset;
	auto Restore = [WeakActor, State]()
	{
		UStateTreeComponent* C = State->Comp.Get();
		if (!C)
		{
			return;
		}
		if (C->IsRunning())
		{
			C->StopLogic(TEXT("RuntimeToolsets undo"));
		}
		if (State->bAdded)
		{
			if (AActor* A = WeakActor.Get())
			{
				A->RemoveInstanceComponent(C);
			}
			C->DestroyComponent();
			State->Comp.Reset();
		}
		else
		{
			C->SetStateTree(State->Previous.Get());
			if (State->bWasRunning && State->Previous.IsValid())
			{
				C->StartLogic();
			}
		}
	};
	auto Do = [WeakActor, WeakTree, State, Restore](FRuntimeCommandContext&, FString& Err)
	{
		AActor* A = WeakActor.Get();
		if (!A || !WeakTree.IsValid())
		{
			Err = TEXT("actor or asset is gone");
			return false;
		}
		UStateTreeComponent* C = A->FindComponentByClass<UStateTreeComponent>();
		State->bAdded = C == nullptr;
		if (C)
		{
			State->Previous = const_cast<UStateTree*>(TreeOf(C));
			State->bWasRunning = C->IsRunning();
			if (State->bWasRunning)
			{
				C->StopLogic(TEXT("RuntimeToolsets run_state_tree"));
			}
			C->SetStateTree(WeakTree.Get());
		}
		else
		{
			C = NewObject<UStateTreeComponent>(A, MakeUniqueObjectName(A, UStateTreeComponent::StaticClass(), TEXT("StateTree")));
			C->SetStartLogicAutomatically(false);
			C->SetStateTree(WeakTree.Get()); // before registering: initialization validates the tree
			A->AddInstanceComponent(C);
			C->RegisterComponent();
		}
		State->Comp = C;
		C->StartLogic();
		if (!C->IsRunning())
		{
			Err = FString::Printf(TEXT("%s did not start (see the log: usually missing context data or a failed enter condition)"), *WeakTree->GetName());
			Restore();
			return false;
		}
		return true;
	};
	auto Undo = [Restore](FRuntimeCommandContext&)
	{
		Restore();
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("run state tree ") + Asset->GetName(), Do, Undo)))
	{
		return {};
	}
	FRuntimeStateTreeStatus S = MakeStatus(Target, State->Comp.Get());
	S.bAddedComponent = State->bAdded;
	return S;
}

FRuntimeStateTreeStatus URuntimeStateTreeToolset::StopStateTree(AActor* Target)
{
	UStateTreeComponent* C = FindComponent(Target);
	if (!C)
	{
		return {};
	}
	if (C->IsRunning())
	{
		C->StopLogic(TEXT("RuntimeToolsets stop_state_tree"));
	}
	return MakeStatus(Target, C);
}

FRuntimeStateTreeStatus URuntimeStateTreeToolset::RestartStateTree(AActor* Target)
{
	UStateTreeComponent* C = FindComponent(Target);
	if (!C)
	{
		return {};
	}
	if (!TreeOf(C))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s has no StateTree set"), *ActorLabel(Target)), TEXT("Use statetree.run_state_tree"));
		return {};
	}
	if (C->IsRunning())
	{
		C->RestartLogic();
	}
	else
	{
		C->StartLogic();
	}
	return MakeStatus(Target, C);
}

FRuntimeStateTreeStatus URuntimeStateTreeToolset::SendEvent(AActor* Target, const FString& Tag, const FRuntimeToolJson& Payload, const FString& Origin)
{
	UStateTreeComponent* C = FindComponent(Target);
	if (!C)
	{
		return {};
	}
	const FGameplayTag EventTag = Tag.IsEmpty() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(FName(*Tag.TrimStartAndEnd()), false);
	if (!EventTag.IsValid())
	{
		const TArray<FString> Close = CloseTags(Tag);
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("unknown gameplay tag '%s'"), *Tag),
			Close.Num() ? TEXT("Close matches: ") + FString::Join(Close, TEXT(", ")) : TEXT("statetree.describe_state_tree lists the events a tree listens for"));
		return {};
	}
	if (!C->IsRunning())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("the StateTree on %s is not running"), *ActorLabel(Target)),
			TEXT("Start it with statetree.restart_state_tree or statetree.run_state_tree"));
		return {};
	}
	FInstancedStruct Data;
	if (Payload.IsSet())
	{
		const TSharedPtr<FJsonObject> Obj = Payload.AsObject();
		const UStateTree* Tree = TreeOf(C);
		const UScriptStruct* Struct = nullptr;
		if (const TArray<FCompactStateTransition>* Transitions = Tree ? TreeArray<FCompactStateTransition>(Tree, TEXT("Transitions")) : nullptr)
		{
			for (const FCompactStateTransition& T : *Transitions)
			{
				if (T.RequiredEvent.PayloadStruct && (!T.RequiredEvent.Tag.IsValid() || EventTag.MatchesTag(T.RequiredEvent.Tag)))
				{
					Struct = T.RequiredEvent.PayloadStruct;
					break;
				}
			}
		}
		if (!Obj.IsValid() || !Struct)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
				!Obj.IsValid() ? FString(TEXT("payload must be an object")) : FString::Printf(TEXT("no transition expects a payload with %s"), *Tag),
				TEXT("Send the event without payload"));
			return {};
		}
		Data.InitializeAs(Struct);
		RuntimeToolReflection::FConvertContext Ctx;
		Ctx.World = RuntimeToolCall::GetWorld();
		Ctx.Graph = RuntimeToolCall::GetGraph();
		FString Err;
		if (!RuntimeToolReflection::JsonToStruct(Obj, Struct, Data.GetMutableMemory(), Ctx, Err))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("payload (%s): %s"), *Struct->GetName(), *Err));
			return {};
		}
	}
	C->SendStateTreeEvent(EventTag, FConstStructView(Data), Origin.IsEmpty() ? FName() : FName(*Origin));
	return MakeStatus(Target, C);
}

FRuntimeStateTreeStatus URuntimeStateTreeToolset::GetActiveStates(AActor* Target)
{
	UStateTreeComponent* C = FindComponent(Target);
	return C ? MakeStatus(Target, C) : FRuntimeStateTreeStatus();
}
