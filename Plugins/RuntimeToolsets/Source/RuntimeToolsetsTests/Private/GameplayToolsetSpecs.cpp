#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
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
		H->GetWorld()->SpawnActor<APlayerController>();
		TestEqual(TEXT("one player"), H->CallOk(TEXT("game.list_players"))->GetArrayField(TEXT("players")).Num(), 1);
	});

	It("sets time dilation and undo restores", [this]()
	{
		H->CallOk(TEXT("game.set_time_dilation"), TEXT("{\"value\":0.5}"));
		TestEqual(TEXT("slowed"), H->GetWorld()->GetWorldSettings()->TimeDilation, 0.5f);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("normal"), H->GetWorld()->GetWorldSettings()->TimeDilation, 1.f);
	});

	It("teleports and needs a pawn when no target is given", [this]()
	{
		const FString Cube = H->SpawnCube();
		H->CallOk(TEXT("game.teleport"), FString::Printf(TEXT("{\"target\":\"%s\",\"location\":[500,0,0],\"rotation\":[0,90,0]}"), *Cube));
		TestTrue(TEXT("moved"), GameplayNodeActor(*H, Cube)->GetActorLocation().Equals(FVector(500, 0, 0), 0.01));
		H->CallOk(TEXT("history.undo"));
		TestTrue(TEXT("back"), GameplayNodeActor(*H, Cube)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
		H->CallFails(TEXT("game.teleport"), TEXT("{\"location\":[0,0,0]}"), ERuntimeToolError::WrongState);
	});

	It("needs a player controller to pause", [this]()
	{
		H->CallFails(TEXT("game.set_pause"), TEXT("{\"paused\":true}"), ERuntimeToolError::WrongState);
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
		H->CallFails(TEXT("physics.line_trace"), TEXT("{\"start\":[0,0,0]}"), ERuntimeToolError::InvalidArguments);
	});

	It("finds overlaps", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("physics.overlap_test"), TEXT("{\"center\":[0,0,0],\"radius\":10}"));
		TArray<FString> Actors;
		R->TryGetStringArrayField(TEXT("actors"), Actors);
		TestTrue(TEXT("cube overlaps"), Actors.Contains(Cube));
		const TSharedRef<FJsonObject> Far = H->CallOk(TEXT("physics.overlap_test"), TEXT("{\"center\":[5000,0,0],\"shape\":\"box\",\"size\":[10,10,10]}"));
		TestEqual(TEXT("nothing far away"), static_cast<int32>(Far->GetNumberField(TEXT("total"))), 0);
	});

	It("simulates, pushes and undo puts the object back", [this]()
	{
		H->CallFails(TEXT("physics.add_impulse"), FString::Printf(TEXT("{\"target\":\"%s\",\"impulse\":[0,0,500]}"), *Cube), ERuntimeToolError::WrongState);
		H->CallOk(TEXT("physics.set_simulate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube));
		TestTrue(TEXT("simulating"), Prim()->IsSimulatingPhysics());
		TestTrue(TEXT("reported"), H->CallOk(TEXT("physics.get_physics"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube))->GetBoolField(TEXT("simulating")));
		H->CallOk(TEXT("physics.add_impulse"), FString::Printf(TEXT("{\"target\":\"%s\",\"impulse\":[0,0,500]}"), *Cube));
		Prim()->SetWorldLocation(FVector(0, 0, 300), false, nullptr, ETeleportType::TeleportPhysics); // as if it had moved
		H->CallOk(TEXT("history.undo")); // skips the irreversible impulse, then undoes set_simulate
		TestFalse(TEXT("stopped"), Prim()->IsSimulatingPhysics());
		TestTrue(TEXT("back in place"), GameplayNodeActor(*H, Cube)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
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

	It("lists levels", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("level.list_levels"));
		TestEqual(TEXT("no streaming levels"), R->GetArrayField(TEXT("streaming")).Num(), 0);
	});

	It("loads a map as an instance, and undo unloads it", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("level.load_level_instance"), TEXT("{\"map\":\"/Game/Maps/Sandbox\",\"location\":[0,0,-5000],\"blocking\":true}"));
		const TSharedPtr<FJsonObject> Level = R->GetObjectField(TEXT("level"));
		TestTrue(TEXT("loaded"), Level->GetBoolField(TEXT("loaded")));
		TestEqual(TEXT("listed"), H->CallOk(TEXT("level.list_levels"))->GetArrayField(TEXT("streaming")).Num(), 1);
		H->CallOk(TEXT("history.undo"));
		const TArray<TSharedPtr<FJsonValue>> After = H->CallOk(TEXT("level.list_levels"))->GetArrayField(TEXT("streaming"));
		TestTrue(TEXT("marked for unload"), After.Num() == 0 || !After[0]->AsObject()->GetBoolField(TEXT("should_be_loaded")));
	});

	It("validates maps and confirmations", [this]()
	{
		H->CallFails(TEXT("level.load_level_instance"), TEXT("{\"map\":\"/Game/Maps/NoSuchMap\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("level.load_level_instance"), TEXT("{\"map\":\"not a path\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("level.open_level"), TEXT("{\"map\":\"/Game/Maps/Sandbox\"}"), ERuntimeToolError::ConfirmRequired);
		H->CallFails(TEXT("level.unload_level"), TEXT("{\"level\":\"Nope\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("level.set_data_layer"), TEXT("{\"name\":\"X\",\"state\":\"loaded\"}"), ERuntimeToolError::Unsupported);
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

	It("needs a local player", [this]()
	{
		// The isolated test world has no local player; the packaged smoke test covers real injection.
		H->CallFails(TEXT("input.inject_key"), TEXT("{\"key\":\"SpaceBar\",\"event\":\"press\"}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("input.add_mapping_context"), TEXT("{\"context\":\"/Game/Input/IMC.IMC\"}"), ERuntimeToolError::WrongState);
	});
}

#endif
