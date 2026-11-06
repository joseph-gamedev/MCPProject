#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
class ARuntimeToolsetsLabelActor;

/**
 * Something on screen while an agent designs and nothing is built yet: a glowing blueprint grid where the player looks,
 * and a scan frame sweeping up and down over it. Removed as soon
 * as the first piece is built. Not a scene node (tools never see it); no collision.
 */
class FBuildPlaceholder
{
public:
	/** Call every frame: shows the placeholder while wanted, animates it, removes it when not. */
	void Update(bool bWanted, double DesigningSeconds, double Now);
	void Clear();

private:
	bool Spawn(UWorld* World);

	TWeakObjectPtr<AActor> Grid;
	TWeakObjectPtr<AActor> Scan;
	TWeakObjectPtr<ARuntimeToolsetsLabelActor> Label;
	FVector At = FVector::ZeroVector;
	double Started = 0.0;
	double NextLabel = 0.0;
	bool bTried = false;
};
