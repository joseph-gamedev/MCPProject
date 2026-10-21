#include "LevelToolset.h"
#include "RuntimeToolHelpers.h"
#include "Containers/Ticker.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

using namespace RuntimeTools;

namespace
{
	FString StreamingName(const ULevelStreaming* L)
	{
		return L->GetWorldAssetPackageName();
	}

	/** Finds a streaming level by package name, short name or instance name. */
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
		return nullptr;
	}

	TSharedRef<FJsonObject> StreamingJson(const ULevelStreaming* L)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("name"), FPackageName::GetShortName(StreamingName(L)));
		J->SetStringField(TEXT("package"), StreamingName(L));
		J->SetBoolField(TEXT("loaded"), L->IsLevelLoaded());
		J->SetBoolField(TEXT("visible"), L->IsLevelVisible());
		J->SetBoolField(TEXT("should_be_loaded"), L->ShouldBeLoaded());
		if (L->IsA<ULevelStreamingDynamic>())
		{
			J->SetBoolField(TEXT("instance"), true);
		}
		return J;
	}

	const TCHAR* StateName(EDataLayerRuntimeState State)
	{
		return State == EDataLayerRuntimeState::Activated ? TEXT("activated") : State == EDataLayerRuntimeState::Loaded ? TEXT("loaded") : TEXT("unloaded");
	}

	/** Validates a map path like /Game/Maps/Sandbox (no object suffix). */
	bool ReadMap(FToolInvocation& Inv, FString& OutPackage)
	{
		OutPackage = Inv.GetString(TEXT("map"));
		if (OutPackage.Contains(TEXT(".")))
		{
			OutPackage = FPackageName::ObjectPathToPackageName(OutPackage);
		}
		if (!FPackageName::IsValidLongPackageName(OutPackage))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is not a map package path"), *OutPackage), TEXT("Use /Game/Maps/MyMap"));
			return false;
		}
		if (!FPackageName::DoesPackageExist(OutPackage))
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("map '%s' not found (is it cooked?)"), *OutPackage));
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

	/** Completes Inv once the streaming level is loaded and visible (or times out). */
	void WaitForStreaming(FToolInvocation& Inv, ULevelStreaming* Level, TSharedRef<FJsonObject> Out)
	{
		TSharedRef<FToolInvocation> Self = Inv.AsShared();
		TWeakObjectPtr<ULevelStreaming> Weak = Level;
		FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, Weak, Out](float)
		{
			if (Self->IsComplete())
			{
				return false;
			}
			if (!Weak.IsValid())
			{
				Self->Fail(ERuntimeToolError::Internal, TEXT("streaming level vanished while loading"));
				return false;
			}
			if (Weak->IsLevelLoaded() && Weak->IsLevelVisible())
			{
				Out->SetObjectField(TEXT("level"), StreamingJson(Weak.Get()));
				Self->Succeed(Out);
				return false;
			}
			Self->ReportProgress(Weak->IsLevelLoaded() ? 0.8f : 0.3f, Weak->IsLevelLoaded() ? TEXT("making visible") : TEXT("loading"));
			return true;
		}), 0.05f);
		Inv.SetCancelHandler([Handle]() { FTSTicker::GetCoreTicker().RemoveTicker(Handle); });
		Inv.Defer();
	}
}

static void ListLevels(FToolInvocation& Inv)
{
	UWorld* World = Inv.GetWorld();
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("persistent"), World->PersistentLevel ? World->PersistentLevel->GetOutermost()->GetName() : World->GetMapName());
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const ULevelStreaming* L : World->GetStreamingLevels())
	{
		if (L)
		{
			Items.Add(MakeShared<FJsonValueObject>(StreamingJson(L)));
		}
	}
	Out->SetArrayField(TEXT("streaming"), Items);
	if (UDataLayerManager* Manager = World->GetDataLayerManager())
	{
		TSharedRef<FJsonObject> Layers = Obj();
		for (UDataLayerInstance* Layer : Manager->GetDataLayerInstances())
		{
			if (Layer)
			{
				Layers->SetStringField(Layer->GetDataLayerShortName(), StateName(Manager->GetDataLayerInstanceRuntimeState(Layer)));
			}
		}
		Out->SetObjectField(TEXT("data_layers"), Layers);
	}
	Inv.Succeed(Out);
}

static void LoadLevelInstance(FToolInvocation& Inv)
{
	FString Package;
	if (!ReadMap(Inv, Package))
	{
		return;
	}
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	Inv.GetVector(TEXT("location"), Location);
	Inv.GetRotator(TEXT("rotation"), Rotation);
	TUniquePtr<FLoadInstanceCommand> Cmd = MakeUnique<FLoadInstanceCommand>(Package, Location, Rotation, Inv.GetString(TEXT("name")));
	FLoadInstanceCommand* Raw = Cmd.Get();
	if (!Inv.RunCommand(MoveTemp(Cmd)))
	{
		return;
	}
	ULevelStreamingDynamic* Level = Raw->Streaming.Get();
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("level"), Level ? StreamingName(Level) : Package);
	if (Inv.GetBool(TEXT("blocking"), false))
	{
		// Loads synchronously (frame hitch); also works in worlds that are not ticking.
		Inv.GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		Out->SetObjectField(TEXT("level"), StreamingJson(Level));
		Inv.Succeed(Out);
		return;
	}
	WaitForStreaming(Inv, Level, Out);
}

static ULevelStreaming* ResolveStreaming(FToolInvocation& Inv)
{
	const FString Name = Inv.GetString(TEXT("level"));
	ULevelStreaming* Level = FindStreaming(Inv.GetWorld(), Name);
	if (!Level)
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no streaming level '%s'"), *Name), TEXT("See level.list_levels"));
	}
	return Level;
}

static void SetLevelState(FToolInvocation& Inv, bool bLoadedArg)
{
	ULevelStreaming* Level = ResolveStreaming(Inv);
	if (!Level)
	{
		return;
	}
	TWeakObjectPtr<ULevelStreaming> Weak = Level;
	const bool bOldLoaded = Level->ShouldBeLoaded();
	const bool bOldVisible = Level->ShouldBeVisible();
	bool bNewLoaded = bOldLoaded;
	bool bNewVisible = bOldVisible;
	if (bLoadedArg)
	{
		bNewLoaded = false; // unload_level
		bNewVisible = false;
	}
	else
	{
		bNewVisible = Inv.GetBool(TEXT("visible"), true);
		bNewLoaded = bNewLoaded || bNewVisible;
	}
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("level"), StreamingName(Level));
	Delta->SetBoolField(TEXT("should_be_loaded"), bNewLoaded);
	Delta->SetBoolField(TEXT("should_be_visible"), bNewVisible);
	auto Set = [Weak](bool bLoaded, bool bVisible)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->SetShouldBeLoaded(bLoaded);
		Weak->SetShouldBeVisible(bVisible);
		return true;
	};
	auto DoFn = [Set, bNewLoaded, bNewVisible](FRuntimeCommandContext&, FString& Error)
	{
		if (!Set(bNewLoaded, bNewVisible))
		{
			Error = TEXT("streaming level no longer exists");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, bOldLoaded, bOldVisible](FRuntimeCommandContext&) { return Set(bOldLoaded, bOldVisible); };
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(bLoadedArg ? TEXT("unload level") : TEXT("level visibility"), DoFn, UndoFn, Delta)))
	{
		if (Inv.GetBool(TEXT("blocking"), false))
		{
			Inv.GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		}
		Inv.Succeed(Delta);
	}
}

static void SetDataLayer(FToolInvocation& Inv)
{
	UDataLayerManager* Manager = Inv.GetWorld()->GetDataLayerManager();
	if (!Manager)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("this world has no data layers (not World Partition)"));
		return;
	}
	const FString Name = Inv.GetString(TEXT("name"));
	const UDataLayerInstance* Layer = nullptr;
	for (UDataLayerInstance* L : Manager->GetDataLayerInstances())
	{
		if (L && (L->GetDataLayerShortName().Equals(Name, ESearchCase::IgnoreCase) || L->GetFName().ToString().Equals(Name, ESearchCase::IgnoreCase)))
		{
			Layer = L;
			break;
		}
	}
	if (!Layer)
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no data layer '%s'"), *Name), TEXT("See level.list_levels"));
		return;
	}
	const FString StateText = Inv.GetString(TEXT("state"));
	const EDataLayerRuntimeState New = StateText == TEXT("activated") ? EDataLayerRuntimeState::Activated
		: StateText == TEXT("loaded") ? EDataLayerRuntimeState::Loaded : EDataLayerRuntimeState::Unloaded;
	const EDataLayerRuntimeState Old = Manager->GetDataLayerInstanceRuntimeState(Layer);
	TWeakObjectPtr<UDataLayerManager> WeakManager = Manager;
	TWeakObjectPtr<const UDataLayerInstance> WeakLayer = Layer;
	const bool bRecursive = Inv.GetBool(TEXT("recursive"), false);
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("data_layer"), Layer->GetDataLayerShortName());
	Delta->SetStringField(TEXT("state"), StateName(New));
	auto Set = [WeakManager, WeakLayer, bRecursive](EDataLayerRuntimeState State)
	{
		return WeakManager.IsValid() && WeakLayer.IsValid() && WeakManager->SetDataLayerInstanceRuntimeState(WeakLayer.Get(), State, bRecursive);
	};
	auto DoFn = [Set, New](FRuntimeCommandContext&, FString& Error)
	{
		if (!Set(New))
		{
			Error = TEXT("the data layer manager refused the state change");
			return false;
		}
		return true;
	};
	auto UndoFn = [Set, Old](FRuntimeCommandContext&) { return Set(Old); };
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("data layer state"), DoFn, UndoFn, Delta)))
	{
		Delta->SetStringField(TEXT("previous"), StateName(Old));
		Inv.Succeed(Delta);
	}
}

static void OpenLevel(FToolInvocation& Inv)
{
	FString Package;
	if (!ReadMap(Inv, Package))
	{
		return;
	}
	if (!Inv.GetBool(TEXT("confirm"), false))
	{
		Inv.Fail(ERuntimeToolError::ConfirmRequired, TEXT("opening a map destroys the current world, its scene graph and all undo history; pass confirm=true"));
		return;
	}
	Inv.RecordIrreversible(FString::Printf(TEXT("open level %s"), *Package));
	UGameplayStatics::OpenLevel(Inv.GetWorld(), FName(*Package), true, Inv.GetString(TEXT("options")));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("travel_to"), Package);
	Out->SetStringField(TEXT("note"), TEXT("travel happens next frame; call session.ping until the new map reports in session.get_build_info"));
	Out->SetBoolField(TEXT("undoable"), false);
	Inv.Succeed(Out);
}

void FLevelToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Map = []() { return FToolParam::String(TEXT("Map package, e.g. /Game/Maps/Sandbox"), 512).Required(); };
	const auto Blocking = []() { return FToolParam::Boolean(TEXT("Finish now (frame hitch) instead of waiting asynchronously")).Default(false); };

	B.Tool(TEXT("list_levels"), TEXT("Persistent level, streaming levels (loaded/visible) and data layer states."))
		.RequiresWorld()
		.Handle(&ListLevels);

	B.Tool(TEXT("load_level_instance"), TEXT("Stream a map in as an instance at a transform. Completes when visible."))
		.Args(FToolSchema()
			.Add(TEXT("map"), Map())
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]")))
			.Add(TEXT("name"), FToolParam::String(TEXT("Optional unique instance name"), 128))
			.Add(TEXT("blocking"), Blocking()))
		.Undoable().RequiresWorld()
		.Handle(&LoadLevelInstance);

	B.Tool(TEXT("unload_level"), TEXT("Unload a streaming level or instance (undo loads it back)."))
		.Args(FToolSchema().Add(TEXT("level"), FToolParam::String(TEXT("Package or short name from list_levels"), 512).Required()).Add(TEXT("blocking"), Blocking()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetLevelState(Inv, true); });

	B.Tool(TEXT("set_level_visibility"), TEXT("Show or hide a streaming level (showing also loads it)."))
		.Args(FToolSchema()
			.Add(TEXT("level"), FToolParam::String(TEXT("Package or short name from list_levels"), 512).Required())
			.Add(TEXT("visible"), FToolParam::Boolean(TEXT("Visible")).Required())
			.Add(TEXT("blocking"), Blocking()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { SetLevelState(Inv, false); });

	B.Tool(TEXT("set_data_layer"), TEXT("Set a World Partition data layer to unloaded, loaded or activated."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Data layer name"), 256).Required())
			.Add(TEXT("state"), FToolParam::Enum(TEXT("Runtime state"), {TEXT("unloaded"), TEXT("loaded"), TEXT("activated")}).Required())
			.Add(TEXT("recursive"), FToolParam::Boolean(TEXT("Apply to child layers")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle(&SetDataLayer);

	B.Tool(TEXT("open_level"), TEXT("Travel to another map. Destroys the world and all undo history; needs confirm."))
		.Args(FToolSchema()
			.Add(TEXT("map"), Map())
			.Add(TEXT("options"), FToolParam::String(TEXT("URL options, e.g. ?game=/Script/Engine.GameModeBase"), 512))
			.Add(TEXT("confirm"), FToolParam::Confirm()))
		.Irreversible().RequiresWorld()
		.Handle(&OpenLevel);
}
