#include "RuntimeMaterialsToolset.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeJson.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace RtMat
{
	using namespace PresentationTool;
	using EParamKind = ERuntimeMaterialParamType;

	const TCHAR* KindName(EParamKind Kind)
	{
		return Kind == EParamKind::Scalar ? TEXT("scalar") : Kind == EParamKind::Vector ? TEXT("vector") : TEXT("texture");
	}

	const TCHAR* SetterFor(EParamKind Kind)
	{
		return Kind == EParamKind::Scalar ? TEXT("materials.set_scalar") : Kind == EParamKind::Vector ? TEXT("materials.set_vector") : TEXT("materials.set_texture");
	}

	TArray<FString> ParameterNames(const UMaterialInterface* Material, EParamKind Kind)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		if (Material)
		{
			switch (Kind)
			{
			case EParamKind::Scalar: Material->GetAllScalarParameterInfo(Infos, Ids); break;
			case EParamKind::Vector: Material->GetAllVectorParameterInfo(Infos, Ids); break;
			case EParamKind::Texture: Material->GetAllTextureParameterInfo(Infos, Ids); break;
			}
		}
		TArray<FString> Names;
		for (const FMaterialParameterInfo& Info : Infos)
		{
			Names.AddUnique(Info.Name.ToString());
		}
		Names.Sort();
		return Names;
	}

	/** Finds a parameter by name (case-insensitive) across all types. */
	bool FindParameter(const UMaterialInterface* Material, const FString& Name, EParamKind& OutKind, FString& OutExact)
	{
		for (EParamKind Kind : {EParamKind::Scalar, EParamKind::Vector, EParamKind::Texture})
		{
			for (const FString& N : ParameterNames(Material, Kind))
			{
				if (N.Equals(Name, ESearchCase::IgnoreCase))
				{
					OutKind = Kind;
					OutExact = N;
					return true;
				}
			}
		}
		return false;
	}

	FString AllParameters(const UMaterialInterface* Material)
	{
		TArray<FString> All;
		for (EParamKind Kind : {EParamKind::Scalar, EParamKind::Vector, EParamKind::Texture})
		{
			for (const FString& N : ParameterNames(Material, Kind))
			{
				All.Add(FString::Printf(TEXT("%s (%s)"), *N, KindName(Kind)));
			}
		}
		return JoinOrNone(All);
	}

	TSharedRef<FJsonValue> ColorJson(const FLinearColor& C)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.R)),
			MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.G)), MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.B)),
			MakeShared<FJsonValueNumber>(RuntimeJson::Round(C.A))});
	}

	/** [r,g,b] or [r,g,b,a] -> color. */
	bool ReadColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Arr) || (Arr->Num() != 3 && Arr->Num() != 4))
		{
			return false;
		}
		double C[4] = {0, 0, 0, 1};
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			if (!(*Arr)[i]->TryGetNumber(C[i]))
			{
				return false;
			}
		}
		Out = FLinearColor(C[0], C[1], C[2], C[3]);
		return true;
	}

	/**
	 * The mesh component to work on: the referenced component when the reference names one (actor#Component), otherwise
	 * the actor's root primitive or its first primitive with material slots.
	 */
	UPrimitiveComponent* PickPrimitive(UPrimitiveComponent* Given)
	{
		if (!Given)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target is required"));
			return nullptr;
		}
		if (Inv().GetString(TEXT("target")).Contains(TEXT("#")))
		{
			if (Given->GetNumMaterials() == 0)
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("component %s has no material slots"), *Given->GetName()));
				return nullptr;
			}
			return Given;
		}
		if (AActor* Owner = Given->GetOwner())
		{
			if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent()); Root && Root->GetNumMaterials() > 0)
			{
				return Root;
			}
			TInlineComponentArray<UPrimitiveComponent*> Prims(Owner);
			for (UPrimitiveComponent* P : Prims)
			{
				if (P->GetNumMaterials() > 0)
				{
					return P;
				}
			}
		}
		if (Given->GetNumMaterials() > 0)
		{
			return Given;
		}
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target has no mesh with material slots"), TEXT("Use target#ComponentName to pick a component"));
		return nullptr;
	}

	bool CheckSlot(UPrimitiveComponent* Prim, int32 Slot)
	{
		if (Slot < 0 || Slot >= Prim->GetNumMaterials())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("slot %d out of range (0..%d)"), Slot, Prim->GetNumMaterials() - 1),
				TEXT("See materials.list_parameters for the slots"));
			return false;
		}
		return true;
	}

	/** Reads a parameter value into a result struct. */
	void ReadInto(const UMaterialInterface* Material, EParamKind Kind, const FString& Name, FRuntimeMaterialParameter& Out)
	{
		Out.Name = Name;
		Out.Type = Kind;
		if (!Material)
		{
			return;
		}
		const FHashedMaterialParameterInfo Info{FName(*Name)};
		switch (Kind)
		{
		case EParamKind::Scalar: Material->GetScalarParameterValue(Info, Out.Scalar); break;
		case EParamKind::Vector: Material->GetVectorParameterValue(Info, Out.Vector); break;
		case EParamKind::Texture:
		{
			UTexture* T = nullptr;
			Material->GetTextureParameterValue(Info, T);
			Out.Texture = T ? T->GetPathName() : FString();
			break;
		}
		}
	}

	/** Sets one material parameter, creating a dynamic instance on the slot if needed. Undo restores the value (or the original material). */
	class FMaterialParamCommand final : public IRuntimeCommand
	{
	public:
		FMaterialParamCommand(UPrimitiveComponent* InComp, int32 InSlot, FName InParam, EParamKind InKind, const FString& InRef)
			: Comp(InComp), Slot(InSlot), Param(InParam), Kind(InKind), TargetRef(InRef)
		{
		}

		float NewScalar = 0.f;
		FLinearColor NewColor = FLinearColor::White;
		TStrongObjectPtr<UTexture> NewTexture;

		virtual FString GetLabel() const override { return FString::Printf(TEXT("set %s param %s on %s"), KindName(Kind), *Param.ToString(), *TargetRef); }

		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override
		{
			UPrimitiveComponent* C = Comp.Get();
			if (!C)
			{
				OutError = TEXT("component no longer exists");
				return false;
			}
			UMaterialInterface* Current = C->GetMaterial(Slot);
			if (!ReadValue(Current, OldScalar, OldColor, OldTexture))
			{
				OutError = FString::Printf(TEXT("material '%s' has no %s parameter '%s' (has: %s)"), Current ? *Current->GetName() : TEXT("none"), KindName(Kind),
					*Param.ToString(), *JoinOrNone(ParameterNames(Current, Kind)));
				return false;
			}
			if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Current))
			{
				MID.Reset(Existing);
				bCreatedMID = false;
			}
			else
			{
				Original.Reset(Current);
				MID.Reset(C->CreateAndSetMaterialInstanceDynamic(Slot));
				bCreatedMID = true;
				if (!MID.IsValid())
				{
					OutError = TEXT("could not create a dynamic material instance");
					return false;
				}
			}
			Apply(NewScalar, NewColor, NewTexture.Get());
			return true;
		}

		virtual bool Undo(FRuntimeCommandContext& Context) override
		{
			UPrimitiveComponent* C = Comp.Get();
			if (!C || !MID.IsValid())
			{
				return false;
			}
			if (bCreatedMID)
			{
				C->SetMaterial(Slot, Original.Get());
			}
			else
			{
				Apply(OldScalar, OldColor, OldTexture.Get());
			}
			return true;
		}

		virtual bool Redo(FRuntimeCommandContext& Context) override
		{
			UPrimitiveComponent* C = Comp.Get();
			if (!C || !MID.IsValid())
			{
				return false;
			}
			if (bCreatedMID)
			{
				C->SetMaterial(Slot, MID.Get());
			}
			Apply(NewScalar, NewColor, NewTexture.Get());
			return true;
		}

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("target"), TargetRef);
			D->SetNumberField(TEXT("slot"), Slot);
			D->SetStringField(TEXT("parameter"), Param.ToString());
			switch (Kind)
			{
			case EParamKind::Scalar: D->SetNumberField(TEXT("value"), RuntimeJson::Round(bForUndo ? OldScalar : NewScalar)); break;
			case EParamKind::Vector: D->SetField(TEXT("value"), ColorJson(bForUndo ? OldColor : NewColor)); break;
			case EParamKind::Texture:
			{
				const UTexture* T = bForUndo ? OldTexture.Get() : NewTexture.Get();
				D->SetStringField(TEXT("value"), T ? T->GetPathName() : TEXT(""));
				break;
			}
			}
			return D;
		}

	private:
		bool ReadValue(const UMaterialInterface* Material, float& OutScalar, FLinearColor& OutColor, TStrongObjectPtr<UTexture>& OutTexture) const
		{
			if (!Material)
			{
				return false;
			}
			const FHashedMaterialParameterInfo Info(Param);
			switch (Kind)
			{
			case EParamKind::Scalar: return Material->GetScalarParameterValue(Info, OutScalar);
			case EParamKind::Vector: return Material->GetVectorParameterValue(Info, OutColor);
			case EParamKind::Texture:
			{
				UTexture* T = nullptr;
				const bool bFound = Material->GetTextureParameterValue(Info, T);
				OutTexture.Reset(T);
				return bFound;
			}
			}
			return false;
		}

		void Apply(float Scalar, const FLinearColor& Color, UTexture* Texture)
		{
			UMaterialInstanceDynamic* M = MID.Get();
			switch (Kind)
			{
			case EParamKind::Scalar: M->SetScalarParameterValue(Param, Scalar); break;
			case EParamKind::Vector: M->SetVectorParameterValue(Param, Color); break;
			case EParamKind::Texture: M->SetTextureParameterValue(Param, Texture); break;
			}
		}

		TWeakObjectPtr<UPrimitiveComponent> Comp;
		int32 Slot;
		FName Param;
		EParamKind Kind;
		FString TargetRef;
		bool bCreatedMID = false;
		TStrongObjectPtr<UMaterialInstanceDynamic> MID;
		TStrongObjectPtr<UMaterialInterface> Original;
		float OldScalar = 0.f;
		FLinearColor OldColor = FLinearColor::White;
		TStrongObjectPtr<UTexture> OldTexture;
	};

	/** Shared body of set_scalar / set_vector / set_texture. */
	FRuntimeMaterialParameter SetParam(UPrimitiveComponent* Target, const FString& Name, int32 Slot, EParamKind Kind, TFunctionRef<bool(FMaterialParamCommand&)> Fill)
	{
		FRuntimeMaterialParameter Out;
		UPrimitiveComponent* Prim = PickPrimitive(Target);
		if (!Prim || !CheckSlot(Prim, Slot))
		{
			return Out;
		}
		UMaterialInterface* Material = Prim->GetMaterial(Slot);
		EParamKind FoundKind;
		FString Exact;
		if (!FindParameter(Material, Name, FoundKind, Exact))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no %s parameter '%s' on %s (has: %s)"), KindName(Kind), *Name,
				Material ? *Material->GetName() : TEXT("an empty slot"), *AllParameters(Material)), TEXT("See materials.list_parameters"));
			return Out;
		}
		if (FoundKind != Kind)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is a %s parameter, not a %s one"), *Exact, KindName(FoundKind), KindName(Kind)),
				FString::Printf(TEXT("Use %s"), SetterFor(FoundKind)));
			return Out;
		}
		TUniquePtr<FMaterialParamCommand> Cmd = MakeUnique<FMaterialParamCommand>(Prim, Slot, FName(*Exact), Kind, Ref(Prim));
		if (!Fill(*Cmd) || !RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return Out;
		}
		Out.Target = Ref(Prim);
		Out.Slot = Slot;
		Out.Material = Prim->GetMaterial(Slot) ? Prim->GetMaterial(Slot)->GetPathName() : FString();
		ReadInto(Prim->GetMaterial(Slot), Kind, Exact, Out);
		return Out;
	}

	UMaterialParameterCollectionInstance* CollectionInstance(const FString& Path, UMaterialParameterCollection*& OutCollection)
	{
		OutCollection = LoadAllowed<UMaterialParameterCollection>(Path, TEXT("collection"), TEXT("/Game/Materials/MPC_Global.MPC_Global"));
		if (!OutCollection)
		{
			return nullptr;
		}
		UMaterialParameterCollectionInstance* Instance = RuntimeToolCall::GetWorld()->GetParameterCollectionInstance(OutCollection);
		if (!Instance)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("collection has no instance in this world"));
		}
		return Instance;
	}

	TArray<FString> CollectionNames(const UMaterialParameterCollection* C, bool bVectors)
	{
		TArray<FString> Out;
		if (bVectors)
		{
			for (const FCollectionVectorParameter& P : C->VectorParameters)
			{
				Out.Add(P.ParameterName.ToString());
			}
		}
		else
		{
			for (const FCollectionScalarParameter& P : C->ScalarParameters)
			{
				Out.Add(P.ParameterName.ToString());
			}
		}
		Out.Sort();
		return Out;
	}
}

FRuntimeMaterialParameters URuntimeMaterialsToolset::ListParameters(UPrimitiveComponent* Target, int32 Slot, int32 Limit)
{
	using namespace RtMat;
	FRuntimeMaterialParameters Out;
	UPrimitiveComponent* Prim = PickPrimitive(Target);
	if (!Prim || !CheckSlot(Prim, Slot))
	{
		return Out;
	}
	Limit = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	UMaterialInterface* Material = Prim->GetMaterial(Slot);
	Out.Component = Ref(Prim);
	for (int32 i = 0; i < Prim->GetNumMaterials(); ++i)
	{
		const UMaterialInterface* M = Prim->GetMaterial(i);
		Out.Slots.Add(M ? M->GetPathName() : FString());
	}
	Out.Slot = Slot;
	Out.Material = Material ? Material->GetPathName() : FString();
	Out.bDynamic = Cast<UMaterialInstanceDynamic>(Material) != nullptr;
	if (!Material)
	{
		return Out;
	}
	for (EParamKind Kind : {EParamKind::Scalar, EParamKind::Vector, EParamKind::Texture})
	{
		int32 Count = 0;
		for (const FString& Name : ParameterNames(Material, Kind))
		{
			if (Count++ >= Limit)
			{
				break;
			}
			FRuntimeMaterialParameter P;
			ReadInto(Material, Kind, Name, P);
			switch (Kind)
			{
			case EParamKind::Scalar: Out.Scalars.Add(Name, P.Scalar); break;
			case EParamKind::Vector: Out.Vectors.Add(Name, P.Vector); break;
			case EParamKind::Texture: Out.Textures.Add(Name, P.Texture); break;
			}
		}
	}
	return Out;
}

FRuntimeMaterialParameter URuntimeMaterialsToolset::GetParameter(UPrimitiveComponent* Target, const FString& Name, int32 Slot)
{
	using namespace RtMat;
	FRuntimeMaterialParameter Out;
	UPrimitiveComponent* Prim = PickPrimitive(Target);
	if (!Prim || !CheckSlot(Prim, Slot))
	{
		return Out;
	}
	UMaterialInterface* Material = Prim->GetMaterial(Slot);
	EParamKind Kind;
	FString Exact;
	if (!FindParameter(Material, Name, Kind, Exact))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no parameter '%s' on %s (has: %s)"), *Name, Material ? *Material->GetName() : TEXT("an empty slot"),
			*AllParameters(Material)));
		return Out;
	}
	Out.Target = Ref(Prim);
	Out.Slot = Slot;
	Out.Material = Material->GetPathName();
	ReadInto(Material, Kind, Exact, Out);
	return Out;
}

FRuntimeMaterialAssignment URuntimeMaterialsToolset::SetMaterial(UPrimitiveComponent* Target, const FString& Material, int32 Slot, bool bAllSlots)
{
	using namespace RtMat;
	FRuntimeMaterialAssignment Out;
	UPrimitiveComponent* Prim = PickPrimitive(Target);
	if (!Prim)
	{
		return Out;
	}
	UMaterialInterface* NewMaterial = LoadAllowed<UMaterialInterface>(Material, TEXT("material"), TEXT("/Game/Materials/M_Stone.M_Stone"));
	if (!NewMaterial)
	{
		return Out;
	}
	TArray<int32> Slots;
	if (bAllSlots)
	{
		for (int32 i = 0; i < Prim->GetNumMaterials(); ++i)
		{
			Slots.Add(i);
		}
	}
	else
	{
		if (!CheckSlot(Prim, Slot))
		{
			return Out;
		}
		Slots.Add(Slot);
	}
	TWeakObjectPtr<UPrimitiveComponent> Comp = Prim;
	TArray<TSharedPtr<TStrongObjectPtr<UMaterialInterface>>> Old;
	for (int32 S : Slots)
	{
		Old.Add(MakeShared<TStrongObjectPtr<UMaterialInterface>>(Prim->GetMaterial(S)));
	}
	TSharedPtr<TStrongObjectPtr<UMaterialInterface>> New = MakeShared<TStrongObjectPtr<UMaterialInterface>>(NewMaterial);
	const FString TargetRef = Ref(Prim);
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("target"), TargetRef);
	Delta->SetStringField(TEXT("material"), NewMaterial->GetPathName());
	Delta->SetNumberField(TEXT("slots"), Slots.Num());

	auto DoFn = [Comp, Slots, New](FRuntimeCommandContext&, FString& Err)
	{
		if (!Comp.IsValid())
		{
			Err = TEXT("component no longer exists");
			return false;
		}
		for (int32 S : Slots)
		{
			Comp->SetMaterial(S, New->Get());
		}
		return true;
	};
	auto UndoFn = [Comp, Slots, Old](FRuntimeCommandContext&)
	{
		if (!Comp.IsValid())
		{
			return false;
		}
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			Comp->SetMaterial(Slots[i], Old[i]->Get());
		}
		return true;
	};
	if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("set material on %s"), *TargetRef), DoFn, UndoFn, Delta)))
	{
		Out.Target = TargetRef;
		Out.Material = NewMaterial->GetPathName();
		Out.Slots = Slots;
	}
	return Out;
}

FRuntimeMaterialParameter URuntimeMaterialsToolset::SetScalar(UPrimitiveComponent* Target, const FString& Name, float Value, int32 Slot)
{
	return RtMat::SetParam(Target, Name, Slot, ERuntimeMaterialParamType::Scalar, [Value](RtMat::FMaterialParamCommand& Cmd)
	{
		Cmd.NewScalar = Value;
		return true;
	});
}

FRuntimeMaterialParameter URuntimeMaterialsToolset::SetVector(UPrimitiveComponent* Target, const FString& Name, const FLinearColor& Value, int32 Slot)
{
	return RtMat::SetParam(Target, Name, Slot, ERuntimeMaterialParamType::Vector, [&Value](RtMat::FMaterialParamCommand& Cmd)
	{
		Cmd.NewColor = Value;
		return true;
	});
}

FRuntimeMaterialParameter URuntimeMaterialsToolset::SetTexture(UPrimitiveComponent* Target, const FString& Name, const FString& Texture, int32 Slot)
{
	return RtMat::SetParam(Target, Name, Slot, ERuntimeMaterialParamType::Texture, [&Texture](RtMat::FMaterialParamCommand& Cmd)
	{
		UTexture* Loaded = nullptr;
		if (!Texture.IsEmpty())
		{
			Loaded = PresentationTool::LoadAllowed<UTexture>(Texture, TEXT("texture"), TEXT("/Game/Textures/T_Brick.T_Brick"));
			if (!Loaded)
			{
				return false;
			}
		}
		Cmd.NewTexture.Reset(Loaded);
		return true;
	});
}

FRuntimeCollectionParameters URuntimeMaterialsToolset::ListCollectionParameters(const FString& Collection)
{
	using namespace RtMat;
	FRuntimeCollectionParameters Out;
	UMaterialParameterCollection* C = nullptr;
	UMaterialParameterCollectionInstance* Instance = CollectionInstance(Collection, C);
	if (!Instance)
	{
		return Out;
	}
	Out.Collection = C->GetPathName();
	for (const FString& N : CollectionNames(C, false))
	{
		float V = 0.f;
		Instance->GetScalarParameterValue(FName(*N), V);
		Out.Scalars.Add(N, V);
	}
	for (const FString& N : CollectionNames(C, true))
	{
		FLinearColor V = FLinearColor::Black;
		Instance->GetVectorParameterValue(FName(*N), V);
		Out.Vectors.Add(N, V);
	}
	return Out;
}

FRuntimeCollectionParameter URuntimeMaterialsToolset::SetCollectionParam(const FString& Collection, const FString& Name, const FRuntimeToolJson& Value)
{
	using namespace RtMat;
	FRuntimeCollectionParameter Out;
	UMaterialParameterCollection* C = nullptr;
	UMaterialParameterCollectionInstance* Instance = CollectionInstance(Collection, C);
	if (!Instance)
	{
		return Out;
	}
	const FName ParamName(*Name);
	const bool bIsScalar = CollectionNames(C, false).Contains(Name);
	const bool bIsVector = CollectionNames(C, true).Contains(Name);
	if (!bIsScalar && !bIsVector)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("collection %s has no parameter '%s' (scalars: %s; vectors: %s)"), *C->GetName(), *Name,
			*JoinOrNone(CollectionNames(C, false)), *JoinOrNone(CollectionNames(C, true))), TEXT("See materials.list_collection_parameters"));
		return Out;
	}
	TWeakObjectPtr<UMaterialParameterCollectionInstance> Weak = Instance;
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("collection"), C->GetPathName());
	Delta->SetStringField(TEXT("parameter"), Name);
	if (Value.Value.IsValid())
	{
		Delta->SetField(TEXT("value"), Value.Value);
	}
	Out.Collection = C->GetPathName();
	Out.Name = Name;

	TUniquePtr<FLambdaCommand> Cmd;
	if (bIsScalar)
	{
		double Number = 0.0;
		if (!Value.Value.IsValid() || !Value.Value->TryGetNumber(Number))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is a scalar parameter: value must be a number"), *Name));
			return Out;
		}
		float Old = 0.f;
		Instance->GetScalarParameterValue(ParamName, Old);
		const float New = static_cast<float>(Number);
		Cmd = MakeUnique<FLambdaCommand>(TEXT("set collection scalar"),
			[Weak, ParamName, New](FRuntimeCommandContext&, FString&) { return Weak.IsValid() && Weak->SetScalarParameterValue(ParamName, New); },
			[Weak, ParamName, Old](FRuntimeCommandContext&) { return Weak.IsValid() && Weak->SetScalarParameterValue(ParamName, Old); }, Delta);
		Out.Type = EParamKind::Scalar;
		Out.Scalar = New;
		Out.Previous = FRuntimeToolJson::Make(MakeShared<FJsonValueNumber>(RuntimeJson::Round(Old)));
	}
	else
	{
		FLinearColor New;
		if (!ReadColor(Value.Value, New))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is a vector parameter: value must be [r,g,b] or [r,g,b,a]"), *Name));
			return Out;
		}
		FLinearColor Old;
		Instance->GetVectorParameterValue(ParamName, Old);
		Cmd = MakeUnique<FLambdaCommand>(TEXT("set collection vector"),
			[Weak, ParamName, New](FRuntimeCommandContext&, FString&) { return Weak.IsValid() && Weak->SetVectorParameterValue(ParamName, New); },
			[Weak, ParamName, Old](FRuntimeCommandContext&) { return Weak.IsValid() && Weak->SetVectorParameterValue(ParamName, Old); }, Delta);
		Out.Type = EParamKind::Vector;
		Out.Vector = New;
		Out.Previous = FRuntimeToolJson::Make(ColorJson(Old));
	}
	RuntimeToolCall::RunCommand(MoveTemp(Cmd));
	return Out;
}
