#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "GameFramework/Actor.h"

BEGIN_DEFINE_SPEC(FActorsToolsetSpec, "RuntimeToolsets.Toolsets.Actors", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	AActor* ActorOf(const FString& Id) const
	{
		const FRuntimeSceneNode* N = H->GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}
END_DEFINE_SPEC(FActorsToolsetSpec)

void FActorsToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	Describe("spawn_actor", [this]()
	{
		It("spawns a shape as a scene node", [this]()
		{
			const FString Id = H->SpawnCube(TEXT("\"name\":\"box\",\"location\":[10,20,30],\"tags\":[\"t1\"]"));
			TestTrue(TEXT("id"), Id.StartsWith(TEXT("n")));
			AActor* A = ActorOf(Id);
			if (TestNotNull(TEXT("actor"), A))
			{
				TestTrue(TEXT("location"), A->GetActorLocation().Equals(FVector(10, 20, 30), 0.01));
				TestTrue(TEXT("tag"), A->ActorHasTag(TEXT("t1")));
			}
		});
		It("spawns an engine class", [this]()
		{
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("actors.spawn_actor"), TEXT("{\"class\":\"/Script/Engine.PointLight\"}"));
			TestTrue(TEXT("id"), R->HasField(TEXT("id")));
		});
		It("rejects bad input", [this]()
		{
			H->CallFails(TEXT("actors.spawn_actor"), TEXT("{}"), ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("actors.spawn_actor"), TEXT("{\"shape\":\"cube\",\"class\":\"/Script/Engine.PointLight\"}"), ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("actors.spawn_actor"), TEXT("{\"shape\":\"donut\"}"), ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("actors.spawn_actor"), TEXT("{\"class\":\"/Script/Engine.NoSuchActor\"}"), ERuntimeToolError::NotFound);
			H->CallFails(TEXT("actors.spawn_actor"), TEXT("{\"class\":\"/Script/CoreUObject.Object\"}"), ERuntimeToolError::NotAllowed);
		});
		It("undo hides, redo restores the same id", [this]()
		{
			const FString Id = H->SpawnCube();
			H->CallOk(TEXT("history.undo"));
			TestTrue(TEXT("soft-deleted"), H->GetGraph()->Find(Id)->bSoftDeleted);
			H->CallFails(TEXT("scene.get_node"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id), ERuntimeToolError::NotFound);
			H->CallOk(TEXT("history.redo"));
			TestFalse(TEXT("restored"), H->GetGraph()->Find(Id)->bSoftDeleted);
			H->CallOk(TEXT("scene.get_node"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id));
		});
	});

	Describe("set_transform", [this]()
	{
		It("moves and undo restores", [this]()
		{
			const FString Id = H->SpawnCube();
			H->CallOk(TEXT("actors.set_transform"), FString::Printf(TEXT("{\"target\":\"%s\",\"location\":[100,0,0],\"scale\":[2,2,2]}"), *Id));
			TestTrue(TEXT("moved"), ActorOf(Id)->GetActorLocation().Equals(FVector(100, 0, 0), 0.01));
			TestTrue(TEXT("scaled"), ActorOf(Id)->GetActorScale3D().Equals(FVector(2), 0.001));
			H->CallOk(TEXT("history.undo"));
			TestTrue(TEXT("restored"), ActorOf(Id)->GetActorLocation().Equals(FVector::ZeroVector, 0.01));
		});
		It("fails for unknown targets", [this]()
		{
			H->CallFails(TEXT("actors.set_transform"), TEXT("{\"target\":\"n999999\",\"location\":[0,0,0]}"), ERuntimeToolError::NotFound);
		});
	});

	Describe("destroy_actor", [this]()
	{
		It("soft-deletes the subtree and undo restores", [this]()
		{
			const FString Parent = H->CallOk(TEXT("scene.create_group"), TEXT("{\"name\":\"g\"}"))->GetStringField(TEXT("id"));
			const FString Child = H->SpawnCube(FString::Printf(TEXT("\"parent\":\"%s\""), *Parent));
			H->CallOk(TEXT("actors.destroy_actor"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Parent));
			TestTrue(TEXT("child hidden"), ActorOf(Child)->IsHidden());
			TestTrue(TEXT("child deleted"), H->GetGraph()->Find(Child)->bSoftDeleted);
			H->CallOk(TEXT("history.undo"));
			TestFalse(TEXT("child visible"), ActorOf(Child)->IsHidden());
		});
	});

	Describe("find_actors", [this]()
	{
		It("filters by tag and class", [this]()
		{
			H->SpawnCube(TEXT("\"tags\":[\"findme\"]"));
			H->SpawnCube();
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("actors.find_actors"), TEXT("{\"tag\":\"findme\"}"));
			TestEqual(TEXT("one tagged"), static_cast<int32>(R->GetNumberField(TEXT("total"))), 1);
			const TSharedRef<FJsonObject> C = H->CallOk(TEXT("actors.find_actors"), TEXT("{\"class\":\"StaticMeshActor\",\"limit\":1}"));
			TestEqual(TEXT("two meshes"), static_cast<int32>(C->GetNumberField(TEXT("total"))), 2);
			TestEqual(TEXT("limit"), C->GetArrayField(TEXT("actors")).Num(), 1);
		});
	});

	Describe("attach", [this]()
	{
		It("attaches, detaches and undoes", [this]()
		{
			const FString A = H->SpawnCube();
			const FString B = H->SpawnCube(TEXT("\"location\":[200,0,0]"));
			H->CallOk(TEXT("actors.attach"), FString::Printf(TEXT("{\"child\":\"%s\",\"parent\":\"%s\"}"), *B, *A));
			TestEqual(TEXT("attached"), ActorOf(B)->GetAttachParentActor(), ActorOf(A));
			TestEqual(TEXT("graph parent"), H->GetGraph()->Find(B)->ParentId, A);
			H->CallOk(TEXT("history.undo"));
			TestNull(TEXT("detached"), ActorOf(B)->GetAttachParentActor());
			TestTrue(TEXT("graph root"), H->GetGraph()->Find(B)->ParentId.IsEmpty());
		});
	});

	Describe("set_visibility", [this]()
	{
		It("hides and undo shows", [this]()
		{
			const FString Id = H->SpawnCube();
			H->CallOk(TEXT("actors.set_visibility"), FString::Printf(TEXT("{\"target\":\"%s\",\"visible\":false}"), *Id));
			TestTrue(TEXT("hidden"), ActorOf(Id)->IsHidden());
			H->CallOk(TEXT("history.undo"));
			TestFalse(TEXT("visible"), ActorOf(Id)->IsHidden());
		});
	});

	Describe("list_components", [this]()
	{
		It("lists the mesh component", [this]()
		{
			const FString Id = H->SpawnCube();
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("actors.list_components"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id));
			TestTrue(TEXT("has components"), R->GetNumberField(TEXT("total")) >= 1);
		});
	});
}

#endif
