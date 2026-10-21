#include "RuntimeLogToolset.h"
#include "Helpers/ToolsCommon.h"
#include "RuntimeLogCapture.h"
#include "RuntimeToolCall.h"
#include "DynamicRHI.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "RenderTimer.h"

namespace
{
	ELogVerbosity::Type ToEngine(ERuntimeLogVerbosity V)
	{
		switch (V)
		{
		case ERuntimeLogVerbosity::Fatal: return ELogVerbosity::Fatal;
		case ERuntimeLogVerbosity::Error: return ELogVerbosity::Error;
		case ERuntimeLogVerbosity::Warning: return ELogVerbosity::Warning;
		case ERuntimeLogVerbosity::Display: return ELogVerbosity::Display;
		case ERuntimeLogVerbosity::Log: return ELogVerbosity::Log;
		case ERuntimeLogVerbosity::Verbose: return ELogVerbosity::Verbose;
		default: return ELogVerbosity::VeryVerbose;
		}
	}

	const TArray<FString>& StatNames()
	{
		static const TArray<FString> Names = {TEXT("fps"), TEXT("frame_ms"), TEXT("game_ms"), TEXT("render_ms"), TEXT("rhi_ms"), TEXT("gpu_ms"),
			TEXT("memory_mb"), TEXT("actors"), TEXT("nodes")};
		return Names;
	}

	double Round2(double V)
	{
		return FMath::RoundToDouble(V * 100.0) / 100.0;
	}
}

FRuntimeLogTail URuntimeLogToolset::TailLog(int32 Limit, int64 SinceSeq, const FString& Category, ERuntimeLogVerbosity MinVerbosity, const FString& Contains)
{
	FRuntimeLogCapture& Capture = IRuntimeToolsetsCore::Get().GetLogCapture();
	FRuntimeLogCapture::FQuery Q;
	Q.Limit = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	Q.SinceSeq = SinceSeq;
	Q.Category = Category.IsEmpty() ? NAME_None : FName(*Category);
	Q.MinVerbosity = ToEngine(MinVerbosity);
	Q.Contains = Contains;
	FRuntimeLogTail Out;
	for (const FRuntimeLogLine& L : Capture.Query(Q))
	{
		FRuntimeLogEntry& E = Out.Lines.AddDefaulted_GetRef();
		E.Seq = L.Seq;
		E.Category = L.Category.ToString();
		E.Verbosity = FRuntimeLogCapture::VerbosityToString(L.Verbosity);
		E.Message = L.Message.Len() > 500 ? L.Message.Left(500) + TEXT("...") : L.Message;
	}
	Out.LastSeq = Capture.GetLastSeq();
	return Out;
}

FRuntimeLogSubscriptions URuntimeLogToolset::SubscribeLogCategory(const FString& Category, ERuntimeLogVerbosity MinVerbosity, bool bEnabled)
{
	FRuntimeLogSubscriptions Out;
	if (Category.IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("category is empty"), TEXT("e.g. \"LogTemp\"; see log.tail_log for categories in use"));
		return Out;
	}
	const FString Session = RuntimeToolCall::Current()->GetCaller().SessionId;
	FRuntimeLogCapture& Capture = IRuntimeToolsetsCore::Get().GetLogCapture();
	Capture.SetSubscription(Session, FName(*Category), ToEngine(MinVerbosity), bEnabled);
	for (const FName& N : Capture.GetSubscriptions(Session))
	{
		Out.Subscriptions.Add(N.ToString());
	}
	Out.Delivery = TEXT("events of type 'log' via the connector");
	return Out;
}

TMap<FString, double> URuntimeLogToolset::GetStat(const TArray<FString>& InNames)
{
	TMap<FString, double> Out;
	TArray<FString> Names = InNames;
	if (Names.Num() == 0)
	{
		Names = {TEXT("fps"), TEXT("frame_ms"), TEXT("game_ms"), TEXT("render_ms"), TEXT("gpu_ms"), TEXT("memory_mb")};
	}
	for (const FString& Name : Names)
	{
		if (!StatNames().Contains(Name))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown stat '%s'"), *Name),
				FString::Printf(TEXT("Use: %s"), *FString::Join(StatNames(), TEXT(", "))));
			return {};
		}
	}
	const double Delta = FApp::GetDeltaTime();
	for (const FString& Name : Names)
	{
		double V = 0.0;
		if (Name == TEXT("fps")) { V = Delta > 0 ? 1.0 / Delta : 0.0; }
		else if (Name == TEXT("frame_ms")) { V = Delta * 1000.0; }
		else if (Name == TEXT("game_ms")) { V = FPlatformTime::ToMilliseconds(GGameThreadTime); }
		else if (Name == TEXT("render_ms")) { V = FPlatformTime::ToMilliseconds(GRenderThreadTime); }
		else if (Name == TEXT("rhi_ms")) { V = FPlatformTime::ToMilliseconds(GRHIThreadTime); }
		else if (Name == TEXT("gpu_ms")) { V = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()); }
		else if (Name == TEXT("memory_mb")) { V = FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0); }
		else if (Name == TEXT("actors"))
		{
			if (UWorld* World = RuntimeToolCall::GetWorld())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					V += 1.0;
				}
			}
		}
		else if (Name == TEXT("nodes"))
		{
			FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
			V = Graph ? Graph->NumLiveNodes() : 0;
		}
		Out.Add(Name, Round2(V));
	}
	return Out;
}
