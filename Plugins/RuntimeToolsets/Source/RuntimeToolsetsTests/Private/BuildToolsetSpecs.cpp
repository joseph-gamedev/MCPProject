#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/App.h"

namespace
{
	AActor* BuildNodeActor(FToolTestHarness& H, const FString& Id)
	{
		const FRuntimeSceneNode* N = H.GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}

	FBox BuildBox(AActor* A)
	{
		FVector O, E;
		A->GetActorBounds(false, O, E, true);
		return FBox(O - E, O + E);
	}
}

BEGIN_DEFINE_SPEC(FBuildToolsetSpec, "RuntimeToolsets.Toolsets.Build", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FString Prim(const FString& Json)
	{
		return H->CallOk(TEXT("build.create_primitive"), Json)->GetStringField(TEXT("id"));
	}
END_DEFINE_SPEC(FBuildToolsetSpec)

void FBuildToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("creates every shape with geometry", [this]()
	{
		for (const TCHAR* Shape : {TEXT("box"), TEXT("sphere"), TEXT("cylinder"), TEXT("cone"), TEXT("pyramid"), TEXT("torus"), TEXT("wedge"), TEXT("capsule"),
				 TEXT("plane"), TEXT("dome"), TEXT("tube"), TEXT("arch")})
		{
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("build.create_primitive"), FString::Printf(TEXT("{\"shape\":\"%s\",\"size\":[200,120,150]}"), Shape));
			TestTrue(FString::Printf(TEXT("%s has triangles"), Shape), R->GetNumberField(TEXT("triangles")) > 0);
		}
		H->CallFails(TEXT("build.create_primitive"), TEXT("{\"shape\":\"blob\"}"), ERuntimeToolError::InvalidArguments);
	});

	It("builds at true scale with a base pivot", [this]()
	{
		const FString Box = Prim(TEXT("{\"shape\":\"box\",\"size\":[200,100,50],\"location\":[0,0,10]}"));
		const FBox B = BuildBox(BuildNodeActor(*H, Box));
		TestTrue(TEXT("size"), B.GetSize().Equals(FVector(200, 100, 50), 0.5));
		TestTrue(TEXT("sits on its location"), FMath::IsNearlyEqual(B.Min.Z, 10.0, 0.5));
		const FString Dome = Prim(TEXT("{\"shape\":\"dome\",\"size\":[300,300,150],\"pivot\":\"base\"}"));
		const FBox D = BuildBox(BuildNodeActor(*H, Dome));
		TestTrue(TEXT("dome height"), FMath::IsNearlyEqual(D.GetSize().Z, 150.0, 2.0));
		TestTrue(TEXT("dome above ground"), D.Min.Z > -1.0);
	});

	It("creates lathe, extrude and sweep solids", [this]()
	{
		H->CallOk(TEXT("build.create_profile_solid"), TEXT("{\"kind\":\"lathe\",\"profile\":[[0,0],[40,0],[30,60],[50,120],[0,130]]}"));
		H->CallOk(TEXT("build.create_profile_solid"), TEXT("{\"kind\":\"extrude\",\"profile\":[[0,0],[300,0],[300,100],[100,100],[100,300],[0,300]],\"height\":250}"));
		H->CallOk(TEXT("build.create_profile_solid"), TEXT("{\"kind\":\"sweep\",\"profile\":[[-5,-5],[5,-5],[5,5],[-5,5]],\"path\":[[0,0,0],[200,0,0],[200,200,50]]}"));
		H->CallFails(TEXT("build.create_profile_solid"), TEXT("{\"kind\":\"extrude\",\"profile\":[[0,0],[1,1]]}"), ERuntimeToolError::InvalidArguments);
	});

	It("subtracts with a boolean and undo restores both meshes", [this]()
	{
		const FString Box = Prim(TEXT("{\"shape\":\"box\",\"size\":[200,200,200],\"detail\":\"low\"}"));
		const FString Cutter = Prim(TEXT("{\"shape\":\"cylinder\",\"size\":[100,100,400],\"pivot\":\"center\",\"location\":[0,0,100]}"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("build.boolean_op"), FString::Printf(TEXT("{\"target\":\"%s\",\"tool\":\"%s\",\"op\":\"subtract\"}"), *Box, *Cutter));
		TestEqual(TEXT("one op"), static_cast<int32>(R->GetNumberField(TEXT("ops"))), 1);
		TestTrue(TEXT("more triangles after the cut"), R->GetNumberField(TEXT("triangles")) > 12);
		TestTrue(TEXT("tool consumed"), H->GetGraph()->Find(Cutter)->bSoftDeleted);
		TestTrue(TEXT("recipe records the op"), H->GetGraph()->Find(Box)->Source.Contains(TEXT("\"ops\"")));
		H->CallOk(TEXT("history.undo"));
		TestFalse(TEXT("tool back"), H->GetGraph()->Find(Cutter)->bSoftDeleted);
		TestFalse(TEXT("recipe restored"), H->GetGraph()->Find(Box)->Source.Contains(TEXT("\"ops\"")));
		H->CallFails(TEXT("build.boolean_op"), FString::Printf(TEXT("{\"target\":\"%s\",\"tool\":\"%s\"}"), *Box, *Box), ERuntimeToolError::InvalidArguments);
		const FString Cube = H->SpawnCube();
		H->CallFails(TEXT("build.boolean_op"), FString::Printf(TEXT("{\"target\":\"%s\",\"tool\":\"%s\"}"), *Cube, *Box), ERuntimeToolError::InvalidArguments);
	});

	It("arrays linearly, radially and in a grid", [this]()
	{
		const FString Column = Prim(TEXT("{\"shape\":\"cylinder\",\"size\":[40,40,300],\"location\":[500,0,0]}"));
		const TSharedRef<FJsonObject> L = H->CallOk(TEXT("build.array"), FString::Printf(TEXT("{\"target\":\"%s\",\"count\":4,\"offset\":[0,150,0]}"), *Column));
		TestEqual(TEXT("3 copies"), static_cast<int32>(L->GetNumberField(TEXT("created"))), 3);
		const TArray<TSharedPtr<FJsonValue>> Ids = L->GetArrayField(TEXT("ids"));
		TestTrue(TEXT("last offset"), BuildNodeActor(*H, Ids.Last()->AsString())->GetActorLocation().Equals(FVector(500, 450, 0), 0.1));
		H->CallOk(TEXT("history.undo"));

		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("build.array"), FString::Printf(TEXT("{\"target\":\"%s\",\"mode\":\"radial\",\"count\":8}"), *Column));
		for (const TSharedPtr<FJsonValue>& Id : R->GetArrayField(TEXT("ids")))
		{
			TestTrue(TEXT("on the circle"), FMath::IsNearlyEqual(FVector2D(BuildNodeActor(*H, Id->AsString())->GetActorLocation()).Size(), 500.0, 0.5));
		}
		const TSharedRef<FJsonObject> G = H->CallOk(TEXT("build.array"), FString::Printf(TEXT("{\"target\":\"%s\",\"mode\":\"grid\",\"counts\":[2,3,1],\"spacing\":[100,100,0]}"), *Column));
		TestEqual(TEXT("grid copies"), static_cast<int32>(G->GetNumberField(TEXT("created"))), 5);
		const FString Group = H->CallOk(TEXT("scene.create_group"), TEXT("{\"name\":\"g\"}"))->GetStringField(TEXT("id"));
		H->CallFails(TEXT("build.array"), FString::Printf(TEXT("{\"target\":\"%s\",\"count\":3}"), *Group), ERuntimeToolError::Unsupported);
	});

	It("mirrors across a plane", [this]()
	{
		const FString Wedge = Prim(TEXT("{\"shape\":\"wedge\",\"size\":[100,100,100],\"location\":[300,0,0]}"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("build.mirror"), FString::Printf(TEXT("{\"target\":\"%s\",\"axis\":\"x\"}"), *Wedge));
		AActor* Copy = BuildNodeActor(*H, R->GetArrayField(TEXT("ids"))[0]->AsString());
		TestTrue(TEXT("reflected position"), Copy->GetActorLocation().Equals(FVector(-300, 0, 0), 0.1));
		TestTrue(TEXT("reflection kept as negative scale"), Copy->GetActorScale3D().X * Copy->GetActorScale3D().Y * Copy->GetActorScale3D().Z < 0.0);
	});

	It("sets palette materials, measures and finds overlaps", [this]()
	{
		const FString A = Prim(TEXT("{\"shape\":\"box\",\"size\":[100,100,100]}"));
		const FString B = Prim(TEXT("{\"shape\":\"box\",\"size\":[100,100,100],\"location\":[50,0,0]}"));
		const FString C = Prim(TEXT("{\"shape\":\"box\",\"size\":[100,100,100],\"location\":[400,0,0]}"));
		H->CallOk(TEXT("build.set_material"), FString::Printf(TEXT("{\"targets\":[\"%s\"],\"material\":\"copper\"}"), *A));
		TestTrue(TEXT("recipe material"), H->GetGraph()->Find(A)->Source.Contains(TEXT("copper")));
		H->CallOk(TEXT("history.undo"));
		TestFalse(TEXT("material undone"), H->GetGraph()->Find(A)->Source.Contains(TEXT("copper")));
		H->CallFails(TEXT("build.set_material"), FString::Printf(TEXT("{\"targets\":[\"%s\"],\"material\":\"unobtainium\"}"), *A), ERuntimeToolError::InvalidArguments);

		const TSharedRef<FJsonObject> M = H->CallOk(TEXT("build.measure"), FString::Printf(TEXT("{\"a\":\"%s\",\"b\":\"%s\"}"), *A, *C));
		TestTrue(TEXT("gap 300"), FMath::IsNearlyEqual(M->GetNumberField(TEXT("gap")), 300.0, 0.5));
		const TSharedRef<FJsonObject> O = H->CallOk(TEXT("build.check_overlaps"));
		TestEqual(TEXT("one overlap"), static_cast<int32>(O->GetNumberField(TEXT("overlaps"))), 1);
		TestTrue(TEXT("stats"), H->CallOk(TEXT("build.get_build_stats"))->GetNumberField(TEXT("triangles")) >= 36);
	});

	It("duplicates and exports builder and vfx nodes through the source registry", [this]()
	{
		const FString Box = Prim(TEXT("{\"shape\":\"arch\",\"size\":[300,60,300],\"material\":\"sandstone\"}"));
		const TSharedRef<FJsonObject> D = H->CallOk(TEXT("scene.duplicate_node"), FString::Printf(TEXT("{\"target\":\"%s\",\"offset\":[0,500,0]}"), *Box));
		const FString Copy = D->GetStringField(TEXT("id"));
		TestEqual(TEXT("same recipe"), H->GetGraph()->Find(Copy)->Source, H->GetGraph()->Find(Box)->Source);
		const FString Fx = H->CallOk(TEXT("vfx.spawn_system"), TEXT("{\"system\":\"/Niagara/DefaultAssets/DefaultSystem.DefaultSystem\"}"))->GetStringField(TEXT("id"));
		H->CallOk(TEXT("scene.duplicate_node"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Fx));
		const TSharedRef<FJsonObject> Spec = H->CallOk(TEXT("scene.export_tree"));
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetObjectField(TEXT("tree"), Spec);
		const TSharedRef<FJsonObject> I = H->CallOk(TEXT("scene.import_tree"), RuntimeJson::Write(Args));
		TestEqual(TEXT("imported all four"), static_cast<int32>(I->GetNumberField(TEXT("nodes"))), 4);
	});

	It("captures an ortho contact sheet, or reports unsupported without rendering", [this]()
	{
		Prim(TEXT("{\"shape\":\"pyramid\",\"size\":[300,300,200]}"));
		const FToolResult R = H->Call(TEXT("build.capture_view"), TEXT("{\"size\":128}"));
		if (!FApp::CanEverRender())
		{
			TestTrue(TEXT("unsupported"), R.Error == ERuntimeToolError::Unsupported);
			return;
		}
		if (TestTrue(TEXT("ok"), R.IsOk()) && TestEqual(TEXT("one image"), R.Attachments.Num(), 1))
		{
			TestEqual(TEXT("2x2 sheet"), R.Attachments[0].Width, 256);
		}
	});
}

BEGIN_DEFINE_SPEC(FBuildAssemblySpec, "RuntimeToolsets.Toolsets.Build.Assembly", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FBuildAssemblySpec)

void FBuildAssemblySpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("builds many parts as one object with a material per distinct look, and undo removes it", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("build.create_assembly"), TEXT(R"({"name":"Kiosk","material":"sandstone","parts":[
			{"shape":"box","size":[400,400,40]},
			{"shape":"cylinder","size":[30,30,300],"location":[150,150,40],"material":"copper"},
			{"shape":"cylinder","size":[30,30,300],"location":[-150,150,40],"material":"copper"},
			{"shape":"loft","sections":[[[-200,-200,340],[200,-200,340],[200,200,340],[-200,200,340]],[[-20,-20,500],[20,-20,500],[20,20,500],[-20,20,500]]],"material":"roof_tile"},
			{"shape":"revolve","profile":[[0,0],[40,0],[30,60],[0,90]],"segments":24,"location":[0,0,500],"material":"gold"},
			{"shape":"extrude","profile":[[0,0,0],[100,0,0],[100,0,80],[0,0,80]],"direction":[0,-10,0],"location":[-50,-200,40],"material":"brick","style":"brick"},
			{"shape":"raw","vertices":[[0,0,0],[50,0,0],[0,50,0],[0,0,50]],"triangles":[[0,2,1],[0,1,3],[0,3,2],[1,2,3]],"location":[0,0,40]}
		]})"));
		TestEqual(TEXT("parts"), static_cast<int32>(R->GetNumberField(TEXT("parts"))), 7);
		TestEqual(TEXT("materials (sandstone, copper, roof_tile, gold, brick)"), static_cast<int32>(R->GetNumberField(TEXT("materials"))), 5);
		const FRuntimeSceneNode* Node = H->GetGraph()->Find(R->GetStringField(TEXT("id")));
		TestTrue(TEXT("one node"), Node && Node->Actor.IsValid());
		if (Node && Node->Actor.IsValid())
		{
			FVector O, E;
			Node->Actor->GetActorBounds(false, O, E, true);
			TestTrue(TEXT("roof apex and finial reach ~590"), FMath::IsNearlyEqual(O.Z + E.Z, 590.0, 2.0));
			TArray<UPrimitiveComponent*> Prims;
			Node->Actor->GetComponents(Prims);
			TestTrue(TEXT("material slots"), Prims.Num() > 0 && Prims[0]->GetNumMaterials() == 5);
		}
		H->CallOk(TEXT("history.undo"));
		const FRuntimeSceneNode* After = H->GetGraph()->Find(R->GetStringField(TEXT("id")));
		TestTrue(TEXT("undo removes the assembly"), !After || After->bSoftDeleted);
	});

	It("nudges overlapping same-plane faces of later parts so they do not flicker", [this]()
	{
		const FString Parts = TEXT(R"("parts":[{"shape":"box","size":[200,200,100]},{"shape":"box","size":[100,100,100],"location":[50,0,0]}])");
		const TSharedRef<FJsonObject> On = H->CallOk(TEXT("build.create_assembly"), TEXT("{") + Parts + TEXT("}"));
		TestTrue(TEXT("bottom and side faces separated"), On->GetNumberField(TEXT("coplanar_faces_separated")) >= 4);
		const TSharedRef<FJsonObject> Off = H->CallOk(TEXT("build.create_assembly"), TEXT("{\"repair_coplanar\":false,") + Parts + TEXT("}"));
		TestEqual(TEXT("repair off"), static_cast<int32>(Off->GetNumberField(TEXT("coplanar_faces_separated"))), 0);
		const TSharedRef<FJsonObject> Apart = H->CallOk(TEXT("build.create_assembly"),
			TEXT(R"({"parts":[{"shape":"box","size":[100,100,100]},{"shape":"box","size":[100,100,100],"location":[100,0,0]}]})"));
		TestEqual(TEXT("touching edges are not overlaps"), static_cast<int32>(Apart->GetNumberField(TEXT("coplanar_faces_separated"))), 0);
	});

	It("rejects bad parts with the part index", [this]()
	{
		H->CallFails(TEXT("build.create_assembly"), TEXT(R"({"parts":[{"shape":"box"},{"shape":"loft","sections":[[[0,0,0],[1,0,0],[0,1,0]],[[0,0,1]]]}]})"),
			ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("build.create_assembly"), TEXT(R"({"parts":[{"shape":"assembly","parts":[]}]})"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("build.create_assembly"), TEXT(R"({"parts":[{"shape":"raw","vertices":[[0,0,0],[1,0,0],[0,1,0]],"triangles":[[0,1,7]]}]})"),
			ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("build.create_assembly"), TEXT(R"({"parts":[{"shape":"box","style":"polka"}]})"), ERuntimeToolError::InvalidArguments);
	});

	It("applies procedural styles to single primitives", [this]()
	{
		H->CallOk(TEXT("build.create_primitive"), TEXT(R"({"shape":"box","material":"brick"})"));
		H->CallOk(TEXT("build.create_primitive"), TEXT(R"({"shape":"box","material":"plaster","style":"blocks","tile":80,"relief":1.5})"));
		H->CallFails(TEXT("build.create_primitive"), TEXT(R"({"shape":"box","style":"polka"})"), ERuntimeToolError::InvalidArguments);
	});
}

#endif
