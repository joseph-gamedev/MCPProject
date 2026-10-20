#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"

struct FRuntimeLogLine
{
	int64 Seq = 0;
	double Time = 0.0;
	FName Category;
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	FString Message;
};

/** Ring buffer of recent log lines. Thread-safe: the engine may log from any thread. */
class RUNTIMETOOLSETSCORE_API FRuntimeLogCapture : public FOutputDevice
{
public:
	explicit FRuntimeLogCapture(int32 InCapacity);

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	struct FQuery
	{
		int64 SinceSeq = 0;
		int32 Limit = 50;
		FName Category;
		ELogVerbosity::Type MinVerbosity = ELogVerbosity::All;
		FString Contains;
	};

	/** Newest matching lines (up to Limit), returned oldest first. */
	TArray<FRuntimeLogLine> Query(const FQuery& Query) const;
	int64 GetLastSeq() const;

	/** Per-session category subscriptions; lines are pushed through IRuntimeToolsetsCore::OnEvent. */
	void SetSubscription(const FString& SessionId, FName Category, ELogVerbosity::Type MinVerbosity, bool bEnabled);
	TArray<FName> GetSubscriptions(const FString& SessionId) const;
	void RemoveSession(const FString& SessionId);

	/** Collects lines newer than the last drain that match some subscription. Game thread. */
	void DrainSubscriptions(TFunctionRef<void(const FString& SessionId, const FRuntimeLogLine& Line)> Emit);

	static FString VerbosityToString(ELogVerbosity::Type Verbosity);
	static ELogVerbosity::Type ParseVerbosity(const FString& Text);

private:
	mutable FCriticalSection Lock;
	TArray<FRuntimeLogLine> Ring;
	int32 Capacity;
	int32 Head = 0;
	int64 NextSeq = 1;
	int64 LastDrainedSeq = 0;
	TMap<FString, TMap<FName, ELogVerbosity::Type>> Subscriptions;
};
