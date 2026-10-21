#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsSettings.h"
#include "Camera/CameraActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ShaderCore.h"

namespace
{
	AActor* NodeActor(FToolTestHarness& H, const FString& Id)
	{
		const FRuntimeSceneNode* N = H.GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}

	FBox Box(AActor* A)
	{
		FVector O, E;
		A->GetActorBounds(false, O, E, true);
		return FBox(O - E, O + E);
	}

	FString Targets(const TArray<FString>& Ids)
	{
		return FString::Printf(TEXT("\"targets\":[\"%s\"]"), *FString::Join(Ids, TEXT("\",\"")));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FMaterialsToolsetSpec, "RuntimeToolsets.Toolsets.Materials", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FString Cube;
	UPrimitiveComponent* Prim() const
	{
		AActor* A = NodeActor(*H, Cube);
		return A ? Cast<UPrimitiveComponent>(A->GetRootComponent()) : nullptr;
	}
END_DEFINE_SPEC(FMaterialsToolsetSpec)

void FMaterialsToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		Cube = H->SpawnCube();
	});
	AfterEach([this]() { H.Reset(); });

	It("lists slots and parameters", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("materials.list_parameters"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube));
		TestEqual(TEXT("one slot"), R->GetArrayField(TEXT("slots")).Num(), 1);
		TestTrue(TEXT("has Color"), R->GetObjectField(TEXT("vectors"))->HasField(TEXT("Color")));
		H->CallFails(TEXT("materials.list_parameters"), FString::Printf(TEXT("{\"target\":\"%s\",\"slot\":3}"), *Cube), ERuntimeToolError::InvalidArguments);
	});

	It("sets a vector parameter through a dynamic instance, and undo restores the original material", [this]()
	{
		UMaterialInterface* Original = Prim()->GetMaterial(0);
		H->CallOk(TEXT("materials.set_vector"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"color\",\"value\":[1,0,0]}"), *Cube));
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Prim()->GetMaterial(0));
		if (TestNotNull(TEXT("dynamic instance"), MID))
		{
			FLinearColor C;
			MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), C);
			TestTrue(TEXT("red"), C.Equals(FLinearColor(1, 0, 0, 1)));
		}
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("original back"), Prim()->GetMaterial(0), Original);
		H->CallOk(TEXT("history.redo"));
		TestEqual(TEXT("same instance on redo"), Prim()->GetMaterial(0), static_cast<UMaterialInterface*>(MID));
	});

	It("rejects unknown parameters and bad values", [this]()
	{
		H->CallFails(TEXT("materials.set_scalar"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Nope\",\"value\":1}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("materials.set_vector"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Color\",\"value\":[1,0]}"), *Cube), ERuntimeToolError::InvalidArguments);
	});

	It("swaps materials and undo restores", [this]()
	{
		UMaterialInterface* Original = Prim()->GetMaterial(0);
		H->CallOk(TEXT("materials.set_material"), FString::Printf(TEXT("{\"target\":\"%s\",\"material\":\"/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial\"}"), *Cube));
		TestEqual(TEXT("swapped"), Prim()->GetMaterial(0)->GetName(), FString(TEXT("WorldGridMaterial")));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("restored"), Prim()->GetMaterial(0), Original);
		H->CallFails(TEXT("materials.set_material"), FString::Printf(TEXT("{\"target\":\"%s\",\"material\":\"/Nowhere/M.M\"}"), *Cube), ERuntimeToolError::NotAllowed);
		H->CallFails(TEXT("materials.set_material"), FString::Printf(TEXT("{\"target\":\"%s\",\"material\":\"/Game/NoSuch.NoSuch\"}"), *Cube), ERuntimeToolError::NotFound);
	});

	It("reads one parameter and names the right setter on a type mismatch", [this]()
	{
		const TSharedRef<FJsonObject> P = H->CallOk(TEXT("materials.get_parameter"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"color\"}"), *Cube));
		TestEqual(TEXT("exact name"), P->GetStringField(TEXT("name")), FString(TEXT("Color")));
		TestEqual(TEXT("type"), P->GetStringField(TEXT("type")), FString(TEXT("Vector")));
		H->CallFails(TEXT("materials.get_parameter"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Nope\"}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("materials.set_scalar"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Color\",\"value\":1}"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("materials.set_texture"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Color\"}"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("materials.set_texture"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Nope\"}"), *Cube), ERuntimeToolError::NotFound);
		const TSharedRef<FJsonObject> Set = H->CallOk(TEXT("materials.set_vector"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Color\",\"value\":[0,1,0]}"), *Cube));
		TestEqual(TEXT("new value returned"), Set->GetArrayField(TEXT("vector"))[1]->AsNumber(), 1.0);
	});

	It("reports missing collections", [this]()
	{
		H->CallFails(TEXT("materials.set_collection_param"), TEXT("{\"collection\":\"/Game/NoSuch.NoSuch\",\"name\":\"X\",\"value\":1}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("materials.list_collection_parameters"), TEXT("{\"collection\":\"/Game/NoSuch.NoSuch\"}"), ERuntimeToolError::NotFound);
		const TCHAR* Style = TEXT("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style");
		URuntimeToolsetsSettings* Settings = GetMutableDefault<URuntimeToolsetsSettings>();
		const TArray<FString> Roots = Settings->AllowedObjectRoots;
		if (Roots.Num() > 0 && !Roots.Contains(TEXT("/RuntimeToolsets/")))
		{
			H->CallFails(TEXT("materials.list_collection_parameters"), FString::Printf(TEXT("{\"collection\":\"%s\"}"), Style), ERuntimeToolError::NotAllowed);
			Settings->AllowedObjectRoots.Add(TEXT("/RuntimeToolsets/"));
		}
		ON_SCOPE_EXIT { GetMutableDefault<URuntimeToolsetsSettings>()->AllowedObjectRoots = Roots; };
		if (LoadObject<UObject>(nullptr, Style))
		{
			TestTrue(TEXT("scalars listed"), H->CallOk(TEXT("materials.list_collection_parameters"), FString::Printf(TEXT("{\"collection\":\"%s\"}"), Style))
				->GetObjectField(TEXT("scalars"))->HasField(TEXT("Faceted")));
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("materials.set_collection_param"), FString::Printf(TEXT("{\"collection\":\"%s\",\"name\":\"Faceted\",\"value\":1}"), Style));
			TestTrue(TEXT("previous reported"), R->HasField(TEXT("previous")));
			H->CallOk(TEXT("history.undo"));
			H->CallFails(TEXT("materials.set_collection_param"), FString::Printf(TEXT("{\"collection\":\"%s\",\"name\":\"Faceted\",\"value\":[1,0,0]}"), Style),
				ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("materials.set_collection_param"), FString::Printf(TEXT("{\"collection\":\"%s\",\"name\":\"Nope\",\"value\":1}"), Style), ERuntimeToolError::NotFound);
		}
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FCameraToolsetSpec, "RuntimeToolsets.Toolsets.Camera", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	APlayerController* PC = nullptr;
	FVector CameraLocation() const
	{
		return PC->GetViewTarget()->GetActorLocation();
	}
END_DEFINE_SPEC(FCameraToolsetSpec)

void FCameraToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		PC = nullptr;
	});
	AfterEach([this]() { H.Reset(); });

	It("needs a player controller", [this]()
	{
		H->CallFails(TEXT("camera.get_camera"), TEXT("{}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("camera.reset_camera"), TEXT("{}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("camera.set_fly_mode"), TEXT("{}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("camera.set_fly_speed"), TEXT("{\"speed\":-5}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("camera.set_view_mode"), TEXT("{\"mode\":\"thermal\"}"), ERuntimeToolError::InvalidArguments);
	});

	It("sets fly speed, keeping what is not given", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("camera.set_fly_speed"), TEXT("{\"speed\":2500}"));
		TestEqual(TEXT("fixed"), R->GetStringField(TEXT("mode")), FString(TEXT("Fixed")));
		TestEqual(TEXT("speed"), R->GetNumberField(TEXT("fixed_speed")), 2500.0);
		const TSharedRef<FJsonObject> A = H->CallOk(TEXT("camera.set_fly_speed"), TEXT("{\"mode\":\"Auto\"}"));
		TestEqual(TEXT("auto"), A->GetStringField(TEXT("mode")), FString(TEXT("Auto")));
		TestEqual(TEXT("speed kept"), A->GetNumberField(TEXT("fixed_speed")), 2500.0);
	});

	Describe("with a player", [this]()
	{
		BeforeEach([this]()
		{
			PC = H->GetWorld()->SpawnActor<APlayerController>();
		});

		It("sets the camera and undo restores the view target", [this]()
		{
			AActor* Before = PC->GetViewTarget();
			H->CallOk(TEXT("camera.set_camera"), TEXT("{\"location\":[0,-1000,500],\"look_at\":[0,0,0],\"fov\":60}"));
			ACameraActor* Cam = Cast<ACameraActor>(PC->GetViewTarget());
			if (TestNotNull(TEXT("tool camera is view target"), Cam))
			{
				TestTrue(TEXT("location"), Cam->GetActorLocation().Equals(FVector(0, -1000, 500), 0.1));
				TestTrue(TEXT("looks at origin"), FVector::DotProduct(Cam->GetActorForwardVector(), (FVector::ZeroVector - Cam->GetActorLocation()).GetSafeNormal()) > 0.999);
			}
			const TSharedRef<FJsonObject> Get = H->CallOk(TEXT("camera.get_camera"));
			TestTrue(TEXT("reports tool camera"), Get->GetBoolField(TEXT("tool_camera")));
			TestEqual(TEXT("mode"), Get->GetStringField(TEXT("mode")), FString(TEXT("ToolCamera")));
			TestEqual(TEXT("fov"), Get->GetNumberField(TEXT("fov")), 60.0);
			H->CallOk(TEXT("history.undo"));
			TestEqual(TEXT("view target restored"), PC->GetViewTarget(), Before);
			H->CallFails(TEXT("camera.set_camera"), TEXT("{\"location\":[0,0,0],\"look_at\":[0,0,0]}"), ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("camera.set_camera"), TEXT("{\"fov\":300}"), ERuntimeToolError::InvalidArguments);
		});

		It("changes the fov only", [this]()
		{
			H->CallOk(TEXT("camera.set_camera"), TEXT("{\"location\":[0,-500,200],\"look_at\":[0,0,0]}"));
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("camera.set_fov"), TEXT("{\"fov\":40}"));
			TestEqual(TEXT("fov"), R->GetNumberField(TEXT("fov")), 40.0);
			TestTrue(TEXT("pose kept"), CameraLocation().Equals(FVector(0, -500, 200), 0.1));
			H->CallFails(TEXT("camera.set_fov"), TEXT("{\"fov\":1}"), ERuntimeToolError::InvalidArguments);
			H->CallOk(TEXT("camera.set_fly_mode"), TEXT("{\"enabled\":false}"));
		});

		It("frames nodes so they fit", [this]()
		{
			const FString A = H->SpawnCube(TEXT("\"location\":[-500,0,0]"));
			const FString B = H->SpawnCube(TEXT("\"location\":[500,0,0]"));
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("camera.frame_nodes"), FString::Printf(TEXT("{%s,\"direction\":[0,1,0],\"fov\":90}"), *Targets({A, B})));
			TestTrue(TEXT("distance"), R->GetNumberField(TEXT("distance")) > 500.0);
			TestTrue(TEXT("camera in front"), CameraLocation().Y < -500.0);
			H->CallFails(TEXT("camera.frame_nodes"), TEXT("{\"targets\":[]}"), ERuntimeToolError::InvalidArguments);
		});

		It("orbits around a point keeping distance", [this]()
		{
			H->CallOk(TEXT("camera.set_camera"), TEXT("{\"location\":[1000,0,0],\"look_at\":[0,0,0]}"));
			H->CallOk(TEXT("camera.orbit"), TEXT("{\"center\":[0,0,0],\"yaw\":90}"));
			const FVector L = CameraLocation();
			TestTrue(TEXT("moved to +Y"), L.Equals(FVector(0, 1000, 0), 1.0));
			H->CallFails(TEXT("camera.orbit"), TEXT("{\"yaw\":10}"), ERuntimeToolError::InvalidArguments);
		});

		It("resets to the player and possesses pawns", [this]()
		{
			H->CallOk(TEXT("camera.set_camera"), TEXT("{\"location\":[0,0,500]}"));
			H->CallOk(TEXT("camera.reset_camera"));
			TestEqual(TEXT("back to controller"), PC->GetViewTarget(), static_cast<AActor*>(PC));
			const FString Pawn = H->CallOk(TEXT("actors.spawn_actor"), TEXT("{\"class\":\"/Script/Engine.DefaultPawn\"}"))->GetStringField(TEXT("id"));
			H->CallOk(TEXT("camera.possess"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Pawn));
			TestEqual(TEXT("possessed"), static_cast<AActor*>(PC->GetPawn()), NodeActor(*H, Pawn));
			H->CallOk(TEXT("history.undo"));
			TestNull(TEXT("unpossessed"), PC->GetPawn());
			const FString Cube = H->SpawnCube();
			H->CallFails(TEXT("camera.possess"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::InvalidArguments);
		});
	});

	It("sets view mode when a viewport exists", [this]()
	{
		const bool bViewport = GEngine && GEngine->GameViewport;
		const FToolResult R = H->Call(TEXT("camera.set_view_mode"), TEXT("{\"mode\":\"unlit\"}"));
		if (!bViewport)
		{
			TestTrue(TEXT("unsupported"), R.Error == ERuntimeToolError::Unsupported);
			return;
		}
		if (!AllowDebugViewmodes())
		{
			// Cooked builds strip debug view modes unless r.ForceDebugViewModes=1; only lit is available.
			TestTrue(TEXT("unsupported without debug view modes"), R.Error == ERuntimeToolError::Unsupported);
			H->CallOk(TEXT("camera.set_view_mode"), TEXT("{\"mode\":\"lit\"}"));
			return;
		}
		TestTrue(TEXT("ok"), R.IsOk());
		TestEqual(TEXT("unlit"), GEngine->GameViewport->ViewModeIndex, static_cast<int32>(VMI_Unlit));
		H->CallOk(TEXT("history.undo"));
		TestNotEqual(TEXT("restored"), GEngine->GameViewport->ViewModeIndex, static_cast<int32>(VMI_Unlit));
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FLayoutToolsetSpec, "RuntimeToolsets.Toolsets.Layout", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	TArray<FString> Cubes;
	AActor* At(int32 i) const { return NodeActor(*H, Cubes[i]); }
END_DEFINE_SPEC(FLayoutToolsetSpec)

void FLayoutToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		Cubes = {H->SpawnCube(TEXT("\"location\":[0,0,0]")), H->SpawnCube(TEXT("\"location\":[100,40,200]")), H->SpawnCube(TEXT("\"location\":[1000,0,-50]"))};
	});
	AfterEach([this]() { H.Reset(); });

	It("aligns on an axis, as one undo step", [this]()
	{
		H->CallOk(TEXT("layout.align_nodes"), FString::Printf(TEXT("{%s,\"axis\":\"z\",\"mode\":\"min\"}"), *Targets(Cubes)));
		TestTrue(TEXT("same min z"), FMath::IsNearlyEqual(Box(At(0)).Min.Z, Box(At(1)).Min.Z, 0.01) && FMath::IsNearlyEqual(Box(At(1)).Min.Z, Box(At(2)).Min.Z, 0.01));
		TestTrue(TEXT("aligned to selection min"), FMath::IsNearlyEqual(Box(At(2)).Min.Z, -100.0, 0.01));
		H->CallOk(TEXT("history.undo"));
		TestTrue(TEXT("restored"), At(1)->GetActorLocation().Equals(FVector(100, 40, 200), 0.01));
		H->CallOk(TEXT("layout.align_nodes"), FString::Printf(TEXT("{%s,\"axis\":\"y\",\"value\":300}"), *Targets(Cubes)));
		TestTrue(TEXT("to value"), FMath::IsNearlyEqual(At(0)->GetActorLocation().Y, 300.0, 0.01));
	});

	It("distributes evenly and by gap", [this]()
	{
		H->CallOk(TEXT("layout.distribute_nodes"), FString::Printf(TEXT("{%s,\"axis\":\"x\"}"), *Targets(Cubes)));
		TestTrue(TEXT("middle centred"), FMath::IsNearlyEqual(At(1)->GetActorLocation().X, 500.0, 0.01));
		H->CallOk(TEXT("layout.distribute_nodes"), FString::Printf(TEXT("{%s,\"axis\":\"x\",\"gap\":50}"), *Targets(Cubes)));
		TestTrue(TEXT("gap 1"), FMath::IsNearlyEqual(At(1)->GetActorLocation().X, 150.0, 0.01));
		TestTrue(TEXT("gap 2"), FMath::IsNearlyEqual(At(2)->GetActorLocation().X, 300.0, 0.01));
		H->CallFails(TEXT("layout.distribute_nodes"), FString::Printf(TEXT("{%s}"), *Targets({Cubes[0]})), ERuntimeToolError::InvalidArguments);
	});

	It("snaps to a grid", [this]()
	{
		H->CallOk(TEXT("actors.set_transform"), FString::Printf(TEXT("{\"target\":\"%s\",\"location\":[130,260,7],\"rotation\":[0,37,0]}"), *Cubes[0]));
		H->CallOk(TEXT("layout.snap_to_grid"), FString::Printf(TEXT("{%s,\"size\":100,\"rotation_step\":45}"), *Targets({Cubes[0]})));
		TestTrue(TEXT("snapped"), At(0)->GetActorLocation().Equals(FVector(100, 300, 7), 0.01));
		TestTrue(TEXT("yaw snapped"), FMath::IsNearlyEqual(At(0)->GetActorRotation().Yaw, 45.0, 0.01));
	});

	It("drops onto the ground", [this]()
	{
		const FString Floor = H->SpawnCube(TEXT("\"location\":[0,0,-1000],\"scale\":[50,50,0.1]"));
		H->CallOk(TEXT("actors.set_transform"), FString::Printf(TEXT("{\"target\":\"%s\",\"location\":[0,0,500]}"), *Cubes[0]));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("layout.drop_to_ground"), FString::Printf(TEXT("{%s}"), *Targets({Cubes[0]})));
		TestEqual(TEXT("moved"), static_cast<int32>(R->GetNumberField(TEXT("moved"))), 1);
		TestTrue(TEXT("resting on floor"), FMath::IsNearlyEqual(Box(At(0)).Min.Z, Box(NodeActor(*H, Floor)).Max.Z, 1.0));
	});

	It("arranges in a row, grid and circle", [this]()
	{
		H->CallOk(TEXT("layout.arrange_in_row"), FString::Printf(TEXT("{%s,\"start\":[0,0,0],\"gap\":50}"), *Targets(Cubes)));
		TestTrue(TEXT("row 2"), At(1)->GetActorLocation().Equals(FVector(150, 0, 0), 0.01));
		TestTrue(TEXT("row 3"), At(2)->GetActorLocation().Equals(FVector(300, 0, 0), 0.01));

		H->CallOk(TEXT("layout.arrange_in_grid"), FString::Printf(TEXT("{%s,\"origin\":[0,0,0],\"columns\":2,\"gap\":0}"), *Targets(Cubes)));
		TestTrue(TEXT("grid 2"), At(1)->GetActorLocation().Equals(FVector(100, 0, 0), 0.01));
		TestTrue(TEXT("grid 3"), At(2)->GetActorLocation().Equals(FVector(0, 100, 0), 0.01));

		H->CallOk(TEXT("layout.arrange_in_circle"), FString::Printf(TEXT("{%s,\"center\":[0,0,0],\"radius\":500,\"facing\":\"center\"}"), *Targets(Cubes)));
		for (int32 i = 0; i < 3; ++i)
		{
			TestTrue(TEXT("on circle"), FMath::IsNearlyEqual(FVector2D(At(i)->GetActorLocation()).Size(), 500.0, 0.5));
		}
		TestTrue(TEXT("faces centre"), FVector::DotProduct(At(0)->GetActorForwardVector(), -At(0)->GetActorLocation().GetSafeNormal2D()) > 0.99);
	});

	It("randomizes deterministically and undoes", [this]()
	{
		const FString Args = FString::Printf(TEXT("{%s,\"location_jitter\":[100,100,0],\"yaw_jitter\":90,\"seed\":7}"), *Targets(Cubes));
		H->CallOk(TEXT("layout.randomize_transform"), Args);
		const FVector First = At(0)->GetActorLocation();
		H->CallOk(TEXT("history.undo"));
		TestTrue(TEXT("undone"), At(0)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
		H->CallOk(TEXT("layout.randomize_transform"), Args);
		TestTrue(TEXT("same seed, same result"), At(0)->GetActorLocation().Equals(First, 0.01));
	});

	It("fails clearly for every tool", [this]()
	{
		const FString T = Targets(Cubes);
		H->CallFails(TEXT("layout.align_nodes"), TEXT("{\"targets\":[]}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.align_nodes"), FString::Printf(TEXT("{%s,\"mode\":\"middle\"}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.align_nodes"), FString::Printf(TEXT("{%s,\"axis\":\"w\"}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.snap_to_grid"), FString::Printf(TEXT("{%s,\"size\":0}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.drop_to_ground"), FString::Printf(TEXT("{%s,\"max_distance\":0}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.arrange_in_row"), FString::Printf(TEXT("{%s,\"direction\":[0,0,0]}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.arrange_in_grid"), FString::Printf(TEXT("{%s,\"columns\":-1}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.arrange_in_circle"), FString::Printf(TEXT("{%s,\"arc\":0}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.randomize_transform"), FString::Printf(TEXT("{%s,\"scale_jitter\":2}"), *T), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("layout.find_free_space"), TEXT("{\"size\":[0,100,100]}"), ERuntimeToolError::InvalidArguments);
		TestTrue(TEXT("nothing moved"), At(1)->GetActorLocation().Equals(FVector(100, 40, 200), 0.01));
	});

	It("aligns to another object and reports targets with no ground", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("layout.align_nodes"), FString::Printf(TEXT("{%s,\"axis\":\"x\",\"mode\":\"max\",\"to\":\"%s\"}"),
			*Targets({Cubes[0], Cubes[1]}), *Cubes[2]));
		TestEqual(TEXT("aligned to the third cube's max x"), R->GetNumberField(TEXT("aligned_to")), 1050.0);
		const TSharedRef<FJsonObject> D = H->CallOk(TEXT("layout.drop_to_ground"), FString::Printf(TEXT("{%s,\"max_distance\":10}"), *Targets({Cubes[1]})));
		TestEqual(TEXT("no ground within 10 cm"), D->GetArrayField(TEXT("no_ground")).Num(), 1);
	});

	It("finds free space next to an obstacle", [this]()
	{
		// Off the ground: the origin is taken by the first cube, so the nearest free cell is one step away.
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("layout.find_free_space"), TEXT("{\"size\":[100,100,100],\"near\":[0,0,0],\"radius\":2000,\"on_ground\":false}"));
		TestTrue(TEXT("found"), R->GetBoolField(TEXT("found")));
		FVector C;
		RuntimeJson::ToVector(R->TryGetField(TEXT("center")), C);
		TestTrue(TEXT("away from the cubes at the origin"), FVector2D(C).Size() >= 100.0);
	});
}

#endif
