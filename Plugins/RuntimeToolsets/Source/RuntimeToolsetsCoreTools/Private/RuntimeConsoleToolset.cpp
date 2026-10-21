#include "RuntimeConsoleToolset.h"
#include "Helpers/ToolsCommon.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"

using namespace RuntimeTools;

namespace
{
	bool IsCommandAllowed(const FString& Command, FString& OutReason)
	{
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		const FString Lower = Command.TrimStartAndEnd().ToLower();
		for (const FString& Prefix : S.ConsoleDenyPrefixes)
		{
			if (!Prefix.IsEmpty() && Lower.StartsWith(Prefix.ToLower()))
			{
				OutReason = FString::Printf(TEXT("commands starting with '%s' are denied by project settings"), *Prefix);
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
			OutReason = FString::Printf(TEXT("command is not in ConsoleAllowPrefixes (%s)"), *FString::Join(S.ConsoleAllowPrefixes, TEXT(", ")));
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

	FRuntimeCVarInfo CVarInfo(const FString& Name, IConsoleVariable* Var, bool bHelp)
	{
		FRuntimeCVarInfo I;
		I.Name = Name;
		I.Value = Var->GetString();
		I.bReadOnly = Var->TestFlags(ECVF_ReadOnly);
		if (bHelp)
		{
			I.Help = Var->GetHelp();
			if (I.Help.Len() > 300)
			{
				I.Help = I.Help.Left(300) + TEXT("...");
			}
		}
		return I;
	}

	FRuntimeScalability ToScalability(const Scalability::FQualityLevels& Q)
	{
		FRuntimeScalability S;
		S.ResolutionScale = FMath::RoundToFloat(Q.ResolutionQuality * 100.f) / 100.f;
		S.ViewDistance = Q.ViewDistanceQuality;
		S.AntiAliasing = Q.AntiAliasingQuality;
		S.Shadows = Q.ShadowQuality;
		S.GlobalIllumination = Q.GlobalIlluminationQuality;
		S.Reflections = Q.ReflectionQuality;
		S.PostProcess = Q.PostProcessQuality;
		S.Textures = Q.TextureQuality;
		S.Effects = Q.EffectsQuality;
		S.Foliage = Q.FoliageQuality;
		S.Shading = Q.ShadingQuality;
		S.Overall = Q.GetMinQualityLevel();
		return S;
	}

	FString NoCvarHint(const FString& Name)
	{
		FString Prefix = Name;
		int32 Dot = INDEX_NONE;
		if (Name.FindChar(TEXT('.'), Dot))
		{
			Prefix = Name.Left(Dot + 1);
		}
		return FString::Printf(TEXT("Search with console.list_cvars {\"prefix\":\"%s\"}"), *Prefix);
	}
}

FRuntimeCommandOutput URuntimeConsoleToolset::ExecCommand(const FString& InCommand)
{
	FRuntimeCommandOutput Out;
	const FString Command = InCommand.TrimStartAndEnd();
	if (Command.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("command is empty"));
		return Out;
	}
	FString Reason;
	if (!IsCommandAllowed(Command, Reason))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, Reason);
		return Out;
	}
	if (Command.Contains(TEXT("|")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("command chaining with | is not allowed"), TEXT("Send one command per call"));
		return Out;
	}
	FCaptureOutputDevice Output;
	Out.bHandled = GEngine && GEngine->Exec(RuntimeToolCall::GetWorld(), *Command, Output);
	RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("exec %s"), *Command));
	Out.Output = Output.Text.TrimEnd();
	if (Out.Output.Len() > 4000)
	{
		Out.Output = Out.Output.Left(4000) + TEXT("...");
		Out.bTruncated = true;
	}
	return Out;
}

FRuntimeCVarInfo URuntimeConsoleToolset::GetCvar(const FString& Name)
{
	IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!Var)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no console variable '%s'"), *Name), NoCvarHint(Name));
		return {};
	}
	return CVarInfo(Name, Var, true);
}

FRuntimeCVarList URuntimeConsoleToolset::ListCvars(const FString& Prefix, int32 Limit)
{
	FRuntimeCVarList Out;
	if (Prefix.Len() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("prefix needs at least 2 characters"), TEXT("e.g. \"r.Shadow\""));
		return Out;
	}
	const int32 MaxItems = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda([&Out, MaxItems](const TCHAR* Name, IConsoleObject* Object)
	{
		IConsoleVariable* Var = Object ? Object->AsVariable() : nullptr;
		if (!Var)
		{
			return;
		}
		++Out.Total;
		if (Out.Cvars.Num() < MaxItems)
		{
			Out.Cvars.Add(CVarInfo(Name, Var, false));
		}
	}), *Prefix);
	return Out;
}

FRuntimeCVarChange URuntimeConsoleToolset::SetCvar(const FString& Name, const FRuntimeToolJson& Value)
{
	FRuntimeCVarChange Out;
	IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!Var)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no console variable '%s'"), *Name), NoCvarHint(Name));
		return Out;
	}
	if (Var->TestFlags(ECVF_ReadOnly))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("%s is read-only (set at startup only)"), *Name));
		return Out;
	}
	FString Reason;
	if (!IsCommandAllowed(Name, Reason))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, Reason);
		return Out;
	}
	const TSharedPtr<FJsonValue>& V = Value.Value;
	if (!V.IsValid() || V->IsNull() || V->Type == EJson::Object || V->Type == EJson::Array)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("value must be a number, bool or string"));
		return Out;
	}
	const FString NewValue = V->Type == EJson::Boolean ? (V->AsBool() ? TEXT("1") : TEXT("0"))
		: V->Type == EJson::Number ? FString::SanitizeFloat(V->AsNumber(), 0) : V->AsString();
	const FString OldValue = Var->GetString();
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("cvar"), Name);
	Delta->SetStringField(TEXT("value"), NewValue);
	auto DoFn = [Name, NewValue](FRuntimeCommandContext&, FString& OutError)
	{
		IConsoleVariable* Found = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!Found)
		{
			OutError = TEXT("cvar vanished");
			return false;
		}
		Found->Set(*NewValue, ECVF_SetByConsole);
		return true;
	};
	auto UndoFn = [Name, OldValue](FRuntimeCommandContext&)
	{
		IConsoleVariable* Found = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!Found)
		{
			return false;
		}
		Found->Set(*OldValue, ECVF_SetByConsole);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(FString::Printf(TEXT("set %s"), *Name), DoFn, UndoFn, Delta)))
	{
		return Out;
	}
	Out.Name = Name;
	Out.Value = Var->GetString();
	Out.Previous = OldValue;
	return Out;
}

FRuntimeScalability URuntimeConsoleToolset::SetScalability(const int32& Overall, const float& ResolutionScale, const int32& ViewDistance,
	const int32& AntiAliasing, const int32& Shadows, const int32& GlobalIllumination, const int32& Reflections, const int32& PostProcess,
	const int32& Textures, const int32& Effects, const int32& Foliage, const int32& Shading)
{
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	if (!GEngine || !GEngine->GetGameUserSettings())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("no game user settings"));
		return {};
	}
	const Scalability::FQualityLevels Old = Scalability::GetQualityLevels();
	Scalability::FQualityLevels New = Old;
	bool bAny = false;
	FString BadArg;
	auto Level = [&Inv, &bAny, &BadArg](const TCHAR* Arg, int32 Value, int32& Field)
	{
		if (Inv.Has(Arg))
		{
			bAny = true;
			if (Value < 0 || Value > 4)
			{
				BadArg = Arg;
			}
			Field = Value;
		}
	};
	int32 OverallLevel = -1;
	Level(TEXT("overall"), Overall, OverallLevel);
	if (OverallLevel >= 0 && BadArg.IsEmpty())
	{
		New.SetFromSingleQualityLevel(OverallLevel);
	}
	Level(TEXT("view_distance"), ViewDistance, New.ViewDistanceQuality);
	Level(TEXT("anti_aliasing"), AntiAliasing, New.AntiAliasingQuality);
	Level(TEXT("shadows"), Shadows, New.ShadowQuality);
	Level(TEXT("global_illumination"), GlobalIllumination, New.GlobalIlluminationQuality);
	Level(TEXT("reflections"), Reflections, New.ReflectionQuality);
	Level(TEXT("post_process"), PostProcess, New.PostProcessQuality);
	Level(TEXT("textures"), Textures, New.TextureQuality);
	Level(TEXT("effects"), Effects, New.EffectsQuality);
	Level(TEXT("foliage"), Foliage, New.FoliageQuality);
	Level(TEXT("shading"), Shading, New.ShadingQuality);
	if (!BadArg.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s must be 0-4 (0 low, 1 medium, 2 high, 3 epic, 4 cinematic)"), *BadArg));
		return {};
	}
	if (Inv.Has(TEXT("resolution_scale")))
	{
		bAny = true;
		if (ResolutionScale < 10.f || ResolutionScale > 200.f)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("resolution_scale must be 10-200"));
			return {};
		}
		New.ResolutionQuality = ResolutionScale;
	}
	if (!bAny)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give overall or at least one group level"), TEXT("e.g. {\"shadows\": 2}"));
		return {};
	}
	auto Apply = [](const Scalability::FQualityLevels& Q)
	{
		Scalability::SetQualityLevels(Q);
		if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			S->ScalabilityQuality = Q;
		}
	};
	auto DoFn = [New, Apply](FRuntimeCommandContext&, FString&)
	{
		Apply(New);
		return true;
	};
	auto UndoFn = [Old, Apply](FRuntimeCommandContext&)
	{
		Apply(Old);
		return true;
	};
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("set scalability"), DoFn, UndoFn)))
	{
		return {};
	}
	return ToScalability(Scalability::GetQualityLevels());
}

FRuntimeUserSettings URuntimeConsoleToolset::GetUserSettings()
{
	FRuntimeUserSettings Out;
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!S)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("no game user settings"));
		return Out;
	}
	Out.Resolution = S->GetScreenResolution();
	const EWindowMode::Type Mode = S->GetFullscreenMode();
	Out.WindowMode = Mode == EWindowMode::Fullscreen ? TEXT("fullscreen") : Mode == EWindowMode::WindowedFullscreen ? TEXT("windowed_fullscreen") : TEXT("windowed");
	Out.bVsync = S->IsVSyncEnabled();
	Out.FrameRateLimit = S->GetFrameRateLimit();
	Out.bDynamicResolution = S->IsDynamicResolutionEnabled();
	Out.Scalability = ToScalability(Scalability::GetQualityLevels());
	return Out;
}
