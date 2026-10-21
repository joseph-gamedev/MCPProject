#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeLogToolset.generated.h"

/** Log verbosity, most severe first. */
UENUM()
enum class ERuntimeLogVerbosity : uint8
{
	Fatal,
	Error,
	Warning,
	Display,
	Log,
	Verbose,
	VeryVerbose,
};

/** One captured log line. */
USTRUCT()
struct FRuntimeLogEntry
{
	GENERATED_BODY()

	/** Sequence number (increasing); pass the last one as since_seq to read only newer lines. */
	UPROPERTY()
	int64 Seq = 0;

	/** Log category, e.g. "LogTemp". */
	UPROPERTY()
	FString Category;

	/** Verbosity (Error, Warning, Display, Log...). */
	UPROPERTY()
	FString Verbosity;

	/** Message (up to 500 characters). */
	UPROPERTY()
	FString Message;
};

USTRUCT()
struct FRuntimeLogTail
{
	GENERATED_BODY()

	/** Matching lines, oldest first. */
	UPROPERTY()
	TArray<FRuntimeLogEntry> Lines;

	/** Newest sequence number in the capture buffer: pass it as since_seq next time. */
	UPROPERTY()
	int64 LastSeq = 0;
};

USTRUCT()
struct FRuntimeLogSubscriptions
{
	GENERATED_BODY()

	/** Categories this session is subscribed to. */
	UPROPERTY()
	TArray<FString> Subscriptions;

	/** How new lines arrive. */
	UPROPERTY()
	FString Delivery;
};

/**
 * Log and stats: read the game log, subscribe to log categories as pushed events, and read frame/memory stats.
 * Workflow: tail_log after an action to see what happened (pass since_seq = last_seq to read only new lines);
 * subscribe_log_category to be notified; get_stat for performance (fps, frame/game/render/gpu ms, memory).
 */
UCLASS()
class URuntimeLogToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Returns recent log lines, oldest first, filtered by category, verbosity and text.
	 *
	 * @param Limit - Maximum lines returned (0 = project default); the newest matching lines are kept.
	 * @param SinceSeq - Only lines after this sequence number (the last_seq of a previous call).
	 * @param Category - Log category, e.g. "LogTemp".
	 * @param MinVerbosity - Most verbose level to include: Error shows only errors (and fatal), Log includes Display and Log.
	 * @param Contains - Substring the message must contain.
	 * @return The lines {seq, category, verbosity, message} and last_seq.
	 */
	UFUNCTION(meta = (AICallable), Category = "Log")
	static FRuntimeLogTail TailLog(int32 Limit = 25, int64 SinceSeq = 0, const FString& Category = TEXT(""),
		ERuntimeLogVerbosity MinVerbosity = ERuntimeLogVerbosity::Log, const FString& Contains = TEXT(""));

	/**
	 * Pushes new lines of a log category to this session as events (type "log"), or stops doing so.
	 *
	 * @param Category - Log category, e.g. "LogBlueprintUserMessages".
	 * @param MinVerbosity - Most verbose level to push.
	 * @param bEnabled - False to unsubscribe.
	 * @return The session's subscriptions and how lines are delivered.
	 */
	UFUNCTION(meta = (AICallable), Category = "Log")
	static FRuntimeLogSubscriptions SubscribeLogCategory(const FString& Category, ERuntimeLogVerbosity MinVerbosity = ERuntimeLogVerbosity::Log,
		bool bEnabled = true);

	/**
	 * Reads frame and memory stats. Fails for unknown stat names.
	 *
	 * @param Names - Stats to read: fps, frame_ms, game_ms, render_ms, rhi_ms, gpu_ms, memory_mb, actors, nodes
	 *        (default: fps, frame_ms, game_ms, render_ms, gpu_ms, memory_mb).
	 * @return Stat name -> value.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Names"), Category = "Log")
	static TMap<FString, double> GetStat(const TArray<FString>& Names);
};
