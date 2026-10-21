#include "BuildToolset.h"
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
	constexpr int32 MaxCopies = 1000;

	TArray<FString> PaletteNames()
	{
		TArray<FString> Names;
		return RuntimePrimitives::PaletteNames();
	}

	/** Resolves a target that must be a live scene node. */
	FRuntimeSceneNode* ResolveNode(FToolInvocation& Inv, const TCHAR* Arg)
	{
		FResolvedTarget T;
		if (!Inv.ResolveTarget(Arg, T))
		{
			return nullptr;
		}
		if (!T.Node || T.Component)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is not a scene node"), Arg));
			return nullptr;
		}
		return T.Node;
	}

	/** Resolves a builder (prim:) node and its recipe. */
	FRuntimeSceneNode* ResolvePrim(FToolInvocation& Inv, const TCHAR* Arg, TSharedPtr<FJsonObject>& OutRecipe)
	{
		FRuntimeSceneNode* Node = ResolveNode(Inv, Arg);
		if (!Node)
		{
			return nullptr;
		}
		OutRecipe = RuntimePrimitives::FromSource(Node->Source);
		if (!OutRecipe.IsValid())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is not a builder primitive"), Arg), TEXT("Create it with build.create_primitive"));
			return nullptr;
		}
		return Node;
	}

	bool ResolveParent(FToolInvocation& Inv, FString& OutParentId)
	{
		OutParentId.Reset();
		if (Inv.GetString(TEXT("parent")).IsEmpty())
		{
			return true;
		}
		const FRuntimeSceneNode* Parent = ResolveNode(Inv, TEXT("parent"));
		if (Parent)
		{
			OutParentId = Parent->Id;
		}
		return Parent != nullptr;
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

	/** Reads recipe fields shared by create_primitive / create_profile_solid. */
	void ReadCommonRecipe(const FToolInvocation& Inv, const TSharedRef<FJsonObject>& R)
	{
		for (const TCHAR* Field : {TEXT("size"), TEXT("detail"), TEXT("pivot"), TEXT("material"), TEXT("color"), TEXT("top_ratio"), TEXT("thickness"),
				 TEXT("profile"), TEXT("path"), TEXT("height"), TEXT("angle"), TEXT("closed"), TEXT("bevel"), TEXT("roughness"), TEXT("metallic"),
				 TEXT("emissive"), TEXT("emissive_strength"), TEXT("opacity"), TEXT("variation"), TEXT("count"), TEXT("taper"), TEXT("beam"),
				 TEXT("levels"), TEXT("bracing"), TEXT("rows"), TEXT("cols"), TEXT("opening"), TEXT("arched"), TEXT("margin")})
		{
			if (Inv.Has(Field))
			{
				R->SetField(Field, Inv.GetValue(Field));
			}
		}
	}

	void CreateFromRecipe(FToolInvocation& Inv, const TSharedRef<FJsonObject>& Recipe, const FString& DefaultName)
	{
		FString ParentId;
		if (!ResolveParent(Inv, ParentId))
		{
			return;
		}
		// Validate by building once into a scratch mesh: errors surface as invalid_arguments, not command failures.
		FString Error;
		if (!RuntimePrimitives::BuildMesh(NewObject<UDynamicMesh>(), Recipe, Error))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, Error);
			return;
		}
		const FString Source = RuntimePrimitives::ToSource(Recipe);
		const FTransform Transform = ReadTransform(Inv);
		const FString Name = Inv.GetString(TEXT("name"), DefaultName);
		auto Spawn = [Source, Transform, Name](FRuntimeCommandContext& Ctx, FString& Err) { return RuntimePrimitives::SpawnFromSource(Ctx.World, Source, Transform, Name, Err); };
		TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("build %s"), *Name), Spawn, Name, ParentId, ERuntimeNodeKind::Actor,
			Source, ToNames(Inv.GetStringArray(TEXT("tags"))));
		FCreateNodeCommand* Raw = Cmd.Get();
		if (!Inv.RunCommand(MoveTemp(Cmd)))
		{
			return;
		}
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetStringField(TEXT("id"), Raw->GetNodeId());
		Out->SetStringField(TEXT("path"), Inv.GetGraph()->GetPath(Raw->GetNodeId()));
		if (const FRuntimeSceneNode* Node = Inv.GetGraph()->Find(Raw->GetNodeId()))
		{
			Out->SetNumberField(TEXT("triangles"), RuntimePrimitives::TriangleCount(Node->Actor.Get()));
		}
		Inv.Succeed(Out);
	}

	/** Spawns copies of a node at relative transforms (same parent). One undo step. */
	void SpawnCopies(FToolInvocation& Inv, const FRuntimeSceneNode& Original, const TArray<FTransform>& Relatives, const FString& Label)
	{
		if (Original.Kind == ERuntimeNodeKind::Group)
		{
			Inv.Fail(ERuntimeToolError::Unsupported, TEXT("arrays and mirrors work on single nodes, not groups"), TEXT("Use scene.duplicate_node for groups"));
			return;
		}
		if (!IRuntimeToolsetsCore::Get().HasSourceSpawner(Original.Source))
		{
			Inv.Fail(ERuntimeToolError::Unsupported, FString::Printf(TEXT("node source '%s' cannot be copied"), *Original.Source.Left(40)));
			return;
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
		if (!Inv.RunCommand(MoveTemp(Cmd)))
		{
			return;
		}
		TArray<FString> Ids;
		for (const FCreateNodeCommand* C : Created)
		{
			Ids.Add(C->GetNodeId());
		}
		TSharedRef<FJsonObject> Out = Obj();
		Out->SetNumberField(TEXT("created"), Ids.Num());
		Out->SetField(TEXT("ids"), RuntimeJson::Strings(Ids.Num() > 100 ? TArray<FString>(Ids.GetData(), 100) : Ids));
		Inv.Succeed(Out);
	}

	FTransform RelativeOf(const AActor* Actor)
	{
		const USceneComponent* Root = Actor->GetRootComponent();
		return Root ? Root->GetRelativeTransform() : Actor->GetActorTransform();
	}

	/** Live, non-group nodes below Under (or all). */
	TArray<FRuntimeSceneNode*> ScopeNodes(FToolInvocation& Inv, bool& bOk)
	{
		bOk = true;
		FRuntimeSceneGraph& Graph = *Inv.GetGraph();
		TArray<FString> Ids;
		if (!Inv.GetString(TEXT("under")).IsEmpty())
		{
			const FRuntimeSceneNode* Root = ResolveNode(Inv, TEXT("under"));
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
}

static void CreatePrimitive(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Recipe = Obj();
	Recipe->SetStringField(TEXT("shape"), Inv.GetString(TEXT("shape")));
	ReadCommonRecipe(Inv, Recipe);
	CreateFromRecipe(Inv, Recipe, Inv.GetString(TEXT("shape")));
}

static void CreateProfileSolid(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Recipe = Obj();
	const FString Kind = Inv.GetString(TEXT("kind"));
	Recipe->SetStringField(TEXT("shape"), Kind);
	ReadCommonRecipe(Inv, Recipe);
	CreateFromRecipe(Inv, Recipe, Kind);
}

static void BooleanOp(FToolInvocation& Inv)
{
	TSharedPtr<FJsonObject> TargetRecipe, ToolRecipe;
	FRuntimeSceneNode* Target = ResolvePrim(Inv, TEXT("target"), TargetRecipe);
	if (!Target)
	{
		return;
	}
	FRuntimeSceneNode* Tool = ResolvePrim(Inv, TEXT("tool"), ToolRecipe);
	if (!Tool)
	{
		return;
	}
	if (Tool == Target)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target and tool are the same node"));
		return;
	}
	// The tool is recorded in the target's mesh space so the recipe replays exactly.
	const FTransform Rel = Tool->Actor->GetActorTransform().GetRelativeTransform(Target->Actor->GetActorTransform());
	TSharedRef<FJsonObject> Op = Obj();
	Op->SetStringField(TEXT("op"), Inv.GetString(TEXT("op"), TEXT("subtract")));
	TSharedRef<FJsonObject> ToolCopy = RuntimeJson::Parse(RuntimeJson::Write(ToolRecipe.ToSharedRef())).ToSharedRef();
	ToolCopy->RemoveField(TEXT("material"));
	ToolCopy->RemoveField(TEXT("color"));
	Op->SetObjectField(TEXT("tool"), ToolCopy);
	Op->SetField(TEXT("location"), RuntimeJson::Vec(Rel.GetLocation()));
	Op->SetField(TEXT("rotation"), RuntimeJson::Rot(Rel.Rotator()));
	Op->SetField(TEXT("scale"), RuntimeJson::Vec(Rel.GetScale3D()));

	TSharedRef<FJsonObject> NewRecipe = RuntimeJson::Parse(RuntimeJson::Write(TargetRecipe.ToSharedRef())).ToSharedRef();
	TArray<TSharedPtr<FJsonValue>> Ops;
	const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
	if (NewRecipe->TryGetArrayField(TEXT("ops"), Existing))
	{
		Ops = *Existing;
	}
	Ops.Add(MakeShared<FJsonValueObject>(Op));
	NewRecipe->SetArrayField(TEXT("ops"), Ops);

	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("target"), Target->Id);
	Delta->SetStringField(TEXT("op"), Op->GetStringField(TEXT("op")));
	Delta->SetNumberField(TEXT("ops"), Ops.Num());
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(FString::Printf(TEXT("boolean %s"), *Target->Name));
	Cmd->Add(MakeRecipeCommand(TEXT("boolean"), Target->Id, Target->Source, RuntimePrimitives::ToSource(NewRecipe), Delta));
	if (!Inv.GetBool(TEXT("keep_tool"), false))
	{
		Cmd->Add(MakeUnique<FSoftDeleteActorsCommand>(TEXT("consume tool"), TArray<AActor*>{Tool->Actor.Get()}));
	}
	const FString TargetId = Target->Id;
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		Delta->SetNumberField(TEXT("triangles"), RuntimePrimitives::TriangleCount(Inv.GetGraph()->Find(TargetId)->Actor.Get()));
		Inv.Succeed(Delta);
	}
}

static void ArrayCopies(FToolInvocation& Inv)
{
	FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	const FString Mode = Inv.GetString(TEXT("mode"), TEXT("linear"));
	const FTransform Base = RelativeOf(Node->Actor.Get());
	TArray<FTransform> Relatives;
	if (Mode == TEXT("grid"))
	{
		FVector Counts(2, 2, 1);
		Inv.GetVector(TEXT("counts"), Counts);
		const FBox Box = ActorBox(Node->Actor.Get());
		FVector Spacing = Box.GetSize();
		Inv.GetVector(TEXT("spacing"), Spacing);
		const int32 NX = FMath::Max(1, FMath::RoundToInt(Counts.X)), NY = FMath::Max(1, FMath::RoundToInt(Counts.Y)), NZ = FMath::Max(1, FMath::RoundToInt(Counts.Z));
		if (NX * NY * NZ - 1 > MaxCopies)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("too many copies (max %d)"), MaxCopies));
			return;
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
					T.AddToTranslation(FVector(X * Spacing.X, Y * Spacing.Y, Z * Spacing.Z));
					Relatives.Add(T);
				}
			}
		}
	}
	else
	{
		const int32 Count = static_cast<int32>(Inv.GetInt(TEXT("count"), 4));
		if (Count - 1 > MaxCopies || Count < 2)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("count must be 2..%d (includes the original)"), MaxCopies + 1));
			return;
		}
		if (Mode == TEXT("radial"))
		{
			FVector Center = FVector::ZeroVector; // parent space
			Inv.GetVector(TEXT("center"), Center);
			FVector Axis(0, 0, 1);
			Inv.GetVector(TEXT("axis"), Axis);
			Axis = Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			const double Total = Inv.GetNumber(TEXT("angle"), 360.0);
			const int32 Divisions = FMath::IsNearlyEqual(Total, 360.0) ? Count : Count - 1;
			for (int32 i = 1; i < Count; ++i)
			{
				const FQuat Q(Axis, FMath::DegreesToRadians(Total * i / Divisions));
				FTransform T = Base;
				T.SetLocation(Center + Q.RotateVector(Base.GetLocation() - Center));
				T.SetRotation(Q * Base.GetRotation());
				Relatives.Add(T);
			}
		}
		else
		{
			FVector Offset(100, 0, 0);
			Inv.GetVector(TEXT("offset"), Offset);
			for (int32 i = 1; i < Count; ++i)
			{
				FTransform T = Base;
				T.AddToTranslation(Offset * i);
				Relatives.Add(T);
			}
		}
	}
	SpawnCopies(Inv, *Node, Relatives, FString::Printf(TEXT("%s array of %s"), *Mode, *Node->Name));
}

static void Mirror(FToolInvocation& Inv)
{
	FRuntimeSceneNode* Node = ResolveNode(Inv, TEXT("target"));
	if (!Node)
	{
		return;
	}
	const FString AxisName = Inv.GetString(TEXT("axis"), TEXT("x"));
	FVector Point = FVector::ZeroVector; // parent space
	Inv.GetVector(TEXT("point"), Point);
	FVector S(1.0);
	S[AxisName == TEXT("y") ? 1 : AxisName == TEXT("z") ? 2 : 0] = -1.0;
	// Reflect through the plane: M' = M * T(-P) * S * T(P). FTransform keeps the reflection as a negative scale.
	const FMatrix Mirrored = RelativeOf(Node->Actor.Get()).ToMatrixWithScale() * FTranslationMatrix(-Point) * FScaleMatrix(S) * FTranslationMatrix(Point);
	SpawnCopies(Inv, *Node, {FTransform(Mirrored)}, FString::Printf(TEXT("mirror %s"), *Node->Name));
}

static void SetMaterial(FToolInvocation& Inv)
{
	TArray<AActor*> Actors;
	if (!ResolveActorList(Inv, TEXT("targets"), Actors))
	{
		return;
	}
	TUniquePtr<FCompositeCommand> Cmd = MakeUnique<FCompositeCommand>(TEXT("set material"));
	TArray<FString> Changed, Skipped;
	for (AActor* A : Actors)
	{
		FRuntimeSceneNode* Node = Inv.GetGraph()->FindByActor(A);
		const TSharedPtr<FJsonObject> Recipe = Node ? RuntimePrimitives::FromSource(Node->Source) : nullptr;
		if (!Recipe.IsValid())
		{
			Skipped.Add(Ref(Inv, A));
			continue;
		}
		TSharedRef<FJsonObject> New = RuntimeJson::Parse(RuntimeJson::Write(Recipe.ToSharedRef())).ToSharedRef();
		if (Inv.Has(TEXT("material")))
		{
			New->SetStringField(TEXT("material"), Inv.GetString(TEXT("material")));
			New->RemoveField(TEXT("color"));
		}
		if (Inv.Has(TEXT("color")))
		{
			New->SetField(TEXT("color"), Inv.GetValue(TEXT("color")));
		}
		FString Error;
		if (!RuntimePrimitives::ApplyMaterial(A, New, Error))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, Error, TEXT("Use a palette name or an allowed material path"));
			return;
		}
		Cmd->Add(MakeRecipeCommand(TEXT("material"), Node->Id, Node->Source, RuntimePrimitives::ToSource(New), nullptr));
		Changed.Add(Node->Id);
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetField(TEXT("changed"), RuntimeJson::Strings(Changed));
	if (Skipped.Num() > 0)
	{
		Out->SetField(TEXT("skipped_not_builder"), RuntimeJson::Strings(Skipped));
	}
	if (Cmd->IsEmpty())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("no builder primitives among the targets"), TEXT("Use materials.* tools for other meshes"));
		return;
	}
	if (Inv.RunCommand(MoveTemp(Cmd)))
	{
		Inv.Succeed(Out);
	}
}

static void Measure(FToolInvocation& Inv)
{
	AActor* A = Inv.ResolveActor(TEXT("a"));
	AActor* B = A ? Inv.ResolveActor(TEXT("b")) : nullptr;
	if (!A || !B)
	{
		return;
	}
	const FBox BA = ActorBox(A), BB = ActorBox(B);
	FVector Gap;
	for (int32 i = 0; i < 3; ++i)
	{
		Gap[i] = FMath::Max(0.0, FMath::Max(BA.Min[i] - BB.Max[i], BB.Min[i] - BA.Max[i]));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("center_distance"), RuntimeJson::Round(FVector::Distance(BA.GetCenter(), BB.GetCenter())));
	Out->SetField(TEXT("center_delta"), RuntimeJson::Vec(BB.GetCenter() - BA.GetCenter()));
	Out->SetNumberField(TEXT("gap"), RuntimeJson::Round(Gap.Size()));
	Out->SetField(TEXT("gap_xyz"), RuntimeJson::Vec(Gap));
	Out->SetField(TEXT("size_a"), RuntimeJson::Vec(BA.GetSize()));
	Out->SetField(TEXT("size_b"), RuntimeJson::Vec(BB.GetSize()));
	Out->SetBoolField(TEXT("overlapping"), BA.Intersect(BB) && Gap.IsNearlyZero());
	Inv.Succeed(Out);
}

static void CheckOverlaps(FToolInvocation& Inv)
{
	bool bOk = true;
	TArray<FRuntimeSceneNode*> Nodes = ScopeNodes(Inv, bOk);
	if (!bOk)
	{
		return;
	}
	const double Tolerance = Inv.GetNumber(TEXT("tolerance"), 1.0);
	const int32 Limit = Inv.GetLimit();
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
	TArray<TSharedPtr<FJsonValue>> Pairs;
	int32 Total = 0;
	FRuntimeSceneGraph& Graph = *Inv.GetGraph();
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
			++Total;
			if (Pairs.Num() < Limit)
			{
				TSharedRef<FJsonObject> P = Obj();
				P->SetStringField(TEXT("a"), Items[i].Node->Id);
				P->SetStringField(TEXT("b"), Items[j].Node->Id);
				P->SetField(TEXT("depth"), RuntimeJson::Vec(Depth));
				Pairs.Add(MakeShared<FJsonValueObject>(P));
			}
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("checked"), Items.Num());
	Out->SetNumberField(TEXT("overlaps"), Total);
	Out->SetArrayField(TEXT("pairs"), Pairs);
	Out->SetStringField(TEXT("note"), TEXT("bounding-box test; intended overlaps (e.g. booleans, touching parts) also appear"));
	Inv.Succeed(Out);
}

static const TCHAR* BuilderGuide = TEXT(R"GUIDE(
QUALITY BAR: aim for a recognisable, realistic landmark at true scale, not a toy block-out.
Units: centimetres, Z up. Use REAL dimensions (Eiffel Tower 33000 cm tall, base 12500 cm square;
Great Pyramid 13800 tall, base 23000). If the scene is small, scale the whole group with scene.scale_node at the end.

WORKFLOW (repeat until it matches a reference photo in your head):
1. Plan the silhouette: list the major masses with real proportions before building.
2. Build mass first (big shapes), then secondary forms (tiers, arches, roofs), then detail
   (columns, windows, railings, steps, ornaments). Realism comes from the detail layer: expect 30-150 parts.
3. Group everything: scene.create_group, then pass "parent" to every create call.
4. Use generators instead of single boxes:
   - lattice: steel truss towers/bridges (levels, taper, beam, bracing). Stack several lattices with taper for curves.
   - tiers: stepped pyramids, ziggurats, wedding-cake towers (count, taper).
   - steps: staircases/plinths. column: classical columns (array them for colonnades).
   - openings: a window/door grid; use it as the tool of boolean_op subtract on a wall box (arched for arcades).
   - roof (gable), dome, arch, tube, lathe (onion domes, finials, vases), sweep (railings, cables), extrude (footprints).
5. Materials sell realism. Use PBR presets: marble, limestone, sandstone, granite, stone, brick, terracotta, concrete,
   plaster, wood, slate_roof, copper, verdigris, bronze, gold, steel, iron, painted_iron, glass, tinted_glass, water,
   grass, sand, asphalt, light. Tune roughness/metallic/color per part; mix 2-4 related materials, never one flat color.
   Use "bevel" (1-5% of the part size) on boxes so edges catch light.
6. Context: a ground plane (grass/sand/asphalt/water) larger than the build, paths, a plinth.
   Then lighting.apply_mood (golden_hour / sunset / overcast) for atmosphere.
7. Verify: build.capture_view (front/right/top/perspective) and capture.screenshot. Compare against the real
   landmark: proportions, silhouette, part count, colors. Fix with scene.move_node/scale_node or rebuild parts. Iterate at least twice.
8. build.check_overlaps and build.measure to catch floating or misplaced parts. Everything rests on something.
9. Frame the view with camera.frame_target; the player keeps WASD/mouse fly control afterwards.
Use ops.run_batch to send many create calls in one round trip.
)GUIDE");

static void GetGuide(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("guide"), FString(BuilderGuide).TrimStartAndEnd());
	Inv.Succeed(Out);
}

static void GetBuildStats(FToolInvocation& Inv)
{
	bool bOk = true;
	TArray<FRuntimeSceneNode*> Nodes = ScopeNodes(Inv, bOk);
	if (!bOk)
	{
		return;
	}
	int64 Triangles = 0;
	int32 Prims = 0, Components = 0, DrawCalls = 0;
	TSet<const UMaterialInterface*> Materials;
	FBox All(ForceInit);
	for (FRuntimeSceneNode* N : Nodes)
	{
		AActor* A = N->Actor.Get();
		All += ActorBox(A);
		if (N->Source.StartsWith(TEXT("prim:")))
		{
			++Prims;
			Triangles += RuntimePrimitives::TriangleCount(A);
		}
		TInlineComponentArray<UPrimitiveComponent*> PrimComps(A);
		for (UPrimitiveComponent* C : PrimComps)
		{
			if (!C->IsVisible())
			{
				continue;
			}
			++Components;
			DrawCalls += FMath::Max(1, C->GetNumMaterials());
			for (int32 i = 0; i < C->GetNumMaterials(); ++i)
			{
				Materials.Add(C->GetMaterial(i));
			}
			if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C); SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetRenderData()
				&& SM->GetStaticMesh()->GetRenderData()->LODResources.Num() > 0)
			{
				Triangles += SM->GetStaticMesh()->GetRenderData()->LODResources[0].GetNumTriangles();
			}
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("nodes"), Nodes.Num());
	Out->SetNumberField(TEXT("builder_primitives"), Prims);
	Out->SetNumberField(TEXT("triangles"), static_cast<double>(Triangles));
	Out->SetNumberField(TEXT("components"), Components);
	Out->SetNumberField(TEXT("materials"), Materials.Num());
	Out->SetNumberField(TEXT("draw_calls_estimate"), DrawCalls);
	if (All.IsValid)
	{
		Out->SetField(TEXT("size"), RuntimeJson::Vec(All.GetSize()));
	}
	Inv.Succeed(Out);
}

static void CaptureView(FToolInvocation& Inv)
{
	if (!FApp::CanEverRender())
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("rendering is disabled (null RHI)"));
		return;
	}
	bool bOk = true;
	TArray<FRuntimeSceneNode*> Nodes;
	TArray<AActor*> Targets;
	if (Inv.Has(TEXT("targets")))
	{
		if (!ResolveActorList(Inv, TEXT("targets"), Targets))
		{
			return;
		}
	}
	else
	{
		for (FRuntimeSceneNode* N : ScopeNodes(Inv, bOk))
		{
			Targets.Add(N->Actor.Get());
		}
		if (!bOk)
		{
			return;
		}
	}
	FBox Box(ForceInit);
	for (AActor* A : Targets)
	{
		Box += ActorBox(A);
	}
	if (!Box.IsValid)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("nothing to capture"), TEXT("Build something first, or pass targets"));
		return;
	}
	TArray<FString> Views = Inv.GetStringArray(TEXT("views"));
	if (Views.Num() == 0)
	{
		Views = {TEXT("front"), TEXT("right"), TEXT("top"), TEXT("perspective")};
	}
	const int32 Size = static_cast<int32>(Inv.GetInt(TEXT("size"), 384));
	const double Padding = Inv.GetNumber(TEXT("padding"), 1.15);
	const FVector Center = Box.GetCenter();
	const FVector Extent = Box.GetExtent();
	const double Radius = FMath::Max(Extent.Size(), 10.0);

	UWorld* World = Inv.GetWorld();
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

	const int32 Columns = FMath::Min(Views.Num(), Views.Num() == 4 ? 2 : 3);
	const int32 Rows = FMath::DivideAndRoundUp(Views.Num(), Columns);
	TArray<FColor> Sheet;
	Sheet.SetNumZeroed(Columns * Size * Rows * Size);
	TArray<FString> Layout;
	for (int32 v = 0; v < Views.Num(); ++v)
	{
		const FString& View = Views[v];
		FVector Dir;     // camera looks along Dir
		double Width;    // ortho width covering the visible extent
		if (View == TEXT("front")) { Dir = FVector(1, 0, 0); Width = 2 * FMath::Max(Extent.Y, Extent.Z); }
		else if (View == TEXT("back")) { Dir = FVector(-1, 0, 0); Width = 2 * FMath::Max(Extent.Y, Extent.Z); }
		else if (View == TEXT("right")) { Dir = FVector(0, -1, 0); Width = 2 * FMath::Max(Extent.X, Extent.Z); }
		else if (View == TEXT("left")) { Dir = FVector(0, 1, 0); Width = 2 * FMath::Max(Extent.X, Extent.Z); }
		else if (View == TEXT("top")) { Dir = FVector(0, 0, -1); Width = 2 * FMath::Max(Extent.X, Extent.Y); }
		else { Dir = FVector(1, 1, -0.6).GetSafeNormal(); Width = 0.0; }
		const bool bOrtho = Width > 0.0;
		FRotator Rot = Dir.Rotation();
		if (View == TEXT("top"))
		{
			Rot = FRotator(-90, 0, 0); // +X up in the image
		}
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
			Inv.Fail(ERuntimeToolError::Internal, Error);
			return;
		}
		const int32 OX = (v % Columns) * Size, OY = (v / Columns) * Size;
		for (int32 Y = 0; Y < FMath::Min(H, Size); ++Y)
		{
			FMemory::Memcpy(&Sheet[(OY + Y) * Columns * Size + OX], &Pixels[Y * W], FMath::Min(W, Size) * sizeof(FColor));
		}
		Layout.Add(FString::Printf(TEXT("%s(row %d, col %d)"), *View, v / Columns, v % Columns));
	}
	Capture->Destroy();
	RT->ReleaseResource();

	FToolAttachment Image;
	FString Error;
	if (!RuntimeImage::Encode(MoveTemp(Sheet), Columns * Size, Rows * Size, Columns * Size, Inv.GetString(TEXT("format"), TEXT("jpg")), 85, Image, Error))
	{
		Inv.Fail(ERuntimeToolError::Internal, Error);
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetField(TEXT("layout"), RuntimeJson::Strings(Layout));
	Out->SetField(TEXT("bounds_size"), RuntimeJson::Vec(Box.GetSize()));
	Out->SetNumberField(TEXT("w"), Image.Width);
	Out->SetNumberField(TEXT("h"), Image.Height);
	Inv.AddAttachment(MoveTemp(Image));
	Inv.Succeed(Out);
}

void FBuildToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Placement = [](FToolSchema& S)
	{
		S.Add(TEXT("location"), FToolParam::Vec3(TEXT("[x,y,z] cm (relative to parent)")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("[pitch,yaw,roll]")))
			.Add(TEXT("scale"), FToolParam::Vec3(TEXT("[x,y,z]")))
			.Add(TEXT("parent"), FToolParam::Target(TEXT("Parent node")))
			.Add(TEXT("name"), FToolParam::String(TEXT("Node name"), 128))
			.Add(TEXT("tags"), FToolParam::StringArray(TEXT("Tags"), 16));
	};
	const auto Looks = [](FToolSchema& S)
	{
		S.Add(TEXT("detail"), FToolParam::Enum(TEXT("Tessellation"), {TEXT("low"), TEXT("medium"), TEXT("high")}).Default(TEXT("medium")))
			.Add(TEXT("material"), FToolParam::Enum(TEXT("PBR preset (or use a material path via build.set_material)"), RuntimePrimitives::PaletteNames()).Default(TEXT("white")))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("Base color override [r,g,b] 0-1 (linear)")))
			.Add(TEXT("roughness"), FToolParam::Number(TEXT("0 mirror .. 1 matte"), 0.0, 1.0))
			.Add(TEXT("metallic"), FToolParam::Number(TEXT("0 dielectric .. 1 metal"), 0.0, 1.0))
			.Add(TEXT("emissive"), FToolParam::Vec3(TEXT("Glow color [r,g,b]")))
			.Add(TEXT("emissive_strength"), FToolParam::Number(TEXT("Glow strength (5-50 for lamps)"), 0.0, 1000.0))
			.Add(TEXT("opacity"), FToolParam::Number(TEXT("Glass opacity"), 0.0, 1.0))
			.Add(TEXT("variation"), FToolParam::Number(TEXT("Surface color variation 0..0.5"), 0.0, 1.0))
			.Add(TEXT("bevel"), FToolParam::Number(TEXT("Chamfer edges by this many cm (boxes, wedges, extrusions)"), 0.0, 500.0));
	};

	{
		FToolSchema S;
		S.Add(TEXT("shape"), FToolParam::Enum(TEXT("Shape or generator"), {TEXT("box"), TEXT("sphere"), TEXT("cylinder"), TEXT("cone"), TEXT("pyramid"),
				TEXT("torus"), TEXT("wedge"), TEXT("capsule"), TEXT("plane"), TEXT("dome"), TEXT("tube"), TEXT("arch"), TEXT("roof"), TEXT("lattice"),
				TEXT("steps"), TEXT("tiers"), TEXT("column"), TEXT("openings")}).Required())
			.Add(TEXT("size"), FToolParam::Vec3(TEXT("[x,y,z] cm: box dims; round shapes use x as diameter, z as height; torus z = tube; arch/roof = width, depth, height")))
			.Add(TEXT("levels"), FToolParam::Integer(TEXT("lattice: bays up the height"), 1, 64))
			.Add(TEXT("taper"), FToolParam::Number(TEXT("lattice/tiers: top size / bottom size"), 0.0, 4.0))
			.Add(TEXT("beam"), FToolParam::Number(TEXT("lattice: member thickness cm"), 0.5))
			.Add(TEXT("bracing"), FToolParam::Boolean(TEXT("lattice: X-bracing on faces")))
			.Add(TEXT("count"), FToolParam::Integer(TEXT("steps/tiers: number of steps or tiers"), 1, 200))
			.Add(TEXT("rows"), FToolParam::Integer(TEXT("openings: rows"), 1, 100))
			.Add(TEXT("cols"), FToolParam::Integer(TEXT("openings: columns"), 1, 200))
			.Add(TEXT("opening"), FToolParam::Array(TEXT("openings: [width,height] cm of each opening"), FToolParam::Number(FString()), 2))
			.Add(TEXT("arched"), FToolParam::Boolean(TEXT("openings: round-topped")))
			.Add(TEXT("margin"), FToolParam::Number(TEXT("openings: border around the grid cm"), 0.0))
			.Add(TEXT("pivot"), FToolParam::Enum(TEXT("Origin"), {TEXT("base"), TEXT("center")}).Default(TEXT("base")))
			.Add(TEXT("top_ratio"), FToolParam::Number(TEXT("Cone top radius / base radius"), 0.0, 1.0))
			.Add(TEXT("thickness"), FToolParam::Number(TEXT("Tube wall thickness cm"), 0.1));
		Looks(S);
		Placement(S);
		B.Tool(TEXT("create_primitive"), TEXT("Create a parametric mesh at true scale (cm). Base pivot by default."))
			.Args(S).Undoable().RequiresWorld().Handle(&CreatePrimitive);
	}
	{
		FToolSchema S;
		S.Add(TEXT("kind"), FToolParam::Enum(TEXT("Solid kind"), {TEXT("lathe"), TEXT("extrude"), TEXT("sweep")}).Required())
			.Add(TEXT("profile"), FToolParam::Array(TEXT("2D points [[x,y],...]: lathe = [radius,height] path; extrude/sweep = closed polygon"),
				FToolParam::Array(FString(), FToolParam::Number(FString()), 2), 512).Required())
			.Add(TEXT("height"), FToolParam::Number(TEXT("Extrude height cm"), 0.1))
			.Add(TEXT("angle"), FToolParam::Number(TEXT("Lathe degrees"), 1.0, 360.0))
			.Add(TEXT("path"), FToolParam::Array(TEXT("Sweep path [[x,y,z],...]"), FToolParam::Vec3(FString()), 512))
			.Add(TEXT("closed"), FToolParam::Boolean(TEXT("Sweep path is a loop")));
		Looks(S);
		Placement(S);
		B.Tool(TEXT("create_profile_solid"), TEXT("Create a solid from a 2D profile: lathe (domes, columns, vases), extrude (walls, plans), sweep (rails, pipes)."))
			.Args(S).Undoable().RequiresWorld().Handle(&CreateProfileSolid);
	}

	B.Tool(TEXT("boolean_op"), TEXT("Cut, join or intersect two builder primitives. The tool is consumed unless keep_tool."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Primitive to change")).Required())
			.Add(TEXT("tool"), FToolParam::Target(TEXT("Primitive used as the cutter/addition")).Required())
			.Add(TEXT("op"), FToolParam::Enum(TEXT("Operation"), {TEXT("subtract"), TEXT("union"), TEXT("intersect")}).Default(TEXT("subtract")))
			.Add(TEXT("keep_tool"), FToolParam::Boolean(TEXT("Keep the tool node")).Default(false)))
		.Undoable().RequiresWorld()
		.Handle(&BooleanOp);

	B.Tool(TEXT("array"), TEXT("Copy a node in a line, around an axis or in a grid (copies share its parent). One undo step."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("mode"), FToolParam::Enum(TEXT("Pattern"), {TEXT("linear"), TEXT("radial"), TEXT("grid")}).Default(TEXT("linear")))
			.Add(TEXT("count"), FToolParam::Integer(TEXT("Total items incl. the original (linear/radial)"), 2, MaxCopies + 1).Default(4))
			.Add(TEXT("offset"), FToolParam::Vec3(TEXT("Linear step [x,y,z] cm (parent space)")))
			.Add(TEXT("center"), FToolParam::Vec3(TEXT("Radial centre (parent space, default origin)")))
			.Add(TEXT("axis"), FToolParam::Vec3(TEXT("Radial axis (default Z)")))
			.Add(TEXT("angle"), FToolParam::Number(TEXT("Radial total degrees"), 1.0, 360.0).Default(360.0))
			.Add(TEXT("counts"), FToolParam::Vec3(TEXT("Grid counts [nx,ny,nz]")))
			.Add(TEXT("spacing"), FToolParam::Vec3(TEXT("Grid spacing [x,y,z] cm (default: bounds size)"))))
		.Undoable().RequiresWorld()
		.Handle(&ArrayCopies);

	B.Tool(TEXT("mirror"), TEXT("Create a mirrored copy of a node across a plane (parent space)."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target().Required())
			.Add(TEXT("axis"), FToolParam::Enum(TEXT("Mirror normal"), {TEXT("x"), TEXT("y"), TEXT("z")}).Default(TEXT("x")))
			.Add(TEXT("point"), FToolParam::Vec3(TEXT("A point on the mirror plane (default parent origin)"))))
		.Undoable().RequiresWorld()
		.Handle(&Mirror);

	B.Tool(TEXT("set_material"), TEXT("Set palette material / color on builder primitives."))
		.Args(FToolSchema()
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Builder nodes"), 500).Required())
			.Add(TEXT("material"), FToolParam::String(TEXT("Palette name or material path"), 512))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("Color override [r,g,b] 0-1"))))
		.Undoable().RequiresWorld()
		.Handle(&SetMaterial);

	B.Tool(TEXT("measure"), TEXT("Distance, gap and sizes between two objects' bounds."))
		.Args(FToolSchema().Add(TEXT("a"), FToolParam::Target().Required()).Add(TEXT("b"), FToolParam::Target().Required()))
		.RequiresWorld()
		.Handle(&Measure);

	B.Tool(TEXT("check_overlaps"), TEXT("Node pairs whose bounds interpenetrate more than the tolerance."))
		.Args(FToolSchema()
			.Add(TEXT("under"), FToolParam::Target(TEXT("Only check below this node")))
			.Add(TEXT("tolerance"), FToolParam::Number(TEXT("Ignore overlaps up to this depth cm"), 0.0).Default(1.0))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&CheckOverlaps);

	B.Tool(TEXT("get_guide"), TEXT("READ FIRST before building: workflow, real-scale dimensions, generators and materials for realistic results."))
		.Args(FToolSchema())
		.Handle(&GetGuide);

	B.Tool(TEXT("get_build_stats"), TEXT("Nodes, triangles, components, materials and estimated draw calls."))
		.Args(FToolSchema().Add(TEXT("under"), FToolParam::Target(TEXT("Only count below this node"))))
		.RequiresWorld()
		.Handle(&GetBuildStats);

	B.Tool(TEXT("capture_view"), TEXT("One contact-sheet image of ortho front/right/top (and perspective) views framing the build."))
		.Args(FToolSchema()
			.Add(TEXT("views"), FToolParam::Array(TEXT("Views (default front,right,top,perspective)"),
				FToolParam::Enum(FString(), {TEXT("front"), TEXT("back"), TEXT("left"), TEXT("right"), TEXT("top"), TEXT("perspective")}), 6))
			.Add(TEXT("targets"), FToolParam::StringArray(TEXT("Frame these (default: all nodes or 'under')"), 500))
			.Add(TEXT("under"), FToolParam::Target(TEXT("Frame this subtree")))
			.Add(TEXT("size"), FToolParam::Integer(TEXT("Pixels per view"), 64, 1024).Default(384))
			.Add(TEXT("padding"), FToolParam::Number(TEXT("Margin factor"), 1.0, 3.0).Default(1.15))
			.Add(TEXT("format"), FToolParam::Enum(TEXT("Image format"), {TEXT("jpg"), TEXT("png")}).Default(TEXT("jpg"))))
		.RequiresWorld()
		.Handle(&CaptureView);
}
