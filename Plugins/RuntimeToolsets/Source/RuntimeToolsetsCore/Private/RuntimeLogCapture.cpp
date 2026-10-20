#include "RuntimeLogCapture.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"

FRuntimeLogCapture::FRuntimeLogCapture(int32 InCapacity)
	: Capacity(FMath::Max(16, InCapacity))
{
	Ring.Reserve(Capacity);
}

void FRuntimeLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	Serialize(V, Verbosity, Category, FPlatformTime::Seconds());
}

void FRuntimeLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time)
{
	if (!V)
	{
		return;
	}
	FRuntimeLogLine Line;
	Line.Time = Time;
	Line.Category = Category;
	Line.Verbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	Line.Message = V;

	FScopeLock ScopeLock(&Lock);
	Line.Seq = NextSeq++;
	if (Ring.Num() < Capacity)
	{
		Ring.Add(MoveTemp(Line));
	}
	else
	{
		Ring[Head] = MoveTemp(Line);
		Head = (Head + 1) % Capacity;
	}
}

TArray<FRuntimeLogLine> FRuntimeLogCapture::Query(const FQuery& Q) const
{
	TArray<FRuntimeLogLine> Out;
	FScopeLock ScopeLock(&Lock);
	const int32 Num = Ring.Num();
	// Walk newest -> oldest, stop at Limit, then reverse.
	for (int32 i = 0; i < Num && Out.Num() < Q.Limit; ++i)
	{
		const int32 Index = Num < Capacity ? (Num - 1 - i) : (((Head - 1 - i) % Capacity) + Capacity) % Capacity;
		const FRuntimeLogLine& L = Ring[Index];
		if (L.Seq <= Q.SinceSeq)
		{
			break;
		}
		if (!Q.Category.IsNone() && L.Category != Q.Category)
		{
			continue;
		}
		if (L.Verbosity > Q.MinVerbosity)
		{
			continue;
		}
		if (!Q.Contains.IsEmpty() && !L.Message.Contains(Q.Contains))
		{
			continue;
		}
		Out.Add(L);
	}
	Algo::Reverse(Out);
	return Out;
}

int64 FRuntimeLogCapture::GetLastSeq() const
{
	FScopeLock ScopeLock(&Lock);
	return NextSeq - 1;
}

void FRuntimeLogCapture::SetSubscription(const FString& SessionId, FName Category, ELogVerbosity::Type MinVerbosity, bool bEnabled)
{
	FScopeLock ScopeLock(&Lock);
	if (bEnabled)
	{
		Subscriptions.FindOrAdd(SessionId).Add(Category, MinVerbosity);
		if (LastDrainedSeq == 0)
		{
			LastDrainedSeq = NextSeq - 1;
		}
	}
	else if (TMap<FName, ELogVerbosity::Type>* Map = Subscriptions.Find(SessionId))
	{
		Map->Remove(Category);
		if (Map->Num() == 0)
		{
			Subscriptions.Remove(SessionId);
		}
	}
}

TArray<FName> FRuntimeLogCapture::GetSubscriptions(const FString& SessionId) const
{
	TArray<FName> Out;
	FScopeLock ScopeLock(&Lock);
	if (const TMap<FName, ELogVerbosity::Type>* Map = Subscriptions.Find(SessionId))
	{
		Map->GetKeys(Out);
	}
	return Out;
}

void FRuntimeLogCapture::RemoveSession(const FString& SessionId)
{
	FScopeLock ScopeLock(&Lock);
	Subscriptions.Remove(SessionId);
}

void FRuntimeLogCapture::DrainSubscriptions(TFunctionRef<void(const FString& SessionId, const FRuntimeLogLine& Line)> Emit)
{
	TArray<TPair<FString, FRuntimeLogLine>> Pending;
	{
		FScopeLock ScopeLock(&Lock);
		if (Subscriptions.Num() == 0)
		{
			LastDrainedSeq = NextSeq - 1;
			return;
		}
		const int32 Num = Ring.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Index = Num < Capacity ? i : (Head + i) % Capacity; // oldest -> newest
			const FRuntimeLogLine& L = Ring[Index];
			if (L.Seq <= LastDrainedSeq)
			{
				continue;
			}
			for (const TPair<FString, TMap<FName, ELogVerbosity::Type>>& Sub : Subscriptions)
			{
				const ELogVerbosity::Type* Min = Sub.Value.Find(L.Category);
				if (Min && L.Verbosity <= *Min)
				{
					Pending.Emplace(Sub.Key, L);
				}
			}
		}
		LastDrainedSeq = NextSeq - 1;
	}
	// Emit outside the lock: handlers may log.
	for (const TPair<FString, FRuntimeLogLine>& P : Pending)
	{
		Emit(P.Key, P.Value);
	}
}

FString FRuntimeLogCapture::VerbosityToString(ELogVerbosity::Type Verbosity)
{
	return ToString(Verbosity);
}

ELogVerbosity::Type FRuntimeLogCapture::ParseVerbosity(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return ELogVerbosity::All;
	}
	return ParseLogVerbosityFromString(Text);
}
