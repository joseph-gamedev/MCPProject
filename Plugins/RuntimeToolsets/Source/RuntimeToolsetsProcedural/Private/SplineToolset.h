#pragma once

#include "RuntimeToolset.h"

class AActor;
class UWorld;
class USplineComponent;

/**
 * Splines as scene nodes: smooth or linear paths through points, optionally shown as a mesh swept along them
 * (ribbon for roads/paths/rivers, wall, tube). Other tools (pcg.scatter, scripts via spline.sample) build on them.
 */
class FSplineToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("spline"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Splines: paths through points shown as roads, rivers, walls or pipes; sample points along them");
	}
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;

	static AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
	/** The spline of a spline node's actor (null if none). */
	static USplineComponent* FindSpline(const AActor* Actor);
};
