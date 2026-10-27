#include "RuntimeFluidToolset.h"
#include "RuntimeRoadToolset.h"
#include "RuntimeSkyToolset.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetExtensions.h"
#include "RuntimeToolsetsCore.h"
#include "Modules/ModuleManager.h"

// Tool docs for packaged builds (doc comments are editor-only metadata).
#include "Generated/ToolDocs.gen.inl"
// Recipes made by Scripts/recipes/make_recipes.py (sea_coast from the FluidNinja LIVE demo harvest).
#include "Generated/OpsRecipes.gen.inl"

namespace
{
	/** What the core can use from the packs when they are in the project (each falls back when its asset is missing). */
	struct FSlot
	{
		const TCHAR* Name;
		const TCHAR* Path;
	};
	const FSlot Slots[] = {
		// weather-aware masters made by Scripts/materials/make_weather_materials.py (wet, snowy, dusty with Ultra Dynamic Weather)
		{TEXT("material.terrain"), TEXT("/Game/RuntimeWeather/M_RT_Terrain_Weather.M_RT_Terrain_Weather")},
		{TEXT("material.pbr"), TEXT("/Game/RuntimeWeather/M_RT_PBR_Weather.M_RT_PBR_Weather")},
		// scanned surfaces from Scripts/architecture/import_architecture.py
		{TEXT("material.scanned"), TEXT("/Game/RuntimeArchitecture/Materials/M_RT_Scanned.M_RT_Scanned")},
		{TEXT("folder.scanned_surfaces"), TEXT("/Game/RuntimeArchitecture/Surfaces")},
		{TEXT("class.third_person_character"), TEXT("/Game/FluidNinjaLive/Meshes/ThirdPersonCharacter.ThirdPersonCharacter_C")},
	};
}

/** Content-pack toolsets: road (Road Creator Pro), fluid (FluidNinja LIVE), sky (Ultra Dynamic Sky). */
class FRuntimeToolsetsPacksModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RuntimeToolReflection::RegisterCompiledDocs(GRuntimeToolDocs, UE_ARRAY_COUNT(GRuntimeToolDocs));
		for (UClass* Class : {URuntimeRoadToolset::StaticClass(), URuntimeFluidToolset::StaticClass(), URuntimeSkyToolset::StaticClass()})
		{
			Registered.Add(RuntimeToolReflection::RegisterToolsetClass(Class));
		}
		FString Recipes;
		for (const TCHAR* Part : GOpsRecipesJsonParts)
		{
			Recipes += Part;
		}
		RuntimeToolsetExtensions::RegisterRecipes(Recipes);
		for (const FSlot& S : Slots)
		{
			RuntimeToolsetExtensions::SetAsset(S.Name, S.Path);
		}
		RuntimeToolsetExtensions::RegisterLightingOwner(TEXT("UltraDynamicSky"), [](UWorld* World, FString& Who, FString& Use)
		{
			if (!RuntimeSky::ActiveSky(World))
			{
				return false;
			}
			Who = TEXT("Ultra Dynamic Sky");
			Use = FString();   // the lighting tool's own hint names the sky.* tool for each part
			return true;
		});
	}

	virtual void ShutdownModule() override
	{
		RuntimeToolsetExtensions::UnregisterLightingOwner(TEXT("UltraDynamicSky"));
		for (const FSlot& S : Slots)
		{
			RuntimeToolsetExtensions::SetAsset(S.Name, FString());
		}
		if (IRuntimeToolsetsCore::IsAvailable())
		{
			for (const TSharedRef<IRuntimeToolset>& Toolset : Registered)
			{
				IRuntimeToolsetsCore::Get().UnregisterToolset(Toolset->GetName());
			}
		}
		Registered.Reset();
	}

private:
	TArray<TSharedRef<IRuntimeToolset>> Registered;
};

IMPLEMENT_MODULE(FRuntimeToolsetsPacksModule, RuntimeToolsetsPacks)
