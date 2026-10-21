#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeOpsToolset.generated.h"

// ---- run_batch ----------------------------------------------------------------------------------------------------------

USTRUCT()
struct FOpsToolBatchCall
{
	GENERATED_BODY()

	/** Qualified tool name, e.g. "actors.spawn_actor" ("actors_spawn_actor" works too). */
	UPROPERTY()
	FString Tool;

	/** Arguments object for the tool. A string value "$N" or "$N.path" is replaced by call N's result (e.g. "$0.id"). */
	UPROPERTY()
	FRuntimeToolJson Arguments;
};

USTRUCT()
struct FOpsToolBatchResult
{
	GENERATED_BODY()

	/** Calls that ran (including a failed one). */
	UPROPERTY()
	int32 Completed = 0;

	/** Calls in the batch. */
	UPROPERTY()
	int32 Total = 0;

	/** Index of the first failed call, -1 if none failed. */
	UPROPERTY()
	int32 FailedAt = -1;

	/** History entry of an atomic batch (undo it with history.undo), -1 otherwise. */
	UPROPERTY()
	int64 HistoryId = -1;

	/** One result per call that ran, in order: {i, tool, ok, ...result} or {i, tool, ok:false, error:{code, message, hint}}. */
	UPROPERTY()
	TArray<FRuntimeToolJson> Results;
};

UCLASS()
class UOpsToolBatchAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FOpsToolBatchResult& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FOpsToolBatchResult Value;
};

// ---- wait_for_condition / wait ------------------------------------------------------------------------------------------

UENUM()
enum class EOpsToolCompareOp : uint8
{
	/** true, a non-zero number, a non-empty string or array, any object. */
	Truthy,
	/** The field exists and is not null. */
	Exists,
	/** The field is missing or null. */
	NotExists,
	Equals,
	NotEquals,
	/** Greater than (numbers). */
	Gt,
	Gte,
	Lt,
	Lte,
	/** A string contains the value, or an array contains an element equal to it. */
	Contains,
};

USTRUCT()
struct FOpsToolConditionResult
{
	GENERATED_BODY()

	/** Always true (a condition that is never met fails the call with a timeout). */
	UPROPERTY()
	bool bMet = false;

	/** How many times the tool was called. */
	UPROPERTY()
	int32 Polls = 0;

	/** Seconds from the first poll to the one that met the condition. */
	UPROPERTY()
	float WaitedS = 0.f;

	/** The value found at `path` when the condition was met. */
	UPROPERTY()
	FRuntimeToolJson Value;
};

UCLASS()
class UOpsToolConditionAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FOpsToolConditionResult& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FOpsToolConditionResult Value;
};

USTRUCT()
struct FOpsToolWaitResult
{
	GENERATED_BODY()

	/** Seconds waited (real time). */
	UPROPERTY()
	float WaitedS = 0.f;
};

UCLASS()
class UOpsToolWaitAsyncResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FOpsToolWaitResult& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FOpsToolWaitResult Value;
};

// ---- automation tests ---------------------------------------------------------------------------------------------------

USTRUCT()
struct FOpsToolTestList
{
	GENERATED_BODY()

	/** Tests matching the filter. */
	UPROPERTY()
	int32 Total = 0;

	/** Full test paths, sorted (at most `limit`). */
	UPROPERTY()
	TArray<FString> Tests;
};

USTRUCT()
struct FOpsToolTestFailure
{
	GENERATED_BODY()

	/** Full test path. */
	UPROPERTY()
	FString Test;

	/** First error messages (at most 5). */
	UPROPERTY()
	TArray<FString> Errors;
};

USTRUCT()
struct FOpsToolTestStatus
{
	GENERATED_BODY()

	/** A run is in progress. */
	UPROPERTY()
	bool bRunning = false;

	/** Filter of the current or last run. */
	UPROPERTY()
	FString Filter;

	/** Tests in the run. */
	UPROPERTY()
	int32 Total = 0;

	UPROPERTY()
	int32 Completed = 0;

	UPROPERTY()
	int32 Passed = 0;

	UPROPERTY()
	int32 Failed = 0;

	/** Test running now (empty when idle). */
	UPROPERTY()
	FString Current;

	/** Failed tests in run order (at most max_failures). */
	UPROPERTY()
	TArray<FOpsToolTestFailure> Failures;
};

UENUM()
enum class EOpsToolTestState : uint8
{
	Running,
	Passed,
	Failed,
	/** Interrupted by stop_tests. */
	Stopped,
	/** Never started because the run was stopped. */
	Skipped,
};

USTRUCT()
struct FOpsToolTestResult
{
	GENERATED_BODY()

	/** Full test path. */
	UPROPERTY()
	FString Test;

	UPROPERTY()
	EOpsToolTestState State = EOpsToolTestState::Running;

	/** Seconds the test ran. */
	UPROPERTY()
	float DurationS = 0.f;

	UPROPERTY()
	TArray<FString> Errors;

	UPROPERTY()
	TArray<FString> Warnings;
};

USTRUCT()
struct FOpsToolTestResults
{
	GENERATED_BODY()

	/** Filter of the run these results belong to. */
	UPROPERTY()
	FString RunFilter;

	/** Per-test results in run order (only tests that started, plus skipped ones after a stop). */
	UPROPERTY()
	TArray<FOpsToolTestResult> Results;
};

/**
 * Orchestration and testing: run many tool calls in one request (optionally atomic, one undo step), wait for game state,
 * and run automation tests inside the running game (also packaged Development builds).
 *
 * Batching workflow: run_batch with calls [{tool, arguments}]; later calls can use earlier results with "$N.path"
 * (e.g. {"parent": "$0.id"}). wait_for_condition polls any tool until a field of its result meets a condition.
 *
 * Testing workflow (like Epic's AutomationTestToolset):
 *   1. list_tests to find tests (filter syntax below).
 *   2. run_automation_test with a filter; tests run over the next frames.
 *   3. get_test_status (counts, failures) and get_test_results (per test state, duration, errors, warnings) to monitor.
 *   4. stop_tests to abort.
 * Filter syntax (several expressions joined by '+', a test matching any of them is selected):
 *   "StartsWith:RuntimeToolsets.Core" or "^RuntimeToolsets.Core"  prefix of the full test path
 *   "Undo$"                                                        suffix
 *   "Schema"                                                       substring anywhere in the path
 *   "Group:Smoke"                                                  a group from the AutomationControllerSettings Groups config
 */
UCLASS()
class URuntimeOpsToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Runs several tool calls in order in one request. Later calls can reference earlier results: a string argument that is
	 * exactly "$N" or "$N.path" becomes that value of call N's result, e.g. {"tool": "actors.spawn_actor", "arguments":
	 * {"parent": "$0.id"}} after a scene.create_group call ("$$" writes a literal "$"). With atomic, everything is one undo
	 * step and a failure rolls the whole batch back.
	 *
	 * @param Calls - Up to 100 calls [{tool, arguments}], e.g. [{"tool": "scene.create_group", "arguments": {"name": "g"}}].
	 * @param bAtomic - All or nothing: roll every change back if a call fails; one undo step on success.
	 * @param bStopOnError - Stop at the first failure (always true when atomic).
	 * @param Label - History label of an atomic batch, e.g. "build village".
	 * @return {completed, total, failed_at (-1 = none), history_id (-1 unless atomic), results:[{i, tool, ok, ...}]} in call order.
	 * Fails if calls is empty or longer than 100, contains ops.run_batch, or references a call that does not run before it;
	 * an atomic batch fails with the failing call's error.
	 */
	UFUNCTION(meta = (AICallable, Undoable), Category = "Ops|Batch")
	static UOpsToolBatchAsyncResult* RunBatch(const TArray<FOpsToolBatchCall>& Calls, bool bAtomic = false, bool bStopOnError = true,
		const FString& Label = TEXT("batch"));

	/**
	 * Polls a tool until a field of its result meets a condition, or times out. Soft errors of the polled tool (e.g.
	 * not_found while an object does not exist yet) count as "not yet"; the path is then evaluated on the error result.
	 *
	 * @param Tool - Tool to poll, e.g. "scene.get_node".
	 * @param Arguments - Arguments for the polled tool, e.g. {"target": "n12"}.
	 * @param Value - Value to compare with (needed by every op except truthy, exists, not_exists), e.g. 200.
	 * @param Path - Field of the result, e.g. "location[2]" or "level.visible"; empty = the whole result. Fields of a
	 *        returnValue are found directly too.
	 * @param Op - Comparison.
	 * @param TimeoutS - Give up after this many seconds (0.05 - 300).
	 * @param IntervalS - Seconds between polls (0.01 - 10).
	 * @return {met, polls, waited_s, value}.
	 * Fails with timeout if the condition is not met in time, with the polled tool's error if it fails with unknown_tool,
	 * invalid_arguments, not_allowed or toolset_disabled, and with invalid_arguments if the op needs a value and none is given.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Arguments,Value"), Category = "Ops|Wait")
	static UOpsToolConditionAsyncResult* WaitForCondition(const FString& Tool, const FRuntimeToolJson& Arguments, const FRuntimeToolJson& Value,
		const FString& Path = TEXT(""), EOpsToolCompareOp Op = EOpsToolCompareOp::Truthy, float TimeoutS = 10.f, float IntervalS = 0.25f);

	/**
	 * Lets the game run for a number of seconds (real time), e.g. after starting physics or an animation.
	 *
	 * @param Seconds - Seconds to wait (0 - 120), e.g. 1.5.
	 * @return {waited_s}.
	 * Fails if seconds is outside 0 - 120.
	 */
	UFUNCTION(meta = (AICallable), Category = "Ops|Wait")
	static UOpsToolWaitAsyncResult* Wait(float Seconds);

	/**
	 * Lists the automation tests available in this build.
	 *
	 * @param Filter - Filter expression (see the toolset description), e.g. "^RuntimeToolsets.Toolsets+Group:Smoke"; empty = all.
	 * @param Limit - Maximum number of paths returned (0 = default limit).
	 * @return {total, tests} with full test paths sorted alphabetically.
	 * Fails if the filter names an unknown group.
	 */
	UFUNCTION(meta = (AICallable), Category = "Ops|Tests")
	static FOpsToolTestList ListTests(const FString& Filter = TEXT(""), int32 Limit = 0);

	/**
	 * Starts automation tests in-process (also in packaged builds). They run over the next frames, one per frame:
	 * poll get_test_status / get_test_results and avoid other world changes meanwhile.
	 *
	 * @param Filter - Filter expression (see the toolset description), e.g. "^RuntimeToolsets.Toolsets.Actors".
	 * @return The run status right after starting.
	 * Fails with busy if a run is in progress, with not_found if no test matches.
	 */
	UFUNCTION(meta = (AICallable, Irreversible), Category = "Ops|Tests")
	static FOpsToolTestStatus RunAutomationTest(const FString& Filter);

	/**
	 * Progress and failures of the current or last test run.
	 *
	 * @param MaxFailures - Failures to include (0 - 200).
	 * @return {running, filter, total, completed, passed, failed, current, failures:[{test, errors}]}.
	 * Fails if max_failures is outside 0 - 200.
	 */
	UFUNCTION(meta = (AICallable), Category = "Ops|Tests")
	static FOpsToolTestStatus GetTestStatus(int32 MaxFailures = 20);

	/**
	 * Per-test results of the current or last run started with run_automation_test.
	 *
	 * @param Filter - Only tests matching this filter expression, e.g. "Undo$"; empty = all.
	 * @param Limit - Maximum number of results (0 = default limit).
	 * @return {run_filter, results:[{test, state, duration_s, errors, warnings}]} in run order.
	 * Fails if the filter names an unknown group.
	 */
	UFUNCTION(meta = (AICallable), Category = "Ops|Tests")
	static FOpsToolTestResults GetTestResults(const FString& Filter = TEXT(""), int32 Limit = 0);

	/**
	 * Stops the running test run: the current test is stopped and the queued ones are skipped.
	 *
	 * @return The status after stopping.
	 * Fails if no run started by run_automation_test is in progress.
	 */
	UFUNCTION(meta = (AICallable, Irreversible), Category = "Ops|Tests")
	static FOpsToolTestStatus StopTests();
};
