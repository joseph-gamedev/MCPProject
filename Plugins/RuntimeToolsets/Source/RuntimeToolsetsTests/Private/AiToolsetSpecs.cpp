#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

BEGIN_DEFINE_SPEC(FAiToolsetSpec, "RuntimeToolsets.Toolsets.AI", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FString Cube(const TCHAR* Name, const FVector& At)
	{
		return H->CallOk(TEXT("build.create_primitive"), FString::Printf(TEXT(R"J({"shape":"box","size":[80,80,180],"location":[%f,%f,%f],"name":"%s"})J"), At.X, At.Y, At.Z, Name))
			->GetStringField(TEXT("id"));
	}
	TArray<FString> Strings(const TSharedRef<FJsonObject>& Payload)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FJsonValue>& V : Payload->GetArrayField(TEXT("returnValue")))
		{
			Out.Add(V->AsString());
		}
		return Out;
	}
END_DEFINE_SPEC(FAiToolsetSpec)

void FAiToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("lists, describes, adds, removes and matches gameplay tags (with undo)", [this]()
	{
		const TArray<FString> State = Strings(H->CallOk(TEXT("tags.list_tags"), TEXT(R"J({"parent_tag":"State"})J")));
		TestTrue(TEXT("starter tags registered"), State.Contains(TEXT("State.Stunned")) && State.Contains(TEXT("State.Alerted")));
		const TSharedRef<FJsonObject> Info = H->CallOk(TEXT("tags.get_tag_info"), TEXT(R"J({"tag_name":"Damage.Fire"})J"));
		TestTrue(TEXT("exists"), Info->GetBoolField(TEXT("exists")));
		TestTrue(TEXT("parent"), Info->GetArrayField(TEXT("parents")).Num() == 1);
		TestFalse(TEXT("unknown"), H->CallOk(TEXT("tags.get_tag_info"), TEXT(R"J({"tag_name":"Nope.Nothing"})J"))->GetBoolField(TEXT("exists")));

		const FString G = Cube(TEXT("guard"), FVector(0, 0, 0));
		const TArray<FString> After = Strings(H->CallOk(TEXT("tags.add_actor_tags"), FString::Printf(TEXT(R"J({"actor":"%s","tags":["State.Alerted","Team.Enemy"]})J"), *G)));
		TestEqual(TEXT("two tags"), After.Num(), 2);
		H->CallFails(TEXT("tags.add_actor_tags"), FString::Printf(TEXT(R"J({"actor":"%s","tags":["State.Bogus"]})J"), *G), ERuntimeToolError::NotFound);
		const TSharedRef<FJsonObject> M = H->CallOk(TEXT("tags.match_tags"), FString::Printf(TEXT(R"J({"actor":"%s","all":["Team"],"none":["State.Dead"]})J"), *G));
		TestTrue(TEXT("parent tag matches child"), M->GetBoolField(TEXT("matches")));
		TestFalse(TEXT("none blocks"), H->CallOk(TEXT("tags.match_tags"), FString::Printf(TEXT(R"J({"actor":"%s","none":["State"]})J"), *G))->GetBoolField(TEXT("matches")));
		TestEqual(TEXT("find"), H->CallOk(TEXT("tags.find_actors_with_tags"), TEXT(R"J({"all":["Team.Enemy"]})J"))->GetArrayField(TEXT("returnValue")).Num(), 1);
		H->CallFails(TEXT("tags.find_actors_with_tags"), TEXT(R"J({"none":["State"]})J"), ERuntimeToolError::InvalidArguments);
		Strings(H->CallOk(TEXT("tags.remove_actor_tags"), FString::Printf(TEXT(R"J({"actor":"%s","tags":["State.Alerted"]})J"), *G)));
		TestEqual(TEXT("removed"), Strings(H->CallOk(TEXT("tags.get_actor_tags"), FString::Printf(TEXT(R"J({"actor":"%s"})J"), *G))).Num(), 1);
		H->CallOk(TEXT("history.undo"));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("undo restores none"), Strings(H->CallOk(TEXT("tags.get_actor_tags"), FString::Printf(TEXT(R"J({"actor":"%s"})J"), *G))).Num(), 0);
	});

	It("makes a builder object an AI agent and back, with perception and errors", [this]()
	{
		const FString G = Cube(TEXT("guard"), FVector(500, 0, 0));
		const TSharedRef<FJsonObject> A = H->CallOk(TEXT("ai.create_agent"), FString::Printf(TEXT(R"J({"target":"%s","settings":{"max_speed":500,"sight_radius":2000}})J"), *G));
		TestEqual(TEXT("keeps the node id"), A->GetStringField(TEXT("id")), G);
		TestEqual(TEXT("settings applied"), A->GetObjectField(TEXT("settings"))->GetNumberField(TEXT("max_speed")), 500.0);
		const FRuntimeSceneNode* N = H->GetGraph()->Find(G);
		TestTrue(TEXT("carried by a pawn"), N && N->Actor.IsValid() && Cast<APawn>(N->Actor->GetAttachParentActor()) != nullptr);
		TestEqual(TEXT("listed"), H->CallOk(TEXT("ai.list_agents"))->GetArrayField(TEXT("returnValue")).Num(), 1);
		TestEqual(TEXT("perceives nothing yet"), H->CallOk(TEXT("ai.get_perception"), FString::Printf(TEXT(R"J({"agent":"%s"})J"), *G))->GetArrayField(TEXT("returnValue")).Num(), 0);
		H->CallFails(TEXT("ai.set_blackboard_value"), FString::Printf(TEXT(R"J({"agent":"%s","key":"Target","value":1})J"), *G), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("ai.stop_behavior_tree"), FString::Printf(TEXT(R"J({"agent":"%s"})J"), *G), ERuntimeToolError::WrongState);
		H->CallOk(TEXT("ai.stop_movement"), FString::Printf(TEXT(R"J({"agent":"%s"})J"), *G));
		H->CallOk(TEXT("ai.remove_agent"), FString::Printf(TEXT(R"J({"agent":"%s"})J"), *G));
		TestTrue(TEXT("free again"), N && N->Actor.IsValid() && N->Actor->GetAttachParentActor() == nullptr);
		H->CallOk(TEXT("history.undo")); // agent again
		TestTrue(TEXT("undo re-creates the agent"), N && N->Actor.IsValid() && Cast<APawn>(N->Actor->GetAttachParentActor()) != nullptr);
		H->CallOk(TEXT("history.undo")); // back to a plain object
		TestTrue(TEXT("undo removes the agent"), N && N->Actor.IsValid() && N->Actor->GetAttachParentActor() == nullptr);

		const FString Plain = Cube(TEXT("crate"), FVector(-500, 0, 0));
		H->CallFails(TEXT("ai.get_perception"), FString::Printf(TEXT(R"J({"agent":"%s"})J"), *Plain), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("ai.remove_agent"), FString::Printf(TEXT(R"J({"agent":"%s"})J"), *Plain), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("ai.make_perceivable"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Plain));
		H->CallFails(TEXT("ai.report_noise"), TEXT(R"J({"location":[0,0,0],"loudness":0})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("ai.report_noise"), TEXT(R"J({"location":[0,0,0],"tag":"Alarm"})J"));
		H->CallFails(TEXT("ai.create_agent"), TEXT(R"J({"target":"n999999"})J"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ai.describe_behavior_tree"), TEXT(R"J({"tree":"/Game/Nope.Nope"})J"), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("ai.list_behavior_trees"));
	});

	It("connects logic to the AI systems: brain backend, has_tag and tag actions", [this]()
	{
		const FString States = TEXT(R"J([{"name":"idle","move":"stop"}])J");
		H->CallOk(TEXT("logic.define_brain"), FString::Printf(TEXT(R"J({"name":"walker","backend":"Ai","states":%s})J"), *States));
		H->CallFails(TEXT("logic.define_brain"), FString::Printf(TEXT(R"J({"name":"jet","backend":"Ai","flight":{"speed":3000},"states":%s})J"), *States),
			ERuntimeToolError::InvalidArguments);
		const FString G = Cube(TEXT("guard"), FVector(0, 300, 0));
		H->CallOk(TEXT("logic.define_vars"), TEXT(R"J({"vars":{"stunned":false}})J"));
		H->CallOk(TEXT("logic.define_rules"), FString::Printf(TEXT(R"J({"rules":[
			{"name":"stun","on":"button","filter":"stun","actions":[{"add_tags":"'%s'","tags":["State.Stunned"]},{"set":"stunned","value":"has_tag('%s', 'State.Stunned')"}]},
			{"name":"bad","on":"button","filter":"bad","actions":[{"add_tags":"'%s'","tags":["State.Nope"]}]}]})J"), *G, *G, *G));
		H->CallOk(TEXT("logic.start"));
		H->CallOk(TEXT("logic.fire_event"), TEXT(R"J({"on":"button","name":"stun"})J"));
		TestTrue(TEXT("tag added by a rule and read by has_tag"), H->CallOk(TEXT("logic.eval"), TEXT(R"J({"expr":"stunned"})J"))->GetBoolField(TEXT("value")));
		const TSharedRef<FJsonObject> Bad = H->CallOk(TEXT("logic.fire_event"), TEXT(R"J({"on":"button","name":"bad"})J"));
		TestTrue(TEXT("unknown tag reported"), RuntimeJson::Write(Bad).Contains(TEXT("not a registered gameplay tag")));
		H->CallFails(TEXT("logic.eval"), FString::Printf(TEXT(R"J({"expr":"has_tag('%s', 'State.Nope')"})J"), *G), ERuntimeToolError::InvalidArguments);
	});
}

#endif
