#pragma once

#include "Dom/JsonObject.h"

/** Small JSON readers shared by the logic files (one copy, so unity builds never see duplicates). */
namespace LogicJson
{
	inline double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, double Default)
	{
		double V = Default;
		if (J.IsValid())
		{
			J->TryGetNumberField(Field, V);
		}
		return V;
	}

	inline FString Str(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Default = FString())
	{
		FString V = Default;
		if (J.IsValid())
		{
			J->TryGetStringField(Field, V);
		}
		return V;
	}
}
using LogicJson::Num;
using LogicJson::Str;
