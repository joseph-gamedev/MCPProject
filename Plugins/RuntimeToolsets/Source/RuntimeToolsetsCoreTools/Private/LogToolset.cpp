#include "LogToolset.h"
#include "Helpers/ToolsCommon.h"
#include "RuntimeLogCapture.h"
#include "DynamicRHI.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "RenderTimer.h"

using namespace RuntimeTools;

static void TailLog(FToolInvocation& Inv)
{
	FRuntimeLogCapture& Capture = IRuntimeToolsetsCore::Get().GetLogCapture();
	FRuntimeLogCapture::FQuery Q;
	Q.Limit = Inv.GetLimit();
	Q.SinceSeq = Inv.GetInt(TEXT("since_seq"), 0);
	if (Inv.Has(TEXT("category")))
	{
		Q.Category = FName(*Inv.GetString(TEXT("category")));
	}
	Q.MinVerbosity = FRuntimeLogCapture::ParseVerbosity(Inv.GetString(TEXT("min_verbosity"), TEXT("Log")));
	Q.Contains = Inv.GetString(TEXT("contains"));

	TArray<TSharedPtr<FJsonValue>> Lines;
	for (const FRuntimeLogLine& L : Capture.Query(Q))
	{
		TSharedRef<FJsonObject> J = Obj();
		J->SetNumberField(TEXT("seq"), static_cast<double>(L.Seq));
		J->SetStringField(TEXT("cat"), L.Category.ToString());
		if (L.Verbosity != ELogVerbosity::Log && L.Verbosity != ELogVerbosity::Display)
		{
			J->SetStringField(TEXT("v"), FRuntimeLogCapture::VerbosityToString(L.Verbosity));
		}
		FString Msg = L.Message;
		if (Msg.Len() > 500)
		{
			Msg = Msg.Left(500) + TEXT("...");
		}
		J->SetStringField(TEXT("msg"), Msg);
		Lines.Add(MakeShared<FJsonValueObject>(J));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetArrayField(TEXT("lines"), Lines);
	Out->SetNumberField(TEXT("last_seq"), static_cast<double>(Capture.GetLastSeq()));
	Inv.Succeed(Out);
}

static void SubscribeLogCategory(FToolInvocation& Inv)
{
	const FName Category(*Inv.GetString(TEXT("category")));
	const bool bEnabled = Inv.GetBool(TEXT("enabled"), true);
	const ELogVerbosity::Type Verbosity = FRuntimeLogCapture::ParseVerbosity(Inv.GetString(TEXT("min_verbosity"), TEXT("Log")));
	FRuntimeLogCapture& Capture = IRuntimeToolsetsCore::Get().GetLogCapture();
	Capture.SetSubscription(Inv.GetCaller().SessionId, Category, Verbosity, bEnabled);
	TSharedRef<FJsonObject> Out = Obj();
	TArray<FString> Subs;
	for (const FName& N : Capture.GetSubscriptions(Inv.GetCaller().SessionId))
	{
		Subs.Add(N.ToString());
	}
	Out->SetField(TEXT("subscriptions"), RuntimeJson::Strings(Subs));
	Out->SetStringField(TEXT("delivery"), TEXT("events of type 'log' via the connector"));
	Inv.Succeed(Out);
}

static void GetStat(FToolInvocation& Inv)
{
	TArray<FString> Names = Inv.GetStringArray(TEXT("names"));
	if (Names.Num() == 0)
	{
		Names = {TEXT("fps"), TEXT("frame_ms"), TEXT("game_ms"), TEXT("render_ms"), TEXT("gpu_ms"), TEXT("memory_mb")};
	}
	TSharedRef<FJsonObject> Out = Obj();
	const double Delta = FApp::GetDeltaTime();
	for (const FString& Name : Names)
	{
		if (Name == TEXT("fps"))
		{
			Out->SetNumberField(Name, Delta > 0 ? RuntimeJson::Round(1.0 / Delta) : 0.0);
		}
		else if (Name == TEXT("frame_ms"))
		{
			Out->SetNumberField(Name, RuntimeJson::Round(Delta * 1000.0));
		}
		else if (Name == TEXT("game_ms"))
		{
			Out->SetNumberField(Name, RuntimeJson::Round(FPlatformTime::ToMilliseconds(GGameThreadTime)));
		}
		else if (Name == TEXT("render_ms"))
		{
			Out->SetNumberField(Name, RuntimeJson::Round(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
		}
		else if (Name == TEXT("rhi_ms"))
		{
			Out->SetNumberField(Name, RuntimeJson::Round(FPlatformTime::ToMilliseconds(GRHIThreadTime)));
		}
		else if (Name == TEXT("gpu_ms"))
		{
			Out->SetNumberField(Name, RuntimeJson::Round(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
		}
		else if (Name == TEXT("memory_mb"))
		{
			const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
			Out->SetNumberField(Name, RuntimeJson::Round(Mem.UsedPhysical / (1024.0 * 1024.0)));
		}
		else if (Name == TEXT("actors"))
		{
			int32 Count = 0;
			if (UWorld* World = Inv.GetWorld())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					++Count;
				}
			}
			Out->SetNumberField(Name, Count);
		}
		else if (Name == TEXT("nodes"))
		{
			Out->SetNumberField(Name, Inv.GetGraph() ? Inv.GetGraph()->NumLiveNodes() : 0);
		}
		else
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown stat '%s'"), *Name));
			return;
		}
	}
	Inv.Succeed(Out);
}

void FLogToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("tail_log"), TEXT("Recent log lines (oldest first). Pass since_seq=last_seq to get only new lines."))
		.Args(FToolSchema()
			.Add(TEXT("limit"), FToolParam::Limit())
			.Add(TEXT("since_seq"), FToolParam::Integer(TEXT("Only lines after this sequence number"), 0))
			.Add(TEXT("category"), FToolParam::String(TEXT("Log category, e.g. LogTemp"), 128))
			.Add(TEXT("min_verbosity"), FToolParam::Enum(TEXT("Most verbose level to include"),
				{TEXT("Fatal"), TEXT("Error"), TEXT("Warning"), TEXT("Display"), TEXT("Log"), TEXT("Verbose"), TEXT("VeryVerbose")}).Default(TEXT("Log")))
			.Add(TEXT("contains"), FToolParam::String(TEXT("Substring filter"), 256)))
		.Handle(&TailLog);

	B.Tool(TEXT("subscribe_log_category"), TEXT("Push new lines of a category to this session as events."))
		.Args(FToolSchema()
			.Add(TEXT("category"), FToolParam::String(TEXT("Log category"), 128).Required())
			.Add(TEXT("min_verbosity"), FToolParam::Enum(TEXT("Most verbose level"), {TEXT("Error"), TEXT("Warning"), TEXT("Display"), TEXT("Log"), TEXT("Verbose")}).Default(TEXT("Log")))
			.Add(TEXT("enabled"), FToolParam::Boolean(TEXT("false to unsubscribe")).Default(true)))
		.Handle(&SubscribeLogCategory);

	B.Tool(TEXT("get_stat"), TEXT("Frame and memory stats: fps, frame_ms, game_ms, render_ms, rhi_ms, gpu_ms, memory_mb, actors, nodes."))
		.Args(FToolSchema().Add(TEXT("names"), FToolParam::Array(TEXT("Stats to read (default: common set)"),
			FToolParam::Enum(FString(), {TEXT("fps"), TEXT("frame_ms"), TEXT("game_ms"), TEXT("render_ms"), TEXT("rhi_ms"), TEXT("gpu_ms"), TEXT("memory_mb"), TEXT("actors"), TEXT("nodes")}), 9)))
		.Handle(&GetStat);
}
