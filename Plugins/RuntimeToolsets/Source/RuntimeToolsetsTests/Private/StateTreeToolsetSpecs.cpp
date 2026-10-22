#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"

BEGIN_DEFINE_SPEC(FStateTreeToolsetSpec, "RuntimeToolsets.Toolsets.StateTree", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	TArray<TSharedPtr<FJsonValue>> RetArray(const TCHAR* Tool, const FString& Args)
	{
		const TSharedRef<FJsonObject> R = H->CallOk(Tool, Args);
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return R->TryGetArrayField(TEXT("returnValue"), A) ? *A : TArray<TSharedPtr<FJsonValue>>();
	}
END_DEFINE_SPEC(FStateTreeToolsetSpec)

void FStateTreeToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("fails cleanly on actors without a StateTree and on missing assets", [this]()
	{
		// StateTree assets cannot be compiled at runtime, so these paths need no asset.
		const FString Cube = H->SpawnCube();
		const FString T = FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube);
		H->CallFails(TEXT("statetree.get_active_states"), T, ERuntimeToolError::WrongState);
		H->CallFails(TEXT("statetree.stop_state_tree"), T, ERuntimeToolError::WrongState);
		H->CallFails(TEXT("statetree.restart_state_tree"), T, ERuntimeToolError::WrongState);
		H->CallFails(TEXT("statetree.send_event"), FString::Printf(TEXT(R"J({"target":"%s","tag":"Gas.Event.Hit"})J"), *Cube), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("statetree.run_state_tree"), FString::Printf(TEXT(R"J({"target":"%s","asset":"/Game/NoSuch/ST_Nope.ST_Nope"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("statetree.run_state_tree"), FString::Printf(TEXT(R"J({"target":"%s","asset":"/RuntimeToolsets/Materials/M_RT_PBR.M_RT_PBR"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("statetree.describe_state_tree"), TEXT(R"J({"asset":"/Game/NoSuch/ST_Nope.ST_Nope"})J"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("statetree.get_active_states"), TEXT(R"J({"target":"n999999"})J"), ERuntimeToolError::NotFound);
	});

	It("reads the compiled layout of a StateTree object and refuses to run an uncompiled one", [this]()
	{
		// An empty (uncompiled) StateTree made through reflection: checks the runtime data is readable in this engine version.
		UClass* TreeClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
		if (!TestNotNull(TEXT("StateTree class"), TreeClass))
		{
			return;
		}
		UObject* Tree = NewObject<UObject>(GetTransientPackage(), TreeClass, MakeUniqueObjectName(GetTransientPackage(), TreeClass, TEXT("ST_RuntimeToolsetsTest")));
		const FString Path = Tree->GetPathName();
		const TSharedRef<FJsonObject> D = H->CallOk(TEXT("statetree.describe_state_tree"), FString::Printf(TEXT(R"J({"asset":"%s"})J"), *Path));
		TestFalse(TEXT("not ready to run"), D->GetBoolField(TEXT("ready_to_run")));
		TestEqual(TEXT("no states"), D->GetArrayField(TEXT("states")).Num(), 0);
		H->CallFails(TEXT("statetree.run_state_tree"), FString::Printf(TEXT(R"J({"target":"%s","asset":"%s"})J"), *H->SpawnCube(), *Path), ERuntimeToolError::WrongState);
		Tree->MarkAsGarbage();
	});

	It("lists and describes packaged StateTree assets and runs one when available", [this]()
	{
		const TArray<TSharedPtr<FJsonValue>> Trees = RetArray(TEXT("statetree.list_state_trees"), TEXT("{}"));
		TestEqual(TEXT("filter that matches nothing"), RetArray(TEXT("statetree.list_state_trees"), TEXT(R"J({"filter":"zz_no_such_tree_zz"})J")).Num(), 0);
		if (Trees.IsEmpty())
		{
			AddInfo(TEXT("No StateTree assets in this project: describe/run skipped (assets cannot be created at runtime)"));
			return;
		}
		FString Runnable;
		for (const TSharedPtr<FJsonValue>& V : Trees)
		{
			const FString Path = V->AsObject()->GetStringField(TEXT("path"));
			const TSharedRef<FJsonObject> D = H->CallOk(TEXT("statetree.describe_state_tree"), FString::Printf(TEXT(R"J({"asset":"%s"})J"), *Path));
			TestEqual(TEXT("path"), D->GetStringField(TEXT("path")), Path);
			if (Runnable.IsEmpty() && D->GetBoolField(TEXT("ready_to_run")) && D->GetStringField(TEXT("schema")) == TEXT("StateTreeComponentSchema")
				&& D->GetArrayField(TEXT("states")).Num() > 0)
			{
				Runnable = Path;
			}
		}
		if (Runnable.IsEmpty())
		{
			AddInfo(TEXT("No compiled StateTree with StateTreeComponentSchema: run skipped"));
			return;
		}
		const FString Cube = H->SpawnCube();
		const FToolResult Run = H->Call(TEXT("statetree.run_state_tree"), FString::Printf(TEXT(R"J({"target":"%s","asset":"%s"})J"), *Cube, *Runnable));
		if (!Run.IsOk())
		{
			AddInfo(FString::Printf(TEXT("%s does not start on a plain actor (%s): run skipped"), *Runnable, *Run.Message));
			return;
		}
		const TSharedRef<FJsonObject> S = H->CallOk(TEXT("statetree.get_active_states"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube));
		TestEqual(TEXT("tree"), S->GetStringField(TEXT("tree")), Runnable);
		H->CallOk(TEXT("statetree.stop_state_tree"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube));
		H->CallOk(TEXT("history.undo"));
		H->CallFails(TEXT("statetree.get_active_states"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube), ERuntimeToolError::WrongState);
	});
}

#endif
