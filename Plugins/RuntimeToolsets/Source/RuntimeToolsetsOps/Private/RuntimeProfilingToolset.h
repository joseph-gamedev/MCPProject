#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeProfilingToolset.generated.h"

USTRUCT()
struct FProfilingToolThreadTimes
{
	GENERATED_BODY()

	/** Game thread time of the most recent frame (0 without rendering). */
	UPROPERTY()
	float GameMs = 0.f;

	UPROPERTY()
	float RenderMs = 0.f;

	UPROPERTY()
	float RhiMs = 0.f;

	UPROPERTY()
	float GpuMs = 0.f;
};

USTRUCT()
struct FProfilingToolFrameTiming
{
	GENERATED_BODY()

	/** Frames sampled. */
	UPROPERTY()
	int32 Frames = 0;

	UPROPERTY()
	float AvgMs = 0.f;

	UPROPERTY()
	float Fps = 0.f;

	UPROPERTY()
	float MinMs = 0.f;

	UPROPERTY()
	float P50Ms = 0.f;

	UPROPERTY()
	float P95Ms = 0.f;

	UPROPERTY()
	float P99Ms = 0.f;

	UPROPERTY()
	float MaxMs = 0.f;

	/** Frames at or above hitch_ms. */
	UPROPERTY()
	int32 Hitches = 0;

	/** Thread breakdown of the most recent frame. */
	UPROPERTY()
	FProfilingToolThreadTimes LastFrame;
};

UCLASS()
class UProfilingToolFrameTimingAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FProfilingToolFrameTiming& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FProfilingToolFrameTiming Value;
};

USTRUCT()
struct FProfilingToolHitch
{
	GENERATED_BODY()

	/** Seconds from the start of sampling. */
	UPROPERTY()
	float AtS = 0.f;

	/** Frame time in ms. */
	UPROPERTY()
	float Ms = 0.f;
};

USTRUCT()
struct FProfilingToolHitches
{
	GENERATED_BODY()

	/** Frames sampled. */
	UPROPERTY()
	int32 Frames = 0;

	/** Frames at or above hitch_ms. */
	UPROPERTY()
	int32 HitchCount = 0;

	/** Worst hitches first (at most `limit`). */
	UPROPERTY()
	TArray<FProfilingToolHitch> Worst;
};

UCLASS()
class UProfilingToolHitchesAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FProfilingToolHitches& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FProfilingToolHitches Value;
};

USTRUCT()
struct FProfilingToolClassCount
{
	GENERATED_BODY()

	/** Class name, e.g. "StaticMeshComponent". */
	UPROPERTY()
	FString Class;

	/** Live objects of exactly this class. */
	UPROPERTY()
	int32 Count = 0;
};

USTRUCT()
struct FProfilingToolMemoryReport
{
	GENERATED_BODY()

	UPROPERTY()
	float UsedPhysicalMb = 0.f;

	UPROPERTY()
	float PeakPhysicalMb = 0.f;

	UPROPERTY()
	float UsedVirtualMb = 0.f;

	UPROPERTY()
	float AvailablePhysicalMb = 0.f;

	/** Live UObjects. */
	UPROPERTY()
	int32 Uobjects = 0;

	/** Most common object classes, most objects first (empty when top_classes is 0). */
	UPROPERTY()
	TArray<FProfilingToolClassCount> TopClasses;
};

USTRUCT()
struct FProfilingToolGpuStats
{
	GENERATED_BODY()

	/** RHI name, e.g. "D3D12" ("none" without rendering). */
	UPROPERTY()
	FString Rhi;

	/** GPU adapter name. */
	UPROPERTY()
	FString Adapter;

	/** GPU time of the last frame. */
	UPROPERTY()
	float GpuMs = 0.f;

	/** Game viewport size in pixels ([0,0] without a viewport). */
	UPROPERTY()
	FIntPoint Viewport = FIntPoint::ZeroValue;

	/** r.ScreenPercentage. */
	UPROPERTY()
	float ScreenPercentage = 100.f;

	UPROPERTY()
	bool bVsync = false;
};

USTRUCT()
struct FProfilingToolTrace
{
	GENERATED_BODY()

	/** Absolute path of the .utrace file. */
	UPROPERTY()
	FString File;

	/** Trace channels enabled (start_trace). */
	UPROPERTY()
	FString Channels;

	/** Program that opens the file (stop_trace). */
	UPROPERTY()
	FString OpenWith;
};

/**
 * Performance profiling of the running game: frame timing percentiles, hitches, memory, GPU info and Unreal Insights traces.
 * Workflow: frame_timing for an overview -> find_hitches to locate spikes -> memory_report / gpu_stats for the cause ->
 * start_trace, reproduce, stop_trace, then open the .utrace in Unreal Insights for detail.
 */
UCLASS()
class URuntimeProfilingToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Samples real frame times for a few seconds.
	 *
	 * @param Seconds - Sample window (0.1 - 60), e.g. 2.
	 * @param HitchMs - Frames at or above this many ms count as hitches, e.g. 50.
	 * @return {frames, avg_ms, fps, min_ms, p50_ms, p95_ms, p99_ms, max_ms, hitches, last_frame:{game_ms, render_ms, rhi_ms, gpu_ms}}.
	 * Fails if seconds is outside 0.1 - 60 or hitch_ms is not positive.
	 */
	UFUNCTION(meta = (AICallable), Category = "Profiling")
	static UProfilingToolFrameTimingAsyncResult* FrameTiming(float Seconds = 2.f, float HitchMs = 50.f);

	/**
	 * Samples frames and lists the worst hitches with their time offsets.
	 *
	 * @param Seconds - Sample window (0.1 - 60), e.g. 5.
	 * @param HitchMs - Threshold in ms, e.g. 33.
	 * @param Limit - Maximum hitches listed (0 = default limit).
	 * @return {frames, hitch_count, worst:[{at_s, ms}]} with the slowest frames first.
	 * Fails if seconds is outside 0.1 - 60 or hitch_ms is not positive.
	 */
	UFUNCTION(meta = (AICallable), Category = "Profiling")
	static UProfilingToolHitchesAsyncResult* FindHitches(float Seconds = 5.f, float HitchMs = 50.f, int32 Limit = 0);

	/**
	 * Process memory, UObject count and the most common object classes.
	 *
	 * @param TopClasses - How many classes to list (0 - 100; 0 skips the object scan).
	 * @return {used_physical_mb, peak_physical_mb, used_virtual_mb, available_physical_mb, uobjects, top_classes:[{class, count}]}.
	 * Fails if top_classes is outside 0 - 100.
	 */
	UFUNCTION(meta = (AICallable), Category = "Profiling")
	static FProfilingToolMemoryReport MemoryReport(int32 TopClasses = 10);

	/**
	 * RHI, GPU adapter, GPU frame time, viewport size, screen percentage and vsync.
	 *
	 * @return {rhi, adapter, gpu_ms, viewport:[w,h], screen_percentage, vsync}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Profiling")
	static FProfilingToolGpuStats GpuStats();

	/**
	 * Starts an Unreal Insights trace to a file in the project's Profiling folder (an existing file is never overwritten:
	 * a numbered name is picked).
	 *
	 * @param File - Plain file name, e.g. "boss_fight" (default: timestamped).
	 * @param Channels - Trace channels, e.g. "default,memory" or "cpu,gpu,frame".
	 * @return {file, channels}.
	 * Fails with wrong_state if a trace is already running, not_allowed if file is a path, unsupported if tracing is not
	 * compiled into this build.
	 */
	UFUNCTION(meta = (AICallable, Irreversible), Category = "Profiling|Trace")
	static FProfilingToolTrace StartTrace(const FString& File = TEXT(""), const FString& Channels = TEXT("default"));

	/**
	 * Stops the running trace.
	 *
	 * @return {file, open_with}: the .utrace path and the program to open it with.
	 * Fails with wrong_state if no trace is running.
	 */
	UFUNCTION(meta = (AICallable, Irreversible), Category = "Profiling|Trace")
	static FProfilingToolTrace StopTrace();
};
