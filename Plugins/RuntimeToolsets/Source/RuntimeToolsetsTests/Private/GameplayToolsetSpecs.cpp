#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"

namespace
{
	AActor* GameplayNodeActor(FToolTestHarness& H, const FString& Id)
	{
		const FRuntimeSceneNode* N = H.GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}

	FBox GameplayBox(AActor* A)
	{
		FVector O, E;
		A->GetActorBounds(false, O, E, true);
		return FBox(O - E, O + E);
	}

	/** Invokes a tool through the real queue (async tools cannot run inline) and runs Check on completion. */
	void GameplayInvokeLatent(FToolTestHarness& H, const FString& Tool, const FString& Json, const FDoneDelegate& Done, TFunction<void(const FToolResult&)> Check)
	{
		FToolCallerContext Caller;
		Caller.SessionId = H.GetSession();
		IRuntimeToolsetsCore::Get().Invoke(Tool, Json, Caller, FOnToolComplete::CreateLambda([Done, Check](const FToolResult& R)
		{
			Check(R);
			Done.Execute();
		}));
	}

	/** The returnValue object of an async result. */
	TSharedPtr<FJsonObject> AsyncValue(const FToolResult& R)
	{
		const TSharedPtr<FJsonObject>* V = nullptr;
		return R.Payload.IsValid() && R.Payload->TryGetObjectField(TEXT("returnValue"), V) ? *V : MakeShared<FJsonObject>();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FGameToolsetSpec, "RuntimeToolsets.Toolsets.Game", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FGameToolsetSpec)

void FGameToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("reports game state and players", [this]()
	{
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("game.get_game_state"));
		TestEqual(TEXT("no players yet"), static_cast<int32>(S->GetNumberField(TEXT("players"))), 0);
		TestEqual(TEXT("normal speed"), S->GetNumberField(TEXT("time_dilation")), 1.0);
		H->GetWorld()->SpawnActor<APlayerController>();
		const TArray<TSharedPtr<FJsonValue>> Players = H->CallOk(TEXT("game.list_players"))->GetArrayField(TEXT("returnValue"));
		if (TestEqual(TEXT("one player"), Players.Num(), 1))
		{
			TestTrue(TEXT("controller ref"), !Players[0]->AsObject()->GetObjectField(TEXT("controller"))->GetStringField(TEXT("ref")).IsEmpty());
		}
		H->CallFails(TEXT("game.get_game_state"), TEXT("{\"bogus\":1}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("game.list_players"), TEXT("{\"bogus\":1}"), ERuntimeToolError::InvalidArguments);
	});

	It("sets time dilation and undo restores", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("game.set_time_dilation"), TEXT("{\"value\":0.5}"));
		TestEqual(TEXT("slowed"), H->GetWorld()->GetWorldSettings()->TimeDilation, 0.5f);
		TestEqual(TEXT("previous"), R->GetNumberField(TEXT("previous")), 1.0);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("normal"), H->GetWorld()->GetWorldSettings()->TimeDilation, 1.f);
		H->CallFails(TEXT("game.set_time_dilation"), TEXT("{\"value\":0}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("game.set_time_dilation"), TEXT("{\"value\":50}"), ERuntimeToolError::InvalidArguments);
	});

	It("teleports and needs a pawn when no target is given", [this]()
	{
		const FString Cube = H->SpawnCube();
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("game.teleport"), FString::Printf(TEXT("{\"target\":\"%s\",\"location\":[500,0,0],\"rotation\":[0,90,0]}"), *Cube));
		TestEqual(TEXT("reports the node"), R->GetStringField(TEXT("teleported")), Cube);
		TestTrue(TEXT("moved"), GameplayNodeActor(*H, Cube)->GetActorLocation().Equals(FVector(500, 0, 0), 0.01));
		TestTrue(TEXT("turned"), FMath::IsNearlyEqual(GameplayNodeActor(*H, Cube)->GetActorRotation().Yaw, 90.0, 0.01));
		H->CallOk(TEXT("history.undo"));
		TestTrue(TEXT("back"), GameplayNodeActor(*H, Cube)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
		H->CallFails(TEXT("game.teleport"), TEXT("{\"location\":[0,0,0]}"), ERuntimeToolError::WrongState);
	});

	It("snaps a teleported actor to the ground (Epic snap_to_ground)", [this]()
	{
		const FString Floor = H->SpawnCube(TEXT("\"location\":[0,0,-1000],\"scale\":[50,50,0.1]"));
		const FString Cube = H->SpawnCube();
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("game.teleport"), FString::Printf(TEXT("{\"target\":\"%s\",\"location\":[300,0,500],\"snap_to_ground\":true}"), *Cube));
		TestTrue(TEXT("snapped"), R->GetBoolField(TEXT("snapped_to_ground")));
		TestTrue(TEXT("resting on the floor"),
			FMath::IsNearlyEqual(GameplayBox(GameplayNodeActor(*H, Cube)).Min.Z, GameplayBox(GameplayNodeActor(*H, Floor)).Max.Z, 1.0));
		TestTrue(TEXT("x/y kept"), FMath::IsNearlyEqual(GameplayNodeActor(*H, Cube)->GetActorLocation().X, 300.0, 0.01));
	});

	It("needs a player controller to pause", [this]()
	{
		H->CallFails(TEXT("game.set_pause"), TEXT("{\"paused\":true}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("game.set_pause"), TEXT("{}"), ERuntimeToolError::InvalidArguments);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FPhysicsToolsetSpec, "RuntimeToolsets.Toolsets.Physics", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FString Cube;
	UPrimitiveComponent* Prim() const { return Cast<UPrimitiveComponent>(GameplayNodeActor(*H, Cube)->GetRootComponent()); }
END_DEFINE_SPEC(FPhysicsToolsetSpec)

void FPhysicsToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		Cube = H->SpawnCube();
	});
	AfterEach([this]() { H.Reset(); });

	It("lists collision channels and profiles (Epic get_collision_channels)", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("physics.get_collision_channels"));
		TArray<FString> Names;
		for (const TSharedPtr<FJsonValue>& V : R->GetArrayField(TEXT("channels")))
		{
			Names.Add(V->AsObject()->GetStringField(TEXT("name")));
		}
		TestTrue(TEXT("Visibility"), Names.Contains(TEXT("Visibility")));
		TestTrue(TEXT("WorldStatic"), Names.Contains(TEXT("WorldStatic")));
		TArray<FString> Profiles;
		R->TryGetStringArrayField(TEXT("profiles"), Profiles);
		TestTrue(TEXT("BlockAll profile"), Profiles.Contains(TEXT("BlockAll")));
		H->CallFails(TEXT("physics.get_collision_channels"), TEXT("{\"bogus\":1}"), ERuntimeToolError::InvalidArguments);
	});

	It("traces rays and reports the hit", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("physics.line_trace"), TEXT("{\"start\":[0,0,500],\"end\":[0,0,-500]}"));
		TestTrue(TEXT("hit"), R->GetBoolField(TEXT("hit")));
		const TSharedPtr<FJsonObject> Hit = R->GetArrayField(TEXT("hits"))[0]->AsObject();
		TestEqual(TEXT("cube"), Hit->GetStringField(TEXT("actor")), Cube);
		FVector L;
		RuntimeJson::ToVector(Hit->TryGetField(TEXT("location")), L);
		TestTrue(TEXT("top face"), FMath::IsNearlyEqual(L.Z, 50.0, 0.5));
		const TSharedRef<FJsonObject> Ignored = H->CallOk(TEXT("physics.line_trace"), FString::Printf(TEXT("{\"start\":[0,0,500],\"direction\":[0,0,-1],\"ignore\":[\"%s\"]}"), *Cube));
		TestFalse(TEXT("ignored"), Ignored->GetBoolField(TEXT("hit")));
		TestTrue(TEXT("object-type channel by snake name"),
			H->CallOk(TEXT("physics.line_trace"), TEXT("{\"start\":[0,0,500],\"end\":[0,0,-500],\"channel\":\"world_dynamic\",\"multi\":true}"))->GetBoolField(TEXT("hit"))
			|| H->CallOk(TEXT("physics.line_trace"), TEXT("{\"start\":[0,0,500],\"end\":[0,0,-500],\"channel\":\"world_static\"}"))->GetBoolField(TEXT("hit")));
		H->CallFails(TEXT("physics.line_trace"), TEXT("{\"start\":[0,0,0]}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("physics.line_trace"), TEXT("{\"start\":[0,0,0],\"end\":[0,0,-1],\"channel\":\"NoSuchChannel\"}"), ERuntimeToolError::InvalidArguments);
	});

	It("finds overlaps", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("physics.overlap_test"), TEXT("{\"center\":[0,0,0],\"radius\":10}"));
		TArray<FString> Actors;
		R->TryGetStringArrayField(TEXT("actors"), Actors);
		TestTrue(TEXT("cube overlaps"), Actors.Contains(Cube));
		const TSharedRef<FJsonObject> Far = H->CallOk(TEXT("physics.overlap_test"), TEXT("{\"center\":[5000,0,0],\"shape\":\"box\",\"size\":[10,10,10]}"));
		TestEqual(TEXT("nothing far away"), static_cast<int32>(Far->GetNumberField(TEXT("total"))), 0);
		H->CallFails(TEXT("physics.overlap_test"), TEXT("{\"center\":[0,0,0],\"shape\":\"box\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("physics.overlap_test"), TEXT("{\"center\":[0,0,0],\"channel\":\"NoSuchChannel\"}"), ERuntimeToolError::InvalidArguments);
	});

	It("simulates, pushes and undo puts the object back", [this]()
	{
		H->CallFails(TEXT("physics.add_impulse"), FString::Printf(TEXT("{\"target\":\"%s\",\"impulse\":[0,0,500]}"), *Cube), ERuntimeToolError::WrongState);
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("physics.set_simulate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube));
		TestTrue(TEXT("simulating"), Prim()->IsSimulatingPhysics());
		TestTrue(TEXT("result says simulating"), S->GetBoolField(TEXT("simulating")));
		const TSharedRef<FJsonObject> State = H->CallOk(TEXT("physics.get_physics"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube));
		TestTrue(TEXT("reported"), State->GetBoolField(TEXT("simulating")));
		TestEqual(TEXT("movable"), State->GetStringField(TEXT("mobility")), FString(TEXT("Movable")));
		TestTrue(TEXT("has mass"), State->GetNumberField(TEXT("mass_kg")) > 0.0);
		H->CallOk(TEXT("physics.add_impulse"), FString::Printf(TEXT("{\"target\":\"%s\",\"impulse\":[0,0,500]}"), *Cube));
		Prim()->SetWorldLocation(FVector(0, 0, 300), false, nullptr, ETeleportType::TeleportPhysics); // as if it had moved
		H->CallOk(TEXT("history.undo")); // skips the irreversible impulse, then undoes set_simulate
		TestFalse(TEXT("stopped"), Prim()->IsSimulatingPhysics());
		TestTrue(TEXT("back in place"), GameplayNodeActor(*H, Cube)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
		H->CallFails(TEXT("physics.get_physics"), TEXT("{\"target\":\"actor:NoSuchActorXyz\"}"), ERuntimeToolError::NotFound);
	});

	It("reports a Static component switched to Movable, and refuses components without collision", [this]()
	{
		Prim()->SetMobility(EComponentMobility::Static);
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("physics.set_simulate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube));
		TestTrue(TEXT("mobility changed"), S->GetBoolField(TEXT("mobility_changed")));
		TestTrue(TEXT("note"), S->GetStringField(TEXT("note")).Contains(TEXT("Static")));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("static again"), static_cast<int32>(Prim()->Mobility), static_cast<int32>(EComponentMobility::Static));
		Prim()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->CallFails(TEXT("physics.set_simulate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::WrongState);
	});

	It("rejects ragdoll without a skeletal mesh", [this]()
	{
		H->CallFails(TEXT("physics.ragdoll"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::InvalidArguments);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Level
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FLevelToolsetSpec, "RuntimeToolsets.Toolsets.Level", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FLevelToolsetSpec)

void FLevelToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("lists levels and the current level", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("level.list_levels"));
		TestEqual(TEXT("no streaming levels"), R->GetArrayField(TEXT("streaming")).Num(), 0);
		TestFalse(TEXT("persistent"), R->GetStringField(TEXT("persistent")).IsEmpty());
		TestFalse(TEXT("current level"), H->CallOk(TEXT("level.get_current_level"))->GetStringField(TEXT("returnValue")).IsEmpty());
		H->CallFails(TEXT("level.list_levels"), TEXT("{\"bogus\":1}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("level.get_current_level"), TEXT("{\"bogus\":1}"), ERuntimeToolError::InvalidArguments);
	});

	It("finds maps in the asset registry", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("level.find_assets"), TEXT("{\"folder_path\":\"/Game/Maps\",\"asset_class\":\"World\"}"));
		TArray<FString> Packages;
		for (const TSharedPtr<FJsonValue>& V : R->GetArrayField(TEXT("assets")))
		{
			Packages.Add(V->AsObject()->GetStringField(TEXT("package")));
		}
		TestTrue(TEXT("Sandbox map found"), Packages.Contains(TEXT("/Game/Maps/Sandbox")));
		const TSharedRef<FJsonObject> Named = H->CallOk(TEXT("level.find_assets"), TEXT("{\"name\":\"sandbox\",\"asset_class\":\"World\"}"));
		TestTrue(TEXT("name filter"), Named->GetNumberField(TEXT("total")) >= 1);
		H->CallFails(TEXT("level.find_assets"), TEXT("{\"folder_path\":\"Game/Maps\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("level.find_assets"), TEXT("{\"asset_class\":\"NoSuchClassXyz\"}"), ERuntimeToolError::NotFound);
	});

	LatentIt("loads a map as an instance, and undo unloads it", FTimespan::FromSeconds(60), [this](const FDoneDelegate& Done)
	{
		GameplayInvokeLatent(*H, TEXT("level.load_level_instance"), TEXT("{\"map\":\"/Game/Maps/Sandbox\",\"location\":[0,0,-5000],\"blocking\":true}"), Done,
			[this](const FToolResult& R)
			{
				if (!TestTrue(FString::Printf(TEXT("ok: %s"), *R.ToJsonString()), R.IsOk()))
				{
					return;
				}
				TestTrue(TEXT("loaded"), AsyncValue(R)->GetObjectField(TEXT("level"))->GetBoolField(TEXT("loaded")));
				TestEqual(TEXT("listed"), H->CallOk(TEXT("level.list_levels"))->GetArrayField(TEXT("streaming")).Num(), 1);
				H->CallOk(TEXT("history.undo"));
				const TArray<TSharedPtr<FJsonValue>> After = H->CallOk(TEXT("level.list_levels"))->GetArrayField(TEXT("streaming"));
				TestTrue(TEXT("marked for unload"), After.Num() == 0 || !After[0]->AsObject()->GetBoolField(TEXT("should_be_loaded")));
			});
	});

	LatentIt("rejects missing maps", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		GameplayInvokeLatent(*H, TEXT("level.load_level_instance"), TEXT("{\"map\":\"/Game/Maps/NoSuchMap\"}"), Done,
			[this](const FToolResult& R) { TestTrue(TEXT("not found"), R.Error == ERuntimeToolError::NotFound); });
	});

	LatentIt("rejects map paths that are not packages", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		GameplayInvokeLatent(*H, TEXT("level.load_level_instance"), TEXT("{\"map\":\"not a path\"}"), Done,
			[this](const FToolResult& R) { TestTrue(TEXT("invalid"), R.Error == ERuntimeToolError::InvalidArguments); });
	});

	It("validates levels, layers and confirmations", [this]()
	{
		H->CallFails(TEXT("level.open_level"), TEXT("{\"map\":\"/Game/Maps/Sandbox\"}"), ERuntimeToolError::ConfirmRequired);
		H->CallFails(TEXT("level.open_level"), TEXT("{\"map\":\"not a path\",\"confirm\":true}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("level.unload_level"), TEXT("{\"level\":\"Nope\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("level.set_level_visibility"), TEXT("{\"level\":\"Nope\",\"visible\":true}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("level.set_data_layer"), TEXT("{\"name\":\"X\",\"state\":\"loaded\"}"), ERuntimeToolError::Unsupported);
		H->CallFails(TEXT("level.set_data_layer"), TEXT("{\"name\":\"X\",\"state\":\"sideways\"}"), ERuntimeToolError::InvalidArguments);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FInputToolsetSpec, "RuntimeToolsets.Toolsets.Input", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FInputToolsetSpec)

void FInputToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	// The isolated test world has no local player; the packaged smoke test covers real injection.
	It("needs a local player for mapping contexts", [this]()
	{
		H->CallFails(TEXT("input.add_mapping_context"), TEXT("{\"context\":\"/Game/Input/IMC.IMC\"}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("input.remove_mapping_context"), TEXT("{\"context\":\"/Game/Input/IMC.IMC\"}"), ERuntimeToolError::WrongState);
	});

	LatentIt("needs a local player to inject keys", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		GameplayInvokeLatent(*H, TEXT("input.inject_key"), TEXT("{\"key\":\"SpaceBar\",\"event\":\"press\"}"), Done,
			[this](const FToolResult& R) { TestTrue(TEXT("wrong state"), R.Error == ERuntimeToolError::WrongState); });
	});

	LatentIt("needs a local player to inject actions", FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
	{
		GameplayInvokeLatent(*H, TEXT("input.inject_action"), TEXT("{\"action\":\"/Game/Input/IA_Jump.IA_Jump\"}"), Done,
			[this](const FToolResult& R) { TestTrue(TEXT("wrong state"), R.Error == ERuntimeToolError::WrongState); });
	});
}

#endif
