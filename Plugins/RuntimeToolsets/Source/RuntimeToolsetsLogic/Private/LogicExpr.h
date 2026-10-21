#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Math/RandomStream.h"

/** A value in game logic: null, bool, number, string, list or map. Lists and maps are copied on assignment. */
struct FLogicValue
{
	enum class EType : uint8
	{
		Null,
		Bool,
		Number,
		String,
		List,
		Map
	};

	EType Type = EType::Null;
	bool B = false;
	double N = 0.0;
	FString S;
	TSharedPtr<TArray<FLogicValue>> L;
	TSharedPtr<TMap<FString, FLogicValue>> M;

	static FLogicValue MakeBool(bool V);
	static FLogicValue MakeNumber(double V);
	static FLogicValue MakeString(const FString& V);
	static FLogicValue MakeList(TArray<FLogicValue> V = {});
	static FLogicValue MakeMap();

	bool IsNull() const { return Type == EType::Null; }
	bool Truthy() const;
	/** Display text ("3", "X", "true", "" for null). */
	FString ToText() const;
	bool Equals(const FLogicValue& Other) const;
	FLogicValue DeepCopy() const;
	const TCHAR* TypeName() const;

	TSharedPtr<FJsonValue> ToJson() const;
	static FLogicValue FromJson(const TSharedPtr<FJsonValue>& Json);
};

struct FLogicNode;

/** Name lookup and error state for one evaluation. */
struct FLogicScope
{
	/** Returns the value of an identifier or nullptr when unknown. */
	TFunction<const FLogicValue*(const FString&)> Lookup;
	FRandomStream* Random = nullptr;
	/** World functions (pos, hp, alive, nodes): 1 handled, 0 not a world function, -1 error (in Error). */
	TFunction<int32(const FString& Name, TArray<FLogicValue>& Args, FLogicValue& Out, FString& Error)> World;
	/** First error, if any. Evaluation returns null after an error. */
	FString Error;
};

namespace LogicExpr
{
	/** Parses an expression (cached). Returns null and sets OutError on a syntax error. */
	TSharedPtr<const FLogicNode> Parse(const FString& Text, FString& OutError);
	FLogicValue Eval(const FLogicNode& Node, FLogicScope& Scope);
	FLogicValue Evaluate(const FString& Text, FLogicScope& Scope);
	/** Replaces {expr} with its text. "{{" and "}}" are literal braces. */
	FString Interpolate(const FString& Text, FLogicScope& Scope);
	/** Checks every {expr} of a template for syntax errors. */
	bool CheckTemplate(const FString& Text, FString& OutError);
	const TArray<FString>& FunctionNames();
}
