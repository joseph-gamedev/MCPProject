#include "ConsoleToolset.h"
#include "Helpers/ToolsCommon.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Scalability.h"

using namespace RuntimeTools;

static bool IsCommandAllowed(const FString& Command, FString& OutReason)
{
	const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
	const FString Lower = Command.TrimStartAndEnd().ToLower();
	for (const FString& Prefix : S.ConsoleDenyPrefixes)
	{
		if (!Prefix.IsEmpty() && Lower.StartsWith(Prefix.ToLower()))
		{
			OutReason = FString::Printf(TEXT("commands starting with '%s' are denied"), *Prefix);
			return false;
		}
	}
	if (S.ConsoleAllowPrefixes.Num() > 0)
	{
		for (const FString& Prefix : S.ConsoleAllowPrefixes)
		{
			if (Lower.StartsWith(Prefix.ToLower()))
			{
				return true;
			}
		}
		OutReason = TEXT("command is not in ConsoleAllowPrefixes");
		return false;
	}
	return true;
}

/** Captures output written while a command runs. */
class FCaptureOutputDevice final : public FOutputDevice
{
public:
	FString Text;
	int32 MaxChars = 8000;
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
	{
		if (Text.Len() < MaxChars)
		{
			Text += V;
			Text += TEXT("\n");
		}
	}
};

static void ExecCommand(FToolInvocation& Inv)
{
	const FString Command = Inv.GetString(TEXT("command")).TrimStartAndEnd();
	FString Reason;
	if (!IsCommandAllowed(Command, Reason))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, Reason);
		return;
	}
	if (Command.Contains(TEXT("|")))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, TEXT("command chaining with | is not allowed; send one command per call"));
		return;
	}
	FCaptureOutputDevice Output;
	const bool bHandled = GEngine && GEngine->Exec(Inv.GetWorld(), *Command, Output);
	Inv.RecordIrreversible(FString::Printf(TEXT("exec %s"), *Command));

	TSharedRef<FJsonObject> Out = Obj();
	Out->SetBoolField(TEXT("handled"), bHandled);
	FString Text = Output.Text.TrimEnd();
	if (Text.Len() > 4000)
	{
		Text = Text.Left(4000) + TEXT("...");
		Out->SetBoolField(TEXT("truncated"), true);
	}
	if (!Text.IsEmpty())
	{
		Out->SetStringField(TEXT("output"), Text);
	}
	Out->SetBoolField(TEXT("undoable"), false);
	Inv.Succeed(Out);
}

static TSharedRef<FJsonObject> CVarToJson(const FString& Name, IConsoleVariable* Var, bool bHelp)
{
	TSharedRef<FJsonObject> J = Obj();
	J->SetStringField(TEXT("name"), Name);
	J->SetStringField(TEXT("value"), Var->GetString());
	if (Var->TestFlags(ECVF_ReadOnly))
	{
		J->SetBoolField(TEXT("read_only"), true);
	}
	if (bHelp)
	{
		FString Help = Var->GetHelp();
		if (Help.Len() > 300)
		{
			Help = Help.Left(300) + TEXT("...");
		}
		J->SetStringField(TEXT("help"), Help);
	}
	return J;
}

static void GetCVar(FToolInvocation& Inv)
{
	if (Inv.Has(TEXT("name")))
	{
		const FString Name = Inv.GetString(TEXT("name"));
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!Var)
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no console variable '%s'"), *Name), TEXT("List with prefix, e.g. {\"prefix\":\"r.Shadow\"}"));
			return;
		}
		Inv.Succeed(CVarToJson(Name, Var, true));
		return;
	}
	const FString Prefix = Inv.GetString(TEXT("prefix"));
	if (Prefix.Len() < 2)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give name, or a prefix of at least 2 characters"));
		return;
	}
	const int32 Limit = Inv.GetLimit();
	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda([&](const TCHAR* Name, IConsoleObject* Object)
	{
		IConsoleVariable* Var = Object ? Object->AsVariable() : nullptr;
		if (!Var)
		{
			return;
		}
		++Total;
		if (Items.Num() < Limit)
		{
			Items.Add(MakeShared<FJsonValueObject>(CVarToJson(Name, Var, false)));
		}
	}), *Prefix);
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("cvars"), Items);
	Inv.Succeed(Out);
}

static void SetCVar(FToolInvocation& Inv)
{
	const FString Name = Inv.GetString(TEXT("name"));
	IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!Var)
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no console variable '%s'"), *Name));
		return;
	}
	if (Var->TestFlags(ECVF_ReadOnly))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("%s is read-only"), *Name));
		return;
	}
	FString Reason;
	if (!IsCommandAllowed(Name, Reason))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, Reason);
		return;
	}
	const TSharedPtr<FJsonValue> V = Inv.GetValue(TEXT("value"));
	FString NewValue;
	if (V->Type == EJson::Boolean)
	{
		NewValue = V->AsBool() ? TEXT("1") : TEXT("0");
	}
	else if (V->Type == EJson::Number)
	{
		NewValue = FString::SanitizeFloat(V->AsNumber(), 0);
	}
	else
	{
		NewValue = V->AsString();
	}
	const FString OldValue = Var->GetString();

	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("cvar"), Name);
	auto DoFn = [Name, NewValue, Delta](FRuntimeCommandContext&, FString& Error)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!Var)
		{
			Error = TEXT("cvar vanished");
			return false;
		}
		Var->Set(*NewValue, ECVF_SetByConsole);
		Delta->SetStringField(TEXT("value"), Var->GetString());
		return true;
	};
	auto UndoFn = [Name, OldValue](FRuntimeCommandContext&)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!Var)
		{
			return false;
		}
		Var->Set(*OldValue, ECVF_SetByConsole);
		return true;
	};
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("set %s"), *Name), DoFn, UndoFn, Delta)))
	{
		Delta->SetStringField(TEXT("previous"), OldValue);
		Inv.Succeed(Delta);
	}
}

static TSharedRef<FJsonObject> QualityToJson(const Scalability::FQualityLevels& Q)
{
	TSharedRef<FJsonObject> J = Obj();
	J->SetNumberField(TEXT("resolution_scale"), RuntimeJson::Round(Q.ResolutionQuality));
	J->SetNumberField(TEXT("view_distance"), Q.ViewDistanceQuality);
	J->SetNumberField(TEXT("anti_aliasing"), Q.AntiAliasingQuality);
	J->SetNumberField(TEXT("shadows"), Q.ShadowQuality);
	J->SetNumberField(TEXT("global_illumination"), Q.GlobalIlluminationQuality);
	J->SetNumberField(TEXT("reflections"), Q.ReflectionQuality);
	J->SetNumberField(TEXT("post_process"), Q.PostProcessQuality);
	J->SetNumberField(TEXT("textures"), Q.TextureQuality);
	J->SetNumberField(TEXT("effects"), Q.EffectsQuality);
	J->SetNumberField(TEXT("foliage"), Q.FoliageQuality);
	J->SetNumberField(TEXT("shading"), Q.ShadingQuality);
	J->SetNumberField(TEXT("overall"), Q.GetMinQualityLevel());
	return J;
}

static void SetScalability(FToolInvocation& Inv)
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("no game user settings"));
		return;
	}
	const Scalability::FQualityLevels Old = Scalability::GetQualityLevels();
	Scalability::FQualityLevels New = Old;
	if (Inv.Has(TEXT("overall")))
	{
		New.SetFromSingleQualityLevel(static_cast<int32>(Inv.GetInt(TEXT("overall"))));
	}
	auto Group = [&Inv](const TCHAR* Name, int32& Field)
	{
		if (Inv.Has(Name))
		{
			Field = static_cast<int32>(Inv.GetInt(Name));
		}
	};
	Group(TEXT("view_distance"), New.ViewDistanceQuality);
	Group(TEXT("anti_aliasing"), New.AntiAliasingQuality);
	Group(TEXT("shadows"), New.ShadowQuality);
	Group(TEXT("global_illumination"), New.GlobalIlluminationQuality);
	Group(TEXT("reflections"), New.ReflectionQuality);
	Group(TEXT("post_process"), New.PostProcessQuality);
	Group(TEXT("textures"), New.TextureQuality);
	Group(TEXT("effects"), New.EffectsQuality);
	Group(TEXT("foliage"), New.FoliageQuality);
	Group(TEXT("shading"), New.ShadingQuality);
	if (Inv.Has(TEXT("resolution_scale")))
	{
		New.ResolutionQuality = static_cast<float>(Inv.GetNumber(TEXT("resolution_scale")));
	}

	TSharedRef<FJsonObject> Delta = Obj();
	auto Apply = [](const Scalability::FQualityLevels& Q)
	{
		Scalability::SetQualityLevels(Q);
		if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			S->ScalabilityQuality = Q;
		}
	};
	auto DoFn = [New, Apply, Delta](FRuntimeCommandContext&, FString&)
	{
		Apply(New);
		Delta->SetObjectField(TEXT("scalability"), QualityToJson(Scalability::GetQualityLevels()));
		return true;
	};
	auto UndoFn = [Old, Apply](FRuntimeCommandContext&)
	{
		Apply(Old);
		return true;
	};
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("set scalability"), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void GetUserSettings(FToolInvocation& Inv)
{
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!S)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("no game user settings"));
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	const FIntPoint Res = S->GetScreenResolution();
	Out->SetArrayField(TEXT("resolution"), {MakeShared<FJsonValueNumber>(Res.X), MakeShared<FJsonValueNumber>(Res.Y)});
	const EWindowMode::Type Mode = S->GetFullscreenMode();
	Out->SetStringField(TEXT("window_mode"), Mode == EWindowMode::Fullscreen ? TEXT("fullscreen") : Mode == EWindowMode::WindowedFullscreen ? TEXT("windowed_fullscreen") : TEXT("windowed"));
	Out->SetBoolField(TEXT("vsync"), S->IsVSyncEnabled());
	Out->SetNumberField(TEXT("frame_rate_limit"), S->GetFrameRateLimit());
	Out->SetBoolField(TEXT("dynamic_resolution"), S->IsDynamicResolutionEnabled());
	Out->SetObjectField(TEXT("scalability"), QualityToJson(Scalability::GetQualityLevels()));
	Inv.Succeed(Out);
}

void FConsoleToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("exec_command"), TEXT("Run one console command (deny-list applies). Returns captured output. Not undoable."))
		.Args(FToolSchema().Add(TEXT("command"), FToolParam::String(TEXT("Console command, e.g. stat fps"), 512).Required()))
		.Irreversible()
		.Handle(&ExecCommand);

	B.Tool(TEXT("get_cvar"), TEXT("Read a console variable by name, or list by prefix."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Exact cvar name"), 256))
			.Add(TEXT("prefix"), FToolParam::String(TEXT("List cvars starting with this"), 256))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.Handle(&GetCVar);

	B.Tool(TEXT("set_cvar"), TEXT("Set a console variable. Undoable."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Cvar name"), 256).Required())
			.Add(TEXT("value"), FToolParam::Any(TEXT("New value (number, bool or string)")).Required()))
		.Undoable()
		.Handle(&SetCVar);

	const auto Level = [](const TCHAR* D) { return FToolParam::Integer(D, 0, 4); };
	B.Tool(TEXT("set_scalability"), TEXT("Set overall or per-group quality (0 low .. 3 epic, 4 cinematic). Undoable."))
		.Args(FToolSchema()
			.Add(TEXT("overall"), Level(TEXT("All groups")))
			.Add(TEXT("resolution_scale"), FToolParam::Number(TEXT("Screen percentage"), 10.0, 200.0))
			.Add(TEXT("view_distance"), Level(TEXT("")))
			.Add(TEXT("anti_aliasing"), Level(TEXT("")))
			.Add(TEXT("shadows"), Level(TEXT("")))
			.Add(TEXT("global_illumination"), Level(TEXT("")))
			.Add(TEXT("reflections"), Level(TEXT("")))
			.Add(TEXT("post_process"), Level(TEXT("")))
			.Add(TEXT("textures"), Level(TEXT("")))
			.Add(TEXT("effects"), Level(TEXT("")))
			.Add(TEXT("foliage"), Level(TEXT("")))
			.Add(TEXT("shading"), Level(TEXT(""))))
		.Undoable()
		.Handle(&SetScalability);

	B.Tool(TEXT("get_user_settings"), TEXT("Resolution, window mode, vsync, frame limit, scalability."))
		.Handle(&GetUserSettings);
}
