#pragma once

#include "CoreMinimal.h"

/** Replays a saved game (recorded tool calls) when the game starts; see LogicStartupGame.cpp. */
namespace LogicStartupGame
{
	void Startup();
	void Shutdown();
}
