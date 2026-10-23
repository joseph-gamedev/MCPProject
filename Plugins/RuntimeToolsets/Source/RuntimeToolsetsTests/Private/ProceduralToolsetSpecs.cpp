#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

static int32 GisRound(double V) { return static_cast<int32>(FMath::RoundToDouble(V)); }

BEGIN_DEFINE_SPEC(FProceduralToolsetSpec, "RuntimeToolsets.Toolsets.Procedural", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	AActor* NodeActor(const FString& Id)
	{
		const FRuntimeSceneNode* N = H->GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}
	/** returnValue of a reflected tool call. */
	TSharedPtr<FJsonObject> Ret(const TCHAR* Tool, const FString& Args)
	{
		return H->CallOk(Tool, Args)->GetObjectField(TEXT("returnValue"));
	}
	FString Ref(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		return O->GetObjectField(Field)->GetStringField(TEXT("ref"));
	}
END_DEFINE_SPEC(FProceduralToolsetSpec)

void FProceduralToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("generates schemas from the doc comments (Epic toolset style)", [this]()
	{
		const TSharedPtr<FJsonObject> D = RuntimeJson::Parse(IRuntimeToolsetsCore::Get().DescribeToolset(TEXT("pcg"), true));
		TSharedPtr<FJsonObject> AddNode;
		for (const TSharedPtr<FJsonValue>& V : D->GetArrayField(TEXT("tools")))
		{
			if (V->AsObject()->GetStringField(TEXT("name")) == TEXT("pcg.add_node"))
			{
				AddNode = V->AsObject();
			}
		}
		TestTrue(TEXT("add_node exists"), AddNode.IsValid());
		if (AddNode.IsValid())
		{
			const TSharedPtr<FJsonObject> In = AddNode->GetObjectField(TEXT("input_schema"));
			TestTrue(TEXT("snake_case args"), In->GetObjectField(TEXT("properties"))->HasField(TEXT("native_node_type")));
			TestTrue(TEXT("param docs"), In->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("graph"))->HasField(TEXT("description")));
			TestFalse(TEXT("params optional"), In->GetArrayField(TEXT("required")).ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return V->AsString() == TEXT("params"); }));
			TestTrue(TEXT("output schema"), AddNode->GetObjectField(TEXT("output_schema"))->GetObjectField(TEXT("properties"))->HasField(TEXT("returnValue")));
		}
	});

	It("creates a spline with a road mesh, samples it, and undo removes it", [this]()
	{
		const TSharedPtr<FJsonObject> R = Ret(TEXT("spline.create_spline"), TEXT(R"J({"name":"road","points":[[0,0,0],[1000,0,0],[2000,500,0]],"profile":{"kind":"ribbon","width":400}})J"));
		const FString Id = Ref(R, TEXT("spline"));
		AActor* A = NodeActor(Id);
		TestTrue(TEXT("has spline"), A && A->FindComponentByClass<USplineComponent>() != nullptr);
		TestTrue(TEXT("length"), R->GetNumberField(TEXT("length_cm")) > 2000.0);
		const TSharedPtr<FJsonObject> S = Ret(TEXT("spline.sample_spline"), FString::Printf(TEXT(R"J({"spline":"%s","spacing":500})J"), *Id));
		TestTrue(TEXT("samples"), S->GetArrayField(TEXT("points")).Num() >= 5);
		H->CallFails(TEXT("spline.create_spline"), TEXT(R"J({"points":[[0,0,0],[1,0,0]],"profile":{"kind":"zigzag"}})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("spline.create_spline"), TEXT(R"J({"points":[[0,0,0]]})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("history.undo"));
		const FRuntimeSceneNode* After = H->GetGraph()->Find(Id);
		TestTrue(TEXT("undone"), !After || After->bSoftDeleted);
	});

	It("builds a PCG graph node by node, runs it on a spline and inspects node data", [this]()
	{
		const FString Path = Ref(Ret(TEXT("spline.create_spline"), TEXT(R"J({"name":"path","points":[[-4000,0,0],[0,500,0],[4000,0,0]]})J")), TEXT("spline"));
		H->CallOk(TEXT("pcg.create_graph"), TEXT(R"J({"name":"G"})J"));
		H->CallFails(TEXT("pcg.create_graph"), TEXT(R"J({"name":"G"})J"), ERuntimeToolError::InvalidArguments);
		const TArray<TSharedPtr<FJsonValue>> Nodes = H->CallOk(TEXT("pcg.list_native_nodes"))->GetArrayField(TEXT("returnValue"));
		TestTrue(TEXT("common nodes"), Nodes.ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return V->AsString() == TEXT("Spline Sampler"); }));
		const TSharedPtr<FJsonObject> Schema = Ret(TEXT("pcg.get_native_node_schema"), TEXT(R"J({"node_name":"Surface Sampler"})J"));
		TestTrue(TEXT("node params"), Schema->GetObjectField(TEXT("properties_schema"))->GetObjectField(TEXT("properties"))->HasField(TEXT("points_per_squared_meter")));

		H->CallOk(TEXT("pcg.add_node"), TEXT(R"J({"graph":"G","native_node_type":"Get Spline Data","node_name":"spline"})J"));
		const TSharedPtr<FJsonObject> Sampler = Ret(TEXT("pcg.add_node"),
			TEXT(R"J({"graph":"G","native_node_type":"Spline Sampler","node_name":"sampler","params":{"sampler_params":{"mode":"Distance","distance_increment":500}}})J"));
		TestTrue(TEXT("override recorded"), Sampler->GetObjectField(TEXT("param_overrides"))->HasField(TEXT("sampler_params")));
		H->CallFails(TEXT("pcg.add_node"), TEXT(R"J({"graph":"G","native_node_type":"Spline Sampler","node_name":"bad","params":{"nope":1}})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("pcg.connect_node_pins"), TEXT(R"J({"graph":"G","from_node":"spline","from_pin":"Out","to_node":"sampler","to_pin":"Spline"})J"));
		H->CallOk(TEXT("pcg.connect_node_pins"), TEXT(R"J({"graph":"G","from_node":"sampler","from_pin":"Out","to_node":"output","to_pin":"Out"})J"));
		H->CallFails(TEXT("pcg.connect_node_pins"), TEXT(R"J({"graph":"G","from_node":"sampler","from_pin":"Nope","to_node":"output","to_pin":"Out"})J"), ERuntimeToolError::InvalidArguments);
		TestEqual(TEXT("edges"), Ret(TEXT("pcg.get_graph_structure"), TEXT(R"J({"graph":"G"})J"))->GetArrayField(TEXT("edges")).Num(), 2);

		const FString Inst = Ref(Ret(TEXT("pcg.spawn_graph_instance"), FString::Printf(TEXT(R"J({"graph":"G","on_actor":"%s"})J"), *Path)), TEXT("actor"));
		const TSharedPtr<FJsonObject> Run = Ret(TEXT("pcg.execute_graph_instance"), FString::Printf(TEXT(R"J({"instance":"%s"})J"), *Inst));
		TestTrue(FString::Printf(TEXT("points reach the output (%d)"), static_cast<int32>(Run->GetNumberField(TEXT("output_points")))), Run->GetNumberField(TEXT("output_points")) >= 10);
		const TSharedPtr<FJsonObject> View = Ret(TEXT("pcg.get_node_data_view"), FString::Printf(TEXT(R"J({"instance":"%s","node":"sampler","end_index":3})J"), *Inst));
		const TArray<TSharedPtr<FJsonValue>> Data = View->GetArrayField(TEXT("data"));
		TestTrue(TEXT("node data"), Data.Num() > 0 && Data[0]->AsObject()->GetArrayField(TEXT("elements")).Num() == 3);

		H->CallOk(TEXT("pcg.set_graph_params"), TEXT(R"J({"graph":"G","params":[{"name":"Density","type":"Float","default_value":0.5}]})J"));
		TestEqual(TEXT("param default"), Ret(TEXT("pcg.get_graph_structure"), TEXT(R"J({"graph":"G"})J"))->GetObjectField(TEXT("graph_params"))->GetNumberField(TEXT("Density")), 0.5);
		H->CallOk(TEXT("pcg.remove_node"), TEXT(R"J({"graph":"G","node":"sampler"})J"));
		H->CallFails(TEXT("pcg.remove_node"), TEXT(R"J({"graph":"G","node":"output"})J"), ERuntimeToolError::NotAllowed);
	});

	It("scatters instances along and inside splines with PCG", [this]()
	{
		H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[20000,20000,20],"location":[0,0,-20],"name":"ground"})J"));
		const FString Tpl = H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"cone","size":[200,200,400],"location":[0,0,-5000],"name":"tree"})J"))->GetStringField(TEXT("id"));
		const FString Path = Ref(Ret(TEXT("spline.create_spline"), TEXT(R"J({"name":"path","points":[[-4000,0,0],[0,500,0],[4000,0,0]]})J")), TEXT("spline"));
		const TSharedPtr<FJsonObject> A = Ret(TEXT("pcg.scatter"), FString::Printf(TEXT(R"J({"spline":"%s","spacing":500,"offset":[300,600],"items":[{"template":"%s"}]})J"), *Path, *Tpl));
		TestTrue(FString::Printf(TEXT("along instances (%d)"), static_cast<int32>(A->GetNumberField(TEXT("instances")))), A->GetNumberField(TEXT("instances")) >= 10);
		AActor* SA = NodeActor(Ref(A, TEXT("scatter")));
		TestTrue(TEXT("instanced"), SA && SA->FindComponentByClass<UHierarchicalInstancedStaticMeshComponent>() != nullptr);

		const FString Area = Ref(Ret(TEXT("spline.create_spline"), TEXT(R"J({"name":"area","closed":true,"points":[[-3000,-3000,0],[3000,-3000,0],[3000,3000,0],[-3000,3000,0]]})J")), TEXT("spline"));
		const FString Args = FString::Printf(TEXT(R"J({"spline":"%s","mode":"Inside","spacing":600,"exclude":[{"spline":"%s","clearance":800}],"items":[{"template":"%s"}]})J"), *Area, *Path, *Tpl);
		const TSharedPtr<FJsonObject> In = Ret(TEXT("pcg.scatter"), Args);
		const double N = In->GetNumberField(TEXT("instances"));
		TestTrue(FString::Printf(TEXT("inside instances (%d)"), static_cast<int32>(N)), N >= 20);
		TestTrue(TEXT("exclusion removed some"), In->GetNumberField(TEXT("filtered_out")) > 0);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("deterministic"), Ret(TEXT("pcg.scatter"), Args)->GetNumberField(TEXT("instances")), N);
		H->CallFails(TEXT("pcg.scatter"), FString::Printf(TEXT(R"J({"spline":"%s","mode":"Inside","items":[{"template":"%s"}]})J"), *Path, *Tpl), ERuntimeToolError::WrongState);

		// A closed exclusion spline keeps its whole inside clear, not only a band along its outline.
		const FString Clearing = Ref(Ret(TEXT("spline.create_spline"), TEXT(R"J({"name":"clearing","closed":true,"points":[[-2000,-2000,0],[2000,-2000,0],[2000,2000,0],[-2000,2000,0]]})J")), TEXT("spline"));
		const TSharedPtr<FJsonObject> Cl = Ret(TEXT("pcg.scatter"), FString::Printf(TEXT(R"J({"spline":"%s","mode":"Inside","spacing":600,"exclude":[{"spline":"%s","clearance":100}],"items":[{"template":"%s"}]})J"), *Area, *Clearing, *Tpl));
		int32 InsideClearing = 0;
		if (AActor* CA = NodeActor(Ref(Cl, TEXT("scatter"))))
		{
			for (UHierarchicalInstancedStaticMeshComponent* Hism : TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*>(CA))
			{
				for (int32 i = 0; i < Hism->GetInstanceCount(); ++i)
				{
					FTransform T;
					Hism->GetInstanceTransform(i, T, true);
					InsideClearing += FMath::Abs(T.GetLocation().X) < 1800 && FMath::Abs(T.GetLocation().Y) < 1800;
				}
			}
		}
		TestEqual(TEXT("nothing inside the closed exclusion"), InsideClearing, 0);
		TestTrue(TEXT("but the rest of the area is planted"), Cl->GetNumberField(TEXT("instances")) >= 10);

		// surface: nothing is planted on top of a building standing on the ground
		const FString Ground = H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[9000,9000,20],"location":[0,0,10],"name":"plot"})J"))->GetStringField(TEXT("id"));
		H->CallOk(TEXT("build.create_primitive"), TEXT(R"J({"shape":"box","size":[3000,3000,600],"location":[0,0,30],"name":"house"})J"));
		const TSharedPtr<FJsonObject> Su = Ret(TEXT("pcg.scatter"), FString::Printf(TEXT(R"J({"spline":"%s","mode":"Inside","spacing":500,"surface":"%s","items":[{"template":"%s"}]})J"), *Area, *Ground, *Tpl));
		int32 OnRoof = 0, Planted = 0;
		if (AActor* SuA = NodeActor(Ref(Su, TEXT("scatter"))))
		{
			for (UHierarchicalInstancedStaticMeshComponent* Hism : TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*>(SuA))
			{
				for (int32 i = 0; i < Hism->GetInstanceCount(); ++i)
				{
					FTransform T;
					Hism->GetInstanceTransform(i, T, true);
					++Planted;
					OnRoof += T.GetLocation().Z > 200;
				}
			}
		}
		TestEqual(TEXT("nothing on the roof"), OnRoof, 0);
		TestTrue(FString::Printf(TEXT("the open ground is planted (%d)"), Planted), Planted >= 10);
	});
	It("gis: random plots with street frontage, own polygons, queries, subdivide, merge, export", [this]()
	{
		const TSharedPtr<FJsonObject> C = Ret(TEXT("gis.create_plots"), TEXT(R"J({"size":[30000,20000],"layout":"Grid","seed":3})J"));
		const int32 N = static_cast<int32>(C->GetNumberField(TEXT("plots")));
		TestTrue(FString::Printf(TEXT("random land has lots (%d) on blocks (%d)"), N, static_cast<int32>(C->GetNumberField(TEXT("blocks")))),
			N > 20 && C->GetNumberField(TEXT("blocks")) >= 4);
		int32 Fronting = 0;
		const TArray<TSharedPtr<FJsonValue>> All = Ret(TEXT("gis.list_plots"), TEXT(R"J({"limit":2000})J"))->GetArrayField(TEXT("plots"));
		for (const TSharedPtr<FJsonValue>& V : All)
		{
			Fronting += V->AsObject()->GetNumberField(TEXT("street_edges")) > 0 ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("lots face a street (%d of %d)"), Fronting, All.Num()), Fronting >= All.Num() * 9 / 10);

		// an L-shaped (concave) plot given clockwise, with one street edge and attributes
		H->CallOk(TEXT("gis.add_plots"), TEXT(R"J({"replace":true,"plots":[{"id":"L1","zoning":"commercial","props":{"levels":5},"street_edges":[0],
			"polygon":[[0,0,0],[0,2000,0],[1000,2000,0],[1000,1000,0],[2000,1000,0],[2000,0,0]]}]})J"));
		const TSharedPtr<FJsonObject> L = Ret(TEXT("gis.get_plot"), TEXT(R"J({"id":"L1"})J"));
		TestEqual(TEXT("area of the L (m2)"), GisRound(L->GetNumberField(TEXT("area_m2"))), 300);
		TestFalse(TEXT("the L is not convex"), L->GetBoolField(TEXT("convex")));
		int32 Streets = 0;
		for (const TSharedPtr<FJsonValue>& E : L->GetArrayField(TEXT("edges")))
		{
			Streets += E->AsObject()->GetBoolField(TEXT("street")) ? 1 : 0;
		}
		TestEqual(TEXT("one street edge survives the reorientation"), Streets, 1);
		const TSharedPtr<FJsonObject> Front = L->GetArrayField(TEXT("edges"))[static_cast<int32>(L->GetNumberField(TEXT("front")))]->AsObject();
		TestTrue(TEXT("the front is the street edge"), Front->GetBoolField(TEXT("street")));
		TestEqual(TEXT("attributes kept"), static_cast<int32>(L->GetObjectField(TEXT("props"))->GetNumberField(TEXT("levels"))), 5);
		TestEqual(TEXT("find by point inside the L"), Ret(TEXT("gis.find_plot"), TEXT(R"J({"location":[500,500,0]})J"))->GetStringField(TEXT("id")), FString(TEXT("L1")));
		H->CallFails(TEXT("gis.find_plot"), TEXT(R"J({"location":[1500,1500,0]})J"), ERuntimeToolError::NotFound);

		// a rectangle cut into 4 lots along its street, two merged back
		H->CallOk(TEXT("gis.add_plots"), TEXT(R"J({"plots":[{"id":"R1","street_edges":[0],"polygon":[[0,3000,0],[4000,3000,0],[4000,4600,0],[0,4600,0]]}]})J"));
		const TArray<TSharedPtr<FJsonValue>> Lots = Ret(TEXT("gis.subdivide_plot"), TEXT(R"J({"id":"R1","parts":4})J"))->GetArrayField(TEXT("ids"));
		TestEqual(TEXT("four lots"), Lots.Num(), 4);
		for (const TSharedPtr<FJsonValue>& Id : Lots)
		{
			const TSharedPtr<FJsonObject> I = Ret(TEXT("gis.get_plot"), FString::Printf(TEXT(R"J({"id":"%s"})J"), *Id->AsString()));
			TestEqual(TEXT("each lot is a quarter (m2)"), GisRound(I->GetNumberField(TEXT("area_m2"))), 160);
			bool bStreet = false;
			for (const TSharedPtr<FJsonValue>& E : I->GetArrayField(TEXT("edges")))
			{
				bStreet |= E->AsObject()->GetBoolField(TEXT("street"));
			}
			TestTrue(TEXT("each lot keeps street frontage"), bStreet);
		}
		const FString Merged = Ret(TEXT("gis.merge_plots"), FString::Printf(TEXT(R"J({"ids":["%s","%s"]})J"), *Lots[0]->AsString(), *Lots[1]->AsString()))
			->GetArrayField(TEXT("ids"))[0]->AsString();
		TestEqual(TEXT("merged lot (m2)"), GisRound(Ret(TEXT("gis.get_plot"), FString::Printf(TEXT(R"J({"id":"%s"})J"), *Merged))->GetNumberField(TEXT("area_m2"))), 320);
		Ret(TEXT("gis.set_plot"), TEXT(R"J({"id":"L1","zoning":"civic","props":{"name":"Town hall"}})J"));
		TestEqual(TEXT("filter by zoning"), static_cast<int32>(Ret(TEXT("gis.list_plots"), TEXT(R"J({"zoning":"civic"})J"))->GetNumberField(TEXT("total"))), 1);
		Ret(TEXT("gis.set_origin"), TEXT(R"J({"latitude":24.49,"longitude":54.6})J"));
		const TSharedPtr<FJsonObject> Fc = H->CallOk(TEXT("gis.export_geo_json"), TEXT("{}"))->GetObjectField(TEXT("returnValue"));
		TestEqual(TEXT("export has every plot (L1, two lots, the merged lot)"), Fc->GetArrayField(TEXT("features")).Num(), 4);
		H->CallOk(TEXT("gis.delete_plots"), TEXT(R"J({"all":true})J"));
		TestEqual(TEXT("all plots deleted"), static_cast<int32>(Ret(TEXT("gis.list_plots"), TEXT("{}"))->GetNumberField(TEXT("total"))), 0);
	});
}

#endif
