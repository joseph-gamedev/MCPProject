#include "RuntimeLevelToolset.h"
#include "RuntimeToolHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Ticker.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

namespace
{
	FString StreamingName(const ULevelStreaming* L)
	{
		return L->GetWorldAssetPackageName();
	}

	TArray<FString> StreamingNames(UWorld* World)
	{
		TArray<FString> Names;
		for (const ULevelStreaming* L : World->GetStreamingLevels())
		{
			if (L)
			{
				Names.Add(FPackageName::GetShortName(StreamingName(L)));
			}
		}
		return Names;
	}

	/** Finds a streaming level by package name or short name; fails the call with the known levels. */
	ULevelStreaming* FindStreaming(UWorld* World, const FString& Name)
	{
		for (ULevelStreaming* L : World->GetStreamingLevels())
		{
			if (!L)
			{
				continue;
			}
			const FString Package = StreamingName(L);
			if (Package.Equals(Name, ESearchCase::IgnoreCase) || FPackageName::GetShortName(Package).Equals(Name, ESearchCase::IgnoreCase))
			{
				return L;
			}
		}
		const TArray<FString> Names = StreamingNames(World);
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no streaming level '%s'"), *Name),
			Names.Num() ? TEXT("Streaming levels: ") + FString::Join(Names, TEXT(", ")) : TEXT("This world has no streaming levels; load one with level.load_level_instance"));
		return nullptr;
	}

	FLevelToolStreamingInfo StreamingInfo(const ULevelStreaming* L)
	{
		FLevelToolStreamingInfo I;
		if (!L)
		{
			return I;
		}
		I.Package = StreamingName(L);
		I.Name = FPackageName::GetShortName(I.Package);
		I.bLoaded = L->IsLevelLoaded();
		I.bVisible = L->IsLevelVisible();
		I.bShouldBeLoaded = L->ShouldBeLoaded();
		I.bShouldBeVisible = L->ShouldBeVisible();
		I.bInstance = L->IsA<ULevelStreamingDynamic>();
		return I;
	}

	ELevelToolDataLayerState ToState(EDataLayerRuntimeState State)
	{
		return State == EDataLayerRuntimeState::Activated ? ELevelToolDataLayerState::Activated
			: State == EDataLayerRuntimeState::Loaded ? ELevelToolDataLayerState::Loaded : ELevelToolDataLayerState::Unloaded;
	}

	EDataLayerRuntimeState FromState(ELevelToolDataLayerState State)
	{
		return State == ELevelToolDataLayerState::Activated ? EDataLayerRuntimeState::Activated
			: State == ELevelToolDataLayerState::Loaded ? EDataLayerRuntimeState::Loaded : EDataLayerRuntimeState::Unloaded;
	}

	/** Validates a map path like /Game/Maps/Sandbox (an object path is accepted too). */
	bool ReadMap(const FString& Map, FString& OutPackage)
	{
		OutPackage = Map;
		if (OutPackage.Contains(TEXT(".")))
		{
			OutPackage = FPackageName::ObjectPathToPackageName(OutPackage);
		}
		if (!FPackageName::IsValidLongPackageName(OutPackage))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is not a map package path"), *Map),
				TEXT("Use a package path like /Game/Maps/MyMap (level.find_assets with asset_class World lists the maps)"));
			return false;
		}
		if (!FPackageName::DoesPackageExist(OutPackage))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("map '%s' not found (is it cooked into the build?)"), *OutPackage),
				TEXT("level.find_assets with asset_class World lists the maps in this build"));
			return false;
		}
		return true;
	}

	/** Streams a level instance in. Undo unloads it; redo loads it again. Removed for good when discarded while undone. */
	class FLoadInstanceCommand final : public IRuntimeCommand
	{
	public:
		FLoadInstanceCommand(const FString& InPackage, const FVector& InLocation, const FRotator& InRotation, const FString& InName)
			: Package(InPackage), Location(InLocation), Rotation(InRotation), Name(InName)
		{
		}

		TWeakObjectPtr<ULevelStreamingDynamic> Streaming;

		virtual FString GetLabel() const override { return FString::Printf(TEXT("load level %s"), *Package); }

		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override
		{
			bool bSuccess = false;
			ULevelStreamingDynamic* L = ULevelStreamingDynamic::LoadLevelInstance(Context.World, Package, Location, Rotation, bSuccess, Name);
			if (!bSuccess || !L)
			{
				OutError = FString::Printf(TEXT("could not load '%s' as an instance"), *Package);
				return false;
			}
			Streaming = L;
			return true;
		}

		virtual bool Undo(FRuntimeCommandContext& Context) override
		{
			ULevelStreamingDynamic* L = Streaming.Get();
			if (!L)
			{
				return false;
			}
			L->SetShouldBeVisible(false);
			L->SetShouldBeLoaded(false);
			return true;
		}

		virtual bool Redo(FRuntimeCommandContext& Context) override
		{
			ULevelStreamingDynamic* L = Streaming.Get();
			if (!L)
			{
				return false;
			}
			L->SetShouldBeLoaded(true);
			L->SetShouldBeVisible(true);
			return true;
		}

		virtual void OnDiscarded(FRuntimeCommandContext& Context, bool bWasUndone) override
		{
			if (bWasUndone && !Context.bWorldTearingDown)
			{
				if (ULevelStreamingDynamic* L = Streaming.Get())
				{
					L->SetIsRequestingUnloadAndRemoval(true);
				}
			}
		}

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(bForUndo ? TEXT("unloaded") : TEXT("loaded"), Streaming.IsValid() ? StreamingName(Streaming.Get()) : Package);
			return D;
		}

	private:
		FString Package;
		FVector Location;
		FRotator Rotation;
		FString Name;
	};

	/** Changes a streaming level's requested loaded/visible state as one undoable step. */
	FLevelToolStateChange SetStreamingState(const FString& LevelName, bool bUnload, bool bVisible, bool bBlocking)
	{
		FLevelToolStateChange Change;
		UWorld* World = RuntimeToolCall::GetWorld();
		ULevelStreaming* Level = FindStreaming(World, LevelName);
		if (!Level)
		{
			return Change;
		}
		TWeakObjectPtr<ULevelStreaming> Weak = Level;
		const bool bOldLoaded = Level->ShouldBeLoaded();
		const bool bOldVisible = Level->ShouldBeVisible();
		const bool bNewLoaded = bUnload ? false : (bOldLoaded || bVisible);
		const bool bNewVisible = bUnload ? false : bVisible;
		TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
		Delta->SetStringField(TEXT("level"), StreamingName(Level));
		Delta->SetBoolField(TEXT("should_be_loaded"), bNewLoaded);
		Delta->SetBoolField(TEXT("should_be_visible"), bNewVisible);
		auto Set = [Weak](bool bLoaded, bool bShown)
		{
			if (!Weak.IsValid())
			{
				return false;
			}
			Weak->SetShouldBeLoaded(bLoaded);
			Weak->SetShouldBeVisible(bShown);
			return true;
		};
		auto DoFn = [Set, bNewLoaded, bNewVisible](FRuntimeCommandContext&, FString& OutError)
		{
			if (!Set(bNewLoaded, bNewVisible))
			{
				OutError = TEXT("streaming level no longer exists");
				return false;
			}
			return true;
		};
		auto UndoFn = [Set, bOldLoaded, bOldVisible](FRuntimeCommandContext&) { return Set(bOldLoaded, bOldVisible); };
		if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(bUnload ? TEXT("unload level") : TEXT("level visibility"), DoFn, UndoFn, Delta)))
		{
			if (bBlocking)
			{
				World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
			}
			Change.Level = StreamingName(Level);
			Change.bShouldBeLoaded = bNewLoaded;
			Change.bShouldBeVisible = bNewVisible;
		}
		return Change;
	}
}

FLevelToolLevels URuntimeLevelToolset::ListLevels()
{
	FLevelToolLevels Out;
	UWorld* World = RuntimeToolCall::GetWorld();
	Out.Persistent = World->PersistentLevel ? World->PersistentLevel->GetOutermost()->GetName() : World->GetMapName();
	for (const ULevelStreaming* L : World->GetStreamingLevels())
	{
		if (L)
		{
			Out.Streaming.Add(StreamingInfo(L));
		}
	}
	if (UDataLayerManager* Manager = World->GetDataLayerManager())
	{
		for (UDataLayerInstance* Layer : Manager->GetDataLayerInstances())
		{
			if (Layer)
			{
				Out.DataLayers.Add(Layer->GetDataLayerShortName(), ToState(Manager->GetDataLayerInstanceRuntimeState(Layer)));
			}
		}
	}
	return Out;
}

FString URuntimeLevelToolset::GetCurrentLevel()
{
	UWorld* World = RuntimeToolCall::GetWorld();
	return World->PersistentLevel ? World->PersistentLevel->GetOutermost()->GetName() : World->GetMapName();
}

FLevelToolAssetSearch URuntimeLevelToolset::FindAssets(const FString& FolderPath, const FString& Name, UClass* AssetClass, bool bRecursive, int32 Limit)
{
	FLevelToolAssetSearch Out;
	if (!FolderPath.IsEmpty() && !FolderPath.StartsWith(TEXT("/")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("folder_path '%s' must start with '/'"), *FolderPath),
			TEXT("Use a content folder like /Game/Maps, or leave it empty to search everything"));
		return Out;
	}
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (AR.IsLoadingAssets())
	{
		AR.WaitForCompletion();
	}
	FARFilter Filter;
	Filter.bRecursivePaths = bRecursive;
	if (FolderPath.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
		{
			Filter.PackagePaths.Add(FName(*(TEXT("/") + Plugin->GetMountedAssetPath().TrimChar(TEXT('/')))));
		}
		Filter.bRecursivePaths = true;
	}
	else
	{
		FString Folder = FolderPath;
		while (Folder.Len() > 1 && Folder.EndsWith(TEXT("/")))
		{
			Folder.LeftChopInline(1);
		}
		Filter.PackagePaths.Add(FName(*Folder));
	}
	if (AssetClass)
	{
		Filter.ClassPaths.Add(AssetClass->GetClassPathName());
		Filter.bRecursiveClasses = true;
	}
	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);
	TSet<FName> Seen;
	TArray<FLevelToolAssetInfo> Found;
	for (const FAssetData& A : Assets)
	{
		const FString Package = A.PackageName.ToString();
		// Per-actor packages are scene actors, not assets (Epic asset.find_assets skips them too).
		if (Package.Contains(TEXT("/__ExternalActors__/")) || Package.Contains(TEXT("/__ExternalObjects__/")) || Seen.Contains(A.PackageName))
		{
			continue;
		}
		if (!Name.IsEmpty() && !A.AssetName.ToString().Contains(Name, ESearchCase::IgnoreCase))
		{
			continue;
		}
		Seen.Add(A.PackageName);
		FLevelToolAssetInfo& I = Found.AddDefaulted_GetRef();
		I.Path = A.GetObjectPathString();
		I.Package = Package;
		I.Class = A.AssetClassPath.GetAssetName().ToString();
	}
	Found.Sort([](const FLevelToolAssetInfo& X, const FLevelToolAssetInfo& Y) { return X.Path < Y.Path; });
	Out.Total = Found.Num();
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	if (Found.Num() > Max)
	{
		Found.SetNum(Max);
	}
	Out.Assets = MoveTemp(Found);
	return Out;
}

URuntimeLevelToolLoadAsyncResult* URuntimeLevelToolset::LoadLevelInstance(const FString& Map, const FVector& Location, const FRotator& Rotation,
	const FString& Name, bool bBlocking)
{
	FString Package;
	if (!ReadMap(Map, Package))
	{
		return nullptr;
	}
	FToolInvocation* Inv = RuntimeToolCall::Current();
	const FVector L = Inv->Has(TEXT("location")) ? Location : FVector::ZeroVector;
	const FRotator R = Inv->Has(TEXT("rotation")) ? Rotation : FRotator::ZeroRotator;
	TUniquePtr<FLoadInstanceCommand> Cmd = MakeUnique<FLoadInstanceCommand>(Package, L, R, Name);
	FLoadInstanceCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return nullptr;
	}
	ULevelStreamingDynamic* Level = Raw->Streaming.Get();
	URuntimeLevelToolLoadAsyncResult* Result = NewObject<URuntimeLevelToolLoadAsyncResult>();
	if (bBlocking)
	{
		// Loads synchronously (frame hitch); also works in worlds that are not ticking.
		RuntimeToolCall::GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		FLevelToolLoadResult Out;
		Out.Level = StreamingInfo(Level);
		Result->SetValue(Out);
		return Result;
	}
	TWeakObjectPtr<ULevelStreaming> Weak = Level;
	TWeakObjectPtr<URuntimeLevelToolLoadAsyncResult> WeakResult = Result;
	FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, WeakResult](float)
	{
		if (!WeakResult.IsValid() || WeakResult->bIsComplete)
		{
			return false;
		}
		if (!Weak.IsValid())
		{
			WeakResult->SetError(TEXT("the streaming level vanished while loading"));
			return false;
		}
		if (Weak->IsLevelLoaded() && Weak->IsLevelVisible())
		{
			FLevelToolLoadResult Out;
			Out.Level = StreamingInfo(Weak.Get());
			WeakResult->SetValue(Out);
			return false;
		}
		return true;
	}), 0.05f);
	Result->OnCancelled = [Handle]() { FTSTicker::GetCoreTicker().RemoveTicker(Handle); };
	RuntimeToolCall::SetTimeout(120.0);
	return Result;
}

FLevelToolStateChange URuntimeLevelToolset::UnloadLevel(const FString& Level, bool bBlocking)
{
	return SetStreamingState(Level, true, false, bBlocking);
}

FLevelToolStateChange URuntimeLevelToolset::SetLevelVisibility(const FString& Level, bool bVisible, bool bBlocking)
{
	return SetStreamingState(Level, false, bVisible, bBlocking);
}

FLevelToolDataLayerChange URuntimeLevelToolset::SetDataLayer(const FString& Name, ELevelToolDataLayerState State, bool bRecursive)
{
	FLevelToolDataLayerChange Change;
	UDataLayerManager* Manager = RuntimeToolCall::GetWorld()->GetDataLayerManager();
	if (!Manager)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("this world has no data layers (it is not a World Partition map)"),
			TEXT("Use level.set_level_visibility for classic streaming levels"));
		return Change;
	}
	const UDataLayerInstance* Layer = nullptr;
	TArray<FString> Names;
	for (UDataLayerInstance* L : Manager->GetDataLayerInstances())
	{
		if (!L)
		{
			continue;
		}
		Names.Add(L->GetDataLayerShortName());
		if (!Layer && (L->GetDataLayerShortName().Equals(Name, ESearchCase::IgnoreCase) || L->GetFName().ToString().Equals(Name, ESearchCase::IgnoreCase)))
		{
			Layer = L;
		}
	}
	if (!Layer)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no data layer '%s'"), *Name),
			Names.Num() ? TEXT("Data layers: ") + FString::Join(Names, TEXT(", ")) : TEXT("This world has no data layers"));
		return Change;
	}
	const EDataLayerRuntimeState New = FromState(State);
	const EDataLayerRuntimeState Old = Manager->GetDataLayerInstanceRuntimeState(Layer);
	TWeakObjectPtr<UDataLayerManager> WeakManager = Manager;
	TWeakObjectPtr<const UDataLayerInstance> WeakLayer = Layer;
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("data_layer"), Layer->GetDataLayerShortName());
	Delta->SetStringField(TEXT("state"), UEnum::GetValueAsString(State));
	auto Set = [WeakManager, WeakLayer, bRecursive](EDataLayerRuntimeState S)
	{
		return WeakManager.IsValid() && WeakLayer.IsValid() && WeakManager->SetDataLayerInstanceRuntimeState(WeakLayer.Get(), S, bRecursive);
	};
	auto DoFn = [Set, New](FRuntimeCommandContext&, FString& OutError)
	{
		if (!Set(New))
		{
			OutError = TEXT("the data layer manager refused the state change");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, Old](FRuntimeCommandContext&) { return Set(Old); };
	if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("data layer state"), DoFn, UndoFn, Delta)))
	{
		Change.DataLayer = Layer->GetDataLayerShortName();
		Change.State = State;
		Change.Previous = ToState(Old);
	}
	return Change;
}

FLevelToolTravelInfo URuntimeLevelToolset::OpenLevel(const FString& Map, const FString& Options, bool bConfirm)
{
	FLevelToolTravelInfo Out;
	FString Package;
	if (!ReadMap(Map, Package))
	{
		return Out;
	}
	if (!bConfirm)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::ConfirmRequired,
			TEXT("opening a map destroys the current world, its scene graph and all undo history"), TEXT("Pass confirm=true once the user agreed"));
		return Out;
	}
	RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("open level %s"), *Package));
	UGameplayStatics::OpenLevel(RuntimeToolCall::GetWorld(), FName(*Package), true, Options);
	Out.TravelTo = Package;
	Out.Note = TEXT("travel happens next frame; call session.ping until the new map reports in session.get_build_info");
	return Out;
}
