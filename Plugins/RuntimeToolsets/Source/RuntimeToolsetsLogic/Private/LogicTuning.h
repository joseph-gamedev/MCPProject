#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Internal tuning values of the logic runtime (perception rates, steering probes, animation amounts, HUD sizes...).
 * Every value has a description and the basis it comes from. Defaults can be overridden in DefaultGame.ini:
 *   [RuntimeToolsets.Tuning]
 *   brain.perceive_interval=0.2
 * and at runtime with logic.set_tuning. Parameters that belong to a definition (weapon rate, health max, vision radius...)
 * are not here: they are arguments of the define_* tools.
 */
namespace LogicTuning
{
	double Get(const TCHAR* Name);
	bool Set(const FString& Name, double Value, FString& OutError);
	void Reset();
	/** Loads overrides from the config section. */
	void LoadConfig();
	TSharedRef<FJsonObject> ToJson();
}
