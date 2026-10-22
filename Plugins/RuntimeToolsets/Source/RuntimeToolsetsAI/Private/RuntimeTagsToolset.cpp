#include "RuntimeTagsToolset.h"
#include "RuntimeCommand.h"
#include "RuntimeToolCall.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"

namespace RuntimeTags
{
	FGameplayTagContainer TagsOf(const AActor* Actor)
	{
		FGameplayTagContainer Out;
		if (!Actor)
		{
			return Out;
		}
		if (const IGameplayTagAssetInterface* I = Cast<IGameplayTagAssetInterface>(Actor))
		{
			I->GetOwnedGameplayTags(Out);
		}
		for (const UActorComponent* C : Actor->GetComponents())
		{
			if (const IGameplayTagAssetInterface* I = Cast<IGameplayTagAssetInterface>(C))
			{
				FGameplayTagContainer More;
				I->GetOwnedGameplayTags(More);
				Out.AppendTags(More);
			}
		}
		return Out;
	}

	FGameplayTag Find(const FString& Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(*Name.TrimStartAndEnd()), false);
	}
}

namespace RtTags
{
	TArray<FString> Names(const FGameplayTagContainer& C)
	{
		TArray<FString> Out;
		for (const FGameplayTag& T : C)
		{
			Out.Add(T.ToString());
		}
		Out.Sort();
		return Out;
	}

	TArray<FString> AllTags()
	{
		FGameplayTagContainer All;
		UGameplayTagsManager::Get().RequestAllGameplayTags(All, false);
		return Names(All);
	}

	/** Resolves names to tags; fails the call with close matches for unknown names. */
	bool Resolve(const TArray<FString>& In, FGameplayTagContainer& Out)
	{
		for (const FString& N : In)
		{
			const FGameplayTag T = RuntimeTags::Find(N);
			if (!T.IsValid())
			{
				TArray<FString> Close;
				const FString Leaf = N.Contains(TEXT(".")) ? N.RightChop(N.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1) : N;
				for (const FString& A : AllTags())
				{
					if (A.Contains(Leaf) && Close.Num() < 12)
					{
						Close.Add(A);
					}
				}
				RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("'%s' is not a registered gameplay tag"), *N),
					Close.Num() ? TEXT("Close matches: ") + FString::Join(Close, TEXT(", ")) : TEXT("See tags.list_tags; projects register tags in Config/Tags/*.ini"));
				return false;
			}
			Out.AddTag(T);
		}
		return true;
	}

	bool Matches(const FGameplayTagContainer& Have, const FGameplayTagContainer& All, const FGameplayTagContainer& Any, const FGameplayTagContainer& None)
	{
		return Have.HasAll(All) && (Any.IsEmpty() || Have.HasAny(Any)) && !Have.HasAny(None);
	}

	/** Changes the actor's runtime tag component (created on first use) as one undoable step. */
	TArray<FString> Change(AActor* Actor, const TArray<FString>& Tags, bool bAdd)
	{
		if (!Actor)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("actor is required"));
			return {};
		}
		FGameplayTagContainer Delta;
		if (!Resolve(Tags, Delta))
		{
			return {};
		}
		URuntimeTagsComponent* Existing = Actor->FindComponentByClass<URuntimeTagsComponent>();
		const FGameplayTagContainer Before = Existing ? Existing->Tags : FGameplayTagContainer();
		TWeakObjectPtr<AActor> Weak = Actor;
		auto Apply = [Weak](const FGameplayTagContainer& Value)
		{
			AActor* A = Weak.Get();
			if (!A)
			{
				return false;
			}
			URuntimeTagsComponent* C = A->FindComponentByClass<URuntimeTagsComponent>();
			if (!C)
			{
				C = NewObject<URuntimeTagsComponent>(A);
				C->RegisterComponent();
				A->AddInstanceComponent(C);
			}
			C->Tags = Value;
			return true;
		};
		FGameplayTagContainer After = Before;
		if (bAdd)
		{
			After.AppendTags(Delta);
		}
		else
		{
			After.RemoveTags(Delta);
		}
		auto Do = [Apply, After](FRuntimeCommandContext&, FString& Err)
		{
			if (!Apply(After))
			{
				Err = TEXT("actor is gone");
				return false;
			}
			return true;
		};
		auto Undo = [Apply, Before](FRuntimeCommandContext&) { return Apply(Before); };
		if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(bAdd ? TEXT("add gameplay tags") : TEXT("remove gameplay tags"), Do, Undo)))
		{
			return {};
		}
		return Names(RuntimeTags::TagsOf(Actor));
	}
}

TArray<FString> URuntimeTagsToolset::ListTags(const FString& ParentTag)
{
	if (ParentTag.IsEmpty())
	{
		return RtTags::AllTags();
	}
	const FGameplayTag Parent = RuntimeTags::Find(ParentTag);
	if (!Parent.IsValid())
	{
		FGameplayTagContainer Unused;
		RtTags::Resolve({ParentTag}, Unused);
		return {};
	}
	TArray<FString> Out;
	FGameplayTagContainer All;
	UGameplayTagsManager::Get().RequestAllGameplayTags(All, false);
	for (const FGameplayTag& T : All)
	{
		if (T.MatchesTag(Parent))
		{
			Out.Add(T.ToString());
		}
	}
	Out.Sort();
	return Out;
}

FRuntimeTagInfo URuntimeTagsToolset::GetTagInfo(const FString& TagName)
{
	FRuntimeTagInfo I;
	I.Name = TagName;
	const FGameplayTag T = RuntimeTags::Find(TagName);
	I.bExists = T.IsValid();
	if (!I.bExists)
	{
		return I;
	}
	const FGameplayTagContainer Parents = T.GetGameplayTagParents();
	for (const FGameplayTag& P : Parents)
	{
		if (P != T)
		{
			I.Parents.Add(P.ToString());
		}
	}
	I.Parents.Sort([](const FString& A, const FString& B) { return A.Len() < B.Len(); });
	if (TSharedPtr<FGameplayTagNode> Node = UGameplayTagsManager::Get().FindTagNode(T))
	{
		for (const TSharedPtr<FGameplayTagNode>& C : Node->GetChildTagNodes())
		{
			I.Children.Add(C->GetCompleteTagString());
		}
		I.Children.Sort();
	}
	return I;
}

TArray<FString> URuntimeTagsToolset::GetActorTags(AActor* Actor)
{
	if (!Actor)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("actor is required"));
		return {};
	}
	return RtTags::Names(RuntimeTags::TagsOf(Actor));
}

TArray<FString> URuntimeTagsToolset::AddActorTags(AActor* Actor, const TArray<FString>& Tags)
{
	return RtTags::Change(Actor, Tags, true);
}

TArray<FString> URuntimeTagsToolset::RemoveActorTags(AActor* Actor, const TArray<FString>& Tags)
{
	return RtTags::Change(Actor, Tags, false);
}

FRuntimeTagQueryResult URuntimeTagsToolset::MatchTags(AActor* Actor, const TArray<FString>& All, const TArray<FString>& Any, const TArray<FString>& None)
{
	FRuntimeTagQueryResult R;
	FGameplayTagContainer A, Y, N;
	if (!Actor)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("actor is required"));
		return R;
	}
	if (!RtTags::Resolve(All, A) || !RtTags::Resolve(Any, Y) || !RtTags::Resolve(None, N))
	{
		return R;
	}
	const FGameplayTagContainer Have = RuntimeTags::TagsOf(Actor);
	R.Tags = RtTags::Names(Have);
	R.bMatches = RtTags::Matches(Have, A, Y, N);
	for (const FGameplayTag& T : A)
	{
		if (!Have.HasTag(T))
		{
			R.MissingAll.Add(T.ToString());
		}
	}
	for (const FGameplayTag& T : N)
	{
		if (Have.HasTag(T))
		{
			R.PresentNone.Add(T.ToString());
		}
	}
	return R;
}

TArray<FRuntimeTaggedActor> URuntimeTagsToolset::FindActorsWithTags(const TArray<FString>& All, const TArray<FString>& Any, const TArray<FString>& None, int32 Limit)
{
	TArray<FRuntimeTaggedActor> Out;
	FGameplayTagContainer A, Y, N;
	if (!RtTags::Resolve(All, A) || !RtTags::Resolve(Any, Y) || !RtTags::Resolve(None, N))
	{
		return Out;
	}
	if (A.IsEmpty() && Y.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give at least one tag in all or any"));
		return Out;
	}
	for (TActorIterator<AActor> It(RuntimeToolCall::GetWorld()); It; ++It)
	{
		const FGameplayTagContainer Have = RuntimeTags::TagsOf(*It);
		if (!Have.IsEmpty() && RtTags::Matches(Have, A, Y, N))
		{
			FRuntimeTaggedActor& R = Out.AddDefaulted_GetRef();
			R.Actor = *It;
			R.Tags = RtTags::Names(Have);
		}
	}
	Out.Sort([](const FRuntimeTaggedActor& X, const FRuntimeTaggedActor& Z) { return X.Actor->GetName() < Z.Actor->GetName(); });
	if (Out.Num() > Limit)
	{
		Out.SetNum(FMath::Max(0, Limit));
	}
	return Out;
}
