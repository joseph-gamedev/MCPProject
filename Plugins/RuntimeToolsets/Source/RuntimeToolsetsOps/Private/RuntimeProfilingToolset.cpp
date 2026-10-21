#include "RuntimeProfilingToolset.h"
#include "RuntimeJson.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolsetsSettings.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "RHIGlobals.h"
#include "RenderTimer.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectIterator.h"

namespace
{
	double Percentile(const TArray<double>& Sorted, double P)
	{
		if (Sorted.Num() == 0)
		{
			return 0.0;
		}
		const int32 Index = FMath::Clamp(FMath::CeilToInt(P * Sorted.Num()) - 1, 0, Sorted.Num() - 1);
		return Sorted[Index];
	}

	float R3(double V)
	{
		return static_cast<float>(RuntimeJson::Round(V));
	}

	bool CheckWindow(float Seconds, float HitchMs)
	{
		if (Seconds < 0.1f || Seconds > 60.f || HitchMs <= 0.f)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
				FString::Printf(TEXT("seconds must be 0.1-60 and hitch_ms > 0 (got %g, %g)"), Seconds, HitchMs), TEXT("Sample longer runs in several calls"));
			return false;
		}
		return true;
	}

	/**
	 * Samples real frame times for Duration seconds on the core ticker, then calls Done(frame ms, offsets s).
	 * Completes through the async result; cancelling stops sampling.
	 */
	template <typename TResult>
	void SampleFrames(TResult* Result, double Duration, TFunction<void(TResult&, const TArray<double>&, const TArray<double>&)> Done)
	{
		struct FState
		{
			TArray<double> FrameMs;
			TArray<double> Offsets;
			double Start = 0.0;
			double Last = 0.0;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		State->Start = State->Last = FPlatformTime::Seconds();
		TSharedPtr<FToolInvocation> Inv = RuntimeToolCall::Current()->AsShared();
		TWeakObjectPtr<TResult> Weak = Result;
		const FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, Inv, State, Duration, Done](float)
		{
			TResult* R = Weak.Get();
			if (!R || R->bIsComplete || Inv->IsComplete())
			{
				return false;
			}
			const double Now = FPlatformTime::Seconds();
			State->FrameMs.Add((Now - State->Last) * 1000.0);
			State->Offsets.Add(Now - State->Start);
			State->Last = Now;
			if (Now - State->Start < Duration)
			{
				Inv->ReportProgress(static_cast<float>((Now - State->Start) / Duration), TEXT("sampling"));
				return true;
			}
			Done(*R, State->FrameMs, State->Offsets);
			return false;
		}));
		Result->OnCancelled = [Handle]() { FTSTicker::GetCoreTicker().RemoveTicker(Handle); };
		RuntimeToolCall::SetTimeout(Duration + 10.0);
	}
}

UProfilingToolFrameTimingAsyncResult* URuntimeProfilingToolset::FrameTiming(float Seconds, float HitchMs)
{
	if (!CheckWindow(Seconds, HitchMs))
	{
		return nullptr;
	}
	UProfilingToolFrameTimingAsyncResult* Result = NewObject<UProfilingToolFrameTimingAsyncResult>();
	SampleFrames<UProfilingToolFrameTimingAsyncResult>(Result, Seconds,
		[HitchMs](UProfilingToolFrameTimingAsyncResult& R, const TArray<double>& Frames, const TArray<double>&)
		{
			TArray<double> Sorted = Frames;
			Sorted.Sort();
			double Sum = 0.0;
			int32 Hitches = 0;
			for (double F : Frames)
			{
				Sum += F;
				Hitches += F >= HitchMs ? 1 : 0;
			}
			const double Avg = Frames.Num() ? Sum / Frames.Num() : 0.0;
			FProfilingToolFrameTiming V;
			V.Frames = Frames.Num();
			V.AvgMs = R3(Avg);
			V.Fps = R3(Avg > 0.0 ? 1000.0 / Avg : 0.0);
			V.MinMs = R3(Percentile(Sorted, 0.0));
			V.P50Ms = R3(Percentile(Sorted, 0.5));
			V.P95Ms = R3(Percentile(Sorted, 0.95));
			V.P99Ms = R3(Percentile(Sorted, 0.99));
			V.MaxMs = R3(Sorted.Num() ? Sorted.Last() : 0.0);
			V.Hitches = Hitches;
			// Thread breakdown of the most recent frame (zero without rendering).
			V.LastFrame.GameMs = R3(FPlatformTime::ToMilliseconds(GGameThreadTime));
			V.LastFrame.RenderMs = R3(FPlatformTime::ToMilliseconds(GRenderThreadTime));
			V.LastFrame.RhiMs = R3(FPlatformTime::ToMilliseconds(GRHIThreadTime));
			V.LastFrame.GpuMs = R3(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			R.SetValue(V);
		});
	return Result;
}

UProfilingToolHitchesAsyncResult* URuntimeProfilingToolset::FindHitches(float Seconds, float HitchMs, int32 Limit)
{
	if (!CheckWindow(Seconds, HitchMs))
	{
		return nullptr;
	}
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	UProfilingToolHitchesAsyncResult* Result = NewObject<UProfilingToolHitchesAsyncResult>();
	SampleFrames<UProfilingToolHitchesAsyncResult>(Result, Seconds,
		[HitchMs, Max](UProfilingToolHitchesAsyncResult& R, const TArray<double>& Frames, const TArray<double>& Offsets)
		{
			TArray<int32> Indices;
			for (int32 i = 0; i < Frames.Num(); ++i)
			{
				if (Frames[i] >= HitchMs)
				{
					Indices.Add(i);
				}
			}
			Indices.Sort([&Frames](int32 A, int32 B) { return Frames[A] > Frames[B]; });
			FProfilingToolHitches V;
			V.Frames = Frames.Num();
			V.HitchCount = Indices.Num();
			for (int32 i = 0; i < Indices.Num() && i < Max; ++i)
			{
				FProfilingToolHitch& H = V.Worst.AddDefaulted_GetRef();
				H.AtS = R3(Offsets[Indices[i]]);
				H.Ms = R3(Frames[Indices[i]]);
			}
			R.SetValue(V);
		});
	return Result;
}

FProfilingToolMemoryReport URuntimeProfilingToolset::MemoryReport(int32 TopClasses)
{
	FProfilingToolMemoryReport Out;
	if (TopClasses < 0 || TopClasses > 100)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("top_classes must be 0-100 (got %d)"), TopClasses));
		return Out;
	}
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	constexpr double MB = 1024.0 * 1024.0;
	Out.UsedPhysicalMb = R3(Mem.UsedPhysical / MB);
	Out.PeakPhysicalMb = R3(Mem.PeakUsedPhysical / MB);
	Out.UsedVirtualMb = R3(Mem.UsedVirtual / MB);
	Out.AvailablePhysicalMb = R3(Mem.AvailablePhysical / MB);
	Out.Uobjects = GUObjectArray.GetObjectArrayNumMinusAvailable();
	if (TopClasses > 0)
	{
		TMap<UClass*, int32> Counts;
		for (TObjectIterator<UObject> It; It; ++It)
		{
			Counts.FindOrAdd(It->GetClass())++;
		}
		Counts.ValueSort([](int32 A, int32 B) { return A > B; });
		for (const TPair<UClass*, int32>& P : Counts)
		{
			if (Out.TopClasses.Num() >= TopClasses)
			{
				break;
			}
			FProfilingToolClassCount& C = Out.TopClasses.AddDefaulted_GetRef();
			C.Class = P.Key->GetName();
			C.Count = P.Value;
		}
	}
	return Out;
}

FProfilingToolGpuStats URuntimeProfilingToolset::GpuStats()
{
	FProfilingToolGpuStats Out;
	Out.Rhi = GDynamicRHI ? FString(GDynamicRHI->GetName()) : FString(TEXT("none"));
	Out.Adapter = GRHIAdapterName;
	Out.GpuMs = R3(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Out.Viewport = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	if (IConsoleVariable* SP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		Out.ScreenPercentage = R3(SP->GetFloat());
	}
	if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		Out.bVsync = VSync->GetInt() != 0;
	}
	return Out;
}

FProfilingToolTrace URuntimeProfilingToolset::StartTrace(const FString& InFile, const FString& Channels)
{
	FProfilingToolTrace Out;
	if (FTraceAuxiliary::IsConnected())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("already tracing to %s"), *FTraceAuxiliary::GetTraceDestinationString()),
			TEXT("Call profiling.stop_trace first"));
		return Out;
	}
	FString File = InFile;
	if (File.IsEmpty())
	{
		File = FString::Printf(TEXT("RuntimeToolsets_%s.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}
	if (File.Contains(TEXT("..")) || File.Contains(TEXT("/")) || File.Contains(TEXT("\\")) || File.Contains(TEXT(":")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("file must be a plain file name (it is written to the Profiling folder)"),
			TEXT("e.g. \"boss_fight\""));
		return Out;
	}
	if (!File.EndsWith(TEXT(".utrace")))
	{
		File += TEXT(".utrace");
	}
	// Insights will not write over an existing file, and existing traces are never deleted: pick a free name.
	FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProfilingDir(), File));
	const FString Base = FPaths::GetBaseFilename(Path, false);
	for (int32 Suffix = 2; IFileManager::Get().FileExists(*Path); ++Suffix)
	{
		Path = FString::Printf(TEXT("%s_%d.utrace"), *Base, Suffix);
	}
	const FString UseChannels = Channels.IsEmpty() ? FString(TEXT("default")) : Channels;
	if (!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *Path, *UseChannels))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("could not start the trace (is tracing compiled into this build?)"));
		return Out;
	}
	RuntimeToolCall::Current()->RecordIrreversible(TEXT("start trace"));
	Out.File = Path;
	Out.Channels = UseChannels;
	return Out;
}

FProfilingToolTrace URuntimeProfilingToolset::StopTrace()
{
	FProfilingToolTrace Out;
	if (!FTraceAuxiliary::IsConnected())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no trace is running"), TEXT("Call profiling.start_trace first"));
		return Out;
	}
	Out.File = FTraceAuxiliary::GetTraceDestinationString();
	FTraceAuxiliary::Stop();
	RuntimeToolCall::Current()->RecordIrreversible(TEXT("stop trace"));
	Out.OpenWith = TEXT("UnrealInsights.exe");
	return Out;
}
