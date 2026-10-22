#include "RuntimeGasToolset.h"
#include "RuntimeGasEffectAbility.h"
#include "RuntimeGenericAttributeSet.h"
#include "RuntimeCommand.h"
#include "RuntimeToolInvocation.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameFramework/Actor.h"
#include "GameplayCueInterface.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayPrediction.h"
#include "GameplayTagsManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Access to the ASC's event-trigger table (protected, not reflected): the standard pointer-to-member idiom. */
	struct FAscAccess : public UAbilitySystemComponent
	{
		using FTriggerMap = TMap<FGameplayTag, TArray<FGameplayAbilitySpecHandle>>;
		static FTriggerMap& Triggers(UAbilitySystemComponent* Asc) { return Asc->*(&FAscAccess::GameplayEventTriggeredAbilities); }
	};

	// ---- tags -------------------------------------------------------------------------------------------------------

	int32 EditDistance(const FString& A, const FString& B)
	{
		TArray<int32> Prev, Cur;
		Prev.SetNum(B.Len() + 1);
		Cur.SetNum(B.Len() + 1);
		for (int32 j = 0; j <= B.Len(); ++j)
		{
			Prev[j] = j;
		}
		for (int32 i = 1; i <= A.Len(); ++i)
		{
			Cur[0] = i;
			for (int32 j = 1; j <= B.Len(); ++j)
			{
				const int32 Cost = FChar::ToLower(A[i - 1]) == FChar::ToLower(B[j - 1]) ? 0 : 1;
				Cur[j] = FMath::Min3(Prev[j] + 1, Cur[j - 1] + 1, Prev[j - 1] + Cost);
			}
			Swap(Prev, Cur);
		}
		return Prev[B.Len()];
	}

	/** Up to Max names closest to Want (substring matches first, then small edit distance). */
	TArray<FString> CloseMatches(const FString& Want, const TArray<FString>& Names, int32 Max = 8)
	{
		const FString W = Want.ToLower();
		FString Leaf = W;
		int32 Dot;
		if (W.FindLastChar(TEXT('.'), Dot))
		{
			Leaf = W.Mid(Dot + 1);
		}
		TArray<TPair<int32, FString>> Scored;
		for (const FString& N : Names)
		{
			const FString L = N.ToLower();
			int32 Score;
			if (L.Contains(W) || (!W.IsEmpty() && W.Contains(L)))
			{
				Score = 0;
			}
			else if (!Leaf.IsEmpty() && L.Contains(Leaf))
			{
				Score = 1;
			}
			else
			{
				Score = 2 + EditDistance(W, L);
				if (Score - 2 > FMath::Max(3, W.Len() / 3))
				{
					continue;
				}
			}
			Scored.Emplace(Score, N);
		}
		Scored.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value; });
		TArray<FString> Out;
		for (int32 i = 0; i < Scored.Num() && i < Max; ++i)
		{
			Out.Add(Scored[i].Value);
		}
		return Out;
	}

	TArray<FString> AllTagNames()
	{
		FGameplayTagContainer All;
		UGameplayTagsManager::Get().RequestAllGameplayTags(All, false);
		TArray<FString> Names;
		for (const FGameplayTag& T : All)
		{
			Names.Add(T.ToString());
		}
		return Names;
	}

	/** Resolves an existing tag; fails the call with close matches otherwise. */
	bool RequestTag(const FString& Name, FGameplayTag& Out, const TCHAR* What = TEXT("gameplay tag"))
	{
		Out = Name.IsEmpty() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(FName(*Name.TrimStartAndEnd()), false);
		if (Out.IsValid())
		{
			return true;
		}
		const TArray<FString> Close = CloseMatches(Name, AllTagNames());
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("unknown %s '%s'"), What, *Name),
			Close.Num() ? TEXT("Close matches: ") + FString::Join(Close, TEXT(", "))
						: TEXT("Tags must exist in the gameplay tag table (project GameplayTags settings or native tags); the gas toolset ships Gas.Event.Hit, Gas.State.Dead and Gas.SetByCaller.Magnitude"));
		return false;
	}

	bool RequestTags(const TArray<FString>& Names, FGameplayTagContainer& Out)
	{
		for (const FString& N : Names)
		{
			FGameplayTag T;
			if (!RequestTag(N, T))
			{
				return false;
			}
			Out.AddTag(T);
		}
		return true;
	}

	TArray<FString> TagStrings(const FGameplayTagContainer& Tags)
	{
		TArray<FString> Out;
		for (const FGameplayTag& T : Tags)
		{
			Out.Add(T.ToString());
		}
		Out.Sort();
		return Out;
	}

	// ---- ability system and attributes ------------------------------------------------------------------------------

	FString ActorLabel(const AActor* A)
	{
		return A ? RuntimeToolCall::MakeReference(A) : TEXT("(none)");
	}

	UAbilitySystemComponent* FindAsc(AActor* Target)
	{
		if (!Target)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"), TEXT("Pass a node id, e.g. \"n12\""));
			return nullptr;
		}
		UAbilitySystemComponent* Asc = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (!Asc)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s has no ability system"), *ActorLabel(Target)),
				TEXT("Add one with gas.add_ability_system"));
		}
		return Asc;
	}

	FString ClassDisplayName(const UClass* C)
	{
		FString N = C ? C->GetName() : FString();
		N.RemoveFromEnd(TEXT("_C"));
		return N;
	}

	bool IsUsableClass(const UClass* C)
	{
		return C && !C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) && !C->GetName().StartsWith(TEXT("SKEL_"))
			&& !C->GetName().StartsWith(TEXT("REINST_"));
	}

	TArray<FProperty*> AttributeProperties(const UClass* SetClass)
	{
		TArray<FProperty*> Out;
		for (TFieldIterator<FProperty> It(SetClass); It; ++It)
		{
			if (FGameplayAttribute::IsGameplayAttributeDataProperty(*It))
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	FString FullAttributeName(const FProperty* P)
	{
		return ClassDisplayName(P->GetOwnerClass()) + TEXT(".") + P->GetName();
	}

	TArray<UClass*> AttributeSetClasses()
	{
		TArray<UClass*> Out;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (*It != UAttributeSet::StaticClass() && It->IsChildOf(UAttributeSet::StaticClass()) && IsUsableClass(*It))
			{
				Out.Add(*It);
			}
		}
		Out.Sort([](const UClass& A, const UClass& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	/**
	 * Resolves "Health", "RuntimeGenericAttributeSet.Health" or "URuntimeGenericAttributeSet.Health" among the ASC's sets
	 * (or every attribute set class when Asc is null). Fails the call with candidates.
	 */
	bool FindAttribute(const FString& Name, UAbilitySystemComponent* Asc, FGameplayAttribute& Out)
	{
		TArray<UClass*> Classes;
		if (Asc)
		{
			for (const UAttributeSet* S : Asc->GetSpawnedAttributes())
			{
				if (S)
				{
					Classes.AddUnique(S->GetClass());
				}
			}
		}
		else
		{
			Classes = AttributeSetClasses();
		}
		FString SetName, AttrName = Name.TrimStartAndEnd();
		if (AttrName.Split(TEXT("."), &SetName, &AttrName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			SetName.RemoveFromStart(TEXT("U"), ESearchCase::CaseSensitive);
		}
		TArray<FProperty*> Matches;
		TArray<FString> Candidates;
		for (UClass* C : Classes)
		{
			for (FProperty* P : AttributeProperties(C))
			{
				Candidates.Add(FullAttributeName(P));
				const FString Owner = ClassDisplayName(P->GetOwnerClass());
				const bool bSetOk = SetName.IsEmpty() || Owner.Equals(SetName, ESearchCase::IgnoreCase)
					|| Owner.Equals(TEXT("U") + SetName, ESearchCase::IgnoreCase);
				if (bSetOk && P->GetName().Equals(AttrName, ESearchCase::IgnoreCase))
				{
					Matches.AddUnique(P);
				}
			}
		}
		if (Matches.Num() > 1)
		{
			if (FProperty** Generic = Matches.FindByPredicate([](const FProperty* P) { return P->GetOwnerClass() == URuntimeGenericAttributeSet::StaticClass(); }))
			{
				Matches = {*Generic};
			}
		}
		if (Matches.Num() == 1)
		{
			Out = FGameplayAttribute(Matches[0]);
			return true;
		}
		Candidates.Sort();
		if (Matches.Num() > 1)
		{
			TArray<FString> Names;
			for (const FProperty* P : Matches)
			{
				Names.Add(FullAttributeName(P));
			}
			RuntimeToolCall::Fail(ERuntimeToolError::AmbiguousTarget, FString::Printf(TEXT("attribute '%s' is ambiguous"), *Name),
				TEXT("Use the full name: ") + FString::Join(Names, TEXT(", ")));
			return false;
		}
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("unknown attribute '%s'"), *Name),
			Candidates.Num() ? TEXT("Attributes: ") + FString::Join(Candidates, TEXT(", ")) : TEXT("No attribute sets: use gas.add_ability_system"));
		return false;
	}

	FRuntimeGasAttributeValue AttributeValue(const UAbilitySystemComponent* Asc, const FGameplayAttribute& A)
	{
		FRuntimeGasAttributeValue V;
		const FProperty* P = A.GetUProperty();
		V.AttributeName = A.GetName();
		V.SetClassName = P ? ClassDisplayName(P->GetOwnerClass()) : FString();
		V.FullName = V.SetClassName + TEXT(".") + V.AttributeName;
		V.BaseValue = Asc->GetNumericAttributeBase(A);
		V.CurrentValue = Asc->GetNumericAttribute(A);
		return V;
	}

	TArray<FGameplayAttribute> AscAttributes(const UAbilitySystemComponent* Asc)
	{
		TArray<FGameplayAttribute> Out;
		for (const UAttributeSet* S : Asc->GetSpawnedAttributes())
		{
			if (S)
			{
				for (FProperty* P : AttributeProperties(S->GetClass()))
				{
					Out.AddUnique(FGameplayAttribute(P));
				}
			}
		}
		return Out;
	}

	TArray<FRuntimeGasAttributeValue> AttributeValues(const UAbilitySystemComponent* Asc)
	{
		TArray<FRuntimeGasAttributeValue> Out;
		for (const FGameplayAttribute& A : AscAttributes(Asc))
		{
			Out.Add(AttributeValue(Asc, A));
		}
		Out.Sort([](const FRuntimeGasAttributeValue& A, const FRuntimeGasAttributeValue& B) { return A.FullName < B.FullName; });
		return Out;
	}

	using FBaseSnapshot = TArray<TPair<FGameplayAttribute, float>>;

	FBaseSnapshot SnapshotBases(const UAbilitySystemComponent* Asc)
	{
		FBaseSnapshot Out;
		for (const FGameplayAttribute& A : AscAttributes(Asc))
		{
			Out.Emplace(A, Asc->GetNumericAttributeBase(A));
		}
		return Out;
	}

	void RestoreBases(UAbilitySystemComponent* Asc, const FBaseSnapshot& Snapshot)
	{
		// Maxima first so clamped values (Health <= MaxHealth) come back intact.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (const TPair<FGameplayAttribute, float>& P : Snapshot)
			{
				if (P.Key.GetName().StartsWith(TEXT("Max")) == (Pass == 0) && Asc->HasAttributeSetForAttribute(P.Key))
				{
					Asc->SetNumericAttributeBase(P.Key, P.Value);
				}
			}
		}
	}

	// ---- effect registry --------------------------------------------------------------------------------------------

	struct FEffectEntry
	{
		TStrongObjectPtr<UGameplayEffect> Effect;
		FRuntimeGasEffectInfo Info;
	};

	TMap<FString, FEffectEntry>& Effects()
	{
		static TMap<FString, FEffectEntry> Map;
		return Map;
	}

	/** Every effect object ever defined (replaced ones stay alive for effects still active) -> its name. */
	TArray<TPair<TStrongObjectPtr<UGameplayEffect>, FString>>& EffectNames()
	{
		static TArray<TPair<TStrongObjectPtr<UGameplayEffect>, FString>> Map;
		return Map;
	}

	FString EffectName(const UGameplayEffect* Def)
	{
		if (!Def)
		{
			return FString();
		}
		for (const TPair<TStrongObjectPtr<UGameplayEffect>, FString>& P : EffectNames())
		{
			if (P.Key.Get() == Def)
			{
				return P.Value;
			}
		}
		return Def->HasAnyFlags(RF_ClassDefaultObject) ? ClassDisplayName(Def->GetClass()) : Def->GetName();
	}

	TArray<FString> EffectList()
	{
		TArray<FString> Names;
		Effects().GetKeys(Names);
		Names.Sort();
		return Names;
	}

	/** A defined effect name or a GameplayEffect class path/name; fails the call with candidates. */
	const UGameplayEffect* ResolveEffect(const FString& Name)
	{
		if (const FEffectEntry* E = Effects().Find(Name.TrimStartAndEnd()))
		{
			return E->Effect.Get();
		}
		UClass* Class = nullptr;
		if (Name.Contains(TEXT("/")))
		{
			Class = FSoftClassPath(Name).TryLoadClass<UGameplayEffect>();
			if (!Class && !Name.EndsWith(TEXT("_C")))
			{
				const FString Path = Name.Contains(TEXT(".")) ? Name + TEXT("_C") : Name + TEXT(".") + FPackageName::GetShortName(Name) + TEXT("_C");
				Class = FSoftClassPath(Path).TryLoadClass<UGameplayEffect>();
			}
		}
		else
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UGameplayEffect::StaticClass()) && IsUsableClass(*It) && ClassDisplayName(*It).Equals(Name, ESearchCase::IgnoreCase))
				{
					Class = *It;
					break;
				}
			}
		}
		if (Class && Class->IsChildOf(UGameplayEffect::StaticClass()))
		{
			return GetDefault<UGameplayEffect>(Class);
		}
		const TArray<FString> Names = EffectList();
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no effect '%s'"), *Name),
			Names.Num() ? TEXT("Defined effects: ") + FString::Join(Names, TEXT(", ")) + TEXT(" (or pass a GameplayEffect class path)")
						: TEXT("Define one with gas.define_effect (or pass a GameplayEffect class path)"));
		return nullptr;
	}

	int32 HandleId(const FActiveGameplayEffectHandle& H)
	{
		return H.IsValid() ? static_cast<int32>(GetTypeHash(H)) : -1;
	}

	FRuntimeGasActiveEffect ActiveEffectInfo(const UAbilitySystemComponent* Asc, const FActiveGameplayEffect& E)
	{
		FRuntimeGasActiveEffect I;
		I.EffectName = EffectName(E.Spec.Def);
		I.Handle = HandleId(E.Handle);
		I.StackCount = E.Spec.GetStackCount();
		I.TotalDuration = E.GetDuration();
		const UWorld* World = Asc->GetWorld();
		I.RemainingDuration = I.TotalDuration < 0.f ? -1.f : FMath::Max(0.f, E.GetTimeRemaining(World ? World->GetTimeSeconds() : 0.f));
		I.Period = FMath::Max(0.f, E.GetPeriod());
		FGameplayTagContainer Granted;
		E.Spec.GetAllGrantedTags(Granted);
		I.GrantedTags = TagStrings(Granted);
		return I;
	}

	// ---- abilities ---------------------------------------------------------------------------------------------------

	TArray<FGameplayTag> ClassTriggers(const UGameplayAbility* Cdo)
	{
		TArray<FGameplayTag> Out;
		static const FArrayProperty* Prop = FindFProperty<FArrayProperty>(UGameplayAbility::StaticClass(), TEXT("AbilityTriggers"));
		if (!Cdo || !Prop || !CastField<FStructProperty>(Prop->Inner) || CastField<FStructProperty>(Prop->Inner)->Struct != FAbilityTriggerData::StaticStruct())
		{
			return Out;
		}
		const TArray<FAbilityTriggerData>& Triggers = *Prop->ContainerPtrToValuePtr<TArray<FAbilityTriggerData>>(Cdo);
		for (const FAbilityTriggerData& T : Triggers)
		{
			if (T.TriggerSource == EGameplayAbilityTriggerSource::GameplayEvent && T.TriggerTag.IsValid())
			{
				Out.Add(T.TriggerTag);
			}
		}
		return Out;
	}

	TArray<UClass*> LoadedAbilityClasses()
	{
		TArray<UClass*> Out;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (*It != UGameplayAbility::StaticClass() && It->IsChildOf(UGameplayAbility::StaticClass()) && IsUsableClass(*It))
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	/** Registered (possibly unloaded) Blueprint ability classes as class paths. */
	TArray<FTopLevelAssetPath> RegisteredAbilityClassPaths()
	{
		TSet<FTopLevelAssetPath> Derived;
		if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
		{
			IAssetRegistry::GetChecked().GetDerivedClassNames({UGameplayAbility::StaticClass()->GetClassPathName()}, {}, Derived);
		}
		return Derived.Array();
	}

	UClass* ResolveAbilityClass(const FString& Name)
	{
		const FString N = Name.TrimStartAndEnd();
		if (N.Contains(TEXT("/")))
		{
			UClass* C = FSoftClassPath(N).TryLoadClass<UGameplayAbility>();
			if (!C && !N.EndsWith(TEXT("_C")))
			{
				const FString Path = N.Contains(TEXT(".")) ? N + TEXT("_C") : N + TEXT(".") + FPackageName::GetShortName(N) + TEXT("_C");
				C = FSoftClassPath(Path).TryLoadClass<UGameplayAbility>();
			}
			if (C && C->IsChildOf(UGameplayAbility::StaticClass()))
			{
				return C;
			}
		}
		else
		{
			FString Want = N;
			Want.RemoveFromEnd(TEXT("_C"));
			for (UClass* C : LoadedAbilityClasses())
			{
				const FString D = ClassDisplayName(C);
				if (D.Equals(Want, ESearchCase::IgnoreCase) || (TEXT("U") + D).Equals(Want, ESearchCase::IgnoreCase))
				{
					return C;
				}
			}
			for (const FTopLevelAssetPath& P : RegisteredAbilityClassPaths())
			{
				FString D = P.GetAssetName().ToString();
				D.RemoveFromEnd(TEXT("_C"));
				if (D.Equals(Want, ESearchCase::IgnoreCase))
				{
					if (UClass* C = FSoftClassPath(P.ToString()).TryLoadClass<UGameplayAbility>())
					{
						return C;
					}
				}
			}
		}
		TArray<FString> Names;
		for (UClass* C : LoadedAbilityClasses())
		{
			Names.AddUnique(ClassDisplayName(C));
		}
		const TArray<FString> Close = CloseMatches(N, Names);
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no gameplay ability class '%s'"), *Name),
			Close.Num() ? TEXT("Close matches: ") + FString::Join(Close, TEXT(", ")) : TEXT("See gas.list_ability_classes, or leave ability_class empty and pass effect"));
		return nullptr;
	}

	FGameplayTagContainer SpecTags(const FGameplayAbilitySpec& Spec)
	{
		FGameplayTagContainer Tags;
		if (Spec.Ability)
		{
			Tags.AppendTags(Spec.Ability->GetAssetTags());
		}
		Tags.AppendTags(Spec.GetDynamicSpecSourceTags());
		return Tags;
	}

	TArray<FString> SpecTriggers(UAbilitySystemComponent* Asc, const FGameplayAbilitySpecHandle& Handle)
	{
		TArray<FString> Out;
		for (const TPair<FGameplayTag, TArray<FGameplayAbilitySpecHandle>>& P : FAscAccess::Triggers(Asc))
		{
			if (P.Value.Contains(Handle))
			{
				Out.Add(P.Key.ToString());
			}
		}
		Out.Sort();
		return Out;
	}

	FString DescribeGranted(UAbilitySystemComponent* Asc)
	{
		TArray<FString> Items;
		for (const FGameplayAbilitySpec& S : Asc->GetActivatableAbilities())
		{
			const TArray<FString> Tags = TagStrings(SpecTags(S));
			Items.Add(ClassDisplayName(S.Ability ? S.Ability->GetClass() : nullptr) + (Tags.Num() ? TEXT(" [") + FString::Join(Tags, TEXT(", ")) + TEXT("]") : FString()));
		}
		return Items.Num() ? TEXT("Granted: ") + FString::Join(Items, TEXT("; ")) : TEXT("Nothing granted: use gas.give_ability");
	}
}

void RuntimeGas::ResetRegistry()
{
	Effects().Reset();
	EffectNames().Reset();
}

// ---- inspection ------------------------------------------------------------------------------------------------------

TArray<FRuntimeGasAttributeValue> URuntimeGasToolset::GetAttributeValues(AActor* Target)
{
	const UAbilitySystemComponent* Asc = FindAsc(Target);
	return Asc ? AttributeValues(Asc) : TArray<FRuntimeGasAttributeValue>();
}

TArray<FRuntimeGasActiveEffect> URuntimeGasToolset::GetActiveEffects(AActor* Target)
{
	TArray<FRuntimeGasActiveEffect> Out;
	const UAbilitySystemComponent* Asc = FindAsc(Target);
	if (!Asc)
	{
		return Out;
	}
	for (const FActiveGameplayEffectHandle& H : Asc->GetActiveGameplayEffects().GetAllActiveEffectHandles())
	{
		const FActiveGameplayEffect* E = Asc->GetActiveGameplayEffect(H);
		if (E && !E->IsPendingRemove)
		{
			Out.Add(ActiveEffectInfo(Asc, *E));
		}
	}
	Out.Sort([](const FRuntimeGasActiveEffect& A, const FRuntimeGasActiveEffect& B) { return A.EffectName != B.EffectName ? A.EffectName < B.EffectName : A.Handle < B.Handle; });
	return Out;
}

TArray<FRuntimeGasGrantedAbility> URuntimeGasToolset::GetGrantedAbilities(AActor* Target)
{
	TArray<FRuntimeGasGrantedAbility> Out;
	UAbilitySystemComponent* Asc = FindAsc(Target);
	if (!Asc)
	{
		return Out;
	}
	for (const FGameplayAbilitySpec& S : Asc->GetActivatableAbilities())
	{
		FRuntimeGasGrantedAbility& G = Out.AddDefaulted_GetRef();
		G.AbilityName = ClassDisplayName(S.Ability ? S.Ability->GetClass() : nullptr);
		G.Level = S.Level;
		G.bIsActive = S.IsActive();
		G.Tags = TagStrings(SpecTags(S));
		G.Effect = EffectName(Cast<UGameplayEffect>(S.SourceObject.Get()));
		G.TriggerEvents = SpecTriggers(Asc, S.Handle);
	}
	Out.Sort([](const FRuntimeGasGrantedAbility& A, const FRuntimeGasGrantedAbility& B) { return A.AbilityName < B.AbilityName; });
	return Out;
}

TArray<FString> URuntimeGasToolset::GetActiveTags(AActor* Target)
{
	const UAbilitySystemComponent* Asc = FindAsc(Target);
	return Asc ? TagStrings(Asc->GetOwnedGameplayTags()) : TArray<FString>();
}

TArray<FRuntimeGasAttributeSetClass> URuntimeGasToolset::FindAttributeSetClasses()
{
	TArray<FRuntimeGasAttributeSetClass> Out;
	for (UClass* C : AttributeSetClasses())
	{
		FRuntimeGasAttributeSetClass& I = Out.AddDefaulted_GetRef();
		I.ClassName = ClassDisplayName(C);
		I.ClassPath = C->GetPathName();
		for (const FProperty* P : AttributeProperties(C))
		{
			FRuntimeGasAttributeInfo& A = I.Attributes.AddDefaulted_GetRef();
			A.AttributeName = P->GetName();
			A.SetClassName = ClassDisplayName(P->GetOwnerClass());
			A.FullName = A.SetClassName + TEXT(".") + A.AttributeName;
		}
	}
	Out.Sort([](const FRuntimeGasAttributeSetClass& A, const FRuntimeGasAttributeSetClass& B) { return A.ClassName < B.ClassName; });
	return Out;
}

// ---- setup and attributes --------------------------------------------------------------------------------------------

FRuntimeGasSystemInfo URuntimeGasToolset::AddAbilitySystem(AActor* Target, const TMap<FString, float>& Attributes)
{
	FRuntimeGasSystemInfo Info;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return Info;
	}
	if (const UAbilitySystemComponent* Existing = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target); Existing && Existing->GetSet<URuntimeGenericAttributeSet>())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s already has the generic attribute set"), *ActorLabel(Target)),
			TEXT("Change values with gas.set_attribute_base"));
		return Info;
	}
	TMap<FName, float> Values = URuntimeGenericAttributeSet::Defaults();
	TSet<FName> Given;
	for (const TPair<FString, float>& P : Attributes)
	{
		const FName* Key = nullptr;
		for (const TPair<FName, float>& D : URuntimeGenericAttributeSet::Defaults())
		{
			if (D.Key.ToString().Equals(P.Key, ESearchCase::IgnoreCase))
			{
				Key = &D.Key;
			}
		}
		if (!Key)
		{
			TArray<FString> Names;
			for (const TPair<FName, float>& D : URuntimeGenericAttributeSet::Defaults())
			{
				Names.Add(D.Key.ToString());
			}
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown attribute '%s'"), *P.Key),
				TEXT("Generic attributes: ") + FString::Join(Names, TEXT(", ")));
			return Info;
		}
		Values.Add(*Key, P.Value);
		Given.Add(*Key);
	}
	for (const TCHAR* Pool : {TEXT("Health"), TEXT("Stamina"), TEXT("Mana")})
	{
		const FName Cur(Pool), Max(FString(TEXT("Max")) + Pool);
		if (Given.Contains(Cur) && !Given.Contains(Max) && Values[Cur] > Values[Max])
		{
			Values.Add(Max, Values[Cur]);
		}
	}
	if (!UAbilitySystemGlobals::Get().IsAbilitySystemGlobalsInitialized())
	{
		UAbilitySystemGlobals::Get().InitGlobalData();
	}

	struct FState
	{
		TWeakObjectPtr<UAbilitySystemComponent> Asc;
		TWeakObjectPtr<URuntimeGenericAttributeSet> Set;
		bool bAdded = false;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<AActor> Weak = Target;
	auto Do = [Weak, Values, State](FRuntimeCommandContext&, FString& Err)
	{
		AActor* A = Weak.Get();
		if (!A)
		{
			Err = TEXT("actor is gone");
			return false;
		}
		UAbilitySystemComponent* Asc = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(A);
		State->bAdded = Asc == nullptr;
		if (!Asc)
		{
			Asc = NewObject<UAbilitySystemComponent>(A, MakeUniqueObjectName(A, UAbilitySystemComponent::StaticClass(), TEXT("AbilitySystem")));
			A->AddInstanceComponent(Asc);
			Asc->RegisterComponent();
			Asc->InitAbilityActorInfo(A, A);
		}
		URuntimeGenericAttributeSet* Set = NewObject<URuntimeGenericAttributeSet>(Asc->GetOwner() ? Asc->GetOwner() : A);
		for (FProperty* P : AttributeProperties(URuntimeGenericAttributeSet::StaticClass()))
		{
			if (const float* V = Values.Find(P->GetFName()))
			{
				FGameplayAttributeData* Data = P->ContainerPtrToValuePtr<FGameplayAttributeData>(Set);
				Data->SetBaseValue(*V);
				Data->SetCurrentValue(*V);
			}
		}
		Asc->AddSpawnedAttribute(Set);
		State->Asc = Asc;
		State->Set = Set;
		return true;
	};
	auto Undo = [Weak, State](FRuntimeCommandContext&)
	{
		UAbilitySystemComponent* Asc = State->Asc.Get();
		if (!Asc)
		{
			return false;
		}
		if (URuntimeGenericAttributeSet* Set = State->Set.Get())
		{
			Asc->RemoveSpawnedAttribute(Set);
		}
		if (State->bAdded)
		{
			if (AActor* A = Weak.Get())
			{
				A->RemoveInstanceComponent(Asc);
			}
			Asc->DestroyComponent();
		}
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("add ability system"), Do, Undo)))
	{
		return Info;
	}
	Info.Id = RuntimeToolCall::MakeReference(Target);
	Info.Name = Target->GetName();
	Info.bAddedComponent = State->bAdded;
	if (const UAbilitySystemComponent* Asc = State->Asc.Get())
	{
		Info.Attributes = AttributeValues(Asc);
	}
	return Info;
}

FRuntimeGasAttributeValue URuntimeGasToolset::SetAttributeBase(AActor* Target, const FString& Attribute, float Value)
{
	UAbilitySystemComponent* Asc = FindAsc(Target);
	FGameplayAttribute Attr;
	if (!Asc || !FindAttribute(Attribute, Asc, Attr))
	{
		return {};
	}
	TWeakObjectPtr<UAbilitySystemComponent> Weak = Asc;
	const float Old = Asc->GetNumericAttributeBase(Attr);
	auto Do = [Weak, Attr, Value](FRuntimeCommandContext&, FString& Err)
	{
		if (!Weak.IsValid())
		{
			Err = TEXT("ability system is gone");
			return false;
		}
		Weak->SetNumericAttributeBase(Attr, Value);
		return true;
	};
	auto Undo = [Weak, Attr, Old](FRuntimeCommandContext&)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetNumericAttributeBase(Attr, Old);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("set attribute ") + Attr.GetName(), Do, Undo)))
	{
		return {};
	}
	return AttributeValue(Asc, Attr);
}

// ---- effects ---------------------------------------------------------------------------------------------------------

FRuntimeGasEffectInfo URuntimeGasToolset::DefineEffect(const FString& Name, ERuntimeGasDurationPolicy DurationPolicy, const TArray<FRuntimeGasModifier>& Modifiers,
	const TArray<FString>& GrantedTags, const TArray<FString>& ApplicationRequiredTags, const TArray<FString>& Cues, float Duration, float Period,
	ERuntimeGasStacking Stacking, int32 StackLimit, bool bReplace)
{
	const FString Key = Name.TrimStartAndEnd();
	if (Key.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("name is required"));
		return {};
	}
	if (Effects().Contains(Key) && !bReplace)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("effect '%s' already exists"), *Key), TEXT("Pass replace true to redefine it"));
		return {};
	}
	const bool bInstant = DurationPolicy == ERuntimeGasDurationPolicy::Instant;
	if (DurationPolicy == ERuntimeGasDurationPolicy::HasDuration && Duration <= 0.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("has_duration needs a positive duration (seconds)"));
		return {};
	}
	if (bInstant && (GrantedTags.Num() || Period > 0.f || Stacking != ERuntimeGasStacking::None))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("instant effects cannot grant tags, be periodic or stack"),
			TEXT("Use has_duration or infinite"));
		return {};
	}
	if (Modifiers.IsEmpty() && GrantedTags.IsEmpty() && Cues.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("the effect does nothing: give modifiers, granted_tags or cues"));
		return {};
	}
	FGameplayTagContainer Granted, Required, CueTags;
	if (!RequestTags(GrantedTags, Granted) || !RequestTags(ApplicationRequiredTags, Required) || !RequestTags(Cues, CueTags))
	{
		return {};
	}
	const FGameplayTag CueRoot = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue"), false);
	for (const FGameplayTag& T : CueTags)
	{
		if (!CueRoot.IsValid() || !T.MatchesTag(CueRoot))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("cue '%s' is not a GameplayCue.* tag"), *T.ToString()));
			return {};
		}
	}

	UGameplayEffect* GE = NewObject<UGameplayEffect>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UGameplayEffect::StaticClass(), FName(TEXT("GE_") + Key)));
	static const EGameplayEffectDurationType Policies[] = {EGameplayEffectDurationType::Instant, EGameplayEffectDurationType::HasDuration, EGameplayEffectDurationType::Infinite};
	GE->DurationPolicy = Policies[static_cast<uint8>(DurationPolicy)];
	if (DurationPolicy == ERuntimeGasDurationPolicy::HasDuration)
	{
		GE->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Duration));
	}
	if (!bInstant && Period > 0.f)
	{
		GE->Period = FScalableFloat(Period);
	}
	static const EGameplayModOp::Type Ops[] = {EGameplayModOp::AddBase, EGameplayModOp::MultiplyAdditive, EGameplayModOp::DivideAdditive, EGameplayModOp::Override,
		EGameplayModOp::AddFinal};
	for (const FRuntimeGasModifier& M : Modifiers)
	{
		FGameplayModifierInfo& Mod = GE->Modifiers.AddDefaulted_GetRef();
		if (!FindAttribute(M.Attribute, nullptr, Mod.Attribute))
		{
			return {};
		}
		Mod.ModifierOp = Ops[static_cast<uint8>(M.Op)];
		if (!M.SetByCaller.IsEmpty())
		{
			FSetByCallerFloat SetByCaller;
			if (!RequestTag(M.SetByCaller, SetByCaller.DataTag, TEXT("set_by_caller tag")))
			{
				return {};
			}
			Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		}
		else
		{
			Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(M.Magnitude));
		}
	}
	if (!Granted.IsEmpty())
	{
		FInheritedTagContainer Tags;
		for (const FGameplayTag& T : Granted)
		{
			Tags.AddTag(T);
		}
		GE->AddComponent<UTargetTagsGameplayEffectComponent>().SetAndApplyTargetTagChanges(Tags);
	}
	if (!Required.IsEmpty())
	{
		GE->AddComponent<UTargetTagRequirementsGameplayEffectComponent>().ApplicationTagRequirements.RequireTags = Required;
	}
	for (const FGameplayTag& T : CueTags)
	{
		GE->GameplayCues.Add(FGameplayEffectCue(T, 0.f, 1.f));
	}
	if (Stacking != ERuntimeGasStacking::None)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		GE->StackingType = Stacking == ERuntimeGasStacking::AggregateBySource ? EGameplayEffectStackingType::AggregateBySource : EGameplayEffectStackingType::AggregateByTarget;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		GE->StackLimitCount = FMath::Max(0, StackLimit);
	}
	GE->OnGameplayEffectChanged();

	FEffectEntry& Entry = Effects().Add(Key);
	Entry.Effect.Reset(GE);
	EffectNames().Emplace(TStrongObjectPtr<UGameplayEffect>(GE), Key);
	FRuntimeGasEffectInfo& I = Entry.Info;
	I.Name = Key;
	I.DurationPolicy = DurationPolicy;
	I.Duration = DurationPolicy == ERuntimeGasDurationPolicy::HasDuration ? Duration : 0.f;
	I.Period = bInstant ? 0.f : FMath::Max(0.f, Period);
	I.Modifiers = Modifiers;
	for (int32 i = 0; i < I.Modifiers.Num(); ++i)
	{
		const FProperty* P = GE->Modifiers[i].Attribute.GetUProperty();
		I.Modifiers[i].Attribute = P ? FullAttributeName(P) : I.Modifiers[i].Attribute;
	}
	I.GrantedTags = TagStrings(Granted);
	I.ApplicationRequiredTags = TagStrings(Required);
	I.Stacking = Stacking;
	I.StackLimit = Stacking != ERuntimeGasStacking::None ? FMath::Max(0, StackLimit) : 0;
	I.Cues = TagStrings(CueTags);
	return I;
}

TArray<FRuntimeGasEffectInfo> URuntimeGasToolset::ListEffects()
{
	TArray<FRuntimeGasEffectInfo> Out;
	for (const FString& N : EffectList())
	{
		Out.Add(Effects()[N].Info);
	}
	return Out;
}

FRuntimeGasApplyResult URuntimeGasToolset::ApplyEffect(AActor* Target, const FString& Effect, const TMap<FString, float>& SetByCaller, float Level,
	AActor* Instigator)
{
	FRuntimeGasApplyResult R;
	UAbilitySystemComponent* Asc = FindAsc(Target);
	if (!Asc)
	{
		return R;
	}
	const UGameplayEffect* GE = ResolveEffect(Effect);
	if (!GE)
	{
		return R;
	}
	TArray<TPair<FGameplayTag, float>> Magnitudes;
	for (const TPair<FString, float>& P : SetByCaller)
	{
		FGameplayTag T;
		if (!RequestTag(P.Key, T, TEXT("set_by_caller tag")))
		{
			return R;
		}
		Magnitudes.Emplace(T, P.Value);
	}
	for (const FGameplayModifierInfo& M : GE->Modifiers)
	{
		if (M.ModifierMagnitude.GetMagnitudeCalculationType() == EGameplayEffectMagnitudeCalculation::SetByCaller)
		{
			const FGameplayTag Need = M.ModifierMagnitude.GetSetByCallerFloat().DataTag;
			if (Need.IsValid() && !Magnitudes.ContainsByPredicate([&Need](const TPair<FGameplayTag, float>& P) { return P.Key == Need; }))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("effect '%s' needs a SetByCaller magnitude for %s"), *Effect, *Need.ToString()),
					FString::Printf(TEXT("Pass set_by_caller {\"%s\": 25}"), *Need.ToString()));
				return R;
			}
		}
	}
	R.Effect = EffectName(GE);

	struct FState
	{
		FActiveGameplayEffectHandle Handle;
		FBaseSnapshot Before;
		bool bApplied = false;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UAbilitySystemComponent> WeakAsc = Asc;
	TWeakObjectPtr<const UGameplayEffect> WeakGE = GE;
	TWeakObjectPtr<AActor> WeakInstigator = Instigator;
	auto Do = [WeakAsc, WeakGE, WeakInstigator, Magnitudes, Level, State](FRuntimeCommandContext&, FString& Err)
	{
		UAbilitySystemComponent* A = WeakAsc.Get();
		if (!A || !WeakGE.IsValid())
		{
			Err = TEXT("ability system or effect is gone");
			return false;
		}
		FGameplayEffectContextHandle Context = A->MakeEffectContext();
		if (AActor* I = WeakInstigator.Get())
		{
			Context.AddInstigator(I, I);
		}
		FGameplayEffectSpec Spec(WeakGE.Get(), Context, Level);
		for (const TPair<FGameplayTag, float>& M : Magnitudes)
		{
			Spec.SetSetByCallerMagnitude(M.Key, M.Value);
		}
		State->Before = SnapshotBases(A);
		State->Handle = A->ApplyGameplayEffectSpecToSelf(Spec);
		State->bApplied = State->Handle.WasSuccessfullyApplied();
		return true;
	};
	auto Undo = [WeakAsc, State](FRuntimeCommandContext&)
	{
		UAbilitySystemComponent* A = WeakAsc.Get();
		if (!A)
		{
			return false;
		}
		if (State->Handle.IsValid())
		{
			A->RemoveActiveGameplayEffect(State->Handle);
		}
		else if (State->bApplied)
		{
			RestoreBases(A, State->Before);
		}
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("apply effect ") + R.Effect, Do, Undo)))
	{
		return R;
	}
	R.bApplied = State->bApplied;
	R.Handle = HandleId(State->Handle);
	R.Attributes = AttributeValues(Asc);
	R.Tags = TagStrings(Asc->GetOwnedGameplayTags());
	return R;
}

FRuntimeGasRemoveResult URuntimeGasToolset::RemoveEffect(AActor* Target, const FString& Effect, int32 Stacks)
{
	FRuntimeGasRemoveResult R;
	UAbilitySystemComponent* Asc = FindAsc(Target);
	if (!Asc)
	{
		return R;
	}
	const FString Want = Effect.TrimStartAndEnd();
	const bool bById = !Want.IsEmpty() && Want.IsNumeric();
	TArray<FActiveGameplayEffectHandle> Handles;
	TArray<FString> Active;
	for (const FActiveGameplayEffectHandle& H : Asc->GetActiveGameplayEffects().GetAllActiveEffectHandles())
	{
		const FActiveGameplayEffect* E = Asc->GetActiveGameplayEffect(H);
		if (!E || E->IsPendingRemove)
		{
			continue;
		}
		const FString Name = EffectName(E->Spec.Def);
		Active.Add(FString::Printf(TEXT("%s (handle %d)"), *Name, HandleId(H)));
		if (bById ? HandleId(H) == FCString::Atoi(*Want) : Name.Equals(Want, ESearchCase::IgnoreCase))
		{
			Handles.Add(H);
		}
	}
	if (Handles.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no active effect '%s' on %s"), *Effect, *ActorLabel(Target)),
			Active.Num() ? TEXT("Active: ") + FString::Join(Active, TEXT(", ")) : TEXT("No active effects (instant effects are never active)"));
		return R;
	}
	struct FState
	{
		TArray<FActiveGameplayEffectHandle> Handles;
		TArray<FGameplayEffectSpec> Removed;
		int32 Count = 0;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	State->Handles = Handles;
	TWeakObjectPtr<UAbilitySystemComponent> WeakAsc = Asc;
	auto Do = [WeakAsc, State, Stacks](FRuntimeCommandContext&, FString& Err)
	{
		UAbilitySystemComponent* A = WeakAsc.Get();
		if (!A)
		{
			Err = TEXT("ability system is gone");
			return false;
		}
		State->Removed.Reset();
		State->Count = 0;
		for (const FActiveGameplayEffectHandle& H : State->Handles)
		{
			if (const FActiveGameplayEffect* E = A->GetActiveGameplayEffect(H))
			{
				FGameplayEffectSpec Copy = E->Spec;
				if (Stacks > 0)
				{
					Copy.SetStackCount(FMath::Min(Stacks, E->Spec.GetStackCount()));
				}
				State->Removed.Add(Copy);
				if (A->RemoveActiveGameplayEffect(H, Stacks))
				{
					++State->Count;
				}
			}
		}
		return true;
	};
	auto Undo = [WeakAsc, State](FRuntimeCommandContext&)
	{
		UAbilitySystemComponent* A = WeakAsc.Get();
		if (!A)
		{
			return false;
		}
		// Re-applied effects get new handles; redo removes those.
		State->Handles.Reset();
		for (const FGameplayEffectSpec& S : State->Removed)
		{
			const FActiveGameplayEffectHandle H = A->ApplyGameplayEffectSpecToSelf(S);
			if (H.IsValid())
			{
				State->Handles.Add(H);
			}
		}
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("remove effect ") + Want, Do, Undo)))
	{
		return R;
	}
	R.Removed = State->Count;
	R.Attributes = AttributeValues(Asc);
	R.Tags = TagStrings(Asc->GetOwnedGameplayTags());
	return R;
}

// ---- abilities -------------------------------------------------------------------------------------------------------

TArray<FRuntimeGasAbilityClass> URuntimeGasToolset::ListAbilityClasses(const FString& Filter)
{
	TArray<FRuntimeGasAbilityClass> Out;
	TSet<FString> Seen;
	for (UClass* C : LoadedAbilityClasses())
	{
		FRuntimeGasAbilityClass I;
		I.Name = ClassDisplayName(C);
		I.Path = C->GetPathName();
		if (Seen.Contains(I.Path) || (!Filter.IsEmpty() && !I.Name.Contains(Filter)))
		{
			continue;
		}
		Seen.Add(I.Path);
		const UGameplayAbility* Cdo = GetDefault<UGameplayAbility>(C);
		I.Tags = TagStrings(Cdo->GetAssetTags());
		for (const FGameplayTag& T : ClassTriggers(Cdo))
		{
			I.TriggerEvents.Add(T.ToString());
		}
		Out.Add(I);
	}
	for (const FTopLevelAssetPath& P : RegisteredAbilityClassPaths())
	{
		FRuntimeGasAbilityClass I;
		I.Path = P.ToString();
		I.Name = P.GetAssetName().ToString();
		I.Name.RemoveFromEnd(TEXT("_C"));
		if (!Seen.Contains(I.Path) && (Filter.IsEmpty() || I.Name.Contains(Filter)) && !I.Name.StartsWith(TEXT("SKEL_")))
		{
			Seen.Add(I.Path);
			Out.Add(I);
		}
	}
	Out.Sort([](const FRuntimeGasAbilityClass& A, const FRuntimeGasAbilityClass& B) { return A.Name < B.Name; });
	return Out;
}

FRuntimeGasGiveResult URuntimeGasToolset::GiveAbility(AActor* Target, const TArray<FString>& AbilityTags, const FString& AbilityClass, const FString& Effect,
	const FString& TriggerEvent, int32 Level)
{
	FRuntimeGasGiveResult R;
	UAbilitySystemComponent* Asc = FindAsc(Target);
	if (!Asc)
	{
		return R;
	}
	if (AbilityClass.IsEmpty() && Effect.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("pass ability_class, or effect for the built-in effect ability"),
			TEXT("See gas.list_ability_classes and gas.list_effects"));
		return R;
	}
	UClass* Class = AbilityClass.IsEmpty() ? URuntimeGasEffectAbility::StaticClass() : ResolveAbilityClass(AbilityClass);
	if (!Class)
	{
		return R;
	}
	const UGameplayEffect* GE = nullptr;
	if (!Effect.IsEmpty())
	{
		if (!Class->IsChildOf(URuntimeGasEffectAbility::StaticClass()))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("effect only applies to the built-in effect ability"), TEXT("Leave ability_class empty"));
			return R;
		}
		GE = ResolveEffect(Effect);
		if (!GE)
		{
			return R;
		}
	}
	else if (Class->IsChildOf(URuntimeGasEffectAbility::StaticClass()))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("the built-in effect ability needs effect"));
		return R;
	}
	FGameplayTagContainer Tags;
	FGameplayTag Trigger;
	if (!RequestTags(AbilityTags, Tags) || (!TriggerEvent.IsEmpty() && !RequestTag(TriggerEvent, Trigger, TEXT("trigger_event tag"))))
	{
		return R;
	}

	TSharedRef<FGameplayAbilitySpecHandle> Handle = MakeShared<FGameplayAbilitySpecHandle>();
	TWeakObjectPtr<UAbilitySystemComponent> WeakAsc = Asc;
	TWeakObjectPtr<UClass> WeakClass = Class;
	TWeakObjectPtr<const UGameplayEffect> WeakGE = GE;
	auto Do = [WeakAsc, WeakClass, WeakGE, Tags, Trigger, Level, Handle](FRuntimeCommandContext&, FString& Err)
	{
		UAbilitySystemComponent* A = WeakAsc.Get();
		if (!A || !WeakClass.IsValid())
		{
			Err = TEXT("ability system or class is gone");
			return false;
		}
		FGameplayAbilitySpec Spec(WeakClass.Get(), FMath::Max(1, Level), INDEX_NONE, const_cast<UGameplayEffect*>(WeakGE.Get()));
		Spec.GetDynamicSpecSourceTags().AppendTags(Tags);
		*Handle = A->GiveAbility(Spec);
		if (!Handle->IsValid())
		{
			Err = TEXT("the ability system refused the ability (abilities can only be given with authority)");
			return false;
		}
		if (Trigger.IsValid())
		{
			FAscAccess::Triggers(A).FindOrAdd(Trigger).AddUnique(*Handle);
		}
		return true;
	};
	auto Undo = [WeakAsc, Trigger, Handle](FRuntimeCommandContext&)
	{
		UAbilitySystemComponent* A = WeakAsc.Get();
		if (!A)
		{
			return false;
		}
		if (Trigger.IsValid())
		{
			if (TArray<FGameplayAbilitySpecHandle>* L = FAscAccess::Triggers(A).Find(Trigger))
			{
				L->Remove(*Handle);
				if (L->IsEmpty())
				{
					FAscAccess::Triggers(A).Remove(Trigger);
				}
			}
		}
		A->ClearAbility(*Handle);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("give ability ") + ClassDisplayName(Class), Do, Undo)))
	{
		return R;
	}
	R.AbilityName = ClassDisplayName(Class);
	if (const FGameplayAbilitySpec* Spec = Asc->FindAbilitySpecFromHandle(*Handle))
	{
		R.Tags = TagStrings(SpecTags(*Spec));
	}
	R.TriggerEvents = SpecTriggers(Asc, *Handle);
	return R;
}

FRuntimeGasActivateResult URuntimeGasToolset::TryActivateAbility(AActor* Target, const FString& Ability)
{
	FRuntimeGasActivateResult R;
	UAbilitySystemComponent* Asc = FindAsc(Target);
	if (!Asc)
	{
		return R;
	}
	FString Want = Ability.TrimStartAndEnd();
	Want.RemoveFromEnd(TEXT("_C"));
	TArray<TPair<FGameplayAbilitySpecHandle, FString>> Matches;
	for (const FGameplayAbilitySpec& S : Asc->GetActivatableAbilities())
	{
		const FString Name = ClassDisplayName(S.Ability ? S.Ability->GetClass() : nullptr);
		if (Name.Equals(Want, ESearchCase::IgnoreCase) || (S.Ability && S.Ability->GetClass()->GetPathName().Equals(Ability, ESearchCase::IgnoreCase)))
		{
			Matches.Emplace(S.Handle, Name);
		}
	}
	if (Matches.IsEmpty())
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Want), false);
		if (Tag.IsValid())
		{
			for (const FGameplayAbilitySpec& S : Asc->GetActivatableAbilities())
			{
				if (SpecTags(S).HasTag(Tag))
				{
					Matches.Emplace(S.Handle, ClassDisplayName(S.Ability ? S.Ability->GetClass() : nullptr));
				}
			}
		}
	}
	if (Matches.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no granted ability matches '%s'"), *Ability), DescribeGranted(Asc));
		return R;
	}
	for (const TPair<FGameplayAbilitySpecHandle, FString>& M : Matches)
	{
		R.Tried.Add(M.Value);
		R.bActivated |= Asc->TryActivateAbility(M.Key);
	}
	R.Attributes = AttributeValues(Asc);
	return R;
}

int32 URuntimeGasToolset::CancelAbilities(AActor* Target, const TArray<FString>& Tags)
{
	UAbilitySystemComponent* Asc = FindAsc(Target);
	FGameplayTagContainer With;
	if (!Asc || !RequestTags(Tags, With))
	{
		return 0;
	}
	TArray<FGameplayAbilitySpecHandle> Active;
	for (const FGameplayAbilitySpec& S : Asc->GetActivatableAbilities())
	{
		if (S.IsActive() && (With.IsEmpty() || SpecTags(S).HasAny(With)))
		{
			Active.Add(S.Handle);
		}
	}
	int32 Cancelled = 0;
	for (const FGameplayAbilitySpecHandle& H : Active)
	{
		Asc->CancelAbilityHandle(H);
		const FGameplayAbilitySpec* S = Asc->FindAbilitySpecFromHandle(H);
		Cancelled += (!S || !S->IsActive()) ? 1 : 0;
	}
	return Cancelled;
}

FRuntimeGasEventResult URuntimeGasToolset::SendGameplayEvent(AActor* Target, const FString& EventTag, float Magnitude, AActor* Instigator)
{
	FRuntimeGasEventResult R;
	UAbilitySystemComponent* Asc = FindAsc(Target);
	FGameplayTag Tag;
	if (!Asc || !RequestTag(EventTag, Tag, TEXT("event tag")))
	{
		return R;
	}
	FGameplayEventData Payload;
	Payload.EventTag = Tag;
	Payload.EventMagnitude = Magnitude;
	Payload.Instigator = Instigator;
	Payload.Target = Target;
	{
		FScopedPredictionWindow Window(Asc, true);
		R.TriggeredAbilities = Asc->HandleGameplayEvent(Tag, &Payload);
	}
	R.Attributes = AttributeValues(Asc);
	R.Tags = TagStrings(Asc->GetOwnedGameplayTags());
	return R;
}

// ---- tags and cues ---------------------------------------------------------------------------------------------------

TArray<FString> URuntimeGasToolset::AddLooseTags(AActor* Target, const TArray<FString>& Tags)
{
	UAbilitySystemComponent* Asc = FindAsc(Target);
	FGameplayTagContainer C;
	if (!Asc || !RequestTags(Tags, C))
	{
		return {};
	}
	if (C.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("tags is empty"));
		return {};
	}
	TWeakObjectPtr<UAbilitySystemComponent> Weak = Asc;
	auto Do = [Weak, C](FRuntimeCommandContext&, FString& Err)
	{
		if (!Weak.IsValid())
		{
			Err = TEXT("ability system is gone");
			return false;
		}
		Weak->AddLooseGameplayTags(C);
		return true;
	};
	auto Undo = [Weak, C](FRuntimeCommandContext&)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->RemoveLooseGameplayTags(C);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("add loose tags"), Do, Undo)))
	{
		return {};
	}
	return TagStrings(Asc->GetOwnedGameplayTags());
}

TArray<FString> URuntimeGasToolset::RemoveLooseTags(AActor* Target, const TArray<FString>& Tags)
{
	UAbilitySystemComponent* Asc = FindAsc(Target);
	FGameplayTagContainer C;
	if (!Asc || !RequestTags(Tags, C))
	{
		return {};
	}
	if (C.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("tags is empty"));
		return {};
	}
	TWeakObjectPtr<UAbilitySystemComponent> Weak = Asc;
	TSharedRef<TMap<FGameplayTag, int32>> Removed = MakeShared<TMap<FGameplayTag, int32>>();
	auto Do = [Weak, C, Removed](FRuntimeCommandContext&, FString& Err)
	{
		UAbilitySystemComponent* A = Weak.Get();
		if (!A)
		{
			Err = TEXT("ability system is gone");
			return false;
		}
		Removed->Reset();
		for (const FGameplayTag& T : C)
		{
			// Loose count is not exposed: the drop in the explicit count is what this removal took away.
			const int32 Before = A->GetTagCount(T);
			A->SetLooseGameplayTagCount(T, 0);
			if (const int32 Diff = Before - A->GetTagCount(T); Diff > 0)
			{
				Removed->Add(T, Diff);
			}
		}
		return true;
	};
	auto Undo = [Weak, Removed](FRuntimeCommandContext&)
	{
		UAbilitySystemComponent* A = Weak.Get();
		if (!A)
		{
			return false;
		}
		for (const TPair<FGameplayTag, int32>& P : *Removed)
		{
			A->AddLooseGameplayTag(P.Key, P.Value);
		}
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("remove loose tags"), Do, Undo)))
	{
		return {};
	}
	return TagStrings(Asc->GetOwnedGameplayTags());
}

namespace
{
	/** Notify path registered for a cue tag (or its closest parent with one), empty if none. */
	FString CueNotifyPath(const FGameplayTag& Tag)
	{
		UGameplayCueManager* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
		UGameplayCueSet* Set = Manager ? Manager->GetRuntimeCueSet() : nullptr;
		if (!Set)
		{
			return FString();
		}
		for (FGameplayTag T = Tag; T.IsValid(); T = T.RequestDirectParent())
		{
			if (const int32* Index = Set->GameplayCueDataMap.Find(T); Index && Set->GameplayCueData.IsValidIndex(*Index))
			{
				return Set->GameplayCueData[*Index].GameplayCueNotifyObj.ToString();
			}
		}
		return FString();
	}
}

TArray<FRuntimeGasCueInfo> URuntimeGasToolset::ListCues(const FString& ParentTag)
{
	TArray<FRuntimeGasCueInfo> Out;
	FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue"), false);
	if (!ParentTag.IsEmpty())
	{
		if (!RequestTag(ParentTag, Root))
		{
			return Out;
		}
	}
	if (!Root.IsValid())
	{
		return Out; // the project has no cue tags
	}
	FGameplayTagContainer Children = UGameplayTagsManager::Get().RequestGameplayTagChildren(Root);
	Children.AddTag(Root);
	for (const FGameplayTag& T : Children)
	{
		FRuntimeGasCueInfo& I = Out.AddDefaulted_GetRef();
		I.Tag = T.ToString();
		I.NotifyPath = CueNotifyPath(T);
	}
	Out.Sort([](const FRuntimeGasCueInfo& A, const FRuntimeGasCueInfo& B) { return A.Tag < B.Tag; });
	return Out;
}

bool URuntimeGasToolset::ExecuteCue(AActor* Target, const FString& CueTag, const FVector& Location, float NormalizedMagnitude)
{
	FGameplayTag Tag;
	if (!Target)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
		return false;
	}
	if (!RequestTag(CueTag, Tag, TEXT("cue tag")))
	{
		return false;
	}
	const FGameplayTag CueRoot = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue"), false);
	if (!CueRoot.IsValid() || !Tag.MatchesTag(CueRoot))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is not a GameplayCue.* tag"), *CueTag), TEXT("See gas.list_cues"));
		return false;
	}
	if (CueNotifyPath(Tag).IsEmpty() && !Cast<IGameplayCueInterface>(Target))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("nothing handles cue '%s'"), *CueTag),
			TEXT("Cues need a GameplayCueNotify asset for the tag (gas.list_cues shows notify paths) or an actor implementing GameplayCueInterface"));
		return false;
	}
	FGameplayCueParameters Params;
	Params.Location = RuntimeToolCall::Current() && RuntimeToolCall::Current()->Has(TEXT("location")) ? Location : Target->GetActorLocation();
	Params.NormalizedMagnitude = FMath::Clamp(NormalizedMagnitude, 0.f, 1.f);
	UGameplayCueManager::ExecuteGameplayCue_NonReplicated(Target, Tag, Params);
	return true;
}
