#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeBuildToolset.generated.h"

class AActor;

/** Parametric shape or generator of build.create_primitive. */
UENUM()
enum class EBuildToolShape : uint8
{
	Box,
	Sphere,
	Cylinder,
	Cone,
	Pyramid,
	Torus,
	Wedge,
	Capsule,
	Plane,
	Dome,
	Tube,
	Arch,
	/** Gable roof. */
	Roof,
	/** Tapered truss (levels, taper, beam, bracing): towers, pylons, cranes. */
	Lattice,
	/** Stairs (count). */
	Steps,
	/** Stacked, shrinking boxes (count, taper): pagodas, ziggurats, wedding cakes. */
	Tiers,
	/** Column with base and capital. */
	Column,
	/** Wall with a grid of openings (rows, cols, opening, arched, margin): facades, arcades. */
	Openings,
};

UENUM()
enum class EBuildToolDetail : uint8
{
	Low,
	Medium,
	High,
};

UENUM()
enum class EBuildToolPivot : uint8
{
	/** Origin at the bottom centre (the object sits on its location). */
	Base,
	Center,
};

UENUM()
enum class EBuildToolSolidKind : uint8
{
	/** Spin a [radius, height] profile around Z: domes, columns, vases. */
	Lathe,
	/** Push a closed polygon up by height: walls, floor plans. */
	Extrude,
	/** Sweep a closed polygon along a path: rails, pipes, mouldings. */
	Sweep,
};

UENUM()
enum class EBuildToolBooleanOp : uint8
{
	Subtract,
	Union,
	Intersect,
};

UENUM()
enum class EBuildToolArrayMode : uint8
{
	/** Copies `offset` apart. */
	Linear,
	/** Copies around an axis through `center`. */
	Radial,
	/** counts [nx, ny, nz] copies, `spacing` apart. */
	Grid,
};

UENUM()
enum class EBuildToolAxis : uint8
{
	X,
	Y,
	Z,
};

UENUM()
enum class EBuildToolView : uint8
{
	/** Orthographic, looking along +X. */
	Front,
	Back,
	Left,
	/** Orthographic, looking along -Y. */
	Right,
	/** Orthographic, looking down (+X up in the image). */
	Top,
	/** Three-quarter perspective from above. */
	Perspective,
};

UENUM()
enum class EBuildToolImageFormat : uint8
{
	Jpg,
	Png,
};

USTRUCT()
struct FBuildToolNodeResult
{
	GENERATED_BODY()

	/** Node id of the new object (pass it to other tools). */
	UPROPERTY()
	FString Id;

	/** Node name (unique among its siblings). */
	UPROPERTY()
	FString Name;

	/** Node path, e.g. "temple/columns/col_3". */
	UPROPERTY()
	FString Path;

	/** Triangles in the generated mesh. */
	UPROPERTY()
	int32 Triangles = 0;

	/** True when snap_to_ground moved the object onto the ground below. */
	UPROPERTY()
	bool bSnappedToGround = false;
};

USTRUCT()
struct FBuildToolAssemblyResult
{
	GENERATED_BODY()

	/** Node id of the assembly (one object for all parts). */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Path;

	UPROPERTY()
	int32 Triangles = 0;

	UPROPERTY()
	bool bSnappedToGround = false;

	/** Parts built. */
	UPROPERTY()
	int32 Parts = 0;

	/** Material slots: one per distinct combination of material fields. */
	UPROPERTY()
	int32 Materials = 0;

	/** Same-plane overlapping faces nudged apart (repair_coplanar) so they do not flicker. */
	UPROPERTY()
	int32 CoplanarFacesSeparated = 0;
};

USTRUCT()
struct FBuildToolBooleanResult
{
	GENERATED_BODY()

	/** The changed node (its id). */
	UPROPERTY()
	FString Target;

	/** subtract, union or intersect. */
	UPROPERTY()
	FString Op;

	/** Boolean operations now recorded on the target. */
	UPROPERTY()
	int32 Ops = 0;

	/** Triangles after the operation. */
	UPROPERTY()
	int32 Triangles = 0;
};

USTRUCT()
struct FBuildToolCopiesResult
{
	GENERATED_BODY()

	/** Copies created (the original is not counted). */
	UPROPERTY()
	int32 Created = 0;

	/** Node ids of the copies in creation order (at most 100). */
	UPROPERTY()
	TArray<FString> Ids;
};

USTRUCT()
struct FBuildToolMaterialResult
{
	GENERATED_BODY()

	/** Node ids whose material changed. */
	UPROPERTY()
	TArray<FString> Changed;

	/** Targets that are not builder objects (use the materials toolset for them). */
	UPROPERTY()
	TArray<FString> SkippedNotBuilder;
};

USTRUCT()
struct FBuildToolMeasurement
{
	GENERATED_BODY()

	/** Distance between the bounds centres (cm). */
	UPROPERTY()
	float CenterDistance = 0.f;

	/** Centre of b minus centre of a. */
	UPROPERTY()
	FVector CenterDelta = FVector::ZeroVector;

	/** Shortest distance between the bounds (0 when they touch or overlap). */
	UPROPERTY()
	float Gap = 0.f;

	/** Gap per axis. */
	UPROPERTY()
	FVector GapXyz = FVector::ZeroVector;

	UPROPERTY()
	FVector SizeA = FVector::ZeroVector;

	UPROPERTY()
	FVector SizeB = FVector::ZeroVector;

	UPROPERTY()
	bool bOverlapping = false;
};

USTRUCT()
struct FBuildToolOverlapPair
{
	GENERATED_BODY()

	UPROPERTY()
	FString A;

	UPROPERTY()
	FString B;

	/** Interpenetration per axis (cm). */
	UPROPERTY()
	FVector Depth = FVector::ZeroVector;
};

USTRUCT()
struct FBuildToolOverlapReport
{
	GENERATED_BODY()

	/** Nodes tested. */
	UPROPERTY()
	int32 Checked = 0;

	/** Overlapping pairs found (pairs may be truncated to limit). */
	UPROPERTY()
	int32 Overlaps = 0;

	UPROPERTY()
	TArray<FBuildToolOverlapPair> Pairs;

	UPROPERTY()
	FString Note;
};

USTRUCT()
struct FBuildToolStats
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Nodes = 0;

	UPROPERTY()
	int32 BuilderPrimitives = 0;

	UPROPERTY()
	int64 Triangles = 0;

	/** Visible primitive components. */
	UPROPERTY()
	int32 Components = 0;

	/** Distinct materials. */
	UPROPERTY()
	int32 Materials = 0;

	UPROPERTY()
	int32 DrawCallsEstimate = 0;

	/** Size of the combined bounds (cm). */
	UPROPERTY()
	FVector Size = FVector::ZeroVector;
};

USTRUCT()
struct FBuildToolCaptureResult
{
	GENERATED_BODY()

	/** Where each view is in the sheet, e.g. "front(row 0, col 0)". */
	UPROPERTY()
	TArray<FString> Layout;

	/** Size of the framed bounds (cm). */
	UPROPERTY()
	FVector BoundsSize = FVector::ZeroVector;

	/** Image width in pixels. */
	UPROPERTY()
	int32 W = 0;

	/** Image height in pixels. */
	UPROPERTY()
	int32 H = 0;
};

USTRUCT()
struct FBuildToolMaterialList
{
	GENERATED_BODY()

	/** Material presets for the `material` field, sorted as defined (stones, metals, glass, ground, plain colours). */
	UPROPERTY()
	TArray<FString> Presets;

	/** Procedural surface styles for the `style` field. */
	UPROPERTY()
	TArray<FString> Styles;

	/** Every part shape accepted by create_assembly. */
	UPROPERTY()
	TArray<FString> Shapes;
};

/**
 * Builder: realistic objects and landmarks from parametric parts at true scale (cm, Z up), as editable meshes with
 * procedural PBR materials. Every create tool is one undo step and rebuilds exactly from its recipe (undo, duplicate,
 * export).
 * Workflow: get_guide (READ FIRST) -> create_assembly (up to 512 parts per call: the fast way) or create_primitive /
 * create_profile_solid -> boolean_op / array / mirror -> set_material -> capture_view, check_overlaps, measure,
 * get_build_stats to verify -> refine.
 * Material fields (on every create tool, per assembly part and in set_material): material (preset from list_materials or
 * an allowed material path), color [r,g,b] linear 0-1, roughness, metallic, emissive [r,g,b], emissive_strength, opacity,
 * variation, style, tile (pattern cm), relief (bump cm). Numeric material and generator fields left at -1 (or 0 for counts)
 * keep the preset's or the shape's own value.
 */
UCLASS()
class URuntimeBuildToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * READ FIRST before building: quality bar, real-scale dimensions, part shapes, generators, materials and the build
	 * workflow for realistic results.
	 *
	 * @param Guide - The guide text.
	 */
	UFUNCTION(meta = (AICallable), Category = "Build|Guide")
	static void GetGuide(FString& Guide);

	/**
	 * Lists the material presets, surface styles and part shapes the builder accepts.
	 *
	 * @return {presets, styles, shapes}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Build|Guide")
	static FBuildToolMaterialList ListMaterials();

	/**
	 * Creates a parametric mesh at true scale (cm). Base pivot by default, so the object sits on its location. Undoable.
	 * Fails if a material field is invalid (unknown preset or style) or the parent is not a scene node.
	 *
	 * @param Shape - Shape or generator, e.g. "box", "lattice".
	 * @param Size - [x,y,z] cm: box dims; round shapes use x as diameter and z as height; torus z = tube; arch/roof = width, depth, height. E.g. [400,300,250].
	 * @param Location - [x,y,z] cm (relative to the parent when one is given).
	 * @param Rotation - [pitch,yaw,roll] degrees.
	 * @param Scale - [x,y,z].
	 * @param Tags - Node tags, e.g. ["wall"].
	 * @param Color - Base colour override [r,g,b] linear 0-1, e.g. [0.42,0.34,0.24].
	 * @param Emissive - Glow colour [r,g,b].
	 * @param Opening - openings: [width,height] cm of each opening, e.g. [120,220].
	 * @param Name - Node name (default: the shape).
	 * @param Parent - Parent node (group or object).
	 * @param Detail - Tessellation.
	 * @param Pivot - Origin of the mesh.
	 * @param Material - Preset (list_materials), e.g. "sandstone", or an allowed material path. Omit for white.
	 * @param Roughness - 0 mirror .. 1 matte (omit for the preset value).
	 * @param Metallic - 0 dielectric .. 1 metal (omit for the preset value).
	 * @param EmissiveStrength - Glow strength (5-50 for lamps).
	 * @param Opacity - Glass opacity 0-1.
	 * @param Variation - Surface colour variation 0..0.5.
	 * @param Style - Surface style override, e.g. "brick" (presets pick one; see list_materials).
	 * @param Tile - Pattern size cm (brick length, block width, tile width).
	 * @param Relief - Bump depth cm.
	 * @param Bevel - Chamfer hard edges by this many cm. Boxes, wedges and extrusions get a small automatic bevel at -1; 0 keeps edges sharp.
	 * @param TopRatio - cone: top radius / base radius (0-1).
	 * @param Thickness - tube: wall thickness cm.
	 * @param Levels - lattice: bays up the height (default 6).
	 * @param Taper - lattice/tiers: top size / bottom size (lattice default 1, tiers 0.4).
	 * @param Beam - lattice: member thickness cm.
	 * @param bBracing - lattice: X-bracing on the faces.
	 * @param Count - steps/tiers: number of steps or tiers (steps default 8, tiers 5).
	 * @param Rows - openings: rows (default 3).
	 * @param Cols - openings: columns (default 5).
	 * @param bArched - openings: round-topped.
	 * @param Margin - openings: border around the grid cm.
	 * @param bSnapToGround - Move the object down (or up) so the bottom of its bounds rests on what is below it.
	 * @return The new node: {id, name, path, triangles, snapped_to_ground}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Size,Location,Rotation,Scale,Tags,Color,Emissive,Opening"), Category = "Build|Create")
	static FBuildToolNodeResult CreatePrimitive(EBuildToolShape Shape, const FVector& Size, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const TArray<FString>& Tags, const FVector& Color, const FVector& Emissive, const FVector2D& Opening, const FString& Name = TEXT(""),
		AActor* Parent = nullptr, EBuildToolDetail Detail = EBuildToolDetail::Medium, EBuildToolPivot Pivot = EBuildToolPivot::Base,
		const FString& Material = TEXT(""), float Roughness = -1.f, float Metallic = -1.f, float EmissiveStrength = -1.f, float Opacity = -1.f,
		float Variation = -1.f, const FString& Style = TEXT(""), float Tile = -1.f, float Relief = -1.f, float Bevel = -1.f, float TopRatio = -1.f,
		float Thickness = -1.f, int32 Levels = 0, float Taper = -1.f, float Beam = -1.f, bool bBracing = true, int32 Count = 0, int32 Rows = 0, int32 Cols = 0,
		bool bArched = false, float Margin = -1.f, bool bSnapToGround = false);

	/**
	 * Creates a solid from a 2D profile: lathe (domes, columns, vases), extrude (walls, floor plans), sweep (rails, pipes).
	 * Undoable. Fails if the profile has too few points (lathe 2, extrude and sweep 3) or a sweep has no path.
	 *
	 * @param Kind - Solid kind.
	 * @param Profile - 2D points [[x,y],...]: lathe = [radius,height] path, e.g. [[0,0],[40,0],[30,60],[0,90]]; extrude/sweep = closed polygon.
	 * @param Path - sweep: path [[x,y,z],...] cm.
	 * @param Location - [x,y,z] cm (relative to the parent when one is given).
	 * @param Rotation - [pitch,yaw,roll] degrees.
	 * @param Scale - [x,y,z].
	 * @param Tags - Node tags.
	 * @param Color - Base colour override [r,g,b] linear 0-1.
	 * @param Emissive - Glow colour [r,g,b].
	 * @param Height - extrude: height cm.
	 * @param Angle - lathe: degrees swept (360 = full turn).
	 * @param bClosed - sweep: the path is a loop.
	 * @param Name - Node name (default: the kind).
	 * @param Parent - Parent node.
	 * @param Detail - Tessellation.
	 * @param Material - Preset (list_materials) or an allowed material path. Omit for white.
	 * @param Roughness - 0 mirror .. 1 matte (omit for the preset value).
	 * @param Metallic - 0 dielectric .. 1 metal (omit for the preset value).
	 * @param EmissiveStrength - Glow strength.
	 * @param Opacity - Glass opacity 0-1.
	 * @param Variation - Surface colour variation 0..0.5.
	 * @param Style - Surface style override (list_materials).
	 * @param Tile - Pattern size cm.
	 * @param Relief - Bump depth cm.
	 * @param Bevel - Chamfer edges by this many cm. Extrusions get a small automatic bevel at -1; 0 keeps edges sharp.
	 * @param bSnapToGround - Rest the bottom of the bounds on the ground below.
	 * @return The new node: {id, name, path, triangles, snapped_to_ground}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Path,Location,Rotation,Scale,Tags,Color,Emissive"), Category = "Build|Create")
	static FBuildToolNodeResult CreateProfileSolid(EBuildToolSolidKind Kind, const TArray<FVector2D>& Profile, const TArray<FVector>& Path, const FVector& Location,
		const FRotator& Rotation, const FVector& Scale, const TArray<FString>& Tags, const FVector& Color, const FVector& Emissive, float Height = -1.f,
		float Angle = -1.f, bool bClosed = false, const FString& Name = TEXT(""), AActor* Parent = nullptr, EBuildToolDetail Detail = EBuildToolDetail::Medium,
		const FString& Material = TEXT(""), float Roughness = -1.f, float Metallic = -1.f, float EmissiveStrength = -1.f, float Opacity = -1.f,
		float Variation = -1.f, const FString& Style = TEXT(""), float Tile = -1.f, float Relief = -1.f, float Bevel = -1.f, bool bSnapToGround = false);

	/**
	 * Builds many parts (up to 512) as ONE object in one call: the fast way to make detailed buildings. Each part is a shape
	 * recipe plus location/rotation/scale and its own material fields; parts without material fields use the defaults
	 * given here. One undo step. Fails with the part index when a part is invalid.
	 *
	 * @param Parts - Part recipes. Each: {shape, size, <shape params>, location, rotation, scale, material, color, roughness, metallic, style, tile, relief, bevel}. Shapes: box sphere cylinder cone pyramid torus wedge capsule plane dome tube arch roof lattice steps tiers column openings, revolve{profile:[[r,z]..],segments,angle}, extrude{profile:[[x,y]..],height | profile:[[x,y,z]..],direction}, sweep{profile:[[x,y]..],path:[[x,y,z]..],closed}, loft{sections:[[[x,y,z]..]..],closed,cap}, raw{vertices,triangles}. Booleans per part: ops:[{op:subtract|union|intersect,tool:{recipe},location}]. E.g. [{"shape":"box","size":[400,400,40]},{"shape":"cylinder","size":[30,30,300],"location":[150,150,40],"material":"copper"}].
	 * @param Location - World location cm (relative to the parent when one is given).
	 * @param Rotation - [pitch,yaw,roll] degrees.
	 * @param Scale - [x,y,z].
	 * @param Tags - Node tags.
	 * @param Color - Default colour [r,g,b] linear 0-1.
	 * @param Emissive - Default glow colour.
	 * @param Name - Node name.
	 * @param Parent - Parent group.
	 * @param Material - Default preset for parts without one, e.g. "sandstone".
	 * @param Roughness - Default roughness (omit for the preset value).
	 * @param Metallic - Default metallic (omit for the preset value).
	 * @param EmissiveStrength - Default glow strength.
	 * @param Opacity - Default glass opacity.
	 * @param Variation - Default surface variation.
	 * @param Style - Default surface style.
	 * @param Tile - Default pattern size cm.
	 * @param Relief - Default bump depth cm.
	 * @param bRepairCoplanar - Nudge overlapping same-plane faces of later parts outward to stop flicker.
	 * @param bSnapToGround - Rest the bottom of the bounds on the ground below.
	 * @return {id, name, path, triangles, parts, materials, coplanar_faces_separated, snapped_to_ground}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,Scale,Tags,Color,Emissive"), Category = "Build|Create")
	static FBuildToolAssemblyResult CreateAssembly(const TArray<FRuntimeToolJson>& Parts, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const TArray<FString>& Tags, const FVector& Color, const FVector& Emissive, const FString& Name = TEXT("Assembly"), AActor* Parent = nullptr,
		const FString& Material = TEXT(""), float Roughness = -1.f, float Metallic = -1.f, float EmissiveStrength = -1.f, float Opacity = -1.f,
		float Variation = -1.f, const FString& Style = TEXT(""), float Tile = -1.f, float Relief = -1.f, bool bRepairCoplanar = true, bool bSnapToGround = false);

	/**
	 * Cuts, joins or intersects two builder objects. The result is recorded in the target's recipe (rebuildable); the tool
	 * object is consumed unless keep_tool. Undoable. Fails if either is not a builder object or both are the same node.
	 *
	 * @param Target - Builder object to change.
	 * @param Tool - Builder object used as the cutter or addition (positioned where it should cut).
	 * @param Op - Operation.
	 * @param bKeepTool - Keep the tool object instead of deleting it.
	 * @return {target, op, ops (operations on the target so far), triangles}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Build|Modify")
	static FBuildToolBooleanResult BooleanOp(AActor* Target, AActor* Tool, EBuildToolBooleanOp Op = EBuildToolBooleanOp::Subtract, bool bKeepTool = false);

	/**
	 * Copies a node in a line, around an axis or in a grid. Copies share its parent; offsets are in the parent's space.
	 * One undo step. Fails for groups (use scene.duplicate_node) or more than 1000 copies.
	 *
	 * @param Target - Node to copy.
	 * @param Offset - linear: step [x,y,z] cm between copies (default [100,0,0]).
	 * @param Center - radial: centre (parent space, default the parent origin).
	 * @param Axis - radial: rotation axis (default [0,0,1]).
	 * @param Counts - grid: copies per axis [nx,ny,nz] including the original (default [2,2,1]).
	 * @param Spacing - grid: spacing [x,y,z] cm (default: the bounds size).
	 * @param Mode - Pattern.
	 * @param Count - linear/radial: total items including the original (2-1001).
	 * @param Angle - radial: total degrees (360 = evenly around).
	 * @return {created, ids} (ids of the copies, at most 100).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Offset,Center,Axis,Counts,Spacing"), Category = "Build|Modify")
	static FBuildToolCopiesResult Array(AActor* Target, const FVector& Offset, const FVector& Center, const FVector& Axis, const FIntVector& Counts,
		const FVector& Spacing, EBuildToolArrayMode Mode = EBuildToolArrayMode::Linear, int32 Count = 4, float Angle = 360.f);

	/**
	 * Creates a mirrored copy of a node across a plane (parent space); the reflection is kept as a negative scale.
	 * Undoable. Fails for groups.
	 *
	 * @param Target - Node to mirror.
	 * @param Point - A point on the mirror plane [x,y,z] (default the parent origin).
	 * @param Axis - Mirror plane normal.
	 * @return {created: 1, ids: [copy]}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Point"), Category = "Build|Modify")
	static FBuildToolCopiesResult Mirror(AActor* Target, const FVector& Point, EBuildToolAxis Axis = EBuildToolAxis::X);

	/**
	 * Changes the material fields of builder objects (same fields as create tools); fields not given keep their value.
	 * One undo step. Fails if no field is given, a field is invalid, or none of the targets is a builder object.
	 *
	 * @param Targets - Builder nodes, e.g. ["n12","n13"].
	 * @param Color - Colour override [r,g,b] linear 0-1.
	 * @param Emissive - Glow colour [r,g,b].
	 * @param Material - Preset (list_materials) or an allowed material path, e.g. "copper". Setting it clears a previous colour override unless color is given.
	 * @param Roughness - 0 mirror .. 1 matte.
	 * @param Metallic - 0 dielectric .. 1 metal.
	 * @param EmissiveStrength - Glow strength.
	 * @param Opacity - Glass opacity.
	 * @param Variation - Surface colour variation.
	 * @param Style - Surface style.
	 * @param Tile - Pattern size cm.
	 * @param Relief - Bump depth cm.
	 * @return {changed: [ids], skipped_not_builder: [refs]}.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Color,Emissive"), Category = "Build|Modify")
	static FBuildToolMaterialResult SetMaterial(const TArray<AActor*>& Targets, const FVector& Color, const FVector& Emissive, const FString& Material = TEXT(""),
		float Roughness = -1.f, float Metallic = -1.f, float EmissiveStrength = -1.f, float Opacity = -1.f, float Variation = -1.f, const FString& Style = TEXT(""),
		float Tile = -1.f, float Relief = -1.f);

	/**
	 * Measures distance, gap and sizes between two objects' bounds (children included).
	 *
	 * @param A - First object.
	 * @param B - Second object.
	 * @return {center_distance, center_delta, gap, gap_xyz, size_a, size_b, overlapping}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Build|Check")
	static FBuildToolMeasurement Measure(AActor* A, AActor* B);

	/**
	 * Finds node pairs whose bounds interpenetrate more than the tolerance (parents and their children are not compared).
	 * A bounding-box test: intended overlaps (booleans, touching parts) also appear.
	 *
	 * @param Under - Only check below this node (default: every node).
	 * @param Tolerance - Ignore overlaps up to this depth cm.
	 * @param Limit - Maximum pairs returned.
	 * @return {checked, overlaps, pairs:[{a, b, depth}], note}, pairs sorted by the first node's min X.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Build|Check")
	static FBuildToolOverlapReport CheckOverlaps(AActor* Under = nullptr, float Tolerance = 1.f, int32 Limit = 50);

	/**
	 * Counts nodes, triangles, components, materials and estimated draw calls (performance budget of a build).
	 *
	 * @param Under - Only count below this node (default: every node).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Build|Check")
	static FBuildToolStats GetBuildStats(AActor* Under = nullptr);

	/**
	 * Renders one contact-sheet image (attached to the result) of orthographic front/right/top and perspective views
	 * framing the build, to check proportions. Fails without rendering (null RHI) or when there is nothing to frame.
	 *
	 * @param Views - Views to render (default front, right, top, perspective).
	 * @param Targets - Frame these objects (default: every node, or `under`).
	 * @param Under - Frame this subtree.
	 * @param Size - Pixels per view (64-1024).
	 * @param Padding - Margin factor (1-3).
	 * @param Format - Image format.
	 * @return {layout, bounds_size, w, h}; the image is attached.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Views,Targets"), Category = "Build|Check")
	static FBuildToolCaptureResult CaptureView(const TArray<EBuildToolView>& Views, const TArray<AActor*>& Targets, AActor* Under = nullptr, int32 Size = 384,
		float Padding = 1.15f, EBuildToolImageFormat Format = EBuildToolImageFormat::Jpg);
};
