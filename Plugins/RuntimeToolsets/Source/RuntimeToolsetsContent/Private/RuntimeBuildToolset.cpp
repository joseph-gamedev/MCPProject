#include "RuntimeBuildToolset.h"
#include "TerrainMesh.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeToolsetsGroupActor.h"
#include "PrimitiveMesh.h"
#include "RuntimeActorUtils.h"
#include "RuntimeImage.h"
#include "RuntimeToolHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"

#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "StaticMeshResources.h"
#include "UDynamicMesh.h"

using namespace RuntimeTools;

namespace
{
	constexpr int32 BuildMaxCopies = 1000;
	constexpr int32 BuildMaxParts = 8192;

	FToolInvocation& BuildCall()
	{
		return *RuntimeToolCall::Current();
	}

	template <typename TEnum>
	FString BuildEnumText(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value)).ToLower();
	}

	/** The scene node of an actor; fails the call when it is not one. */
	FRuntimeSceneNode* BuildRequireNode(AActor* Actor, const TCHAR* Arg)
	{
		FRuntimeSceneNode* Node = Actor ? RuntimeToolCall::GetGraph()->FindByActor(Actor) : nullptr;
		if (!Node)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is not a scene node"), Arg),
				TEXT("Pass a node id from build.*, actors.spawn_actor or scene.find_nodes"));
		}
		return Node;
	}

	/** A builder (prim:) node and its recipe; fails the call otherwise. */
	FRuntimeSceneNode* BuildRequirePrim(AActor* Actor, const TCHAR* Arg, TSharedPtr<FJsonObject>& OutRecipe)
	{
		FRuntimeSceneNode* Node = BuildRequireNode(Actor, Arg);
		if (!Node)
		{
			return nullptr;
		}
		OutRecipe = RuntimePrimitives::FromSource(Node->Source);
		if (!OutRecipe.IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s (%s) is not a builder object"), Arg, *Node->Id),
				TEXT("Create it with build.create_primitive, create_profile_solid or create_assembly"));
			return nullptr;
		}
		return Node;
	}

	/** Swaps a builder node's recipe (rebuilding its mesh); undo swaps back. */
	TUniquePtr<IRuntimeCommand> MakeRecipeCommand(const FString& Label, const FString& NodeId, const FString& OldSource, const FString& NewSource,
		TSharedPtr<FJsonObject> Delta)
	{
		auto Apply = [NodeId](FRuntimeCommandContext& Ctx, const FString& Source, FString& Error)
		{
			FRuntimeSceneNode* Node = Ctx.Graph ? Ctx.Graph->Find(NodeId) : nullptr;
			AActor* Actor = Node ? Node->Actor.Get() : nullptr;
			if (!Actor)
			{
				Error = TEXT("node no longer exists");
				return false;
			}
			if (!RuntimePrimitives::Rebuild(Actor, RuntimePrimitives::FromSource(Source).ToSharedRef(), Error))
			{
				return false;
			}
			Node->Source = Source;
			return true;
		};
		return MakeUnique<FLambdaCommand>(Label,
			[Apply, NewSource](FRuntimeCommandContext& Ctx, FString& Error) { return Apply(Ctx, NewSource, Error); },
			[Apply, OldSource](FRuntimeCommandContext& Ctx) { FString Error; return Apply(Ctx, OldSource, Error); }, Delta);
	}

	/**
	 * Copies a recipe field when bSet (the typed value is not the "unset" default). The typed parameters define the schema
	 * and validate the values; the recipe keeps the exact JSON the caller sent (the recipe format predates the reflected
	 * tools and scripts depend on it). Struct and array fields without a default are only present when passed.
	 */
	void BuildCopyArg(const TSharedRef<FJsonObject>& Recipe, const TCHAR* Field, bool bSet = true)
	{
		const TSharedPtr<FJsonValue> V = BuildCall().GetValue(Field);
		if (bSet && V.IsValid() && !V->IsNull())
		{
			Recipe->SetField(Field, V);
		}
	}

	/** Material fields shared by every create tool and set_material (-1 / "" = keep the preset value). */
	void BuildCopyMaterialArgs(const TSharedRef<FJsonObject>& Recipe, const FString& Material, float Roughness, float Metallic, float EmissiveStrength,
		float Opacity, float Variation, const FString& Style, float Tile, float Relief)
	{
		BuildCopyArg(Recipe, TEXT("material"), !Material.IsEmpty());
		BuildCopyArg(Recipe, TEXT("color"));
		BuildCopyArg(Recipe, TEXT("emissive"));
		BuildCopyArg(Recipe, TEXT("roughness"), Roughness >= 0.f);
		BuildCopyArg(Recipe, TEXT("metallic"), Metallic >= 0.f);
		BuildCopyArg(Recipe, TEXT("emissive_strength"), EmissiveStrength >= 0.f);
		BuildCopyArg(Recipe, TEXT("opacity"), Opacity >= 0.f);
		BuildCopyArg(Recipe, TEXT("variation"), Variation >= 0.f);
		BuildCopyArg(Recipe, TEXT("style"), !Style.IsEmpty());
		BuildCopyArg(Recipe, TEXT("tile"), Tile >= 0.f);
		BuildCopyArg(Recipe, TEXT("relief"), Relief >= 0.f);
	}

	FTransform BuildArgTransform(const FVector& Location, const FRotator& Rotation, const FVector& Scale)
	{
		FTransform T = FTransform::Identity;
		if (BuildCall().Has(TEXT("location")))
		{
			T.SetLocation(Location);
		}
		if (BuildCall().Has(TEXT("rotation")))
		{
			T.SetRotation(Rotation.Quaternion());
		}
		if (BuildCall().Has(TEXT("scale")))
		{
			T.SetScale3D(Scale);
		}
		return T;
	}

	/** Moves Relative so the bottom of LocalBox rests on the ground below (the parent's transform taken into account). */
	bool BuildSnapToGround(const FBox& LocalBox, const AActor* ParentActor, FTransform& Relative)
	{
		UWorld* World = RuntimeToolCall::GetWorld();
		if (!World || !LocalBox.IsValid)
		{
			return false;
		}
		const FTransform ParentWorld = ParentActor ? ParentActor->GetActorTransform() : FTransform::Identity;
		const FTransform World0 = Relative * ParentWorld;
		const FBox Box = LocalBox.TransformBy(World0);
		const FVector Start(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 1.0);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsBuildSnap), true);
		if (!World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 100000.0), ECC_Visibility, Params))
		{
			return false;
		}
		FTransform Moved = World0;
		Moved.AddToTranslation(FVector(0, 0, Hit.ImpactPoint.Z - Box.Min.Z));
		Relative = Moved.GetRelativeTransform(ParentWorld);
		return true;
	}

	/** Validates the recipe, spawns the builder node (one undo step) and describes it. */
	bool CreateFromRecipe(const TSharedRef<FJsonObject>& Recipe, const FString& Name, AActor* Parent, FTransform Transform, const TArray<FString>& Tags,
		bool bSnap, FBuildToolAssemblyResult& Out)
	{
		FString ParentId;
		if (Parent)
		{
			const FRuntimeSceneNode* P = BuildRequireNode(Parent, TEXT("parent"));
			if (!P)
			{
				return false;
			}
			ParentId = P->Id;
		}
		// Build once: errors surface as invalid_arguments (not command failures) and the same mesh becomes the actor.
		TSharedRef<RuntimePrimitives::FPrebuilt> Built = MakeShared<RuntimePrimitives::FPrebuilt>();
		FString Error;
		if (!RuntimePrimitives::BuildPrebuilt(Recipe, *Built, Error) || !RuntimePrimitives::ValidateMaterials(Recipe, Error))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Error, TEXT("See build.get_guide and build.list_materials"));
			return false;
		}
		if (bSnap)
		{
			FBox Local(ForceInit);
			const UE::Geometry::FAxisAlignedBox3d B = Built->Mesh.GetBounds();
			if (!B.IsEmpty())
			{
				Local = FBox(FVector(B.Min), FVector(B.Max));
			}
			Out.bSnappedToGround = BuildSnapToGround(Local, Parent, Transform);
		}
		const FString Source = RuntimePrimitives::ToSource(Recipe);
		const TSharedRef<const RuntimePrimitives::FPrebuilt> Shared = Built;
		auto Spawn = [Shared, Recipe, Transform, Name](FRuntimeCommandContext& Ctx, FString& Err)
		{
			return RuntimePrimitives::SpawnPrebuilt(Ctx.World, Shared, Recipe, Transform, Name, Err);
		};
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("build %s"), *Name), Spawn, Name, ParentId, ERuntimeNodeKind::Actor,
			Source, ToNames(Tags));
		FCreateNodeCommand* Raw = Cmd.Get();
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return false;
		}
		FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
		Out.Id = Raw->GetNodeId();
		Out.Path = Graph->GetPath(Out.Id);
		if (const FRuntimeSceneNode* Node = Graph->Find(Out.Id))
		{
			Out.Name = Node->Name;
			Out.Triangles = RuntimePrimitives::TriangleCount(Node->Actor.Get());
		}
		if (Recipe->GetStringField(TEXT("shape")) == TEXT("assembly"))
		{
			const RuntimePrimitives::FAssemblyStats& Stats = RuntimePrimitives::LastAssemblyStats();
			Out.Parts = Stats.Parts;
			Out.Materials = Stats.Materials;
			Out.CoplanarFacesSeparated = Stats.CoplanarSeparated;
		}
		return true;
	}

	FBuildToolNodeResult ToNodeResult(const FBuildToolAssemblyResult& A)
	{
		FBuildToolNodeResult R;
		R.Id = A.Id;
		R.Name = A.Name;
		R.Path = A.Path;
		R.Triangles = A.Triangles;
		R.bSnappedToGround = A.bSnappedToGround;
		return R;
	}

	/** Spawns copies of a node at relative transforms (same parent). One undo step. */
	FBuildToolCopiesResult SpawnCopies(const FRuntimeSceneNode& Original, const TArray<FTransform>& Relatives, const FString& Label)
	{
		FBuildToolCopiesResult Out;
		if (Original.Kind == ERuntimeNodeKind::Group)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("arrays and mirrors work on single nodes, not groups"), TEXT("Use scene.duplicate_node for groups"));
			return Out;
		}
		if (!IRuntimeToolsetsCore::Get().HasSourceSpawner(Original.Source))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("node source '%s' cannot be copied"), *Original.Source.Left(40)),
				TEXT("Copy builder objects, shapes or class actors"));
			return Out;
		}
		TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(Label);
		TArray<FCreateNodeCommand*> Created;
		TWeakObjectPtr<AActor> Template = Original.Actor;
		const bool bCopyMaterials = !Original.Source.StartsWith(TEXT("prim:"));
		for (int32 i = 0; i < Relatives.Num(); ++i)
		{
			const FString Source = Original.Source;
			const FTransform T = Relatives[i];
			const FString Name = FString::Printf(TEXT("%s_%d"), *Original.Name, i + 1);
			auto Spawn = [Source, T, Name, Template, bCopyMaterials](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
			{
				AActor* A = IRuntimeToolsetsCore::Get().SpawnFromSource(Ctx.World, Source, T, Name, Err);
				if (A && bCopyMaterials && Template.IsValid())
				{
					RuntimeActorUtils::CopyMaterials(Template.Get(), A);
				}
				return A;
			};
			TUniquePtr<FCreateNodeCommand> Create = MakeUnique<FCreateNodeCommand>(Label, Spawn, Name, Original.ParentId, Original.Kind, Source, Original.Tags);
			Created.Add(Create.Get());
			Cmd->Add(MoveTemp(Create));
		}
		if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
		{
			return Out;
		}
		Out.Created = Created.Num();
		for (const FCreateNodeCommand* C : Created)
		{
			if (Out.Ids.Num() < 100)
			{
				Out.Ids.Add(C->GetNodeId());
			}
		}
		return Out;
	}

	FTransform BuildRelativeOf(const AActor* Actor)
	{
		const USceneComponent* Root = Actor->GetRootComponent();
		return Root ? Root->GetRelativeTransform() : Actor->GetActorTransform();
	}

	/** Live, non-group nodes below Under (or all). Fails the call when Under is not a node. */
	TArray<FRuntimeSceneNode*> BuildScopeNodes(AActor* Under, bool& bOk)
	{
		bOk = true;
		FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
		TArray<FString> Ids;
		if (Under)
		{
			const FRuntimeSceneNode* Root = BuildRequireNode(Under, TEXT("under"));
			if (!Root)
			{
				bOk = false;
				return {};
			}
			Ids = Graph.CollectSubtree(Root->Id);
		}
		else
		{
			for (const FString& R : Graph.GetRootIds())
			{
				Ids.Append(Graph.CollectSubtree(R));
			}
		}
		TArray<FRuntimeSceneNode*> Out;
		for (const FString& Id : Ids)
		{
			FRuntimeSceneNode* N = Graph.Find(Id);
			if (N && N->Kind != ERuntimeNodeKind::Group && N->Actor.IsValid())
			{
				Out.Add(N);
			}
		}
		return Out;
	}

	const TCHAR* BuilderGuide = TEXT(R"GUIDE(
QUALITY BAR: a recognisable, realistic recreation at TRUE SCALE. Hero landmarks need 500-3000 parts, not 30.
Units: cm, Z up, rotations [pitch,yaw,roll]. Use real dimensions (Eiffel Tower 33000 tall, base 12500 square;
Great Pyramid base 23040, height 13860; Taj Mahal dome top 7300).

QUALITY IN STAGES, PROGRESS ON SCREEN (the player watches a progress panel):
  1. Plan 3-5 stages (masses; facades and windows; signature features; roof and details).
  2. Before each stage ui.show_activity {status:"Stage 2 of 4: facades"} (instant: progress bar while you write).
  3. Write and run the stage: a short script with loops when Python is available (session_get_build_info ->
     scripting.endpoint, plain urllib), else a few ops.run_batch calls. Stage 1 (ground + main masses +
     camera.frame_nodes) is short. Never the whole building in one script or call.
  4. ui.show_activity {status:"Done: ..."}; one screenshot, at most one fix round, answer and offer more.
ELEMENTS (generic, one part line each): wall (openings grid, top {kind teeth|ledge|parapet}), tower (round or N sides,
  window rows, cap {kind cone|dome|flat|teeth}), railing (posts, rails, bars or glass), arcade (arches between piers). Combine them.
GROUND: hills, moats, rivers, roads and coasts with build.create_terrain (flatten pads, carve channels), never hand-built.
FEW LINES, MANY PARTS: part fields repeat {count:[nx,ny,nz], step:[dx,dy,dz], skip:[[i,j,k]]} (a window grid is ONE
  line) and ring {count, radius, start, angle, face} (colonnades, spokes, seats); generator shapes openings, column,
  steps, tiers, lattice, roof; build.array / mirror; pcg.scatter for trees and props (packaged meshes,
  level.find_assets path /Game/RuntimeFoliage). Group a building's pieces with scene.create_group.
BUILD WHAT WAS ASKED: the object plus a simple ground; neighbours, streets, cars and props only when asked.
DESIGN IT YOURSELF: every building gets its own massing, proportions, style and details (no templates, no clones).
REUSE - scene.save_template {target, name, description} keeps anything you built; scene.place_template places copies
  in later sessions; scene.list_templates shows what exists. Check it before building something again.

PART SHAPES (create_assembly parts / create_primitive):
  box sphere cylinder cone pyramid torus wedge capsule plane dome tube arch roof(gable)
  generators: lattice(levels,taper,beam,bracing) tiers(count,taper) steps(count) column openings(rows,cols,arched)
  revolve{profile:[[r,z]..],segments} - domes, bulbs, finials, drums, vases
  loft{sections:[[[x,y,z]..]..]} - rings with equal point counts: curved/tapering towers, legs, onion domes, spires
  extrude{profile:[[x,y,z]..],direction} - flat outline pushed out: facade panels, arch frames, ornaments
  sweep{profile:[[x,y]..],path:[[x,y,z]..]} - rails, cornices, cables, arch ribs, mouldings
  raw{vertices,triangles} - anything else. ops:[{op:subtract,tool:{openings...}}] cuts windows/arcades.
  Every part takes location/rotation/scale and its own material fields.

MATERIALS sell realism. Presets carry PBR values AND a procedural surface (world-projected, no UVs):
  marble(veins) limestone/sandstone/granite/stone/cobblestone(blocks) brick(bond+mortar) roof_tile/terracotta/slate_roof
  (tile rows) concrete(panels) plaster wood(planks) copper/bronze/gold/steel/iron(brushed) painted_iron verdigris glass
  tinted_glass water grass sand asphalt light. Override color (LINEAR rgb: real colours are darker than you think,
  e.g. limestone [0.42,0.34,0.24], weathered iron [0.15,0.1,0.065]), roughness, metallic, style, tile (pattern cm),
  relief (bump cm). Use 3-6 related materials per building, never one flat colour. bevel 1-5% on hard edges.
  build.list_materials lists every preset and style.

WORKFLOW:
0. Something visible first: the player watches the game, not your plan. If the scene is empty (scene.list_tree shows
   no ground under the build site), create a floor first (build.create_primitive box, about 1.5x the build's footprint,
   20 cm thick, neutral material) and point the view at it (camera.frame_nodes). After each batch, frame the new work so
   the player sees it appear.
1. Plan: list the defining features and their real measurements (silhouette first, then secondary forms, then detail).
2. Build site + massing in one batch; check build.capture_view (front/right/top/perspective). Fix proportions.
3. Add detail layers (structure, openings, ornament, railings) in batches; group with scene.create_group + parent.
4. Context: a ground much larger than the view (sand/grass/asphalt), paths, trees, plinths. Nothing floats
   (snap_to_ground on create tools, layout.drop_to_ground for existing objects).
5. Light: leave the level's lighting, time of day and style alone unless the player asks for them (then lighting.apply_mood).
6. Verify with capture.screenshot from a wide and a close view; compare with the real landmark and refine at least twice.
7. Frame with camera.set_camera or camera.frame_nodes; the player keeps WASD/mouse fly control.
Only if the user asks to see progress: ui.show_activity {enabled:true, status:"..."}.
)GUIDE");
}

// ---- guide --------------------------------------------------------------------------------------------------------------

void URuntimeBuildToolset::GetGuide(FString& Guide)
{
	Guide = FString(BuilderGuide).TrimStartAndEnd();
}

FBuildToolMaterialList URuntimeBuildToolset::ListMaterials()
{
	FBuildToolMaterialList Out;
	Out.Presets = RuntimePrimitives::PaletteNames();
	Out.Styles = RuntimePrimitives::StyleNames();
	Out.Shapes = RuntimePrimitives::ShapeNames();
	Out.Shapes.Remove(TEXT("assembly"));
	return Out;
}

// ---- create -------------------------------------------------------------------------------------------------------------

FBuildToolNodeResult URuntimeBuildToolset::CreatePrimitive(EBuildToolShape Shape, const FVector& Size, const FVector& Location, const FRotator& Rotation,
	const FVector& Scale, const TArray<FString>& Tags, const FVector& Color, const FVector& Emissive, const FVector2D& Opening, const FString& Name,
	AActor* Parent, EBuildToolDetail Detail, EBuildToolPivot Pivot, const FString& Material, float Roughness, float Metallic, float EmissiveStrength,
	float Opacity, float Variation, const FString& Style, float Tile, float Relief, float Bevel, float TopRatio, float Thickness, int32 Levels, float Taper,
	float Beam, bool bBracing, int32 Count, int32 Rows, int32 Cols, bool bArched, float Margin, bool bSnapToGround)
{
	TSharedRef<FJsonObject> Recipe = Obj();
	const FString ShapeName = BuildEnumText(Shape);
	Recipe->SetStringField(TEXT("shape"), ShapeName);
	if (Detail != EBuildToolDetail::Medium)
	{
		Recipe->SetStringField(TEXT("detail"), BuildEnumText(Detail));
	}
	if (Pivot != EBuildToolPivot::Base)
	{
		Recipe->SetStringField(TEXT("pivot"), BuildEnumText(Pivot));
	}
	BuildCopyArg(Recipe, TEXT("size"));
	BuildCopyArg(Recipe, TEXT("opening"));
	BuildCopyArg(Recipe, TEXT("top_ratio"), TopRatio >= 0.f);
	BuildCopyArg(Recipe, TEXT("thickness"), Thickness >= 0.f);
	BuildCopyArg(Recipe, TEXT("bevel"), Bevel >= 0.f);  // -1 = automatic, 0 = sharp
	BuildCopyArg(Recipe, TEXT("count"), Count > 0);
	BuildCopyArg(Recipe, TEXT("taper"), Taper >= 0.f);
	BuildCopyArg(Recipe, TEXT("beam"), Beam >= 0.f);
	BuildCopyArg(Recipe, TEXT("levels"), Levels > 0);
	BuildCopyArg(Recipe, TEXT("bracing"), !bBracing);
	BuildCopyArg(Recipe, TEXT("rows"), Rows > 0);
	BuildCopyArg(Recipe, TEXT("cols"), Cols > 0);
	BuildCopyArg(Recipe, TEXT("arched"), bArched);
	BuildCopyArg(Recipe, TEXT("margin"), Margin >= 0.f);
	BuildCopyMaterialArgs(Recipe, Material, Roughness, Metallic, EmissiveStrength, Opacity, Variation, Style, Tile, Relief);
	FBuildToolAssemblyResult Out;
	CreateFromRecipe(Recipe, Name.IsEmpty() ? ShapeName : Name, Parent, BuildArgTransform(Location, Rotation, Scale), Tags, bSnapToGround, Out);
	return ToNodeResult(Out);
}

FBuildToolNodeResult URuntimeBuildToolset::CreateProfileSolid(EBuildToolSolidKind Kind, const TArray<FVector2D>& Profile, const TArray<FVector>& Path,
	const FVector& Location, const FRotator& Rotation, const FVector& Scale, const TArray<FString>& Tags, const FVector& Color, const FVector& Emissive,
	float Height, float Angle, bool bClosed, const FString& Name, AActor* Parent, EBuildToolDetail Detail, const FString& Material, float Roughness,
	float Metallic, float EmissiveStrength, float Opacity, float Variation, const FString& Style, float Tile, float Relief, float Bevel, bool bSnapToGround)
{
	const FString KindName = BuildEnumText(Kind);
	const int32 MinPoints = Kind == EBuildToolSolidKind::Lathe ? 2 : 3;
	if (Profile.Num() < MinPoints)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s needs at least %d profile points (got %d)"), *KindName, MinPoints, Profile.Num()),
			Kind == EBuildToolSolidKind::Lathe ? TEXT("profile is a [radius,height] path, e.g. [[0,0],[40,0],[30,60],[0,90]]") : TEXT("profile is a closed polygon [[x,y],...]"));
		return {};
	}
	if (Kind == EBuildToolSolidKind::Sweep && Path.Num() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("sweep needs a path of at least 2 points"), TEXT("path: [[0,0,0],[200,0,0],...]"));
		return {};
	}
	TSharedRef<FJsonObject> Recipe = Obj();
	Recipe->SetStringField(TEXT("shape"), KindName);
	if (Detail != EBuildToolDetail::Medium)
	{
		Recipe->SetStringField(TEXT("detail"), BuildEnumText(Detail));
	}
	BuildCopyArg(Recipe, TEXT("profile"));
	BuildCopyArg(Recipe, TEXT("path"));
	BuildCopyArg(Recipe, TEXT("height"), Height >= 0.f);
	BuildCopyArg(Recipe, TEXT("angle"), Angle >= 0.f);
	BuildCopyArg(Recipe, TEXT("closed"), bClosed);
	BuildCopyArg(Recipe, TEXT("bevel"), Bevel >= 0.f);  // -1 = automatic, 0 = sharp
	BuildCopyMaterialArgs(Recipe, Material, Roughness, Metallic, EmissiveStrength, Opacity, Variation, Style, Tile, Relief);
	FBuildToolAssemblyResult Out;
	CreateFromRecipe(Recipe, Name.IsEmpty() ? KindName : Name, Parent, BuildArgTransform(Location, Rotation, Scale), Tags, bSnapToGround, Out);
	return ToNodeResult(Out);
}

namespace
{
	TSharedPtr<FJsonValue> Vec3(double X, double Y, double Z)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(X), MakeShared<FJsonValueNumber>(Y), MakeShared<FJsonValueNumber>(Z)});
	}

	bool ReadVec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() < 3)
		{
			return false;
		}
		Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		return true;
	}

	/**
	 * Expands parts with "ring" (copies around a circle centred on the part's location, facing outward) and "repeat"
	 * (a row or grid of copies, offsets in assembly space) into plain parts.
	 */
	bool ExpandPartCopies(const TArray<TSharedPtr<FJsonValue>>& In, int32 MaxParts, TArray<TSharedPtr<FJsonValue>>& Out, FString& Error)
	{
		for (int32 PartIndex = 0; PartIndex < In.Num(); ++PartIndex)
		{
			const TSharedPtr<FJsonObject> Part = In[PartIndex]->AsObject();
			if (!Part.IsValid() || (!Part->HasField(TEXT("repeat")) && !Part->HasField(TEXT("ring"))))
			{
				Out.Add(In[PartIndex]);
				continue;
			}
			FVector Base = FVector::ZeroVector, Rot = FVector::ZeroVector; // rotation as [pitch, yaw, roll]
			ReadVec3(Part, TEXT("location"), Base);
			ReadVec3(Part, TEXT("rotation"), Rot);
			// ring: positions (relative to the part's location) and extra yaw
			TArray<TPair<FVector, double>> Ring{TPair<FVector, double>(FVector::ZeroVector, 0.0)};
			const TSharedPtr<FJsonObject>* RingObj = nullptr;
			if (Part->TryGetObjectField(TEXT("ring"), RingObj))
			{
				const int32 Count = FMath::Clamp(static_cast<int32>((*RingObj)->GetNumberField(TEXT("count"))), 1, MaxParts);
				const double Radius = (*RingObj)->HasField(TEXT("radius")) ? (*RingObj)->GetNumberField(TEXT("radius")) : 0.0;
				const double Start = (*RingObj)->HasField(TEXT("start")) ? (*RingObj)->GetNumberField(TEXT("start")) : 0.0;
				const double Sweep = (*RingObj)->HasField(TEXT("angle")) ? (*RingObj)->GetNumberField(TEXT("angle")) : 360.0;
				bool bFace = true;
				(*RingObj)->TryGetBoolField(TEXT("face"), bFace);
				// a full circle spaces copies count ways; an arc puts the first and last copy on its ends
				const double StepDeg = FMath::Abs(Sweep) >= 359.999 ? Sweep / Count : (Count > 1 ? Sweep / (Count - 1) : 0.0);
				Ring.Reset();
				for (int32 i = 0; i < Count; ++i)
				{
					const double A = Start + StepDeg * i;
					const double R = FMath::DegreesToRadians(A);
					Ring.Add(TPair<FVector, double>(FVector(Radius * FMath::Cos(R), Radius * FMath::Sin(R), 0.0), bFace ? A : 0.0));
				}
			}
			// repeat: grid offsets
			TArray<FVector> Grid{FVector::ZeroVector};
			const TSharedPtr<FJsonObject>* RepObj = nullptr;
			if (Part->TryGetObjectField(TEXT("repeat"), RepObj))
			{
				FIntVector N(1, 1, 1);
				FVector Step = FVector::ZeroVector;
				const TArray<TSharedPtr<FJsonValue>>* CountArr = nullptr;
				const bool bGrid = (*RepObj)->TryGetArrayField(TEXT("count"), CountArr);
				if (bGrid)
				{
					for (int32 k = 0; k < 3 && k < CountArr->Num(); ++k)
					{
						N[k] = FMath::Max(1, static_cast<int32>((*CountArr)[k]->AsNumber()));
					}
				}
				else
				{
					N.X = FMath::Max(1, static_cast<int32>((*RepObj)->GetNumberField(TEXT("count"))));
				}
				ReadVec3(*RepObj, TEXT("step"), Step);
				TSet<FIntVector> Skip;
				const TArray<TSharedPtr<FJsonValue>>* SkipArr = nullptr;
				if ((*RepObj)->TryGetArrayField(TEXT("skip"), SkipArr))
				{
					for (const TSharedPtr<FJsonValue>& S : *SkipArr)
					{
						const TArray<TSharedPtr<FJsonValue>>* Idx = nullptr;
						FIntVector I(0, 0, 0);
						if (S->TryGetArray(Idx))
						{
							for (int32 k = 0; k < 3 && k < Idx->Num(); ++k)
							{
								I[k] = static_cast<int32>((*Idx)[k]->AsNumber());
							}
						}
						else
						{
							I.X = static_cast<int32>(S->AsNumber());
						}
						Skip.Add(I);
					}
				}
				if (static_cast<int64>(N.X) * N.Y * N.Z > MaxParts)
				{
					Error = FString::Printf(TEXT("part %d: repeat makes %lld copies (max %d)"), PartIndex, static_cast<int64>(N.X) * N.Y * N.Z, MaxParts);
					return false;
				}
				Grid.Reset();
				for (int32 z = 0; z < N.Z; ++z)
				{
					for (int32 y = 0; y < N.Y; ++y)
					{
						for (int32 x = 0; x < N.X; ++x)
						{
							if (!Skip.Contains(FIntVector(x, y, z)))
							{
								// one count with a step vector: copies along that vector
								Grid.Add(bGrid ? FVector(Step.X * x, Step.Y * y, Step.Z * z) : Step * x);
							}
						}
					}
				}
			}
			if (Out.Num() + static_cast<int64>(Ring.Num()) * Grid.Num() > MaxParts)
			{
				Error = FString::Printf(TEXT("part %d: repeat/ring copies make more than %d parts; split into several assemblies"), PartIndex, MaxParts);
				return false;
			}
			for (const TPair<FVector, double>& R : Ring)
			{
				for (const FVector& G : Grid)
				{
					TSharedRef<FJsonObject> Copy = MakeShared<FJsonObject>(*Part);
					Copy->RemoveField(TEXT("repeat"));
					Copy->RemoveField(TEXT("ring"));
					const FVector L = Base + R.Key + G;
					Copy->SetField(TEXT("location"), Vec3(L.X, L.Y, L.Z));
					if (R.Value != 0.0 || Part->HasField(TEXT("rotation")))
					{
						Copy->SetField(TEXT("rotation"), Vec3(Rot.X, Rot.Y + R.Value, Rot.Z));
					}
					Out.Add(MakeShared<FJsonValueObject>(Copy));
				}
			}
		}
		if (Out.Num() == 0 || Out.Num() > MaxParts)
		{
			Error = FString::Printf(TEXT("parts needs 1-%d entries after repeat/ring (got %d)"), MaxParts, Out.Num());
			return false;
		}
		return true;
	}

	// ---- generic elements: one part line expands into the parts of a wall, tower, railing or arcade ----------------

	double Field(const TSharedPtr<FJsonObject>& O, const TCHAR* Name, double Default)
	{
		double V = Default;
		return O.IsValid() && O->TryGetNumberField(Name, V) ? V : Default;
	}

	const TCHAR* const InheritedFields[] = {TEXT("material"), TEXT("color"), TEXT("roughness"), TEXT("metallic"), TEXT("style"), TEXT("tile"),
		TEXT("relief"), TEXT("variation"), TEXT("bevel"), TEXT("detail"), TEXT("emissive"), TEXT("emissive_strength"), TEXT("opacity")};

	/** Emits the parts of one element in the element's own space (length along X, front facing -Y, base at the location). */
	struct FElementBuilder
	{
		TSharedPtr<FJsonObject> Src;
		FVector Origin = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		TArray<TSharedPtr<FJsonValue>>* Out = nullptr;

		/** Material: nullptr = the element's own; "glass" etc = that preset; an object {material, color, ...} = those fields. */
		TSharedRef<FJsonObject> Add(const TCHAR* Shape, const FVector& Size, const FVector& Local, const FRotator& LocalRot = FRotator::ZeroRotator,
			const TCHAR* Preset = nullptr, const TSharedPtr<FJsonObject>& Fields = nullptr)
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			if (!Preset)
			{
				for (const TCHAR* F : InheritedFields)
				{
					if (TSharedPtr<FJsonValue> V = Src->TryGetField(F))
					{
						P->SetField(F, V);
					}
				}
			}
			else
			{
				P->SetStringField(TEXT("material"), Preset);
			}
			if (Fields.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& F : Fields->Values)
				{
					P->SetField(F.Key, F.Value);
				}
			}
			P->SetStringField(TEXT("shape"), Shape);
			P->SetField(TEXT("size"), Vec3(Size.X, Size.Y, Size.Z));
			const FVector W = Origin + Rot.RotateVector(Local);
			P->SetField(TEXT("location"), Vec3(W.X, W.Y, W.Z));
			const FRotator R = (Rot * LocalRot.Quaternion()).Rotator();
			P->SetField(TEXT("rotation"), Vec3(R.Pitch, R.Yaw, R.Roll));
			Out->Add(MakeShared<FJsonValueObject>(P));
			return P;
		}

		/** Material fields given for a sub-part, e.g. "cap": {"material": "copper"} or "frame_material": "bronze". */
		TSharedPtr<FJsonObject> SubFields(const TSharedPtr<FJsonObject>& Sub) const
		{
			if (!Sub.IsValid())
			{
				return nullptr;
			}
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			for (const TCHAR* Name : InheritedFields)
			{
				if (TSharedPtr<FJsonValue> V = Sub->TryGetField(Name))
				{
					F->SetField(Name, V);
				}
			}
			return F->Values.Num() ? F : nullptr;
		}

		/** Teeth, a ledge (cornice) or a parapet along the top of a straight element. */
		void Top(const TSharedPtr<FJsonObject>& T, double L, double Th, double H)
		{
			if (!T.IsValid())
			{
				return;
			}
			FString Style = TEXT("teeth");
			T->TryGetStringField(TEXT("kind"), Style);
			const TSharedPtr<FJsonObject> F = SubFields(T);
			if (Style == TEXT("ledge"))
			{
				const double D = Field(T, TEXT("depth"), 15.0), Hh = Field(T, TEXT("height"), 25.0);
				Add(TEXT("box"), FVector(L + 2 * D, Th + 2 * D, Hh), FVector(0, 0, H), FRotator::ZeroRotator, nullptr, F);
			}
			else if (Style == TEXT("parapet"))
			{
				Add(TEXT("box"), FVector(L, FMath::Max(10.0, Th * 0.5), Field(T, TEXT("height"), 100.0)), FVector(0, 0, H), FRotator::ZeroRotator, nullptr, F);
			}
			else
			{
				const double W = Field(T, TEXT("width"), FMath::Max(40.0, Th * 1.5)), Hh = Field(T, TEXT("height"), FMath::Max(50.0, Th * 1.5));
				const double Gap = Field(T, TEXT("gap"), W);
				const int32 N = FMath::Max(1, FMath::FloorToInt((L + Gap) / (W + Gap)));
				const double Span = N * W + (N - 1) * Gap;
				for (int32 i = 0; i < N; ++i)
				{
					Add(TEXT("box"), FVector(W, Th, Hh), FVector(-Span / 2 + W / 2 + i * (W + Gap), 0, H), FRotator::ZeroRotator, nullptr, F);
				}
			}
		}
	};

	/** A grid of openings as a boolean cutter (the "openings" generator) for a face L long, H high, Th thick. */
	TSharedPtr<FJsonValue> OpeningsCut(double L, double Th, double H, int32 Rows, int32 Cols, const FVector2D& Opening, bool bArched, double Margin, double Z)
	{
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("shape"), TEXT("openings"));
		Tool->SetField(TEXT("size"), Vec3(L, Th + 40.0, H));
		Tool->SetNumberField(TEXT("rows"), Rows);
		Tool->SetNumberField(TEXT("cols"), Cols);
		Tool->SetArrayField(TEXT("opening"), {MakeShared<FJsonValueNumber>(Opening.X), MakeShared<FJsonValueNumber>(Opening.Y)});
		Tool->SetBoolField(TEXT("arched"), bArched);
		Tool->SetNumberField(TEXT("margin"), Margin);
		TSharedRef<FJsonObject> Op = MakeShared<FJsonObject>();
		Op->SetStringField(TEXT("op"), TEXT("subtract"));
		Op->SetObjectField(TEXT("tool"), Tool);
		Op->SetField(TEXT("location"), Vec3(0, 0, Z));
		return MakeShared<FJsonValueObject>(Op);
	}

	bool ExpandElement(const TSharedPtr<FJsonObject>& Part, const FString& Shape, TArray<TSharedPtr<FJsonValue>>& Out, FString& Error)
	{
		FElementBuilder B;
		B.Src = Part;
		B.Out = &Out;
		FVector Rot3 = FVector::ZeroVector;
		ReadVec3(Part, TEXT("location"), B.Origin);
		if (ReadVec3(Part, TEXT("rotation"), Rot3))
		{
			B.Rot = FRotator(Rot3.X, Rot3.Y, Rot3.Z).Quaternion();
		}
		FVector S;
		const TSharedPtr<FJsonObject>* OpenObj = nullptr;
		const TSharedPtr<FJsonObject> Open = Part->TryGetObjectField(TEXT("openings"), OpenObj) ? *OpenObj : nullptr;
		const TSharedPtr<FJsonObject>* TopObj = nullptr;
		const TSharedPtr<FJsonObject> TopSpec = Part->TryGetObjectField(TEXT("top"), TopObj) ? *TopObj : nullptr;
		FString GlassPreset = TEXT("glass");
		Part->TryGetStringField(TEXT("glass_material"), GlassPreset);

		if (Shape == TEXT("wall"))
		{
			S = FVector(400, 30, 300);
			ReadVec3(Part, TEXT("size"), S);
			TSharedRef<FJsonObject> Body = B.Add(TEXT("box"), S, FVector::ZeroVector);
			if (Open.IsValid())
			{
				const int32 Rows = FMath::Clamp(static_cast<int32>(Field(Open, TEXT("rows"), 3)), 1, 100);
				const int32 Cols = FMath::Clamp(static_cast<int32>(Field(Open, TEXT("cols"), 5)), 1, 200);
				const double Margin = Field(Open, TEXT("margin"), FMath::Min(S.X, S.Z) * 0.08);
				const double CellW = (S.X - 2 * Margin) / Cols, CellH = (S.Z - 2 * Margin) / Rows;
				FVector2D Op(CellW * 0.55, CellH * 0.6);
				const TArray<TSharedPtr<FJsonValue>>* OpSize = nullptr;
				if (Open->TryGetArrayField(TEXT("size"), OpSize) && OpSize->Num() >= 2)
				{
					Op = FVector2D((*OpSize)[0]->AsNumber(), (*OpSize)[1]->AsNumber());
				}
				bool bArched = false, bGlass = true, bSill = true;
				Open->TryGetBoolField(TEXT("arched"), bArched);
				Open->TryGetBoolField(TEXT("glass"), bGlass);
				Open->TryGetBoolField(TEXT("sill"), bSill);
				Body->SetArrayField(TEXT("ops"), {OpeningsCut(S.X, S.Y, S.Z, Rows, Cols, Op, bArched, Margin, 0.0)});
				for (int32 R = 0; R < Rows; ++R)
				{
					for (int32 C = 0; C < Cols; ++C)
					{
						const double X = -S.X / 2 + Margin + CellW * (C + 0.5);
						const double Z = Margin + CellH * R + (CellH - Op.Y) / 2;
						const double RectH = bArched ? Op.Y - Op.X / 2 : Op.Y;
						if (bGlass)
						{
							B.Add(TEXT("box"), FVector(Op.X, 3, RectH), FVector(X, 0, Z), FRotator::ZeroRotator, *GlassPreset);
							if (bArched)
							{
								B.Add(TEXT("cylinder"), FVector(Op.X, Op.X, 3), FVector(X, 1.5, Z + RectH), FRotator(0, 0, 90), *GlassPreset)
									->SetStringField(TEXT("pivot"), TEXT("center"));
							}
						}
						if (bSill)
						{
							const double Depth = S.Y * 0.5 + 10.0;
							B.Add(TEXT("box"), FVector(Op.X + 16, Depth, 6), FVector(X, -S.Y / 2 - 10.0 + Depth / 2, Z - 6), FRotator::ZeroRotator, nullptr,
								B.SubFields(Open));
						}
					}
				}
			}
			B.Top(TopSpec, S.X, S.Y, S.Z);
			return true;
		}
		if (Shape == TEXT("arcade"))
		{
			S = FVector(1600, 60, 500);
			ReadVec3(Part, TEXT("size"), S);
			const int32 Count = FMath::Clamp(static_cast<int32>(Field(Part, TEXT("count"), FMath::Max(1.0, FMath::RoundToDouble(S.X / (S.Z * 0.8))))), 1, 200);
			const double Cell = S.X / Count;
			const double Pier = FMath::Clamp(Field(Part, TEXT("pier"), Cell * 0.22), 5.0, Cell - 10.0);
			const double Span = Cell - Pier;
			const double OpenH = FMath::Clamp(Field(Part, TEXT("opening_height"), S.Z * 0.75), Span / 2 + 10.0, S.Z - 5.0);
			// each bay is its exact outline (half piers, round arch, top band) pushed through the depth: no booleans
			const double Spring = OpenH - Span / 2;
			const int32 Steps = 16;
			for (int32 i = 0; i < Count; ++i)
			{
				const double Cx = -S.X / 2 + Cell * (i + 0.5);
				TArray<FVector> Outline = {{-Cell / 2, 0, 0}, {-Span / 2, 0, 0}, {-Span / 2, 0, Spring}};
				for (int32 k = 1; k < Steps; ++k)
				{
					const double A = PI - PI * k / Steps;
					Outline.Add(FVector(Span / 2 * FMath::Cos(A), 0, Spring + Span / 2 * FMath::Sin(A)));
				}
				Outline.Append({{Span / 2, 0, Spring}, {Span / 2, 0, 0}, {Cell / 2, 0, 0}, {Cell / 2, 0, S.Z}, {-Cell / 2, 0, S.Z}});
				TArray<TSharedPtr<FJsonValue>> Profile;
				for (const FVector& V : Outline)
				{
					Profile.Add(Vec3(V.X, V.Y, V.Z));
				}
				TSharedRef<FJsonObject> Bay = B.Add(TEXT("extrude"), FVector(Cell, S.Y, S.Z), FVector(Cx, -S.Y / 2, 0));
				Bay->SetArrayField(TEXT("profile"), Profile);
				Bay->SetField(TEXT("direction"), Vec3(0, S.Y, 0));
			}
			B.Top(TopSpec, S.X, S.Y, S.Z);
			return true;
		}
		if (Shape == TEXT("railing"))
		{
			S = FVector(400, 8, 100);
			ReadVec3(Part, TEXT("size"), S);
			const double T = FMath::Max(2.0, S.Y);
			const int32 Posts = FMath::Clamp(FMath::CeilToInt(S.X / FMath::Max(20.0, Field(Part, TEXT("spacing"), 150.0))) + 1, 2, 400);
			for (int32 i = 0; i < Posts; ++i)
			{
				B.Add(TEXT("box"), FVector(T, T, S.Z), FVector(-S.X / 2 + S.X * i / (Posts - 1), 0, 0));
			}
			B.Add(TEXT("box"), FVector(S.X, T * 1.4, T * 0.7), FVector(0, 0, S.Z - T * 0.7));
			const int32 Rails = FMath::Clamp(static_cast<int32>(Field(Part, TEXT("rails"), 1)), 0, 10);
			for (int32 k = 1; k <= Rails; ++k)
			{
				B.Add(TEXT("box"), FVector(S.X, T * 0.6, T * 0.5), FVector(0, 0, S.Z * k / (Rails + 1)));
			}
			FString Infill = TEXT("bars");
			Part->TryGetStringField(TEXT("infill"), Infill);
			if (Infill == TEXT("panel"))
			{
				B.Add(TEXT("box"), FVector(S.X - T, 2, S.Z * 0.75), FVector(0, 0, S.Z * 0.1), FRotator::ZeroRotator, *GlassPreset);
			}
			else if (Infill == TEXT("bars"))
			{
				const double Gap = FMath::Max(5.0, Field(Part, TEXT("bar_spacing"), 12.0));
				const int32 Bars = FMath::Clamp(FMath::FloorToInt(S.X / Gap), 1, 2000);
				for (int32 i = 1; i < Bars; ++i)
				{
					B.Add(TEXT("box"), FVector(2.5, 2.5, S.Z - T * 1.2), FVector(-S.X / 2 + Gap * i, 0, T * 0.5));
				}
			}
			return true;
		}
		if (Shape == TEXT("tower"))
		{
			S = FVector(600, 600, 1500);
			ReadVec3(Part, TEXT("size"), S);
			const double D = S.X, R = D / 2;
			const int32 Sides = static_cast<int32>(Field(Part, TEXT("sides"), 0));
			if (Sides >= 3)
			{
				TSharedRef<FJsonObject> Body = B.Add(TEXT("extrude"), S, FVector::ZeroVector);
				TArray<TSharedPtr<FJsonValue>> Profile;
				for (int32 i = 0; i < Sides; ++i)
				{
					const double A = 2 * PI * i / Sides + PI / Sides;
					Profile.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(R * FMath::Cos(A)),
						MakeShared<FJsonValueNumber>(R * FMath::Sin(A))}));
				}
				Body->SetArrayField(TEXT("profile"), Profile);
				Body->SetNumberField(TEXT("height"), S.Z);
			}
			else
			{
				B.Add(TEXT("cylinder"), S, FVector::ZeroVector);
			}
			if (Open.IsValid())
			{
				// windows set into the surface, facing out
				const int32 Rows = FMath::Clamp(static_cast<int32>(Field(Open, TEXT("rows"), 3)), 1, 100);
				const int32 Cols = FMath::Clamp(static_cast<int32>(Field(Open, TEXT("cols"), 4)), 1, 64);
				FVector2D Op(FMath::Min(120.0, D * 0.2), 180.0);
				const TArray<TSharedPtr<FJsonValue>>* OpSize = nullptr;
				if (Open->TryGetArrayField(TEXT("size"), OpSize) && OpSize->Num() >= 2)
				{
					Op = FVector2D((*OpSize)[0]->AsNumber(), (*OpSize)[1]->AsNumber());
				}
				const double Z0 = Field(Open, TEXT("start"), S.Z * 0.15);
				const double Step = (S.Z - Z0 - Op.Y) / FMath::Max(1, Rows - 1);
				for (int32 Rw = 0; Rw < Rows; ++Rw)
				{
					for (int32 C = 0; C < Cols; ++C)
					{
						const double A = 360.0 * C / Cols;
						const double Rad = FMath::DegreesToRadians(A);
						B.Add(TEXT("box"), FVector(Op.X, 6, Op.Y), FVector((R + 1) * FMath::Cos(Rad), (R + 1) * FMath::Sin(Rad), Z0 + Step * Rw),
							FRotator(0, A + 90.0, 0), *GlassPreset);
					}
				}
			}
			const TSharedPtr<FJsonObject>* CapObj = nullptr;
			if (Part->TryGetObjectField(TEXT("cap"), CapObj))
			{
				const TSharedPtr<FJsonObject> Cap = *CapObj;
				FString Style = TEXT("cone");
				Cap->TryGetStringField(TEXT("kind"), Style);
				const TSharedPtr<FJsonObject> F = B.SubFields(Cap);
				const double Over = Field(Cap, TEXT("overhang"), Style == TEXT("cone") ? D * 0.12 : (Style == TEXT("flat") ? 20.0 : 0.0));
				const double CapD = D + 2 * Over;
				if (Style == TEXT("cone"))
				{
					B.Add(TEXT("cone"), FVector(CapD, CapD, Field(Cap, TEXT("height"), D * 0.9)), FVector(0, 0, S.Z), FRotator::ZeroRotator, nullptr, F);
				}
				else if (Style == TEXT("dome"))
				{
					B.Add(TEXT("dome"), FVector(CapD, CapD, Field(Cap, TEXT("height"), D * 0.5)), FVector(0, 0, S.Z), FRotator::ZeroRotator, nullptr, F);
				}
				else if (Style == TEXT("flat"))
				{
					B.Add(TEXT("cylinder"), FVector(CapD, CapD, Field(Cap, TEXT("height"), 30.0)), FVector(0, 0, S.Z), FRotator::ZeroRotator, nullptr, F);
				}
				else if (Style == TEXT("teeth"))
				{
					const double Th = FMath::Max(30.0, D * 0.08);
					B.Add(TEXT("cylinder"), FVector(CapD, CapD, 20.0), FVector(0, 0, S.Z), FRotator::ZeroRotator, nullptr, F);
					const double W = Field(Cap, TEXT("width"), FMath::Max(40.0, Th * 1.5)), Hh = Field(Cap, TEXT("height"), FMath::Max(60.0, Th * 1.6));
					const int32 N = FMath::Clamp(FMath::RoundToInt(PI * D / (2 * W)), 4, 128);
					for (int32 i = 0; i < N; ++i)
					{
						const double A = 360.0 * i / N, Rad = FMath::DegreesToRadians(A), Rr = R + Over - Th / 2;
						B.Add(TEXT("box"), FVector(W, Th, Hh), FVector(Rr * FMath::Cos(Rad), Rr * FMath::Sin(Rad), S.Z + 20.0), FRotator(0, A + 90.0, 0), nullptr, F);
					}
				}
			}
			return true;
		}
		Error = FString::Printf(TEXT("unknown element %s"), *Shape);
		return false;
	}

	/** Expands element parts (wall, tower, railing, arcade) into plain parts. */
	bool ExpandElements(const TArray<TSharedPtr<FJsonValue>>& In, int32 MaxParts, TArray<TSharedPtr<FJsonValue>>& Out, FString& Error)
	{
		static const TSet<FString> Elements = {TEXT("wall"), TEXT("tower"), TEXT("railing"), TEXT("arcade")};
		for (int32 i = 0; i < In.Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Part = In[i]->AsObject();
			FString Shape;
			if (!Part.IsValid() || !Part->TryGetStringField(TEXT("shape"), Shape) || !Elements.Contains(Shape.ToLower()))
			{
				Out.Add(In[i]);
				continue;
			}
			if (!ExpandElement(Part, Shape.ToLower(), Out, Error))
			{
				Error = FString::Printf(TEXT("part %d: %s"), i, *Error);
				return false;
			}
			if (Out.Num() > MaxParts)
			{
				Error = FString::Printf(TEXT("part %d: elements make more than %d parts; split into several assemblies"), i, MaxParts);
				return false;
			}
		}
		return true;
	}
}

FBuildToolAssemblyResult URuntimeBuildToolset::CreateAssembly(const TArray<FRuntimeToolJson>& Parts, const FVector& Location, const FRotator& Rotation,
	const FVector& Scale, const TArray<FString>& Tags, const FVector& Color, const FVector& Emissive, const FString& Name, AActor* Parent,
	const FString& Material, float Roughness, float Metallic, float EmissiveStrength, float Opacity, float Variation, const FString& Style, float Tile,
	float Relief, bool bRepairCoplanar, bool bSnapToGround)
{
	FBuildToolAssemblyResult Out;
	if (Parts.Num() == 0 || Parts.Num() > BuildMaxParts)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("parts needs 1-%d entries (got %d)"), BuildMaxParts, Parts.Num()),
			TEXT("Split big builds into several assemblies under one group"));
		return Out;
	}
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		if (!Parts[i].AsObject().IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("parts[%d] must be an object like {\"shape\":\"box\",\"size\":[100,100,100]}"), i));
			return Out;
		}
	}
	TSharedRef<FJsonObject> Recipe = Obj();
	Recipe->SetStringField(TEXT("shape"), TEXT("assembly"));
	BuildCopyArg(Recipe, TEXT("parts"));
	{
		// "repeat" / "ring" on a part: one line becomes a window grid or a colonnade
		const TArray<TSharedPtr<FJsonValue>>* Raw = nullptr;
		TArray<TSharedPtr<FJsonValue>> Expanded;
		FString Error;
		TArray<TSharedPtr<FJsonValue>> Built;
		if (Recipe->TryGetArrayField(TEXT("parts"), Raw) &&
			(!ExpandPartCopies(*Raw, BuildMaxParts, Expanded, Error) || !ExpandElements(Expanded, BuildMaxParts, Built, Error)))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Error,
				TEXT("repeat: {count: n | [nx,ny,nz], step: [dx,dy,dz], skip}; ring: {count, radius, start, angle, face}; elements: wall, tower, railing, arcade"));
			return Out;
		}
		Recipe->SetArrayField(TEXT("parts"), Built);
	}
	BuildCopyMaterialArgs(Recipe, Material, Roughness, Metallic, EmissiveStrength, Opacity, Variation, Style, Tile, Relief);
	if (!bRepairCoplanar)
	{
		Recipe->SetBoolField(TEXT("repair_coplanar"), false);
	}
	CreateFromRecipe(Recipe, Name.IsEmpty() ? TEXT("Assembly") : Name, Parent, BuildArgTransform(Location, Rotation, Scale), Tags, bSnapToGround, Out);
	return Out;
}

FBuildToolNodeResult URuntimeBuildToolset::PlaceMesh(const FString& Asset, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const TArray<FString>& Tags, const FString& Name, AActor* Parent, bool bCollision)
{
	FBuildToolNodeResult Out;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Asset);
	if (!Mesh)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no static mesh asset '%s'"), *Asset),
			TEXT("level.find_assets {\"asset_class\":\"StaticMesh\"} lists packaged meshes"));
		return Out;
	}
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* P = BuildRequireNode(Parent, TEXT("parent"));
		if (!P)
		{
			return Out;
		}
		ParentId = P->Id;
	}
	const FString Source = FString::Printf(TEXT("mesh:%s%s"), bCollision ? TEXT("") : TEXT("nocollision:"), *Mesh->GetPathName());
	const FString NodeName = Name.IsEmpty() ? Mesh->GetName() : Name;
	const FTransform Transform = BuildArgTransform(Location, Rotation, Scale);
	auto Spawn = [Source, Transform, NodeName](FRuntimeCommandContext& Ctx, FString& Err) { return RuntimeMeshAssets::SpawnFromSource(Ctx.World, Source, Transform, NodeName, Err); };
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("place %s"), *NodeName), Spawn, NodeName, ParentId, ERuntimeNodeKind::Actor,
		Source, ToNames(Tags));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return Out;
	}
	FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
	Out.Id = Raw->GetNodeId();
	Out.Path = Graph->GetPath(Out.Id);
	if (const FRuntimeSceneNode* Node = Graph->Find(Out.Id))
	{
		Out.Name = Node->Name;
	}
	Out.Triangles = Mesh->GetNumTriangles(0);
	return Out;
}

FBuildToolNodeResult URuntimeBuildToolset::CreateRiver(const TArray<FVector>& Path, const TArray<FString>& Tags, float Width, float Overlap,
	EBuildToolWater Water, float FlowSpeed, float Ripple, float Foam, const FString& Name, AActor* Parent)
{
	if (Path.Num() < 2)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("a river needs a path of at least 2 points [[x,y,z],...]"),
			TEXT("Use the path from build.create_terrain rivers[] or your own water-level points, upstream first"));
		return {};
	}
	// the node sits at the first point; the path is stored relative to it so the recipe rebuilds anywhere
	const FVector Origin = Path[0];
	TSharedRef<FJsonObject> Recipe = Obj();
	Recipe->SetStringField(TEXT("shape"), TEXT("river"));
	TArray<TSharedPtr<FJsonValue>> Local;
	for (const FVector& P : Path)
	{
		Local.Add(RuntimeJson::Vec(P - Origin));
	}
	Recipe->SetArrayField(TEXT("path"), Local);
	Recipe->SetNumberField(TEXT("width"), Width);
	if (Overlap >= 0.f)
	{
		Recipe->SetNumberField(TEXT("overlap"), Overlap);
	}
	Recipe->SetStringField(TEXT("water"), BuildEnumText(Water));
	Recipe->SetNumberField(TEXT("flow_speed"), FlowSpeed);
	Recipe->SetNumberField(TEXT("ripple"), Ripple);
	Recipe->SetNumberField(TEXT("foam"), Foam);
	FBuildToolAssemblyResult Out;
	CreateFromRecipe(Recipe, Name.IsEmpty() ? TEXT("River") : Name, Parent, FTransform(Origin), Tags, false, Out);
	return ToNodeResult(Out);
}

FBuildToolTerrainResult URuntimeBuildToolset::CreateTerrain(const FVector2D& Size, const FVector& Location, const TArray<FRuntimeToolJson>& Hills,
	const TArray<FRuntimeToolJson>& Flatten, const TArray<FRuntimeToolJson>& Carve, const TArray<FString>& Tags, const FVector& Color,
	const float& SandLine, const float& SeaLevel, const TArray<FRuntimeToolJson>& Islands, float Height,
	float FeatureSize, float Ridged, float Roughness, int32 Seed, float Edge, EBuildToolBiome Biome, float Resolution, float SnowLine, const FString& Name,
	AActor* Parent, const FString& Material)
{
	FBuildToolTerrainResult Out;
	const FVector2D Sz = BuildCall().Has(TEXT("size")) ? Size : FVector2D(20000, 20000);
	const FVector2D Origin(Location.X, Location.Y);
	TSharedRef<FJsonObject> Recipe = Obj();
	Recipe->SetStringField(TEXT("shape"), TEXT("terrain"));
	Recipe->SetArrayField(TEXT("size"), {MakeShared<FJsonValueNumber>(Sz.X), MakeShared<FJsonValueNumber>(Sz.Y)});
	auto SetNum = [&Recipe](const TCHAR* F, double V, bool bSet) { if (bSet) Recipe->SetNumberField(F, V); };
	SetNum(TEXT("height"), BuildCall().Has(TEXT("height")) ? Height : 2000.0, true);
	SetNum(TEXT("feature_size"), FeatureSize, BuildCall().Has(TEXT("feature_size")));
	SetNum(TEXT("ridged"), Ridged, BuildCall().Has(TEXT("ridged")));
	SetNum(TEXT("roughness"), Roughness, BuildCall().Has(TEXT("roughness")));
	SetNum(TEXT("seed"), Seed, BuildCall().Has(TEXT("seed")));
	SetNum(TEXT("edge"), Edge, Edge > 0);
	SetNum(TEXT("resolution"), Resolution, Resolution > 0);
	// world x,y (and pad heights) -> the terrain's local frame, so the recipe rebuilds anywhere
	auto ToLocalXY = [&Origin](const TSharedPtr<FJsonValue>& V) -> TSharedPtr<FJsonValue>
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!V.IsValid() || !V->TryGetArray(A) || A->Num() < 2)
		{
			return V;
		}
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>((*A)[0]->AsNumber() - Origin.X),
			MakeShared<FJsonValueNumber>((*A)[1]->AsNumber() - Origin.Y)});
	};
	auto LocalList = [&](const TArray<FRuntimeToolJson>& In, const TCHAR* Field, const TCHAR* What) -> bool
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (int32 i = 0; i < In.Num(); ++i)
		{
			const TSharedPtr<FJsonObject> J = In[i].AsObject();
			if (!J.IsValid())
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s[%d] must be an object"), What, i));
				return false;
			}
			TSharedRef<FJsonObject> C = RuntimeJson::Parse(RuntimeJson::Write(J.ToSharedRef())).ToSharedRef();
			if (C->HasField(TEXT("at")))
			{
				C->SetField(TEXT("at"), ToLocalXY(C->TryGetField(TEXT("at"))));
			}
			for (const TCHAR* PathField : {TEXT("area"), TEXT("path")})
			{
				const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
				if (C->TryGetArrayField(PathField, Pts))
				{
					TArray<TSharedPtr<FJsonValue>> Local;
					for (const TSharedPtr<FJsonValue>& P : *Pts)
					{
						Local.Add(ToLocalXY(P));
					}
					C->SetArrayField(PathField, Local);
				}
			}
			double Hz;
			if (C->TryGetNumberField(TEXT("height"), Hz) && FCString::Strcmp(Field, TEXT("flatten")) == 0)
			{
				C->SetNumberField(TEXT("height"), Hz - Location.Z);
			}
			List.Add(MakeShared<FJsonValueObject>(C));
		}
		if (List.Num() > 0)
		{
			Recipe->SetArrayField(Field, List);
		}
		return true;
	};
	if (!LocalList(Hills, TEXT("hills"), TEXT("hills")) || !LocalList(Flatten, TEXT("flatten"), TEXT("flatten")) || !LocalList(Carve, TEXT("carve"), TEXT("carve"))
		|| !LocalList(Islands, TEXT("islands"), TEXT("islands")))
	{
		return Out;
	}
	Recipe->SetStringField(TEXT("biome"), BuildEnumText(Biome));
	SetNum(TEXT("snow_line"), SnowLine - Location.Z, SnowLine != 0);
	const bool bSea = BuildCall().Has(TEXT("sea_level")), bSand = BuildCall().Has(TEXT("sand_line"));
	SetNum(TEXT("sea_level"), SeaLevel - Location.Z, bSea);
	SetNum(TEXT("sand_line"), (bSand ? SandLine : SeaLevel + 150.0) - Location.Z, bSand || bSea);
	if (!Material.IsEmpty())
	{
		Recipe->SetStringField(TEXT("material"), Material);
	}
	BuildCopyArg(Recipe, TEXT("color"));
	TArray<TSharedPtr<FJsonValue>> Rivers;
	double MinZ = 0, MaxZ = 0;
	FString Error;
	if (!RuntimeTerrain::Describe(Recipe, Rivers, MinZ, MaxZ, Error))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Error, TEXT("size [x,y]; hills [{at,radius,height}]; flatten [{at,radius}|{area}]; carve [{path,width,depth}]"));
		return Out;
	}
	FBuildToolAssemblyResult A;
	if (!CreateFromRecipe(Recipe, Name.IsEmpty() ? TEXT("Terrain") : Name, Parent, FTransform(Location), Tags, false, A))
	{
		return Out;
	}
	Out.Id = A.Id;
	Out.Name = A.Name;
	Out.Path = A.Path;
	Out.Triangles = A.Triangles;
	Out.MinZ = MinZ + Location.Z;
	Out.MaxZ = MaxZ + Location.Z;
	for (const TSharedPtr<FJsonValue>& R : Rivers)
	{
		const TSharedPtr<FJsonObject> J = R->AsObject();
		TArray<TSharedPtr<FJsonValue>> World;
		for (const TSharedPtr<FJsonValue>& P : J->GetArrayField(TEXT("path")))
		{
			FVector V;
			RuntimeJson::ToVector(P, V);
			World.Add(RuntimeJson::Vec(V + Location));
		}
		J->SetArrayField(TEXT("path"), World);
		Out.Rivers.Add(FRuntimeToolJson::Make(J));
	}
	return Out;
}

FBuildToolInstancesResult URuntimeBuildToolset::PlaceInstances(const FString& Asset, const TArray<FRuntimeToolJson>& Instances,
	const TArray<FRuntimeToolJson>& CustomData, const FVector& Location, const TArray<FString>& Tags, const FString& Name, AActor* Parent, bool bCollision)
{
	FBuildToolInstancesResult Out;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Asset);
	if (!Mesh)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no static mesh asset '%s'"), *Asset),
			TEXT("level.find_assets {\"asset_class\":\"StaticMesh\"} lists packaged meshes"));
		return Out;
	}
	constexpr int32 MaxInstances = 200000;
	if (Instances.Num() == 0 || Instances.Num() > MaxInstances)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("instances must hold 1-%d entries"), MaxInstances));
		return Out;
	}
	if (CustomData.Num() > 0 && CustomData.Num() != Instances.Num())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			FString::Printf(TEXT("custom_data has %d entries but instances has %d"), CustomData.Num(), Instances.Num()), TEXT("Give one entry per instance, or none"));
		return Out;
	}
	// Canonical compact source: the node rebuilds from it (undo/redo, duplicate, templates, export).
	TArray<TSharedPtr<FJsonValue>> T, D;
	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!Instances[i].IsSet() || !Instances[i].Value->TryGetArray(A) || !(A->Num() == 3 || A->Num() == 4 || A->Num() == 5 || A->Num() == 7))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
				FString::Printf(TEXT("instances[%d] must be [x,y,z], [x,y,z,yaw], [x,y,z,yaw,scale] or [x,y,z,yaw,sx,sy,sz]"), i));
			return Out;
		}
		double V[7] = {0, 0, 0, 0, 1, 1, 1};
		for (int32 k = 0; k < A->Num(); ++k)
		{
			if (!(*A)[k].IsValid() || !(*A)[k]->TryGetNumber(V[k]))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("instances[%d][%d] is not a number"), i, k));
				return Out;
			}
		}
		if (A->Num() == 5)
		{
			V[5] = V[6] = V[4];
		}
		TArray<TSharedPtr<FJsonValue>> Row;
		for (double X : V)
		{
			Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(X * 100.0) / 100.0));
		}
		T.Add(MakeShared<FJsonValueArray>(Row));
	}
	for (int32 i = 0; i < CustomData.Num(); ++i)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!CustomData[i].IsSet() || !CustomData[i].Value->TryGetArray(A) || A->Num() > 16)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("custom_data[%d] must be an array of at most 16 numbers"), i));
			return Out;
		}
		TArray<TSharedPtr<FJsonValue>> Row;
		for (const TSharedPtr<FJsonValue>& X : *A)
		{
			double N = 0;
			if (!X.IsValid() || !X->TryGetNumber(N))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("custom_data[%d] holds a non-number"), i));
				return Out;
			}
			Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(N * 1000.0) / 1000.0));
		}
		D.Add(MakeShared<FJsonValueArray>(Row));
	}
	FString ParentId;
	if (Parent)
	{
		const FRuntimeSceneNode* P = BuildRequireNode(Parent, TEXT("parent"));
		if (!P)
		{
			return Out;
		}
		ParentId = P->Id;
	}
	TSharedRef<FJsonObject> Src = MakeShared<FJsonObject>();
	Src->SetStringField(TEXT("a"), Mesh->GetPathName());
	Src->SetBoolField(TEXT("c"), bCollision);
	Src->SetArrayField(TEXT("t"), T);
	if (D.Num() > 0)
	{
		Src->SetArrayField(TEXT("d"), D);
	}
	const FString Source = TEXT("inst:") + RuntimeJson::Write(Src);
	const FString NodeName = Name.IsEmpty() ? Mesh->GetName() : Name;
	const FTransform Transform(Location);
	auto Spawn = [Source, Transform, NodeName](FRuntimeCommandContext& Ctx, FString& Err)
	{ return RuntimeMeshAssets::SpawnInstancesFromSource(Ctx.World, Source, Transform, NodeName, Err); };
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("place %d x %s"), Instances.Num(), *NodeName), Spawn, NodeName,
		ParentId, ERuntimeNodeKind::Actor, Source, ToNames(Tags));
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return Out;
	}
	FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
	Out.Id = Raw->GetNodeId();
	Out.Path = Graph->GetPath(Out.Id);
	if (const FRuntimeSceneNode* Node = Graph->Find(Out.Id))
	{
		Out.Name = Node->Name;
	}
	Out.Instances = Instances.Num();
	Out.Triangles = Mesh->GetNumTriangles(0);
	return Out;
}

AActor* RuntimeMeshAssets::SpawnInstancesFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	const TSharedPtr<FJsonObject> Src = RuntimeJson::Parse(Source.Mid(5));  // after "inst:"
	FString Path;
	const TArray<TSharedPtr<FJsonValue>>* T = nullptr;
	if (!Src.IsValid() || !Src->TryGetStringField(TEXT("a"), Path) || !Src->TryGetArrayField(TEXT("t"), T))
	{
		OutError = TEXT("malformed instance source");
		return nullptr;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("no static mesh asset '%s'"), *Path);
		return nullptr;
	}
	bool bCollision = true;
	Src->TryGetBoolField(TEXT("c"), bCollision);
	const TArray<TSharedPtr<FJsonValue>>* D = nullptr;
	Src->TryGetArrayField(TEXT("d"), D);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Transform, Params);
	if (!Actor)
	{
		OutError = TEXT("could not spawn the instance group");
		return nullptr;
	}
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(Actor, TEXT("Instances"));
	Ism->SetMobility(EComponentMobility::Movable);
	Ism->SetStaticMesh(Mesh);
	Ism->SetCollisionProfileName(bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
	Actor->SetRootComponent(Ism);
	Ism->RegisterComponent();
	Actor->AddInstanceComponent(Ism);
	Actor->SetActorTransform(Transform);

	TArray<FTransform> Xf;
	Xf.Reserve(T->Num());
	for (const TSharedPtr<FJsonValue>& Row : *T)
	{
		const TArray<TSharedPtr<FJsonValue>>& V = Row->AsArray();
		auto N = [&V](int32 i, double Def) { return V.IsValidIndex(i) ? V[i]->AsNumber() : Def; };
		Xf.Add(FTransform(FRotator(0, N(3, 0), 0), FVector(N(0, 0), N(1, 0), N(2, 0)), FVector(N(4, 1), N(5, 1), N(6, 1))));
	}
	int32 NumCustom = 0;
	if (D)
	{
		for (const TSharedPtr<FJsonValue>& Row : *D)
		{
			NumCustom = FMath::Max(NumCustom, Row->AsArray().Num());
		}
	}
	Ism->SetNumCustomDataFloats(NumCustom);
	Ism->AddInstances(Xf, false, false);
	if (D && NumCustom > 0)
	{
		TArray<float> Data;
		for (int32 i = 0; i < D->Num() && i < Xf.Num(); ++i)
		{
			const TArray<TSharedPtr<FJsonValue>>& V = (*D)[i]->AsArray();
			Data.Init(0.f, NumCustom);
			for (int32 k = 0; k < V.Num() && k < NumCustom; ++k)
			{
				Data[k] = float(V[k]->AsNumber());
			}
			Ism->SetCustomData(i, Data, false);
		}
		Ism->MarkRenderStateDirty();
	}
	return Actor;
}

AActor* RuntimeMeshAssets::SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	FString Path = Source.Mid(5);  // after "mesh:"
	const bool bNoCollision = Path.RemoveFromStart(TEXT("nocollision:"));
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("no static mesh asset '%s'"), *Path);
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, Params);
	if (!Actor)
	{
		OutError = TEXT("could not spawn a static mesh actor");
		return nullptr;
	}
	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionProfileName(bNoCollision ? TEXT("NoCollision") : TEXT("BlockAll"));
	return Actor;
}

// ---- modify -------------------------------------------------------------------------------------------------------------

FBuildToolBooleanResult URuntimeBuildToolset::BooleanOp(AActor* Target, AActor* Tool, EBuildToolBooleanOp Op, bool bKeepTool)
{
	FBuildToolBooleanResult Out;
	TSharedPtr<FJsonObject> TargetRecipe, ToolRecipe;
	FRuntimeSceneNode* TargetNode = BuildRequirePrim(Target, TEXT("target"), TargetRecipe);
	FRuntimeSceneNode* ToolNode = TargetNode ? BuildRequirePrim(Tool, TEXT("tool"), ToolRecipe) : nullptr;
	if (!TargetNode || !ToolNode)
	{
		return Out;
	}
	if (ToolNode == TargetNode)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("target and tool are the same node"), TEXT("Create a separate cutter object"));
		return Out;
	}
	// The tool is recorded in the target's mesh space so the recipe replays exactly.
	const FTransform Rel = Tool->GetActorTransform().GetRelativeTransform(Target->GetActorTransform());
	TSharedRef<FJsonObject> OpJson = Obj();
	OpJson->SetStringField(TEXT("op"), BuildEnumText(Op));
	TSharedRef<FJsonObject> ToolCopy = RuntimeJson::Parse(RuntimeJson::Write(ToolRecipe.ToSharedRef())).ToSharedRef();
	ToolCopy->RemoveField(TEXT("material"));
	ToolCopy->RemoveField(TEXT("color"));
	OpJson->SetObjectField(TEXT("tool"), ToolCopy);
	OpJson->SetField(TEXT("location"), RuntimeJson::Vec(Rel.GetLocation()));
	OpJson->SetField(TEXT("rotation"), RuntimeJson::Rot(Rel.Rotator()));
	OpJson->SetField(TEXT("scale"), RuntimeJson::Vec(Rel.GetScale3D()));

	TSharedRef<FJsonObject> NewRecipe = RuntimeJson::Parse(RuntimeJson::Write(TargetRecipe.ToSharedRef())).ToSharedRef();
	TArray<TSharedPtr<FJsonValue>> Ops;
	const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
	if (NewRecipe->TryGetArrayField(TEXT("ops"), Existing))
	{
		Ops = *Existing;
	}
	Ops.Add(MakeShared<FJsonValueObject>(OpJson));
	NewRecipe->SetArrayField(TEXT("ops"), Ops);

	Out.Target = TargetNode->Id;
	Out.Op = BuildEnumText(Op);
	Out.Ops = Ops.Num();
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Out.Target);
	Delta->SetStringField(TEXT("op"), Out.Op);
	Delta->SetNumberField(TEXT("ops"), Out.Ops);
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(FString::Printf(TEXT("boolean %s"), *TargetNode->Name));
	Cmd->Add(MakeRecipeCommand(TEXT("boolean"), TargetNode->Id, TargetNode->Source, RuntimePrimitives::ToSource(NewRecipe), Delta));
	if (!bKeepTool)
	{
		Cmd->Add(MakeUnique<FSoftDeleteActorsCommand>(TEXT("consume tool"), TArray<AActor*>{Tool}));
	}
	if (RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		Out.Triangles = RuntimePrimitives::TriangleCount(Target);
	}
	return Out;
}

FBuildToolCopiesResult URuntimeBuildToolset::Array(AActor* Target, const FVector& Offset, const FVector& Center, const FVector& Axis, const FIntVector& Counts,
	const FVector& Spacing, EBuildToolArrayMode Mode, int32 Count, float Angle)
{
	FRuntimeSceneNode* Node = BuildRequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	const FTransform Base = BuildRelativeOf(Target);
	TArray<FTransform> Relatives;
	if (Mode == EBuildToolArrayMode::Grid)
	{
		const FIntVector N = BuildCall().Has(TEXT("counts")) ? Counts : FIntVector(2, 2, 1);
		const FVector Step = BuildCall().Has(TEXT("spacing")) ? Spacing : ActorBox(Target).GetSize();
		const int32 NX = FMath::Max(1, N.X), NY = FMath::Max(1, N.Y), NZ = FMath::Max(1, N.Z);
		if (static_cast<int64>(NX) * NY * NZ - 1 > BuildMaxCopies)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("too many copies: %d x %d x %d (max %d)"), NX, NY, NZ, BuildMaxCopies + 1),
				TEXT("Use several arrays or pcg.scatter for large counts"));
			return {};
		}
		for (int32 Z = 0; Z < NZ; ++Z)
		{
			for (int32 Y = 0; Y < NY; ++Y)
			{
				for (int32 X = 0; X < NX; ++X)
				{
					if (X == 0 && Y == 0 && Z == 0)
					{
						continue;
					}
					FTransform T = Base;
					T.AddToTranslation(FVector(X * Step.X, Y * Step.Y, Z * Step.Z));
					Relatives.Add(T);
				}
			}
		}
	}
	else
	{
		if (Count < 2 || Count - 1 > BuildMaxCopies)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("count must be 2..%d (includes the original), got %d"), BuildMaxCopies + 1, Count));
			return {};
		}
		if (Mode == EBuildToolArrayMode::Radial)
		{
			const FVector C = BuildCall().Has(TEXT("center")) ? Center : FVector::ZeroVector; // parent space
			const FVector A = (BuildCall().Has(TEXT("axis")) ? Axis : FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			const int32 Divisions = FMath::IsNearlyEqual(Angle, 360.f) ? Count : Count - 1;
			for (int32 i = 1; i < Count; ++i)
			{
				const FQuat Q(A, FMath::DegreesToRadians(static_cast<double>(Angle) * i / Divisions));
				FTransform T = Base;
				T.SetLocation(C + Q.RotateVector(Base.GetLocation() - C));
				T.SetRotation(Q * Base.GetRotation());
				Relatives.Add(T);
			}
		}
		else
		{
			const FVector Step = BuildCall().Has(TEXT("offset")) ? Offset : FVector(100, 0, 0);
			for (int32 i = 1; i < Count; ++i)
			{
				FTransform T = Base;
				T.AddToTranslation(Step * i);
				Relatives.Add(T);
			}
		}
	}
	return SpawnCopies(*Node, Relatives, FString::Printf(TEXT("%s array of %s"), *BuildEnumText(Mode), *Node->Name));
}

FBuildToolCopiesResult URuntimeBuildToolset::Mirror(AActor* Target, const FVector& Point, EBuildToolAxis Axis)
{
	FRuntimeSceneNode* Node = BuildRequireNode(Target, TEXT("target"));
	if (!Node)
	{
		return {};
	}
	const FVector P = BuildCall().Has(TEXT("point")) ? Point : FVector::ZeroVector; // parent space
	FVector S(1.0);
	S[static_cast<int32>(Axis)] = -1.0;
	// Reflect through the plane: M' = M * T(-P) * S * T(P). FTransform keeps the reflection as a negative scale.
	const FMatrix Mirrored = BuildRelativeOf(Target).ToMatrixWithScale() * FTranslationMatrix(-P) * FScaleMatrix(S) * FTranslationMatrix(P);
	return SpawnCopies(*Node, {FTransform(Mirrored)}, FString::Printf(TEXT("mirror %s"), *Node->Name));
}

FBuildToolMaterialResult URuntimeBuildToolset::SetMaterial(const TArray<AActor*>& Targets, const FVector& Color, const FVector& Emissive,
	const FString& Material, float Roughness, float Metallic, float EmissiveStrength, float Opacity, float Variation, const FString& Style, float Tile,
	float Relief)
{
	FBuildToolMaterialResult Out;
	if (Targets.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("targets is empty"));
		return Out;
	}
	TSharedRef<FJsonObject> Fields = Obj();
	BuildCopyMaterialArgs(Fields, Material, Roughness, Metallic, EmissiveStrength, Opacity, Variation, Style, Tile, Relief);
	if (Fields->Values.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("nothing to change"),
			FString::Printf(TEXT("Pass at least one of: %s"), *FString::Join(RuntimePrimitives::MaterialFieldNames(), TEXT(", "))));
		return Out;
	}
	FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("set material"));
	for (AActor* A : Targets)
	{
		FRuntimeSceneNode* Node = A ? Graph->FindByActor(A) : nullptr;
		const TSharedPtr<FJsonObject> Recipe = Node ? RuntimePrimitives::FromSource(Node->Source) : nullptr;
		if (!Recipe.IsValid())
		{
			Out.SkippedNotBuilder.Add(RuntimeToolCall::MakeReference(A));
			continue;
		}
		TSharedRef<FJsonObject> New = RuntimeJson::Parse(RuntimeJson::Write(Recipe.ToSharedRef())).ToSharedRef();
		if (Fields->HasField(TEXT("material")) && !Fields->HasField(TEXT("color")))
		{
			New->RemoveField(TEXT("color")); // a new preset brings its own colour
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& F : Fields->Values)
		{
			New->SetField(F.Key, F.Value);
		}
		FString Error;
		if (!RuntimePrimitives::ValidateMaterials(New, Error))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Error, TEXT("Use a preset or style from build.list_materials, or an allowed material path"));
			return Out;
		}
		Cmd->Add(MakeRecipeCommand(TEXT("material"), Node->Id, Node->Source, RuntimePrimitives::ToSource(New), nullptr));
		Out.Changed.Add(Node->Id);
	}
	if (Cmd->IsEmpty())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("no builder objects among the targets"), TEXT("Use materials.* tools for other meshes"));
		return Out;
	}
	RuntimeToolCall::RunCommand(MoveTemp(Cmd));
	return Out;
}

// ---- check --------------------------------------------------------------------------------------------------------------

FBuildToolMeasurement URuntimeBuildToolset::Measure(AActor* A, AActor* B)
{
	FBuildToolMeasurement Out;
	const FBox BA = ActorBox(A), BB = ActorBox(B);
	FVector Gap;
	for (int32 i = 0; i < 3; ++i)
	{
		Gap[i] = FMath::Max(0.0, FMath::Max(BA.Min[i] - BB.Max[i], BB.Min[i] - BA.Max[i]));
	}
	Out.CenterDistance = RuntimeJson::Round(FVector::Distance(BA.GetCenter(), BB.GetCenter()));
	Out.CenterDelta = BB.GetCenter() - BA.GetCenter();
	Out.Gap = RuntimeJson::Round(Gap.Size());
	Out.GapXyz = Gap;
	Out.SizeA = BA.GetSize();
	Out.SizeB = BB.GetSize();
	Out.bOverlapping = BA.Intersect(BB) && Gap.IsNearlyZero();
	return Out;
}

FBuildToolOverlapReport URuntimeBuildToolset::CheckOverlaps(AActor* Under, float Tolerance, int32 Limit)
{
	FBuildToolOverlapReport Out;
	bool bOk = true;
	TArray<FRuntimeSceneNode*> Nodes = BuildScopeNodes(Under, bOk);
	if (!bOk)
	{
		return Out;
	}
	Limit = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	struct FItem
	{
		FRuntimeSceneNode* Node;
		FBox Box;
	};
	TArray<FItem> Items;
	for (FRuntimeSceneNode* N : Nodes)
	{
		Items.Add({N, ActorBox(N->Actor.Get())});
	}
	// Sweep and prune along X.
	Items.Sort([](const FItem& A, const FItem& B) { return A.Box.Min.X < B.Box.Min.X; });
	FRuntimeSceneGraph& Graph = *RuntimeToolCall::GetGraph();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		for (int32 j = i + 1; j < Items.Num() && Items[j].Box.Min.X < Items[i].Box.Max.X - Tolerance; ++j)
		{
			const FBox& A = Items[i].Box;
			const FBox& B = Items[j].Box;
			const FVector Depth(FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X), FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y),
				FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z));
			if (Depth.GetMin() <= Tolerance)
			{
				continue;
			}
			if (Graph.IsAncestorOrSelf(Items[i].Node->Id, Items[j].Node->Id) || Graph.IsAncestorOrSelf(Items[j].Node->Id, Items[i].Node->Id))
			{
				continue;
			}
			++Out.Overlaps;
			if (Out.Pairs.Num() < Limit)
			{
				FBuildToolOverlapPair& P = Out.Pairs.AddDefaulted_GetRef();
				P.A = Items[i].Node->Id;
				P.B = Items[j].Node->Id;
				P.Depth = Depth;
			}
		}
	}
	Out.Checked = Items.Num();
	Out.Note = TEXT("bounding-box test; intended overlaps (e.g. booleans, touching parts) also appear");
	return Out;
}

FBuildToolStats URuntimeBuildToolset::GetBuildStats(AActor* Under)
{
	FBuildToolStats Out;
	bool bOk = true;
	TArray<FRuntimeSceneNode*> Nodes = BuildScopeNodes(Under, bOk);
	if (!bOk)
	{
		return Out;
	}
	TSet<const UMaterialInterface*> Materials;
	FBox All(ForceInit);
	for (FRuntimeSceneNode* N : Nodes)
	{
		AActor* A = N->Actor.Get();
		All += ActorBox(A);
		if (N->Source.StartsWith(TEXT("prim:")))
		{
			++Out.BuilderPrimitives;
			Out.Triangles += RuntimePrimitives::TriangleCount(A);
		}
		TInlineComponentArray<UPrimitiveComponent*> PrimComps(A);
		for (UPrimitiveComponent* C : PrimComps)
		{
			if (!C->IsVisible())
			{
				continue;
			}
			++Out.Components;
			Out.DrawCallsEstimate += FMath::Max(1, C->GetNumMaterials());
			for (int32 i = 0; i < C->GetNumMaterials(); ++i)
			{
				Materials.Add(C->GetMaterial(i));
			}
			if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C); SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetRenderData()
				&& SM->GetStaticMesh()->GetRenderData()->LODResources.Num() > 0)
			{
				Out.Triangles += SM->GetStaticMesh()->GetRenderData()->LODResources[0].GetNumTriangles();
			}
		}
	}
	Out.Nodes = Nodes.Num();
	Out.Materials = Materials.Num();
	if (All.IsValid)
	{
		Out.Size = All.GetSize();
	}
	return Out;
}

FBuildToolCaptureResult URuntimeBuildToolset::CaptureView(const TArray<EBuildToolView>& Views, const TArray<AActor*>& Targets, AActor* Under, int32 Size,
	float Padding, EBuildToolImageFormat Format)
{
	FBuildToolCaptureResult Out;
	if (Size < 64 || Size > 1024 || Padding < 1.f || Padding > 3.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size must be 64-1024 (got %d) and padding 1-3 (got %.2f)"), Size, Padding));
		return Out;
	}
	if (!FApp::CanEverRender())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("rendering is disabled (null RHI)"), TEXT("Run the game with rendering to capture views"));
		return Out;
	}
	TArray<AActor*> Framed = Targets;
	if (Framed.Num() == 0)
	{
		bool bOk = true;
		for (FRuntimeSceneNode* N : BuildScopeNodes(Under, bOk))
		{
			Framed.Add(N->Actor.Get());
		}
		if (!bOk)
		{
			return Out;
		}
	}
	FBox Box(ForceInit);
	for (AActor* A : Framed)
	{
		Box += ActorBox(A);
	}
	if (!Box.IsValid)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("nothing to capture"), TEXT("Build something first, or pass targets"));
		return Out;
	}
	TArray<EBuildToolView> ViewList = Views;
	if (ViewList.Num() == 0)
	{
		ViewList = {EBuildToolView::Front, EBuildToolView::Right, EBuildToolView::Top, EBuildToolView::Perspective};
	}
	const FVector Center = Box.GetCenter();
	const FVector Extent = Box.GetExtent();
	const double Radius = FMath::Max(Extent.Size(), 10.0);

	UWorld* World = RuntimeToolCall::GetWorld();
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FTransform::Identity, Params);
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	RT->InitCustomFormat(Size, Size, PF_B8G8R8A8, false);
	RT->UpdateResourceImmediate(true);
	USceneCaptureComponent2D* Comp = Capture->GetCaptureComponent2D();
	Comp->TextureTarget = RT;
	Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Comp->bCaptureEveryFrame = false;
	Comp->bCaptureOnMovement = false;
	RuntimeImage::MatchSceneExposure(Comp);

	const int32 Columns = FMath::Min(ViewList.Num(), ViewList.Num() == 4 ? 2 : 3);
	const int32 Rows = FMath::DivideAndRoundUp(ViewList.Num(), Columns);
	TArray<FColor> Sheet;
	Sheet.SetNumZeroed(Columns * Size * Rows * Size);
	for (int32 v = 0; v < ViewList.Num(); ++v)
	{
		const EBuildToolView View = ViewList[v];
		FVector Dir;  // camera looks along Dir
		double Width; // ortho width covering the visible extent
		switch (View)
		{
		case EBuildToolView::Front: Dir = FVector(1, 0, 0); Width = 2 * FMath::Max(Extent.Y, Extent.Z); break;
		case EBuildToolView::Back: Dir = FVector(-1, 0, 0); Width = 2 * FMath::Max(Extent.Y, Extent.Z); break;
		case EBuildToolView::Right: Dir = FVector(0, -1, 0); Width = 2 * FMath::Max(Extent.X, Extent.Z); break;
		case EBuildToolView::Left: Dir = FVector(0, 1, 0); Width = 2 * FMath::Max(Extent.X, Extent.Z); break;
		case EBuildToolView::Top: Dir = FVector(0, 0, -1); Width = 2 * FMath::Max(Extent.X, Extent.Y); break;
		default: Dir = FVector(1, 1, -0.6).GetSafeNormal(); Width = 0.0; break;
		}
		const bool bOrtho = Width > 0.0;
		const FRotator Rot = View == EBuildToolView::Top ? FRotator(-90, 0, 0) : Dir.Rotation(); // top: +X up in the image
		Comp->ProjectionType = bOrtho ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
		Comp->OrthoWidth = static_cast<float>(Width * Padding + 1.0);
		Comp->FOVAngle = 50.f;
		const double Distance = bOrtho ? Radius * 2.0 + 500.0 : Radius * Padding / FMath::Sin(FMath::DegreesToRadians(25.0));
		Capture->SetActorLocationAndRotation(Center - Dir * Distance, Rot);
		Comp->CaptureScene();
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		FString Error;
		if (!RuntimeImage::ReadRenderTarget(RT, Pixels, W, H, Error))
		{
			Capture->Destroy();
			RuntimeToolCall::Fail(ERuntimeToolError::Internal, Error);
			return Out;
		}
		const int32 OX = (v % Columns) * Size, OY = (v / Columns) * Size;
		for (int32 Y = 0; Y < FMath::Min(H, Size); ++Y)
		{
			FMemory::Memcpy(&Sheet[(OY + Y) * Columns * Size + OX], &Pixels[Y * W], FMath::Min(W, Size) * sizeof(FColor));
		}
		Out.Layout.Add(FString::Printf(TEXT("%s(row %d, col %d)"), *BuildEnumText(View), v / Columns, v % Columns));
	}
	Capture->Destroy();
	RT->ReleaseResource();

	FToolAttachment Image;
	FString Error;
	if (!RuntimeImage::Encode(MoveTemp(Sheet), Columns * Size, Rows * Size, Columns * Size, BuildEnumText(Format), 85, Image, Error))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, Error);
		return Out;
	}
	Out.BoundsSize = Box.GetSize();
	Out.W = Image.Width;
	Out.H = Image.Height;
	BuildCall().AddAttachment(MoveTemp(Image));
	return Out;
}
