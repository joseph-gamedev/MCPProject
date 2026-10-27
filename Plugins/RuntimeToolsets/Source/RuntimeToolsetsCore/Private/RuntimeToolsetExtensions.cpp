#include "RuntimeToolsetExtensions.h"
#include "Interfaces/IPluginManager.h"

namespace
{
	TArray<FString>& Recipes()
	{
		static TArray<FString> R;
		return R;
	}

	TMap<FName, FString>& Assets()
	{
		static TMap<FName, FString> A;
		return A;
	}

	TMap<FName, RuntimeToolsetExtensions::FLightingOwner>& LightingOwners()
	{
		static TMap<FName, RuntimeToolsetExtensions::FLightingOwner> L;
		return L;
	}
}

void RuntimeToolsetExtensions::RegisterRecipes(const FString& JsonObjectText)
{
	Recipes().Add(JsonObjectText);
}

const TArray<FString>& RuntimeToolsetExtensions::GetRecipes()
{
	return Recipes();
}

void RuntimeToolsetExtensions::SetAsset(FName Slot, const FString& Path)
{
	if (Path.IsEmpty())
	{
		Assets().Remove(Slot);
	}
	else
	{
		Assets().Add(Slot, Path);
	}
}

FString RuntimeToolsetExtensions::GetAsset(FName Slot)
{
	const FString* P = Assets().Find(Slot);
	return P ? *P : FString();
}

void RuntimeToolsetExtensions::RegisterLightingOwner(FName Id, FLightingOwner Owner)
{
	LightingOwners().Add(Id, MoveTemp(Owner));
}

void RuntimeToolsetExtensions::UnregisterLightingOwner(FName Id)
{
	LightingOwners().Remove(Id);
}

bool RuntimeToolsetExtensions::FindLightingOwner(UWorld* World, FString& OutWho, FString& OutUseInstead)
{
	for (const TPair<FName, FLightingOwner>& KV : LightingOwners())
	{
		if (KV.Value && KV.Value(World, OutWho, OutUseInstead))
		{
			return true;
		}
	}
	return false;
}

FString RuntimeToolsetExtensions::PluginDirForModule(const FString& ModuleName)
{
	for (const TSharedRef<IPlugin>& P : IPluginManager::Get().GetEnabledPlugins())
	{
		for (const FModuleDescriptor& M : P->GetDescriptor().Modules)
		{
			if (M.Name.ToString() == ModuleName)
			{
				return P->GetBaseDir();
			}
		}
	}
	return FString();
}
