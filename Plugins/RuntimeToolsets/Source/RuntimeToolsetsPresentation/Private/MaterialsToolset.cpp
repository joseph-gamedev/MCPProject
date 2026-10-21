#include "MaterialsToolset.h"
#include "RuntimeToolHelpers.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/StrongObjectPtr.h"

using namespace RuntimeTools;

namespace
{
	enum class EParamKind : uint8
	{
		Scalar,
		Vector,
		Texture,
	};

	const TCHAR* KindName(EParamKind Kind)
	{
		return Kind == EParamKind::Scalar ? TEXT("scalar") : Kind == EParamKind::Vector ? TEXT("vector") : TEXT("texture");
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
		return Names;
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

	/** Loads an asset by path, enforcing AllowedObjectRoots. */
	template <typename T>
	T* LoadAllowed(FToolInvocation& Inv, const FString& Path, const TCHAR* What)
	{
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		bool bAllowed = S.AllowedObjectRoots.Num() == 0;
		for (const FString& Root : S.AllowedObjectRoots)
		{
			bAllowed |= Path.StartsWith(Root);
		}
		if (!bAllowed)
		{
			Inv.Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("'%s' is outside AllowedObjectRoots"), *Path));
			return nullptr;
		}
		T* Asset = LoadObject<T>(nullptr, *Path);
		if (!Asset)
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s '%s' not found (is it cooked?)"), What, *Path),
				TEXT("Use the full object path, e.g. /Game/Materials/M_Stone.M_Stone"));
		}
		return Asset;
	}

	/** Resolves "target" to a primitive component that has material slots. */
	UPrimitiveComponent* ResolvePrimitive(FToolInvocation& Inv)
	{
		FResolvedTarget Target;
		if (!Inv.ResolveTarget(TEXT("target"), Target))
		{
			return nullptr;
		}
		if (Target.Component)
		{
			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Target.Component);
			if (!Prim || Prim->GetNumMaterials() == 0)
			{
				Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("component has no material slots"));
				return nullptr;
			}
			return Prim;
		}
		if (Target.Actor)
		{
			if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Target.Actor->GetRootComponent()); Root && Root->GetNumMaterials() > 0)
			{
				return Root;
			}
			TInlineComponentArray<UPrimitiveComponent*> Prims(Target.Actor);
			for (UPrimitiveComponent* P : Prims)
			{
				if (P->GetNumMaterials() > 0)
				{
					return P;
				}
			}
		}
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target has no mesh with material slots"), TEXT("Use target#ComponentName to pick a component"));
		return nullptr;
	}

	bool ReadSlot(FToolInvocation& Inv, UPrimitiveComponent* Prim, int32& OutSlot)
	{
		OutSlot = static_cast<int32>(Inv.GetInt(TEXT("slot"), 0));
		if (OutSlot < 0 || OutSlot >= Prim->GetNumMaterials())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("slot %d out of range (0..%d)"), OutSlot, Prim->GetNumMaterials() - 1));
			return false;
		}
		return true;
	}

	/** Sets one material parameter, creating a dynamic instance on the slot if needed. Undo restores the value (or the original material). */
	class FMaterialParamCommand final : public IRuntimeCommand
	{
	public:
		FMaterialParamCommand(UPrimitiveComponent* InComp, int32 InSlot, FName InParam, EParamKind InKind, const FString& InRef)
			: Comp(InComp), Slot(InSlot), Param(InParam), Kind(InKind), Ref(InRef)
		{
		}

		float NewScalar = 0.f;
		FLinearColor NewColor = FLinearColor::White;
		TStrongObjectPtr<UTexture> NewTexture;

		virtual FString GetLabel() const override { return FString::Printf(TEXT("set %s param %s on %s"), KindName(Kind), *Param.ToString(), *Ref); }

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
				TArray<FString> Names = ParameterNames(Current, Kind);
				OutError = FString::Printf(TEXT("material '%s' has no %s parameter '%s' (has: %s)"), Current ? *Current->GetName() : TEXT("none"), KindName(Kind),
					*Param.ToString(), Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none"));
				return false;
			}
			UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Current);
			if (Existing)
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
			D->SetStringField(TEXT("target"), Ref);
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
		FString Ref;
		bool bCreatedMID = false;
		TStrongObjectPtr<UMaterialInstanceDynamic> MID;
		TStrongObjectPtr<UMaterialInterface> Original;
		float OldScalar = 0.f;
		FLinearColor OldColor = FLinearColor::White;
		TStrongObjectPtr<UTexture> OldTexture;
	};
}

static void ListParameters(FToolInvocation& Inv)
{
	UPrimitiveComponent* Prim = ResolvePrimitive(Inv);
	int32 Slot = 0;
	if (!Prim || !ReadSlot(Inv, Prim, Slot))
	{
		return;
	}
	const int32 Limit = Inv.GetLimit();
	UMaterialInterface* Material = Prim->GetMaterial(Slot);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("component"), Prim->GetName());
	TArray<FString> Slots;
	for (int32 i = 0; i < Prim->GetNumMaterials(); ++i)
	{
		const UMaterialInterface* M = Prim->GetMaterial(i);
		Slots.Add(M ? M->GetPathName() : TEXT(""));
	}
	Out->SetField(TEXT("slots"), RuntimeJson::Strings(Slots));
	Out->SetNumberField(TEXT("slot"), Slot);
	Out->SetBoolField(TEXT("dynamic"), Cast<UMaterialInstanceDynamic>(Material) != nullptr);

	auto AddKind = [&](EParamKind Kind, const TCHAR* Field)
	{
		TSharedRef<FJsonObject> Params = Obj();
		int32 Count = 0;
		for (const FString& Name : ParameterNames(Material, Kind))
		{
			if (Count++ >= Limit)
			{
				break;
			}
			const FHashedMaterialParameterInfo Info{FName(*Name)};
			if (Kind == EParamKind::Scalar)
			{
				float V = 0.f;
				Material->GetScalarParameterValue(Info, V);
				Params->SetNumberField(Name, RuntimeJson::Round(V));
			}
			else if (Kind == EParamKind::Vector)
			{
				FLinearColor V;
				Material->GetVectorParameterValue(Info, V);
				Params->SetField(Name, ColorJson(V));
			}
			else
			{
				UTexture* T = nullptr;
				Material->GetTextureParameterValue(Info, T);
				Params->SetStringField(Name, T ? T->GetPathName() : TEXT(""));
			}
		}
		Out->SetObjectField(Field, Params);
	};
	AddKind(EParamKind::Scalar, TEXT("scalars"));
	AddKind(EParamKind::Vector, TEXT("vectors"));
	AddKind(EParamKind::Texture, TEXT("textures"));
	Inv.Succeed(Out);
}

static void SetMaterial(FToolInvocation& Inv)
{
	UPrimitiveComponent* Prim = ResolvePrimitive(Inv);
	if (!Prim)
	{
		return;
	}
	UMaterialInterface* Material = LoadAllowed<UMaterialInterface>(Inv, Inv.GetString(TEXT("material")), TEXT("material"));
	if (!Material)
	{
		return;
	}
	TArray<int32> Slots;
	if (Inv.GetBool(TEXT("all_slots"), false))
	{
		for (int32 i = 0; i < Prim->GetNumMaterials(); ++i)
		{
			Slots.Add(i);
		}
	}
	else
	{
		int32 Slot = 0;
		if (!ReadSlot(Inv, Prim, Slot))
		{
			return;
		}
		Slots.Add(Slot);
	}
	TWeakObjectPtr<UPrimitiveComponent> Comp = Prim;
	TArray<TSharedPtr<TStrongObjectPtr<UMaterialInterface>>> Old;
	for (int32 Slot : Slots)
	{
		Old.Add(MakeShared<TStrongObjectPtr<UMaterialInterface>>(Prim->GetMaterial(Slot)));
	}
	TSharedPtr<TStrongObjectPtr<UMaterialInterface>> New = MakeShared<TStrongObjectPtr<UMaterialInterface>>(Material);
	const FString TargetRef = Ref(Inv, Prim);
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), TargetRef);
	Delta->SetStringField(TEXT("material"), Material->GetPathName());
	Delta->SetNumberField(TEXT("slots"), Slots.Num());

	auto DoFn = [Comp, Slots, New](FRuntimeCommandContext&, FString& Error)
	{
		if (!Comp.IsValid())
		{
			Error = TEXT("component no longer exists");
			return false;
		}
		for (int32 Slot : Slots)
		{
			Comp->SetMaterial(Slot, New->Get());
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
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("set material on %s"), *TargetRef), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetParam(FToolInvocation& Inv, EParamKind Kind)
{
	UPrimitiveComponent* Prim = ResolvePrimitive(Inv);
	int32 Slot = 0;
	if (!Prim || !ReadSlot(Inv, Prim, Slot))
	{
		return;
	}
	const FString ParamName = Inv.GetString(TEXT("name"));
	const TArray<FString> Available = ParameterNames(Prim->GetMaterial(Slot), Kind);
	if (!Available.ContainsByPredicate([&ParamName](const FString& N) { return N.Equals(ParamName, ESearchCase::IgnoreCase); }))
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no %s parameter '%s' (has: %s)"), KindName(Kind), *ParamName,
			Available.Num() ? *FString::Join(Available, TEXT(", ")) : TEXT("none")), TEXT("See materials.list_parameters"));
		return;
	}
	const FString* Exact = Available.FindByPredicate([&ParamName](const FString& N) { return N.Equals(ParamName, ESearchCase::IgnoreCase); });
	TUniquePtr<FMaterialParamCommand> Cmd = MakeUnique<FMaterialParamCommand>(Prim, Slot, FName(**Exact), Kind, Ref(Inv, Prim));
	if (Kind == EParamKind::Scalar)
	{
		Cmd->NewScalar = static_cast<float>(Inv.GetNumber(TEXT("value")));
	}
	else if (Kind == EParamKind::Vector)
	{
		if (!ReadColor(Inv.GetValue(TEXT("value")), Cmd->NewColor))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("value must be [r,g,b] or [r,g,b,a]"));
			return;
		}
	}
	else
	{
		UTexture* Texture = nullptr;
		const FString Path = Inv.GetString(TEXT("texture"));
		if (!Path.IsEmpty())
		{
			Texture = LoadAllowed<UTexture>(Inv, Path, TEXT("texture"));
			if (!Texture)
			{
				return;
			}
		}
		Cmd->NewTexture.Reset(Texture);
	}
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MoveTemp(Cmd), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void SetCollectionParam(FToolInvocation& Inv)
{
	UMaterialParameterCollection* Collection = LoadAllowed<UMaterialParameterCollection>(Inv, Inv.GetString(TEXT("collection")), TEXT("collection"));
	if (!Collection)
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance = Inv.GetWorld()->GetParameterCollectionInstance(Collection);
	if (!Instance)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("collection has no instance in this world"));
		return;
	}
	const FName Name(*Inv.GetString(TEXT("name")));
	const TSharedPtr<FJsonValue> Value = Inv.GetValue(TEXT("value"));
	TWeakObjectPtr<UMaterialParameterCollectionInstance> Weak = Instance;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("collection"), Collection->GetPathName());
	Delta->SetStringField(TEXT("parameter"), Name.ToString());
	Delta->SetField(TEXT("value"), Value);

	TUniquePtr<FLambdaCommand> Cmd;
	if (Value->Type == EJson::Number)
	{
		float Old = 0.f;
		if (!Instance->GetScalarParameterValue(Name, Old))
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("collection has no scalar '%s'"), *Name.ToString()));
			return;
		}
		const float New = static_cast<float>(Value->AsNumber());
		Cmd = MakeUnique<FLambdaCommand>(TEXT("set collection scalar"),
			[Weak, Name, New](FRuntimeCommandContext&, FString& Error) { return Weak.IsValid() && Weak->SetScalarParameterValue(Name, New); },
			[Weak, Name, Old](FRuntimeCommandContext&) { return Weak.IsValid() && Weak->SetScalarParameterValue(Name, Old); }, Delta);
	}
	else
	{
		FLinearColor New;
		if (!ReadColor(Value, New))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("value must be a number or [r,g,b(,a)]"));
			return;
		}
		FLinearColor Old;
		if (!Instance->GetVectorParameterValue(Name, Old))
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("collection has no vector '%s'"), *Name.ToString()));
			return;
		}
		Cmd = MakeUnique<FLambdaCommand>(TEXT("set collection vector"),
			[Weak, Name, New](FRuntimeCommandContext&, FString& Error) { return Weak.IsValid() && Weak->SetVectorParameterValue(Name, New); },
			[Weak, Name, Old](FRuntimeCommandContext&) { return Weak.IsValid() && Weak->SetVectorParameterValue(Name, Old); }, Delta);
	}
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		Inv.Succeed(Delta);
	}
}

void FMaterialsToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Target = []() { return FToolParam::Target(TEXT("Actor or actor#Component with a mesh")).Required(); };
	const auto Slot = []() { return FToolParam::Integer(TEXT("Material slot"), 0, 255).Default(0); };
	const auto Name = []() { return FToolParam::String(TEXT("Parameter name"), 128).Required(); };

	B.Tool(TEXT("list_parameters"), TEXT("Material slots and the scalar/vector/texture parameters (with values) of one slot."))
		.Args(FToolSchema().Add(TEXT("target"), Target()).Add(TEXT("slot"), Slot()).Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&ListParameters);

	B.Tool(TEXT("set_material"), TEXT("Assign a material asset to a slot (or all slots)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), Target())
			.Add(TEXT("material"), FToolParam::String(TEXT("Material path, e.g. /Game/M_Stone.M_Stone"), 512).Required())
			.Add(TEXT("slot"), Slot())
			.Add(TEXT("all_slots"), FToolParam::Boolean(TEXT("Apply to every slot")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle(&SetMaterial);

	B.Tool(TEXT("set_scalar"), TEXT("Set a scalar material parameter (creates a dynamic instance)."))
		.Args(FToolSchema().Add(TEXT("target"), Target()).Add(TEXT("name"), Name()).Add(TEXT("value"), FToolParam::Number(TEXT("Value")).Required()).Add(TEXT("slot"), Slot()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetParam(Inv, EParamKind::Scalar); });

	B.Tool(TEXT("set_vector"), TEXT("Set a vector/color material parameter, e.g. Color [r,g,b(,a)] 0-1."))
		.Args(FToolSchema()
			.Add(TEXT("target"), Target())
			.Add(TEXT("name"), Name())
			.Add(TEXT("value"), FToolParam::Array(TEXT("[r,g,b] or [r,g,b,a]"), FToolParam::Number(FString()), 4).Required())
			.Add(TEXT("slot"), Slot()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetParam(Inv, EParamKind::Vector); });

	B.Tool(TEXT("set_texture"), TEXT("Set a texture material parameter (empty texture clears it)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), Target())
			.Add(TEXT("name"), Name())
			.Add(TEXT("texture"), FToolParam::String(TEXT("Texture path"), 512))
			.Add(TEXT("slot"), Slot()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetParam(Inv, EParamKind::Texture); });

	B.Tool(TEXT("set_collection_param"), TEXT("Set a material parameter collection value (number or [r,g,b(,a)]) for the whole world."))
		.Args(FToolSchema()
			.Add(TEXT("collection"), FToolParam::String(TEXT("Collection path"), 512).Required())
			.Add(TEXT("name"), Name())
			.Add(TEXT("value"), FToolParam::Any(TEXT("Number or [r,g,b(,a)]")).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetCollectionParam);
}
