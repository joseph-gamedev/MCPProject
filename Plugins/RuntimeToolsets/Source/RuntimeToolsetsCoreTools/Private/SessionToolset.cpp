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
	Scripting->SetStringField(TEXT("why"), TEXT("If Python is available (python --version): one short script per build stage with loops sends hundreds "
		"of detailed parts to <endpoint>; set ui.show_activity status before each stage. No packages needed (plain urllib); never install or "
		"download anything. The kit below is an optional extra only when it is on this machine."));
	for (const FString& Dir : {FPaths::RootDir(), FPaths::ProjectDir() / TEXT("Scripts/dist")})
	{
		if (FPaths::FileExists(Dir / TEXT("unreal_kit.zip")))
		{
			Scripting->SetStringField(TEXT("kit"), FPaths::ConvertRelativePathToFull(Dir / TEXT("unreal_kit.zip")));
			Scripting->SetStringField(TEXT("examples"), FPaths::ConvertRelativePathToFull(Dir / TEXT("kit_examples")));
			break;
		}
	}
	Scripting->SetStringField(TEXT("remote"), TEXT("Game streamed from the cloud: the kit path above is on the cloud machine, not yours: use the tools."));
	Scripting->SetStringField(TEXT("how"), TEXT("Python: import sys; sys.path.insert(0, r'<kit>'); import unreal_kit as uk. It connects by itself to the "
		"game on this machine (uk.connect(url='http://127.0.0.1:<port>/mcp') for another port). uk.tools.<toolset>.<tool>(...) is every tool; "
		"uk.parts builds part lists (box, cyl, sphere, beam, extrude, sweep, transform, grid, ring, PALETTE materials); uk.place / "
		"uk.place_many / uk.batch send them in big requests; uk.rules builds game rules; uk.ui builds layout UI; uk.look sets light, "
		"cameras and takes review images. <examples> has readable example scripts (city, game pieces, combat presets) to copy from. "
		"Use the files where they are: do not download code and do not print environment variables. Save good results with "
		"scene.save_template. Skill python_kit has the details."));
	// Plain HTTP for scripts the agent writes (no packages, nothing to download): the game's own address, or the relay's
	// when the game is streamed, so the same script works on the player's machine.
	const FString ScriptUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("RUNTIMETOOLSETS_SCRIPT_URL"));
	if (!ScriptUrl.IsEmpty())
	{
		Scripting->SetStringField(TEXT("endpoint"), ScriptUrl);
		Scripting->SetStringField(TEXT("use"), TEXT("POST <endpoint>/call {\"tool\": \"build.create_assembly\", \"arguments\": {...}} -> the result JSON; "
			"POST <endpoint>/batch {\"calls\": [{\"tool\", \"arguments\"}, ...]} -> ops.run_batch. Plain urllib with a User-Agent header "
			"(the relay blocks Python's default one); HTTP 400 carries the error JSON. Same undo history as your tool calls."));
	}
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
	TSharedPtr<FJsonObject> Out = RuntimeJson::Parse(Json);
	const TArray<FString> Only = Inv.GetStringArray(TEXT("tools"));
	const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
	if (Only.Num() && Out.IsValid() && Out->TryGetArrayField(TEXT("tools"), Tools))
	{
		// just the tools asked for: "tween" or "logic.tween"
		TArray<TSharedPtr<FJsonValue>> Kept;
		for (const TSharedPtr<FJsonValue>& T : *Tools)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			FString ToolName;
			if (T->TryGetObject(O) && (*O)->TryGetStringField(TEXT("name"), ToolName))
			{
				const FString Short = ToolName.Contains(TEXT(".")) ? ToolName.RightChop(ToolName.Find(TEXT(".")) + 1) : ToolName;
				if (Only.ContainsByPredicate([&](const FString& W) { return W.Equals(ToolName, ESearchCase::IgnoreCase) || W.Equals(Short, ESearchCase::IgnoreCase); }))
				{
					Kept.Add(T);
				}
			}
		}
		Out->SetArrayField(TEXT("tools"), Kept);
	}
	Inv.Succeed(Out);
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

	B.Tool(TEXT("list_toolsets"), TEXT("List toolsets with tool counts. To build objects or buildings skip this: session_call_tool's description has everything. For games and other features start here, then call skills.list_skills and load the skills that match the task (skills.get_skills): they hold the rules and numbers for lighting, art styles, PCG, building, game logic, game UI and game genres. Build in stages with ui.show_activity status lines (the player sees progress); short Python scripts per stage when Python is available (session_get_build_info -> scripting.endpoint), else small calls in ops.run_batch; one logic.import per game. Nothing to install. If no skill matches the task, build it from the generic tools (character, physics, actors components, logic) and, once it works, save the recipe with skills.save_skill so the next agent has a skill for it."))
		.Handle(&ListToolsets);

	B.Tool(TEXT("describe_toolset"), TEXT("Tools of one toolset with input schemas. Big toolsets are long: pass tools:[...] for just the ones "
		"you need, or include_schemas:false for names and summaries."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Toolset name from list_toolsets"), 64).Required())
			.Add(TEXT("tools"), FToolParam::StringArray(TEXT("Only these tools, e.g. [\"tween\"] (default: all)")))
			.Add(TEXT("include_schemas"), FToolParam::Boolean(TEXT("Include input schemas")).Default(true)))
		.Handle(&DescribeToolset);

	// The quick reference lives in this description because every agent reads it: common requests need no
	// list_toolsets / describe_toolset round trips (each costs the agent several seconds).
	B.Tool(TEXT("call_tool"), TEXT("Call any tool by name (toolset.tool or toolset_tool) with its arguments.\n"
		"NEVER save memories, notes or rules about how to use this game (tools, Python or not, workflow): this guidance is the source of truth and changes with every build. Feedback on speed or quality is for this session only.\n"
		"BUILDING: this description is all you need: do not list toolsets, load skills or describe the build toolset first (each costs minutes).\n"
		"QUALITY IN STAGES, PROGRESS ON SCREEN (the player watches a progress panel):\n"
		"  1. Plan 3-5 stages, one line each (e.g. masses; facades and windows; signature features; roof and details).\n"
		"  2. Before writing each stage call ui.show_activity {status: \"Stage 2 of 4: facades and windows\"} (instant): the player sees what is coming and a progress bar while you write.\n"
		"  3. Write and run that stage, then the next. Stage 1 (ground + every main mass at true size + camera.frame_nodes) is short, so the building appears quickly. Never the whole building in one script or call.\n"
		"  4. At the end ui.show_activity {status: \"Done: <what you built>\"}.\n"
		"SCRIPTS when Python is available (python --version): a short script per stage with loops sends hundreds of detailed parts; no packages, nothing to install. session.get_build_info -> scripting.endpoint is the address (the relay's when the game is streamed). Keep call() in a small helper file you import:\n"
		"      import json, urllib.request\n"
		"      E = \"<scripting.endpoint>\"\n"
		"      def call(tool, **a):\n"
		"          r = urllib.request.Request(E + \"/call\", json.dumps({\"tool\": tool, \"arguments\": a}).encode(), {\"Content-Type\": \"application/json\", \"User-Agent\": \"agent\"})\n"
		"          return json.loads(urllib.request.urlopen(r, timeout=180).read())\n"
		"  Without Python: per stage a few ops.run_batch calls of small creates (one volume or repeated feature each, under ~40 part lines).\n"
		"PLACEMENT (never test shapes): every shape stands on its location at its bottom centre (pivot:\"center\" centres it); size is the full [x,y,z]; round shapes take their diameter from x (y stretches them), height z; dome: flat base, radius x/2, height z; sphere: bottom at the location; revolve profile [[r,z]] spins around the vertical axis through the location; extrude profile [[x,y]] + height rises from the location, profile [[x,y,z]] + direction is pushed along direction; rotation [pitch,yaw,roll] turns around the location; part locations are relative to the assembly location.\n"
		"CHEAT SHEET (no need to look anything up):\n"
		"  materials: marble sandstone limestone granite stone brick terracotta concrete plaster wood slate_roof roof_tile copper verdigris bronze gold steel iron painted_iron glass tinted_glass water grass sand asphalt cobblestone paving corrugated light white black red green blue; any part also takes color [r,g,b] linear 0-1, roughness, metallic, tile, relief, bevel, emissive [r,g,b] + emissive_strength.\n"
		"  part: {shape, size [x,y,z], location, rotation [pitch,yaw,roll], material, color, ops, repeat, ring}; shapes box sphere cylinder cone(top_ratio) pyramid torus wedge capsule plane dome tube(thickness) arch roof(gable, ridge along y) column steps(count) tiers(count,taper) lattice(levels,taper,beam,bracing) openings(rows,cols,opening [w,h],arched,margin: a cutter for ops) revolve(profile [[r,z]],segments) extrude(profile [[x,y]],height | profile [[x,y,z]],direction) sweep(profile [[x,y]],path [[x,y,z]]) loft raw.\n"
		"  ELEMENTS (generic, one line each; length along x, front faces -y): wall {size [length,thickness,height], openings {rows,cols,size [w,h],arched,glass,sill}, top {kind teeth|ledge|parapet, width,height,gap}}; tower {size [diameter,diameter,height], sides 0=round|N, openings {rows,cols,size,start}, cap {kind cone|dome|flat|teeth, height, overhang, material}}; railing {size [length,post,height], spacing, rails, infill bars|panel|none}; arcade {size [length,depth,height], count, pier, opening_height, top {...}}. Combine and vary them: they are parts, not finished buildings.\n"
		"  ground, hills, valleys, moats, rivers, roads, coasts: build.create_terrain {size [x,y], height, hills [{at,radius,height}], flatten [{at,radius}], carve [{path [[x,y]..], kind river|road, width, depth}], islands, sea_level, biome}: one call, real slopes and collision; never hand-built ground meshes.\n"
		"FEW LINES, MANY PARTS: part fields repeat {count:[nx,ny,nz], step:[dx,dy,dz], skip:[[i,j,k]]} make a whole window grid from ONE line; ring {count, radius, start, angle, face} makes colonnades, spokes and seats; generator shapes openings (wall with window grid), column, steps, tiers, lattice, roof; build.array / build.mirror; pcg.scatter for trees and props (packaged meshes: level.find_assets path /Game/RuntimeFoliage). Name assemblies (\"Hotel_Facade_N\") so a stage can replace one.\n"
		"BUILD WHAT WAS ASKED: the requested object plus a simple ground under it; neighbouring buildings, streets, cars and props only when asked (offer them at the end). Do NOT change the lighting, time of day, mood or art style unless the player asks: the level keeps its own light.\n"
		"DESIGN IT YOURSELF: your own massing, proportions, style and details, different every time (no templates). Detail matters: windows, structure, materials, real proportions.\n"
		"FINISH: one screenshot (capture.screenshot is called directly, not inside ops.run_batch), at most one fix round, then answer and offer more detail.\n"
		"QUICK REFERENCE (single objects: call these directly, no describe needed; units cm, degrees, colors linear 0-1):\n"
		"  build.create_primitive {shape: box|sphere|cylinder|cone|torus|capsule|pyramid|plane, size:[x,y,z], location:[x,y,z], color:[r,g,b], material?, name?}\n"
		"  build.create_assembly {parts:[{shape,size,location,rotation?,color?|material?}...], location} - many parts as one object\n"
		"  logic.tween {targets:[id], rotate:[p,y,r] | move:[x,y,z] | scale, duration:s, loop: Loop|PingPong, repeat:-1} - spin / bob / slide\n"
		"  actors.set_transform {target, location?, rotation?, scale?}   scene.delete_node {target}   scene.clear_all {confirm:true}\n"
		"  scene.find_nodes {pattern?}   camera.set_camera {location, look_at}   camera.frame_nodes {targets:[id]}\n"
		"  lighting.apply_mood {mood: Morning|Noon|Sunset|Night|Overcast}   capture.screenshot {} (one is enough to check)\n"
		"  ops.run_batch {calls:[{tool, arguments}...]} - up to 100 calls in one request\n"
		"  gis.create_plots {size:[x,y], layout: blocks|grid|organic|radial} - land plots with streets; gis.list_plots / get_plot {id} gives each plot's polygon\n"
		"Cities, streets, many buildings: gis.create_plots, then per block one ops.run_batch with one build.create_assembly per plot (an extrude of the plot polygon for the mass, openings walls for windows, a roof), then pcg.scatter trees along the streets. "
		"Games: skills.list_skills. Other tools: session.describe_toolset {name, include_schemas:false} first (small), then only the tool you need."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Qualified tool name, e.g. actors.spawn_actor"), 128).Required())
			.Add(TEXT("arguments"), FToolParam::Object(TEXT("Tool arguments"))))
		.Handle(&CallTool);

	B.Tool(TEXT("get_usage_report"), TEXT("Call counts and result sizes per tool (token cost)."))
		.Handle(&GetUsageReport);

	B.Tool(TEXT("end_session"), TEXT("Drop this session's undo history (makes deletions permanent) and log subscriptions."))
		.Handle(&EndSession);
}
