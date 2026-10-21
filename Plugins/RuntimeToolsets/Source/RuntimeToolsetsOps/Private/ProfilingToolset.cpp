#include "ProfilingToolset.h"
#include "RuntimeToolHelpers.h"
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

using namespace RuntimeTools;

namespace
{
	double Percentile(TArray<double> Sorted, double P)
	{
		if (Sorted.Num() == 0)
		{
			return 0.0;
		}
		const int32 Index = FMath::Clamp(FMath::CeilToInt(P * Sorted.Num()) - 1, 0, Sorted.Num() - 1);
		return Sorted[Index];
	}

	/** Samples real frame times for Duration seconds on the core ticker, then calls Done(frame ms, offsets s). */
	void SampleFrames(FToolInvocation& Inv, double Duration, TFunction<void(FToolInvocation&, const TArray<double>&, const TArray<double>&)> Done)
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
		TSharedRef<FToolInvocation> Self = Inv.AsShared();
		FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Duration, Done](float)
		{
			if (Self->IsComplete())
			{
				return false;
			}
			const double Now = FPlatformTime::Seconds();
			State->FrameMs.Add((Now - State->Last) * 1000.0);
			State->Offsets.Add(Now - State->Start);
			State->Last = Now;
			if (Now - State->Start < Duration)
			{
				Self->ReportProgress(static_cast<float>((Now - State->Start) / Duration), TEXT("sampling"));
				return true;
			}
			Done(*Self, State->FrameMs, State->Offsets);
			return false;
		}));
		Inv.SetCancelHandler([Handle]() { FTSTicker::GetCoreTicker().RemoveTicker(Handle); });
		Inv.SetTimeout(Duration + 10.0);
		Inv.Defer();
	}
}

static void FrameTiming(FToolInvocation& Inv)
{
	const double HitchMs = Inv.GetNumber(TEXT("hitch_ms"), 50.0);
	SampleFrames(Inv, Inv.GetNumber(TEXT("seconds"), 2.0), [HitchMs](FToolInvocation& Self, const TArray<double>& Frames, const TArray<double>&)
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
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("frames"), Frames.Num());
		Out->SetNumberField(TEXT("avg_ms"), RuntimeJson::Round(Avg));
		Out->SetNumberField(TEXT("fps"), RuntimeJson::Round(Avg > 0.0 ? 1000.0 / Avg : 0.0));
		Out->SetNumberField(TEXT("min_ms"), RuntimeJson::Round(Percentile(Sorted, 0.0)));
		Out->SetNumberField(TEXT("p50_ms"), RuntimeJson::Round(Percentile(Sorted, 0.5)));
		Out->SetNumberField(TEXT("p95_ms"), RuntimeJson::Round(Percentile(Sorted, 0.95)));
		Out->SetNumberField(TEXT("p99_ms"), RuntimeJson::Round(Percentile(Sorted, 0.99)));
		Out->SetNumberField(TEXT("max_ms"), RuntimeJson::Round(Sorted.Num() ? Sorted.Last() : 0.0));
		Out->SetNumberField(TEXT("hitches"), Hitches);
		// Thread breakdown of the most recent frame (zero without rendering).
		TSharedRef<FJsonObject> Threads = Obj();
		Threads->SetNumberField(TEXT("game_ms"), RuntimeJson::Round(FPlatformTime::ToMilliseconds(GGameThreadTime)));
		Threads->SetNumberField(TEXT("render_ms"), RuntimeJson::Round(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
		Threads->SetNumberField(TEXT("rhi_ms"), RuntimeJson::Round(FPlatformTime::ToMilliseconds(GRHIThreadTime)));
		Threads->SetNumberField(TEXT("gpu_ms"), RuntimeJson::Round(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
		Out->SetObjectField(TEXT("last_frame"), Threads);
		Self.Succeed(Out);
	});
}

static void FindHitches(FToolInvocation& Inv)
{
	const double HitchMs = Inv.GetNumber(TEXT("hitch_ms"), 50.0);
	const int32 Limit = Inv.GetLimit();
	SampleFrames(Inv, Inv.GetNumber(TEXT("seconds"), 5.0), [HitchMs, Limit](FToolInvocation& Self, const TArray<double>& Frames, const TArray<double>& Offsets)
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
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 i = 0; i < Indices.Num() && i < Limit; ++i)
		{
			TSharedRef<FJsonObject> J = Obj();
			J->SetNumberField(TEXT("at_s"), RuntimeJson::Round(Offsets[Indices[i]]));
			J->SetNumberField(TEXT("ms"), RuntimeJson::Round(Frames[Indices[i]]));
			Items.Add(MakeShared<FJsonValueObject>(J));
		}
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("frames"), Frames.Num());
		Out->SetNumberField(TEXT("hitch_count"), Indices.Num());
		Out->SetArrayField(TEXT("worst"), Items);
		Self.Succeed(Out);
	});
}

static void MemoryReport(FToolInvocation& Inv)
{
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	constexpr double MB = 1024.0 * 1024.0;
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("used_physical_mb"), RuntimeJson::Round(Mem.UsedPhysical / MB));
	Out->SetNumberField(TEXT("peak_physical_mb"), RuntimeJson::Round(Mem.PeakUsedPhysical / MB));
	Out->SetNumberField(TEXT("used_virtual_mb"), RuntimeJson::Round(Mem.UsedVirtual / MB));
	Out->SetNumberField(TEXT("available_physical_mb"), RuntimeJson::Round(Mem.AvailablePhysical / MB));
	Out->SetNumberField(TEXT("uobjects"), GUObjectArray.GetObjectArrayNumMinusAvailable());

	const int32 Top = static_cast<int32>(Inv.GetInt(TEXT("top_classes"), 10));
	if (Top > 0)
	{
		TMap<UClass*, int32> Counts;
		for (TObjectIterator<UObject> It; It; ++It)
		{
			Counts.FindOrAdd(It->GetClass())++;
		}
		Counts.ValueSort([](int32 A, int32 B) { return A > B; });
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const TPair<UClass*, int32>& P : Counts)
		{
			if (Items.Num() >= Top)
			{
				break;
			}
			TSharedRef<FJsonObject> J = Obj();
			J->SetStringField(TEXT("class"), P.Key->GetName());
			J->SetNumberField(TEXT("count"), P.Value);
			Items.Add(MakeShared<FJsonValueObject>(J));
		}
		Out->SetArrayField(TEXT("top_classes"), Items);
	}
	Inv.Succeed(Out);
}

static void GpuStats(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("rhi"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
	Out->SetStringField(TEXT("adapter"), GRHIAdapterName);
	Out->SetNumberField(TEXT("gpu_ms"), RuntimeJson::Round(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		Out->SetArrayField(TEXT("viewport"), {MakeShared<FJsonValueNumber>(Size.X), MakeShared<FJsonValueNumber>(Size.Y)});
	}
	if (IConsoleVariable* SP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		Out->SetNumberField(TEXT("screen_percentage"), RuntimeJson::Round(SP->GetFloat()));
	}
	if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		Out->SetBoolField(TEXT("vsync"), VSync->GetInt() != 0);
	}
	Inv.Succeed(Out);
}

static void StartTrace(FToolInvocation& Inv)
{
	if (FTraceAuxiliary::IsConnected())
	{
		Inv.Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("already tracing to %s"), *FTraceAuxiliary::GetTraceDestinationString()),
			TEXT("Call profiling.stop_trace first"));
		return;
	}
	FString File = Inv.GetString(TEXT("file"));
	if (File.IsEmpty())
	{
		File = FString::Printf(TEXT("RuntimeToolsets_%s.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}
	if (File.Contains(TEXT("..")) || File.Contains(TEXT("/")) || File.Contains(TEXT("\\")) || File.Contains(TEXT(":")))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, TEXT("file must be a plain file name (it is written to the Profiling folder)"));
		return;
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
	const FString Channels = Inv.GetString(TEXT("channels"), TEXT("default"));
	if (!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *Path, *Channels))
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("could not start the trace (is tracing compiled into this build?)"));
		return;
	}
	Inv.RecordIrreversible(TEXT("start trace"));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("file"), Path);
	Out->SetStringField(TEXT("channels"), Channels);
	Inv.Succeed(Out);
}

static void StopTrace(FToolInvocation& Inv)
{
	if (!FTraceAuxiliary::IsConnected())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("no trace is running"));
		return;
	}
	const FString Destination = FTraceAuxiliary::GetTraceDestinationString();
	FTraceAuxiliary::Stop();
	Inv.RecordIrreversible(TEXT("stop trace"));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("file"), Destination);
	Out->SetStringField(TEXT("open_with"), TEXT("UnrealInsights.exe"));
	Inv.Succeed(Out);
}

void FProfilingToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("frame_timing"), TEXT("Sample frame times for a few seconds: avg, p50/p95/p99, max, fps, hitches."))
		.Args(FToolSchema()
			.Add(TEXT("seconds"), FToolParam::Number(TEXT("Sample window"), 0.1, 60.0).Default(2.0))
			.Add(TEXT("hitch_ms"), FToolParam::Number(TEXT("Frames at or above this count as hitches"), 1.0).Default(50.0)))
		.Async()
		.Handle(&FrameTiming);

	B.Tool(TEXT("find_hitches"), TEXT("Sample frames and list the worst hitches with their time offsets."))
		.Args(FToolSchema()
			.Add(TEXT("seconds"), FToolParam::Number(TEXT("Sample window"), 0.1, 60.0).Default(5.0))
			.Add(TEXT("hitch_ms"), FToolParam::Number(TEXT("Threshold ms"), 1.0).Default(50.0))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.Async()
		.Handle(&FindHitches);

	B.Tool(TEXT("memory_report"), TEXT("Process memory, UObject count and the most common object classes."))
		.Args(FToolSchema().Add(TEXT("top_classes"), FToolParam::Integer(TEXT("How many classes to list (0 = skip)"), 0, 100).Default(10)))
		.Handle(&MemoryReport);

	B.Tool(TEXT("gpu_stats"), TEXT("RHI, GPU adapter, GPU frame time, viewport size, screen percentage, vsync."))
		.Handle(&GpuStats);

	B.Tool(TEXT("start_trace"), TEXT("Start an Unreal Insights trace to a file in the Profiling folder."))
		.Args(FToolSchema()
			.Add(TEXT("file"), FToolParam::String(TEXT("File name (default: timestamped)"), 128))
			.Add(TEXT("channels"), FToolParam::String(TEXT("Trace channels, e.g. default,memory or cpu,gpu,frame"), 256).Default(TEXT("default"))))
		.Irreversible()
		.Handle(&StartTrace);

	B.Tool(TEXT("stop_trace"), TEXT("Stop the running trace; returns the .utrace path."))
		.Irreversible()
		.Handle(&StopTrace);
}
