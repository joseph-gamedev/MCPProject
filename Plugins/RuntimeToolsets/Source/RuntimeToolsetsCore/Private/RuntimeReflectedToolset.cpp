// UFUNCTION toolsets (Epic ToolsetRegistry style) exposed through IRuntimeToolset.
#include "RuntimeToolReflection.h"
#include "RuntimeJson.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolset.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsLog.h"
#include "RuntimeToolsetExtensions.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace RuntimeToolCallPrivate
{
	void PushCurrent(FToolInvocation* Inv);
	void PopCurrent();
}

namespace RuntimeToolReflection
{
	namespace
	{
		TMap<FName, TSharedPtr<FJsonObject>>& CompiledDocs()
		{
			static TMap<FName, TSharedPtr<FJsonObject>> Map;
			return Map;
		}

		TArray<TWeakObjectPtr<UClass>>& RegisteredClasses()
		{
			static TArray<TWeakObjectPtr<UClass>> List;
			return List;
		}

		bool IsInput(const FProperty* P)
		{
			return P->HasAnyPropertyFlags(CPF_Parm) && !P->HasAnyPropertyFlags(CPF_ReturnParm)
				&& (!P->HasAnyPropertyFlags(CPF_OutParm) || P->HasAnyPropertyFlags(CPF_ReferenceParm));
		}

		bool IsOutput(const FProperty* P)
		{
			return P->HasAnyPropertyFlags(CPF_ReturnParm) || (P->HasAnyPropertyFlags(CPF_OutParm) && !P->HasAnyPropertyFlags(CPF_ReferenceParm));
		}

		UClass* AsyncResultClass(const FProperty* P)
		{
			const FObjectProperty* OP = CastField<FObjectProperty>(P);
			return OP && OP->PropertyClass && OP->PropertyClass->IsChildOf(URuntimeToolAsyncResult::StaticClass()) ? OP->PropertyClass.Get() : nullptr;
		}

#if WITH_METADATA
		/** Splits a UHT ToolTip into the summary, "@param Name - text" entries and the "@return" text. */
		void ParseDoc(const FString& Doc, FString& OutSummary, TMap<FString, FString>& OutParams, FString& OutReturn)
		{
			TArray<FString> Lines;
			Doc.ParseIntoArrayLines(Lines, false);
			FString* Target = &OutSummary;
			for (FString Line : Lines)
			{
				Line.TrimStartAndEndInline();
				if (Line.StartsWith(TEXT("@param")))
				{
					FString Rest = Line.Mid(6).TrimStart();
					FString Name;
					if (!Rest.Split(TEXT(" "), &Name, &Rest))
					{
						Name = Rest;
						Rest.Empty();
					}
					Rest.TrimStartInline();
					Rest.RemoveFromStart(TEXT("-"));
					Rest.RemoveFromStart(TEXT(":"));
					OutParams.Add(Name, Rest.TrimStart());
					Target = &OutParams[Name];
				}
				else if (Line.StartsWith(TEXT("@return")))
				{
					OutReturn = Line.Mid(7).TrimStart();
					OutReturn.RemoveFromStart(TEXT("-"));
					OutReturn.TrimStartInline();
					Target = &OutReturn;
				}
				else if (Line.StartsWith(TEXT("@")))
				{
					Target = nullptr; // @note, @see ...: not part of the agent-facing text
				}
				else if (Target)
				{
					if (Line.IsEmpty())
					{
						if (!Target->IsEmpty() && !Target->EndsWith(TEXT("\n")))
						{
							*Target += TEXT("\n");
						}
					}
					else
					{
						*Target += (Target->IsEmpty() || Target->EndsWith(TEXT("\n")) ? TEXT("") : TEXT(" ")) + Line;
					}
				}
			}
			OutSummary.TrimStartAndEndInline();
			OutReturn.TrimStartAndEndInline();
		}

		/** Default value of a parameter from UHT's CPP_Default_ metadata, as JSON (null if it cannot be expressed). */
		TSharedPtr<FJsonValue> DefaultValue(const UFunction* Fn, const FProperty* P)
		{
			const FString Key = TEXT("CPP_Default_") + P->GetName();
			if (!Fn->HasMetaData(*Key))
			{
				return nullptr;
			}
			const FString Text = Fn->GetMetaData(*Key);
			if (CastField<FObjectPropertyBase>(P))
			{
				return MakeShared<FJsonValueNull>(); // nullptr default
			}
			void* Temp = FMemory::Malloc(P->GetSize(), P->GetMinAlignment());
			P->InitializeValue(Temp);
			TSharedPtr<FJsonValue> Out = MakeShared<FJsonValueNull>();
			if (const FStructProperty* SP = CastField<FStructProperty>(P); SP && SP->Struct == TBaseStructure<FVector>::Get())
			{
				TArray<FString> Parts;
				Text.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() == 3)
				{
					*static_cast<FVector*>(Temp) = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
					Out = PropertyToJson(P, Temp);
				}
			}
			else if (Text.IsEmpty() && (CastField<FStrProperty>(P) || CastField<FNameProperty>(P) || CastField<FTextProperty>(P)))
			{
				Out = MakeShared<FJsonValueString>(FString());
			}
			else if (P->ImportText_Direct(*Text, Temp, nullptr, PPF_None))
			{
				Out = PropertyToJson(P, Temp);
			}
			else if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
			{
				Out = MakeShared<FJsonValueString>(Text.Contains(TEXT("::")) ? Text.RightChop(Text.Find(TEXT("::")) + 2) : Text);
			}
			else if (const FByteProperty* BP = CastField<FByteProperty>(P); BP && BP->Enum)
			{
				Out = MakeShared<FJsonValueString>(Text.Contains(TEXT("::")) ? Text.RightChop(Text.Find(TEXT("::")) + 2) : Text);
			}
			P->DestroyValue(Temp);
			FMemory::Free(Temp);
			return Out;
		}

		TSharedPtr<FJsonObject> ToolDocs(UClass* Class, UFunction* Fn)
		{
			FString Summary, Return;
			TMap<FString, FString> ParamDocs;
			ParseDoc(Fn->GetMetaData(TEXT("ToolTip")), Summary, ParamDocs, Return);

			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			TArray<FString> Required;
			TSharedRef<FJsonObject> OutProps = MakeShared<FJsonObject>();
			TArray<FString> OutRequired;
			bool bAsync = false;
			for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FString Name = JsonName(*It);
				if (IsInput(*It))
				{
					TSharedRef<FJsonObject> S = PropertySchema(*It);
					if (const FString* D = ParamDocs.Find(It->GetName()))
					{
						FString Existing;
						S->TryGetStringField(TEXT("description"), Existing);
						S->SetStringField(TEXT("description"), Existing.IsEmpty() || D->Contains(Existing) ? *D : *D + TEXT(" (") + Existing + TEXT(")"));
					}
					TSharedPtr<FJsonValue> Def = DefaultValue(Fn, *It);
					// AutoCreateRefTerm (the Blueprint idiom for optional struct/array refs) marks a parameter optional.
					TArray<FString> AutoRefs;
					Fn->GetMetaData(TEXT("AutoCreateRefTerm")).ParseIntoArray(AutoRefs, TEXT(","));
					for (FString& R : AutoRefs)
					{
						if (R.TrimStartAndEnd() == It->GetName() && !Def.IsValid())
						{
							Def = MakeShared<FJsonValueNull>();
						}
					}
					if (Def.IsValid())
					{
						if (!Def->IsNull())
						{
							S->SetField(TEXT("default"), Def);
						}
					}
					else
					{
						Required.Add(Name);
					}
					Props->SetObjectField(Name, S);
				}
				else if (IsOutput(*It))
				{
					const bool bReturn = It->HasAnyPropertyFlags(CPF_ReturnParm);
					if (bReturn && AsyncResultClass(*It))
					{
						bAsync = true;
						if (const FProperty* ValueProp = AsyncResultClass(*It)->FindPropertyByName(TEXT("Value")))
						{
							OutProps->SetObjectField(TEXT("returnValue"), PropertySchema(ValueProp, true));
							OutRequired.Add(TEXT("returnValue"));
						}
						continue;
					}
					TSharedRef<FJsonObject> S = PropertySchema(*It, true);
					if (bReturn && !Return.IsEmpty())
					{
						S->SetStringField(TEXT("description"), Return);
					}
					else if (const FString* D = ParamDocs.Find(It->GetName()))
					{
						S->SetStringField(TEXT("description"), *D);
					}
					const FString OutName = bReturn ? FString(TEXT("returnValue")) : Name;
					OutProps->SetObjectField(OutName, S);
					OutRequired.Add(OutName);
				}
			}
			TSharedRef<FJsonObject> Input = MakeShared<FJsonObject>();
			Input->SetStringField(TEXT("type"), TEXT("object"));
			Input->SetObjectField(TEXT("properties"), Props);
			if (Required.Num() > 0)
			{
				Input->SetField(TEXT("required"), RuntimeJson::Strings(Required));
			}
			Input->SetBoolField(TEXT("additionalProperties"), false);
			TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetStringField(TEXT("type"), TEXT("object"));
			Output->SetObjectField(TEXT("properties"), OutProps);
			if (OutRequired.Num() > 0)
			{
				Output->SetField(TEXT("required"), RuntimeJson::Strings(OutRequired));
			}

			const bool bUndoable = Fn->HasMetaData(TEXT("Undoable"));
			const bool bIrreversible = Fn->HasMetaData(TEXT("Irreversible"));
			FString Description = Summary;
			if (!Return.IsEmpty())
			{
				Description += TEXT("\nReturns: ") + Return;
			}
			if (bIrreversible)
			{
				// Epic's wording for tools with lasting effects.
				Description += TEXT("\nThis cannot be undone: only call it after explicit direction or permission from the user.");
			}
			TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("name"), ToSnakeCase(Fn->GetName()));
			T->SetStringField(TEXT("function"), Fn->GetName());
			T->SetStringField(TEXT("description"), Description);
			T->SetObjectField(TEXT("input_schema"), Input);
			T->SetObjectField(TEXT("output_schema"), Output);
			T->SetBoolField(TEXT("undoable"), bUndoable);
			T->SetBoolField(TEXT("irreversible"), bIrreversible);
			T->SetBoolField(TEXT("requires_world"), Fn->HasMetaData(TEXT("RequiresWorld")));
			T->SetBoolField(TEXT("async"), bAsync);
			return T;
		}
#endif

		/** Runs a reflected tool: JSON args -> parameters, ProcessEvent, result -> {"returnValue": ...}. */
		void Invoke(FToolInvocation& Inv, UFunction* Fn)
		{
			UObject* CDO = Fn->GetOuterUClass()->GetDefaultObject();
			uint8* Parms = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Fn->ParmsSize, 1), 16));
			FMemory::Memzero(Parms, Fn->ParmsSize);
			for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->InitializeValue_InContainer(Parms);
			}
			ON_SCOPE_EXIT
			{
				for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					It->DestroyValue_InContainer(Parms);
				}
				FMemory::Free(Parms);
			};

			FConvertContext Ctx;
			Ctx.World = Inv.GetWorld();
			Ctx.Graph = Inv.GetGraph();
			Ctx.Outer = GetTransientPackage();
			for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (!IsInput(*It))
				{
					continue;
				}
				const FString Name = JsonName(*It);
				const TSharedPtr<FJsonValue> V = Inv.GetArgs()->TryGetField(Name);
				if (!V.IsValid() || V->IsNull())
				{
					continue;
				}
				const bool bReference = CastField<FObjectPropertyBase>(*It) != nullptr;
				FString RefText;
				if (bReference && V->TryGetString(RefText) && RefText.IsEmpty())
				{
					continue; // "" = no object (e.g. "parent": "" moves to the root)
				}
				FString Err;
				if (!JsonToProperty(V, *It, It->ContainerPtrToValuePtr<void>(Parms), Ctx, Err))
				{
					// A reference that does not resolve is a missing object, not a malformed argument.
					// Also arrays/sets of references: their element errors look like "[0]: no Actor 'n9'".
					const FArrayProperty* AP = CastField<FArrayProperty>(*It);
					const bool bRefArray = AP && CastField<FObjectPropertyBase>(AP->Inner) && !CastField<FClassProperty>(AP->Inner);
					const bool bMissing = (bReference && !CastField<FClassProperty>(*It)) || bRefArray || Err.StartsWith(TEXT("no "));
					Inv.Fail(bMissing ? ERuntimeToolError::NotFound : ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("$.%s: %s"), *Name, *Err));
					return;
				}
			}

			RuntimeToolCallPrivate::PushCurrent(&Inv);
			CDO->ProcessEvent(Fn, Parms);
			RuntimeToolCallPrivate::PopCurrent();

			if (Inv.IsComplete())
			{
				return; // the tool failed (RuntimeToolCall::Fail)
			}
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (!IsOutput(*It))
				{
					continue;
				}
				const void* Ptr = It->ContainerPtrToValuePtr<void>(Parms);
				if (It->HasAnyPropertyFlags(CPF_ReturnParm) && AsyncResultClass(*It))
				{
					URuntimeToolAsyncResult* R = Cast<URuntimeToolAsyncResult>(CastField<FObjectProperty>(*It)->GetObjectPropertyValue(Ptr));
					if (!R)
					{
						Inv.Fail(ERuntimeToolError::Internal, TEXT("the tool returned no async result"));
						return;
					}
					Inv.Defer();
					R->Bind(Inv.AsShared());
					return;
				}
				Out->SetField(It->HasAnyPropertyFlags(CPF_ReturnParm) ? FString(TEXT("returnValue")) : JsonName(*It), PropertyToJson(*It, Ptr, Ctx.Graph));
			}
			Inv.Succeed(Out);
		}

		class FReflectedToolset final : public IRuntimeToolset
		{
		public:
			explicit FReflectedToolset(UClass* InClass) : Class(InClass)
			{
				const URuntimeToolsetDefinition* CDO = GetDefault<URuntimeToolsetDefinition>(InClass);
				Name = CDO->GetToolsetName();
				Version = CDO->GetToolsetVersion();
			}

			virtual FName GetName() const override { return Name; }
			virtual FString GetVersion() const override { return Version; }
			virtual FString GetDescription() const override
			{
				const TSharedPtr<FJsonObject> D = Docs();
				FString Desc;
				if (D.IsValid())
				{
					D->TryGetStringField(TEXT("description"), Desc);
				}
				return Desc;
			}

			virtual void RegisterTools(FRuntimeToolsetBuilder& B) override
			{
				const TSharedPtr<FJsonObject> D = Docs();
				if (!D.IsValid() || !Class.IsValid())
				{
					UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("Toolset %s has no docs (run RuntimeToolsets.UpdateSchemaSnapshots in the editor)"), *Name.ToString());
					return;
				}
				for (const TSharedPtr<FJsonValue>& V : D->GetArrayField(TEXT("tools")))
				{
					const TSharedPtr<FJsonObject> T = V->AsObject();
					UFunction* Fn = Class->FindFunctionByName(FName(*T->GetStringField(TEXT("function"))));
					if (!Fn)
					{
						UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("%s: function %s not found"), *Name.ToString(), *T->GetStringField(TEXT("function")));
						continue;
					}
					FRuntimeToolBuilder Tool = B.Tool(T->GetStringField(TEXT("name")), T->GetStringField(TEXT("description")));
					Tool.ArgsJson(T->GetObjectField(TEXT("input_schema")).ToSharedRef());
					Tool.Returns(T->GetObjectField(TEXT("output_schema")).ToSharedRef());
					if (T->GetBoolField(TEXT("undoable")))
					{
						Tool.Undoable();
					}
					if (T->GetBoolField(TEXT("irreversible")))
					{
						Tool.Irreversible();
					}
					if (T->GetBoolField(TEXT("requires_world")))
					{
						Tool.RequiresWorld();
					}
					if (T->GetBoolField(TEXT("async")))
					{
						Tool.Async();
					}
					TWeakObjectPtr<UFunction> WeakFn = Fn;
					Tool.Handle([WeakFn](FToolInvocation& Inv)
					{
						if (UFunction* F = WeakFn.Get())
						{
							Invoke(Inv, F);
						}
						else
						{
							Inv.Fail(ERuntimeToolError::Internal, TEXT("tool function was unloaded"));
						}
					});
				}
			}

		private:
			TSharedPtr<FJsonObject> Docs() const
			{
#if WITH_METADATA
				if (Class.IsValid())
				{
					return GenerateDocs(Class.Get()); // fresh from the doc comments
				}
#endif
				return FindCompiledDocs(Name);
			}

			TWeakObjectPtr<UClass> Class;
			FName Name;
			FString Version;
		};
	}

	TSharedPtr<FJsonObject> GenerateDocs(UClass* Class)
	{
#if WITH_METADATA
		if (!Class || !Class->IsChildOf(URuntimeToolsetDefinition::StaticClass()))
		{
			return nullptr;
		}
		const URuntimeToolsetDefinition* CDO = GetDefault<URuntimeToolsetDefinition>(Class);
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("name"), CDO->GetToolsetName().ToString());
		D->SetStringField(TEXT("version"), CDO->GetToolsetVersion());
		FString ClassDoc = Class->GetMetaData(TEXT("ToolTip"));
		ClassDoc.TrimStartAndEndInline();
		D->SetStringField(TEXT("description"), ClassDoc);
		TArray<TSharedPtr<FJsonValue>> Tools;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->HasAnyFunctionFlags(FUNC_Static) && It->HasMetaData(TEXT("AICallable")) && !It->HasMetaData(TEXT("AIIgnore")))
			{
				Tools.Add(MakeShared<FJsonValueObject>(ToolDocs(Class, *It).ToSharedRef()));
			}
		}
		// Declaration order is not guaranteed by the iterator: keep a stable order.
		Tools.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			return A->AsObject()->GetStringField(TEXT("name")) < B->AsObject()->GetStringField(TEXT("name"));
		});
		D->SetArrayField(TEXT("tools"), Tools);
		return D;
#else
		return nullptr;
#endif
	}

	void RegisterCompiledDocs(const TCHAR* const* Chunks, int32 NumChunks)
	{
		FString Text;
		for (int32 i = 0; i < NumChunks; ++i)
		{
			Text += Chunks[i];
		}
		const TSharedPtr<FJsonObject> J = RuntimeJson::Parse(Text);
		if (!J.IsValid())
		{
			UE_LOG(LogRuntimeToolsetsCore, Error, TEXT("Compiled tool docs are not valid JSON"));
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : J->Values)
		{
			CompiledDocs().Add(FName(*P.Key), P.Value->AsObject());
		}
	}

	TSharedPtr<FJsonObject> FindCompiledDocs(FName Toolset)
	{
		const TSharedPtr<FJsonObject>* D = CompiledDocs().Find(Toolset);
		return D ? *D : nullptr;
	}

	FString ModuleForToolset(FName Toolset)
	{
		for (const TWeakObjectPtr<UClass>& C : RegisteredClasses())
		{
			if (C.IsValid() && GetDefault<URuntimeToolsetDefinition>(C.Get())->GetToolsetName() == Toolset)
			{
				FString Module = C->GetOutermost()->GetName();
				Module.RemoveFromStart(TEXT("/Script/"));
				return Module;
			}
		}
		return FString();
	}

	TSharedRef<IRuntimeToolset> MakeToolset(UClass* ToolsetClass)
	{
		check(ToolsetClass && ToolsetClass->IsChildOf(URuntimeToolsetDefinition::StaticClass()));
		RegisteredClasses().AddUnique(ToolsetClass);
		return MakeShared<FReflectedToolset>(ToolsetClass);
	}

	TSharedRef<IRuntimeToolset> RegisterToolsetClass(UClass* ToolsetClass)
	{
		TSharedRef<IRuntimeToolset> T = MakeToolset(ToolsetClass);
		IRuntimeToolsetsCore::Get().RegisterToolset(T);
		return T;
	}

	int32 WriteCompiledDocs(FString& OutReport, bool bDryRun)
	{
#if WITH_METADATA
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RuntimeToolsets"));
		if (!Plugin.IsValid())
		{
			OutReport = TEXT("plugin not found");
			return 0;
		}
		// Module name -> toolset name -> docs
		TMap<FString, TSharedRef<FJsonObject>> PerModule;
		for (const TWeakObjectPtr<UClass>& C : RegisteredClasses())
		{
			if (!C.IsValid())
			{
				continue;
			}
			FString Module = C->GetOutermost()->GetName();
			Module.RemoveFromStart(TEXT("/Script/"));
			const TSharedPtr<FJsonObject> Docs = GenerateDocs(C.Get());
			if (Docs.IsValid())
			{
				TSharedRef<FJsonObject>& M = PerModule.FindOrAdd(Module, MakeShared<FJsonObject>());
				M->SetObjectField(Docs->GetStringField(TEXT("name")), Docs);
			}
		}
		int32 Written = 0;
		for (const TPair<FString, TSharedRef<FJsonObject>>& P : PerModule)
		{
			const FString Json = RuntimeJson::Write(P.Value);
			FString Content = TEXT("// Generated from the toolset doc comments by RuntimeToolsets.UpdateSchemaSnapshots. Do not edit.\n");
			Content += TEXT("// Packaged builds have no UFUNCTION metadata; these docs replace it.\n");
			Content += TEXT("static const TCHAR* const GRuntimeToolDocs[] = {\n");
			constexpr int32 Chunk = 8000; // MSVC limits string literals to 16 KB
			for (int32 i = 0; i < Json.Len(); i += Chunk)
			{
				Content += TEXT("\tTEXT(R\"RTD(") + Json.Mid(i, Chunk) + TEXT(")RTD\"),\n");
			}
			Content += TEXT("};\n");
			// the docs go next to the module, in whichever plugin holds it (extension plugins too)
			FString PluginDir = RuntimeToolsetExtensions::PluginDirForModule(P.Key);
			if (PluginDir.IsEmpty())
			{
				PluginDir = Plugin->GetBaseDir();
			}
			const FString Path = FPaths::Combine(PluginDir, TEXT("Source"), P.Key, TEXT("Private"), TEXT("Generated"), TEXT("ToolDocs.gen.inl"));
			FString Existing;
			FFileHelper::LoadFileToString(Existing, *Path);
			Existing.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
			if (Existing != Content)
			{
				if (bDryRun)
				{
					OutReport += Path + TEXT("\n");
					++Written;
					continue;
				}
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
				FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				OutReport += Path + TEXT("\n");
				++Written;
			}
		}
		return Written;
#else
		OutReport = TEXT("no metadata in this build");
		return 0;
#endif
	}
}
