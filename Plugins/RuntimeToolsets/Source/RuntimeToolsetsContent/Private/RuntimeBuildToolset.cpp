#include "RuntimeBuildToolset.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeToolsetsGroupActor.h"
#include "PrimitiveMesh.h"
#include "RuntimeActorUtils.h"
#include "RuntimeImage.h"
#include "RuntimeToolHelpers.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "StaticMeshResources.h"
#include "UDynamicMesh.h"

using namespace RuntimeTools;

namespace
{
	constexpr int32 BuildMaxCopies = 1000;
	constexpr int32 BuildMaxParts = 512;

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

FASTEST - a generator script plus big batches:
  If you can run Python, use the kit shipped with the game (session_get_build_info -> scripting; skill python_kit):
  uk.parts builds part dicts with loops (window bays, colonnades, truss bracing, railings, stairs, whole streets),
  uk.place_many sends many objects per request. Example scripts (a realistic city, game pieces) show the patterns.
  Otherwise call build.create_assembly directly with up to 512 parts per call (one object, one undo step).
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
1. Plan: list the defining features and their real measurements (silhouette first, then secondary forms, then detail).
2. Build site + massing in one batch; check build.capture_view (front/right/top/perspective). Fix proportions.
3. Add detail layers (structure, openings, ornament, railings) in batches; group with scene.create_group + parent.
4. Context: a ground much larger than the view (sand/grass/asphalt), paths, trees, plinths. Nothing floats
   (snap_to_ground on create tools, layout.drop_to_ground for existing objects).
5. Light: lighting.apply_mood, then lighting.set_sun so the sun is behind/beside the camera (not backlit).
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
	BuildCopyMaterialArgs(Recipe, Material, Roughness, Metallic, EmissiveStrength, Opacity, Variation, Style, Tile, Relief);
	if (!bRepairCoplanar)
	{
		Recipe->SetBoolField(TEXT("repair_coplanar"), false);
	}
	CreateFromRecipe(Recipe, Name.IsEmpty() ? TEXT("Assembly") : Name, Parent, BuildArgTransform(Location, Rotation, Scale), Tags, bSnapToGround, Out);
	return Out;
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
