#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "GameFramework/Actor.h"

BEGIN_DEFINE_SPEC(FSceneGraphToolsetSpec, "RuntimeToolsets.Toolsets.Scene", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	FString House;
	FString Box;
	AActor* ActorOf(const FString& Id) const
	{
		const FRuntimeSceneNode* N = H->GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}
	FString Target(const FString& Id, const FString& Extra = FString()) const
	{
		return Extra.IsEmpty() ? FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id) : FString::Printf(TEXT("{\"target\":\"%s\",%s}"), *Id, *Extra);
	}
END_DEFINE_SPEC(FSceneGraphToolsetSpec)

void FSceneGraphToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		House = H->CallOk(TEXT("scene.create_group"), TEXT("{\"name\":\"house\",\"location\":[1000,0,0]}"))->GetStringField(TEXT("id"));
		Box = H->SpawnCube(FString::Printf(TEXT("\"name\":\"box\",\"parent\":\"%s\",\"location\":[0,0,100]"), *House));
	});
	AfterEach([this]() { H.Reset(); });

	It("places children relative to their parent and resolves paths", [this]()
	{
		TestTrue(TEXT("child world"), ActorOf(Box)->GetActorLocation().Equals(FVector(1000, 0, 100), 0.01));
		const TSharedRef<FJsonObject> N = H->CallOk(TEXT("scene.get_node"), TEXT("{\"target\":\"house/box\"}"));
		TestEqual(TEXT("path resolves"), N->GetStringField(TEXT("id")), Box);
	});

	It("moves children with the parent", [this]()
	{
		H->CallOk(TEXT("scene.move_node"), Target(House, TEXT("\"offset\":[0,500,0]")));
		TestTrue(TEXT("child followed"), ActorOf(Box)->GetActorLocation().Equals(FVector(1000, 500, 100), 0.01));
		H->CallFails(TEXT("scene.move_node"), Target(House), ERuntimeToolError::InvalidArguments);
	});

	It("rotates and scales nodes", [this]()
	{
		H->CallOk(TEXT("scene.rotate_node"), Target(House, TEXT("\"delta\":[0,90,0]")));
		TestTrue(TEXT("yaw"), FMath::IsNearlyEqual(ActorOf(House)->GetActorRotation().Yaw, 90.0, 0.01));
		H->CallOk(TEXT("scene.scale_node"), Target(Box, TEXT("\"factor\":[2,2,2]")));
		TestTrue(TEXT("scaled"), ActorOf(Box)->GetActorScale3D().Equals(FVector(2), 0.001));
	});

	It("lists the tree depth-limited", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("scene.list_tree"), TEXT("{\"depth\":1}"));
		const TArray<TSharedPtr<FJsonValue>> Nodes = R->GetArrayField(TEXT("nodes"));
		TestEqual(TEXT("one root"), Nodes.Num(), 1);
		TestEqual(TEXT("collapsed"), static_cast<int32>(Nodes[0]->AsObject()->GetNumberField(TEXT("child_count"))), 1);
	});

	It("finds nodes by name and kind", [this]()
	{
		TestEqual(TEXT("groups"), static_cast<int32>(H->CallOk(TEXT("scene.find_nodes"), TEXT("{\"kind\":\"group\"}"))->GetNumberField(TEXT("total"))), 1);
		TestEqual(TEXT("pattern"), static_cast<int32>(H->CallOk(TEXT("scene.find_nodes"), TEXT("{\"pattern\":\"bo*\"}"))->GetNumberField(TEXT("total"))), 1);
	});

	It("reparents keeping world position, and undo restores", [this]()
	{
		H->CallOk(TEXT("scene.reparent_node"), Target(Box, TEXT("\"parent\":\"\"")));
		TestTrue(TEXT("root"), H->GetGraph()->Find(Box)->ParentId.IsEmpty());
		TestTrue(TEXT("same world"), ActorOf(Box)->GetActorLocation().Equals(FVector(1000, 0, 100), 0.01));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("back under house"), H->GetGraph()->Find(Box)->ParentId, House);
		H->CallFails(TEXT("scene.reparent_node"), FString::Printf(TEXT("{\"target\":\"%s\",\"parent\":\"%s\"}"), *House, *Box), ERuntimeToolError::InvalidArguments);
	});

	It("renames with sibling uniqueness", [this]()
	{
		H->SpawnCube(FString::Printf(TEXT("\"name\":\"door\",\"parent\":\"%s\""), *House));
		H->CallFails(TEXT("scene.rename_node"), Target(Box, TEXT("\"name\":\"door\"")), ERuntimeToolError::WrongState);
		H->CallOk(TEXT("scene.rename_node"), Target(Box, TEXT("\"name\":\"wall\"")));
		H->CallOk(TEXT("scene.get_node"), TEXT("{\"target\":\"house/wall\"}"));
		H->CallOk(TEXT("history.undo"));
		H->CallOk(TEXT("scene.get_node"), TEXT("{\"target\":\"house/box\"}"));
	});

	It("sets tags", [this]()
	{
		H->CallOk(TEXT("scene.set_node_tags"), Target(Box, TEXT("\"tags\":[\"wall\",\"stone\"]")));
		TestTrue(TEXT("actor tag"), ActorOf(Box)->ActorHasTag(TEXT("stone")));
		H->CallOk(TEXT("scene.set_node_tags"), Target(Box, TEXT("\"tags\":[\"stone\"],\"mode\":\"remove\"")));
		TestFalse(TEXT("removed"), ActorOf(Box)->ActorHasTag(TEXT("stone")));
		TestTrue(TEXT("kept rt id tag"), ActorOf(Box)->ActorHasTag(*FString::Printf(TEXT("rt:%s"), *Box)));
	});

	It("moves a group pivot without moving children", [this]()
	{
		H->CallOk(TEXT("scene.set_pivot"), Target(House, TEXT("\"location\":[0,0,0]")));
		TestTrue(TEXT("group moved"), ActorOf(House)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
		TestTrue(TEXT("child stayed"), ActorOf(Box)->GetActorLocation().Equals(FVector(1000, 0, 100), 0.01));
		H->CallFails(TEXT("scene.set_pivot"), Target(Box), ERuntimeToolError::Unsupported);
	});

	It("duplicates a subtree as one undo step", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("scene.duplicate_node"), Target(House, TEXT("\"offset\":[0,1000,0],\"name\":\"house2\"")));
		TestEqual(TEXT("nodes"), static_cast<int32>(R->GetNumberField(TEXT("nodes"))), 2);
		const TSharedRef<FJsonObject> Copy = H->CallOk(TEXT("scene.get_node"), TEXT("{\"target\":\"house2/box\"}"));
		AActor* CopyBox = ActorOf(Copy->GetStringField(TEXT("id")));
		TestTrue(TEXT("offset applied"), CopyBox && CopyBox->GetActorLocation().Equals(FVector(1000, 1000, 100), 0.01));
		H->CallOk(TEXT("history.undo"));
		H->CallFails(TEXT("scene.get_node"), TEXT("{\"target\":\"house2\"}"), ERuntimeToolError::NotFound);
	});

	It("deletes with promote_children", [this]()
	{
		H->CallOk(TEXT("scene.delete_node"), Target(House, TEXT("\"promote_children\":true")));
		TestTrue(TEXT("box is root"), H->GetGraph()->Find(Box)->ParentId.IsEmpty());
		TestFalse(TEXT("box alive"), H->GetGraph()->Find(Box)->bSoftDeleted);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("box back"), H->GetGraph()->Find(Box)->ParentId, House);
	});

	It("clear_all needs confirm, checkpoints, and rollback restores", [this]()
	{
		H->CallFails(TEXT("scene.clear_all"), TEXT("{}"), ERuntimeToolError::ConfirmRequired);
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("scene.clear_all"), TEXT("{\"confirm\":true}"));
		TestEqual(TEXT("deleted"), static_cast<int32>(R->GetNumberField(TEXT("deleted"))), 2);
		TestEqual(TEXT("empty"), H->GetGraph()->NumLiveNodes(), 0);
		const FString Checkpoint = R->GetStringField(TEXT("checkpoint"));
		H->CallOk(TEXT("history.rollback"), FString::Printf(TEXT("{\"name\":\"%s\"}"), *Checkpoint));
		TestEqual(TEXT("restored"), H->GetGraph()->NumLiveNodes(), 2);
	});

	It("clear_scope keeps the scope node", [this]()
	{
		H->CallOk(TEXT("scene.clear_scope"), FString::Printf(TEXT("{\"root\":\"%s\",\"confirm\":true}"), *House));
		TestEqual(TEXT("only house"), H->GetGraph()->NumLiveNodes(), 1);
	});

	It("exports and imports a tree", [this]()
	{
		const TSharedRef<FJsonObject> Spec = H->CallOk(TEXT("scene.export_tree"), FString::Printf(TEXT("{\"root\":\"%s\"}"), *House));
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetObjectField(TEXT("tree"), Spec);
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("scene.import_tree"), RuntimeJson::Write(Args));
		TestEqual(TEXT("imported"), static_cast<int32>(R->GetNumberField(TEXT("nodes"))), 2);
		TestEqual(TEXT("total"), H->GetGraph()->NumLiveNodes(), 4);
		TestTrue(TEXT("id map"), R->HasField(TEXT("id_map")));
		H->CallFails(TEXT("scene.import_tree"), TEXT("{\"tree\":{\"nodes\":[{\"source\":\"bad\"}]}}"), ERuntimeToolError::InvalidArguments);
	});
}

// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FHistoryToolsetSpec, "RuntimeToolsets.Toolsets.History", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FHistoryToolsetSpec)

void FHistoryToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("reports nothing to undo or redo", [this]()
	{
		H->CallFails(TEXT("history.undo"), TEXT("{}"), ERuntimeToolError::NothingToUndo);
		H->CallFails(TEXT("history.redo"), TEXT("{}"), ERuntimeToolError::NothingToUndo);
		H->CallFails(TEXT("history.end_transaction"), TEXT("{}"), ERuntimeToolError::WrongState);
	});

	It("groups a transaction into one undo step", [this]()
	{
		H->CallOk(TEXT("history.begin_transaction"), TEXT("{\"label\":\"two cubes\"}"));
		H->SpawnCube();
		H->SpawnCube();
		H->CallFails(TEXT("history.undo"), TEXT("{}"), ERuntimeToolError::WrongState);
		H->CallOk(TEXT("history.end_transaction"));
		TestEqual(TEXT("two nodes"), H->GetGraph()->NumLiveNodes(), 2);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("both undone"), H->GetGraph()->NumLiveNodes(), 0);
	});

	It("rolls back to a named checkpoint and lists history", [this]()
	{
		H->SpawnCube();
		H->CallOk(TEXT("history.checkpoint"), TEXT("{\"name\":\"base\"}"));
		H->SpawnCube();
		H->SpawnCube();
		const TSharedRef<FJsonObject> Hist = H->CallOk(TEXT("history.get_history"));
		TestEqual(TEXT("entries"), static_cast<int32>(Hist->GetNumberField(TEXT("undo_count"))), 4);
		H->CallOk(TEXT("history.rollback"), TEXT("{\"name\":\"base\"}"));
		TestEqual(TEXT("one left"), H->GetGraph()->NumLiveNodes(), 1);
		H->CallFails(TEXT("history.rollback"), TEXT("{\"name\":\"missing\"}"), ERuntimeToolError::NotFound);
	});

	It("keeps sessions separate", [this]()
	{
		H->SpawnCube();
		FToolCallerContext Other;
		Other.SessionId = TEXT("other-session");
		const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("history.undo"), nullptr, Other, 0);
		TestTrue(TEXT("other session has nothing"), R.Error == ERuntimeToolError::NothingToUndo);
		IRuntimeToolsetsCore::Get().EndSession(TEXT("other-session"));
	});
}

#endif
