#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** Small JSON helpers shared by toolsets. Vectors are [x,y,z]; rotators are [pitch,yaw,roll]. */
namespace RuntimeJson
{
	RUNTIMETOOLSETSCORE_API TSharedRef<FJsonValue> Vec(const FVector& V);
	RUNTIMETOOLSETSCORE_API TSharedRef<FJsonValue> Rot(const FRotator& R);
	RUNTIMETOOLSETSCORE_API TSharedRef<FJsonValue> Strings(const TArray<FString>& Values);
	RUNTIMETOOLSETSCORE_API TSharedRef<FJsonValue> Names(const TArray<FName>& Values);

	RUNTIMETOOLSETSCORE_API bool ToVector(const TSharedPtr<FJsonValue>& Value, FVector& Out);
	RUNTIMETOOLSETSCORE_API bool ToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& Out);

	/** Compact (no whitespace) serialization. */
	RUNTIMETOOLSETSCORE_API FString Write(const TSharedRef<FJsonObject>& Object);
	RUNTIMETOOLSETSCORE_API FString WriteValue(const TSharedPtr<FJsonValue>& Value);
	/** Pretty, sorted-keys serialization (schema snapshots). */
	RUNTIMETOOLSETSCORE_API FString WriteSorted(const TSharedRef<FJsonObject>& Object);
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonObject> Parse(const FString& Text);
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonValue> ParseValue(const FString& Text);

	/** Field names of an object (works with UE 5.8 shared-string keys). */
	RUNTIMETOOLSETSCORE_API TArray<FString> Keys(const TSharedPtr<FJsonObject>& Object);

	/** Field value or nullptr. */
	RUNTIMETOOLSETSCORE_API TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Object, FStringView Name);

	/** True if the field exists and is not JSON null. */
	RUNTIMETOOLSETSCORE_API bool HasValue(const TSharedPtr<FJsonObject>& Object, FStringView Name);

	/** Rounds to 3 decimals so results stay short. */
	RUNTIMETOOLSETSCORE_API double Round(double Value);

	/** Returns true if Fields is empty or contains Name. */
	inline bool Wants(const TArray<FString>& Fields, const TCHAR* Name) { return Fields.Num() == 0 || Fields.Contains(Name); }
}
