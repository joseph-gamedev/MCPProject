#pragma once

#include "RuntimeToolset.h"

class AActor;
class UWorld;

/**
 * PCG scattering: a real PCG graph (spline data -> spline sampler -> jitter/offset -> bounds -> self pruning) samples points
 * along or inside a spline; the points are filtered (noise, exclusion splines, ground) and rendered as GPU instances of
 * builder objects (any node with a mesh), weighted per item. Scatter nodes are re-creatable from their recipe (undo/redo).
 */
class FPcgToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("pcg"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Procedural scattering with Unreal PCG: trees, rocks, fences, lamps along or inside splines, as instanced meshes");
	}
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;

	static AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
};
