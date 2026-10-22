#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"
#include "Internationalization/Regex.h"

BEGIN_DEFINE_SPEC(FSkillsSpec, "RuntimeToolsets.Toolsets.Skills", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FSkillsSpec)

void FSkillsSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("lists skills and returns instructions with live data", [this]()
	{
		const TSharedPtr<FJsonObject> List = H->CallOk(TEXT("skills.list_skills"))->GetObjectField(TEXT("returnValue"));
		for (const TCHAR* N : {TEXT("outdoor_lighting"), TEXT("art_style"), TEXT("pcg_graph_generation"), TEXT("builder"), TEXT("game_logic")})
		{
			TestTrue(FString::Printf(TEXT("has %s with a description"), N), List->HasField(N) && !List->GetStringField(N).IsEmpty());
		}
		const TSharedPtr<FJsonObject> Got = H->CallOk(TEXT("skills.get_skills"), TEXT(R"J({"skill_names":["pcg_graph_generation","art_style","builder"]})J"))
			->GetObjectField(TEXT("returnValue"));
		const FString Pcg = Got->GetObjectField(TEXT("pcg_graph_generation"))->GetStringField(TEXT("instructions"));
		TestFalse(TEXT("placeholders filled"), Pcg.Contains(TEXT("{Nodes}")) || Pcg.Contains(TEXT("{Graphs}")));
		TestTrue(TEXT("node list"), Pcg.Contains(TEXT("Surface Sampler")));
		TestTrue(TEXT("presets listed"), Got->GetObjectField(TEXT("art_style"))->GetStringField(TEXT("instructions")).Contains(TEXT("**toon**")));
		TestTrue(TEXT("guide reused"), Got->GetObjectField(TEXT("builder"))->GetStringField(TEXT("instructions")).Len() > 500);
		H->CallFails(TEXT("skills.get_skills"), TEXT(R"J({"skill_names":["nope"]})J"), ERuntimeToolError::NotFound);
	});

	It("saves, lists, loads and deletes agent-written skills", [this]()
	{
		const TSharedPtr<FJsonObject> Before = H->CallOk(TEXT("skills.list_skills"))->GetObjectField(TEXT("returnValue"));
		TestTrue(TEXT("recipe skills exist"), Before->HasField(TEXT("arcade_shooter")) && Before->HasField(TEXT("game_ui"))
			&& Before->HasField(TEXT("realistic_scene")) && Before->HasField(TEXT("python_kit")));
		H->CallOk(TEXT("skills.save_skill"), TEXT(R"J({"name":"zz_test_recipe","description":"Apply when testing.","instructions":"# Steps
1. Do it.","toolsets":["logic"]})J"));
		TestEqual(TEXT("listed"), H->CallOk(TEXT("skills.list_skills"))->GetObjectField(TEXT("returnValue"))->GetStringField(TEXT("zz_test_recipe")), FString(TEXT("Apply when testing.")));
		const TSharedPtr<FJsonObject> Got = H->CallOk(TEXT("skills.get_skills"), TEXT(R"J({"skill_names":["zz_test_recipe"]})J"))->GetObjectField(TEXT("returnValue"))
			->GetObjectField(TEXT("zz_test_recipe"));
		TestTrue(TEXT("instructions round trip"), Got->GetStringField(TEXT("instructions")).Contains(TEXT("1. Do it.")));
		H->CallFails(TEXT("skills.save_skill"), TEXT(R"J({"name":"game_logic","description":"x","instructions":"y","toolsets":[]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("skills.save_skill"), TEXT(R"J({"name":"Bad Name","description":"x","instructions":"y","toolsets":[]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("skills.delete_skill"), TEXT(R"J({"name":"game_logic"})J"), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("skills.delete_skill"), TEXT(R"J({"name":"zz_test_recipe"})J"));
		TestFalse(TEXT("gone"), H->CallOk(TEXT("skills.list_skills"))->GetObjectField(TEXT("returnValue"))->HasField(TEXT("zz_test_recipe")));
	});

	It("only mentions tools that exist", [this]()
	{
		// Epic's advice: skills go stale silently; check every toolset.tool reference against the registry.
		const TArray<FString> Names = RuntimeJson::Keys(H->CallOk(TEXT("skills.list_skills"))->GetObjectField(TEXT("returnValue")));
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetField(TEXT("skill_names"), RuntimeJson::Strings(Names));
		const TSharedPtr<FJsonObject> Got = H->CallOk(TEXT("skills.get_skills"), RuntimeJson::Write(Args))->GetObjectField(TEXT("returnValue"));
		const TSharedPtr<FJsonObject> Toolsets = RuntimeJson::Parse(IRuntimeToolsetsCore::Get().ListToolsets());
		TSet<FString> Prefixes;
		for (const TSharedPtr<FJsonValue>& T : Toolsets->GetArrayField(TEXT("toolsets")))
		{
			Prefixes.Add(T->AsObject()->GetStringField(TEXT("name")));
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& S : Got->Values)
		{
			const FString Text = S.Value->AsObject()->GetStringField(TEXT("instructions"));
			FRegexMatcher M(FRegexPattern(TEXT("\\b([a-z]+)\\.([a-z_]+)\\b")), Text);
			while (M.FindNext())
			{
				const FString Toolset = M.GetCaptureGroup(1);
				if (Prefixes.Contains(Toolset))
				{
					const FString Tool = Toolset + TEXT(".") + M.GetCaptureGroup(2);
					TestTrue(FString::Printf(TEXT("skill %s mentions %s"), *S.Key, *Tool), IRuntimeToolsetsCore::Get().FindTool(Tool).IsValid());
				}
			}
		}
	});
}

#endif
