#include "SessionToolset.h"
#include "Misc/Paths.h"
#include "Helpers/ToolsCommon.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"

using namespace RuntimeTools;

static void Ping(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetBoolField(TEXT("pong"), true);
	Out->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
	Out->SetNumberField(TEXT("time"), RuntimeJson::Round(FApp::GetCurrentTime()));
	Inv.Succeed(Out);
}

static void GetBuildInfo(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
	Out->SetStringField(TEXT("config"), LexToString(FApp::GetBuildConfiguration()));
	Out->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
	Out->SetStringField(TEXT("project"), FApp::GetProjectName());
	Out->SetBoolField(TEXT("editor"), GIsEditor);
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RuntimeToolsets")))
	{
		Out->SetStringField(TEXT("plugin_version"), Plugin->GetDescriptor().VersionName);
	}
	if (UWorld* World = Inv.GetWorld())
	{
		Out->SetStringField(TEXT("world"), World->GetMapName());
		const ENetMode NetMode = World->GetNetMode();
		Out->SetStringField(TEXT("net_mode"), NetMode == NM_Standalone ? TEXT("standalone") : NetMode == NM_Client ? TEXT("client") : TEXT("server"));
	}
	TSharedRef<FJsonObject> Scripting = Obj();
	// Python kit shipped in the build folder (packaged) or the repo (editor): used in place, nothing downloaded or installed.
	Scripting->SetStringField(TEXT("why"), TEXT("A script builds hundreds of parts and sends them in a few requests: much faster than single tool calls."));
	for (const FString& Dir : {FPaths::RootDir(), FPaths::ProjectDir() / TEXT("Scripts/dist")})
	{
		if (FPaths::FileExists(Dir / TEXT("unreal_kit.zip")))
		{
			Scripting->SetStringField(TEXT("kit"), FPaths::ConvertRelativePathToFull(Dir / TEXT("unreal_kit.zip")));
			Scripting->SetStringField(TEXT("examples"), FPaths::ConvertRelativePathToFull(Dir / TEXT("kit_examples")));
			break;
		}
	}
	Scripting->SetStringField(TEXT("how"), TEXT("Python: import sys; sys.path.insert(0, r'<kit>'); import unreal_kit as uk. It connects by itself to the "
		"game on this machine (uk.connect(url='http://127.0.0.1:<port>/mcp') for another port). uk.tools.<toolset>.<tool>(...) is every tool; "
		"uk.parts builds part lists (box, cyl, sphere, beam, extrude, sweep, transform, grid, ring, PALETTE materials); uk.place / "
		"uk.place_many / uk.batch send them in big requests; uk.rules builds game rules; uk.ui builds layout UI; uk.look sets light, "
		"cameras and takes review images. <examples> has readable example scripts (city, game pieces, combat presets) to copy from. "
		"Use the files where they are: do not download code and do not print environment variables. Save good results with "
		"scene.save_template. Skill python_kit has the details."));
	Out->SetObjectField(TEXT("scripting"), Scripting);
	Inv.Succeed(Out);
}

static void GetCapabilities(FToolInvocation& Inv)
{
	const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
	TSharedRef<FJsonObject> Out = Obj();

	TArray<FString> Toolsets;
	for (const TSharedRef<const FRuntimeToolDescriptor>& T : IRuntimeToolsetsCore::Get().GetAllTools())
	{
		Toolsets.AddUnique(T->Toolset.ToString().ToLower());
	}
	Out->SetField(TEXT("toolsets"), RuntimeJson::Strings(Toolsets));
	Out->SetStringField(TEXT("session"), Inv.GetCaller().SessionId);
	Out->SetBoolField(TEXT("world"), Inv.GetWorld() != nullptr);
	Out->SetBoolField(TEXT("can_render"), FApp::CanEverRender());
	Out->SetBoolField(TEXT("viewport"), GEngine && GEngine->GameViewport != nullptr);

	TSharedRef<FJsonObject> Limits = Obj();
	Limits->SetNumberField(TEXT("list_default"), S.DefaultListLimit);
	Limits->SetNumberField(TEXT("list_max"), S.MaxListLimit);
	Limits->SetNumberField(TEXT("history"), S.MaxHistoryPerSession);
	Limits->SetNumberField(TEXT("queue"), S.MaxQueuedCalls);
	Limits->SetNumberField(TEXT("result_bytes"), S.MaxResultBytes);
	Limits->SetNumberField(TEXT("capture_max_dim"), S.CaptureMaxDimension);
	Limits->SetNumberField(TEXT("timeout_s"), S.DefaultTimeoutSeconds);
	Out->SetObjectField(TEXT("limits"), Limits);

	TSharedRef<FJsonObject> Policy = Obj();
	Policy->SetBoolField(TEXT("destroy_level_actors"), S.bAllowDestroyLevelActors);
	Policy->SetBoolField(TEXT("write_non_editable_properties"), S.bAllowWriteNonEditableProperties);
	Policy->SetBoolField(TEXT("console_allow_list"), S.ConsoleAllowPrefixes.Num() > 0);
	Out->SetObjectField(TEXT("policy"), Policy);

	Out->SetStringField(TEXT("units"), TEXT("cm, degrees [pitch,yaw,roll], Z up"));
	Out->SetStringField(TEXT("targets"), TEXT("n12 | group/child | actor:Name | /Path.Object | class:/Script/X.Y | subsystem:<kind>/<Class> | pawn | player_controller | game_mode | game_state | world_settings | game_user_settings, optional #Component"));
	Inv.Succeed(Out);
}

static void ListToolsets(FToolInvocation& Inv)
{
	Inv.Succeed(RuntimeJson::Parse(IRuntimeToolsetsCore::Get().ListToolsets()));
}

static void DescribeToolset(FToolInvocation& Inv)
{
	const FString Name = Inv.GetString(TEXT("name"));
	const FString Json = IRuntimeToolsetsCore::Get().DescribeToolset(Name, Inv.GetBool(TEXT("include_schemas"), true));
	if (Json.IsEmpty())
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no enabled toolset '%s'"), *Name), TEXT("See session.list_toolsets"));
		return;
	}
	Inv.Succeed(RuntimeJson::Parse(Json));
}

static void CallTool(FToolInvocation& Inv)
{
	const FString Name = Inv.GetString(TEXT("name"));
	if (Name == TEXT("session.call_tool"))
	{
		Inv.Fail(ERuntimeToolError::NotAllowed, TEXT("call_tool cannot call itself"));
		return;
	}
	TSharedRef<FToolInvocation> Self = Inv.AsShared();
	TFunction<void()> Cancel = IRuntimeToolsetsCore::Get().ExecuteNested(Name, Inv.GetObject(TEXT("arguments")), Inv.GetCaller(), Inv.GetDepth() + 1,
		[Self](const FToolResult& Result)
		{
			if (!Result.IsOk())
			{
				Self->Fail(Result.Error, Result.Message, Result.Hint);
				return;
			}
			for (FToolAttachment A : Result.Attachments)
			{
				Self->AddAttachment(MoveTemp(A));
			}
			TSharedPtr<FJsonObject> Payload = Result.Payload.IsValid() ? Result.Payload : MakeShared<FJsonObject>();
			if (Result.HistoryEntryId != INDEX_NONE)
			{
				Payload->SetNumberField(TEXT("history_id"), static_cast<double>(Result.HistoryEntryId));
			}
			Self->Succeed(Payload);
		});
	if (!Inv.IsComplete())
	{
		// Nested tool is async: wait for it.
		Inv.SetCancelHandler(MoveTemp(Cancel));
		// The nested tool enforces its own timeout (waits, long samples); allow it the room.
		Inv.SetTimeout(600.0);
		Inv.Defer();
	}
}

static void GetUsageReport(FToolInvocation& Inv)
{
	Inv.Succeed(IRuntimeToolsetsCore::Get().GetUsageReport());
}

static void EndSession(FToolInvocation& Inv)
{
	// Deferred to the next tick: the current call still uses this session's history.
	const FString Session = Inv.GetCaller().SessionId;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Session](float)
	{
		IRuntimeToolsetsCore::Get().EndSession(Session);
		return false;
	}));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("ended"), Session);
	Inv.Succeed(Out);
}

void FSessionToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("ping"), TEXT("Health check. Returns frame number."))
		.Handle(&Ping);

	B.Tool(TEXT("get_build_info"), TEXT("Engine version, build config, platform, project, current map."))
		.Handle(&GetBuildInfo);

	B.Tool(TEXT("get_capabilities"), TEXT("Enabled toolsets, limits, policies, target syntax and units."))
		.Handle(&GetCapabilities);

	B.Tool(TEXT("list_toolsets"), TEXT("List toolsets with tool counts. Start here, then call skills.list_skills and load the skills that match the task (skills.get_skills): they hold the rules and numbers for lighting, art styles, PCG, building, game logic, game UI and game genres. Build fast with ops.run_batch (up to 100 calls per request), one logic.import per game, or Python scripts (session_get_build_info -> scripting). If no skill matches the task, build it from the generic tools (character, physics, actors components, logic) and, once it works, save the recipe with skills.save_skill so the next agent has a skill for it."))
		.Handle(&ListToolsets);

	B.Tool(TEXT("describe_toolset"), TEXT("Tools of one toolset with input schemas."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Toolset name from list_toolsets"), 64).Required())
			.Add(TEXT("include_schemas"), FToolParam::Boolean(TEXT("Include input schemas")).Default(true)))
		.Handle(&DescribeToolset);

	B.Tool(TEXT("call_tool"), TEXT("Call any tool by name (toolset.tool or toolset_tool) with its arguments."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Qualified tool name, e.g. actors.spawn_actor"), 128).Required())
			.Add(TEXT("arguments"), FToolParam::Object(TEXT("Tool arguments"))))
		.Handle(&CallTool);

	B.Tool(TEXT("get_usage_report"), TEXT("Call counts and result sizes per tool (token cost)."))
		.Handle(&GetUsageReport);

	B.Tool(TEXT("end_session"), TEXT("Drop this session's undo history (makes deletions permanent) and log subscriptions."))
		.Handle(&EndSession);
}
