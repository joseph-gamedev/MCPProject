#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeRoadToolset.generated.h"

class AActor;

/** A road or roadside object that was made. */
USTRUCT()
struct FRoadToolResult
{
	GENERATED_BODY()

	/** The node (id). */
	UPROPERTY()
	FString Id;

	/** Road length (m); 0 for objects. */
	UPROPERTY()
	float LengthM = 0.f;

	/** Points the road follows (after draping onto the ground). */
	UPROPERTY()
	int32 Points = 0;
};

/** Where a road can join a junction: start (or end) a road.create route exactly here and it lines up. */
USTRUCT()
struct FRoadToolExit
{
	GENERATED_BODY()

	/** The end of the junction's stub (cm). */
	UPROPERTY()
	FVector Point = FVector::ZeroVector;

	/** The way a road leaves the junction from here. */
	UPROPERTY()
	FVector Direction = FVector::ZeroVector;
};

/** A junction piece that was placed. */
USTRUCT()
struct FRoadToolJunction
{
	GENERATED_BODY()

	/** The junction's main piece (node id). */
	UPROPERTY()
	FString Id;

	/** Where roads join it. */
	UPROPERTY()
	TArray<FRoadToolExit> Exits;
};

/** A generated town. */
USTRUCT()
struct FRoadToolTown
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Roads = 0;

	UPROPERTY()
	int32 Junctions = 0;

	UPROPERTY()
	int32 SideStreets = 0;

	UPROPERTY()
	int32 Buildings = 0;

	UPROPERTY()
	int32 Trees = 0;

	/** The roads (node ids). */
	UPROPERTY()
	TArray<FString> RoadIds;
};

/** What the road toolset can make. */
USTRUCT()
struct FRoadToolCatalog
{
	GENERATED_BODY()

	/** False when the Road Creator Pro pack is not in the project. */
	UPROPERTY()
	bool bInstalled = false;

	/** Road types for road.create type. */
	UPROPERTY()
	TArray<FString> Types;

	/** Kinds for road.place. */
	UPROPERTY()
	TArray<FString> Kinds;

	/** Every road setting road.create options accepts: name -> type. */
	UPROPERTY()
	TMap<FString, FString> Settings;
};

/**
 * Roads (Road Creator Pro): asphalt roads with lane markings, sidewalks and curbs, lampposts with lights, guardrails,
 * fences, concrete and noise barriers, gravel shoulders, cat's eyes, patches and skid marks, bridges with supports, and the
 * street furniture around them (traffic lights, signs, barriers, crossings and road decals, parking, buildings).
 * Workflow: list -> junctions (intersection, roundabout, branch) -> create roads starting/ending on their exits -> place
 * (lights, signs, crossings) -> screenshot. Or generate a whole town in one call.
 * Roads that start or end on a junction exit snap to it and leave it straight, so the asphalt joins without gaps.
 * A road is built when it is made: to change one, remove it and create it again.
 */
UCLASS()
class URuntimeRoadToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	/**
	 * Road types, the kinds road.place makes, and every road setting by name.
	 *
	 * @return The catalog (installed false without the pack).
	 */
	UFUNCTION(meta = (AICallable), Category = "Road")
	static FRoadToolCatalog List();

	/**
	 * Builds a road along a route. The route is draped onto the ground below (terrain, landscape, anything with collision)
	 * every few metres, so the road follows hills. Undoable.
	 *
	 * @param Points - Route [[x,y,z],...] (cm, at least 2), smoothed into a curve; z only matters where follow_ground is false.
	 *                 A first or last point on a junction exit (within 5 m) snaps to it and leaves the junction straight.
	 * @param Spline - Or follow a spline node (spline.create_spline) instead of points.
	 * @param Type - Road type from list, e.g. "2-Lane Road" (default).
	 * @param Sidewalks - none, left, right or both.
	 * @param Lampposts - none, left, right, center or both.
	 * @param Guardrails - none, left, right or both.
	 * @param Fences - none, left, right or both.
	 * @param bLights - Lamppost lights on (night scenes).
	 * @param bFollowGround - Drape the route onto the ground (default true).
	 * @param bClosed - A loop (ring road, race track).
	 * @param Options - Any other Road Creator setting by name (road.list settings), e.g. {"Guardrails R": true, "💦 Wet": 0.6}.
	 * @param Name - Node name.
	 * @return The road, its length and point count.
	 * Fails without the pack, with fewer than 2 points, or for an unknown type or setting.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Points,Options"), Category = "Road")
	static FRoadToolResult Create(const TArray<FVector>& Points, const FRuntimeToolJson& Options, AActor* Spline = nullptr, const FString& Type = TEXT(""),
		const FString& Sidewalks = TEXT("none"), const FString& Lampposts = TEXT("none"), const FString& Guardrails = TEXT("none"),
		const FString& Fences = TEXT("none"), bool bLights = false, bool bFollowGround = true, bool bClosed = false,
		const FString& Name = TEXT("Road"));

	/**
	 * Places street furniture from the pack, standing on the ground. Undoable.
	 *
	 * @param Kind - From list kinds, e.g. traffic_lights, road_sign, destination_sign, lamp_post, boom_barrier, zebra_crossing,
	 *               stop_line, speed_limit, parking_area, building, bench, cones.
	 * @param Location - Where (cm); dropped onto the ground.
	 * @param Yaw - Facing, degrees.
	 * @param Options - The object's settings by name (reflection.list_properties on one shows them).
	 * @return The object.
	 * Fails without the pack or for an unknown kind.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Yaw,Options"), Category = "Road")
	static FRoadToolResult Place(const FString& Kind, const FVector& Location, const float& Yaw, const FRuntimeToolJson& Options);

	/**
	 * A 4-way intersection (20 m square, for 2-lane roads with sidewalks). Undoable.
	 *
	 * @param Location - Centre (cm); dropped onto the ground.
	 * @param Yaw - Turn, degrees.
	 * @return The piece and its 4 exits (start each road on one).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Yaw"), Category = "Road")
	static FRoadToolJunction Intersection(const FVector& Location, const float& Yaw);

	/**
	 * A roundabout (70 m ring with an island) with one 2-lane arm aimed at each point in toward. Undoable.
	 * Keep arms at least ~50 degrees apart.
	 *
	 * @param Location - Centre (cm); dropped onto the ground.
	 * @param Toward - Where each road goes [[x,y,z],...] (one arm per point, 1 to 6).
	 * @return The ring and one exit per arm, in the order of toward.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Road")
	static FRoadToolJunction Roundabout(const FVector& Location, const TArray<FVector>& Toward);

	/**
	 * A side road's mouth on an existing road (a T-junction). Start the side road on the exit. Undoable.
	 *
	 * @param Road - The road to branch from (road.create id).
	 * @param At - Near where on the road (cm); the nearest point on it is used.
	 * @param Side - left, right (of the road's direction) or auto (the side At is on).
	 * @param Lanes - 1 or 2: the side road's lanes.
	 * @return The mouth and its exit.
	 * Fails on a side with a sidewalk (the mouth would cut it): branch from the other side or a road without one.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Road")
	static FRoadToolJunction Branch(AActor* Road, const FVector& At, const FString& Side = TEXT("auto"), int32 Lanes = 1);

	/**
	 * Builds a whole town on the ground below: roads joined by real junctions, side streets and buildings facing the
	 * roads, trees on the verges and woods between the roads. organic: junctions scattered at random, joined by curving roads, roundabouts where 3+ roads meet, dead-end
	 * side streets. grid: a street grid with intersections, traffic lights and crossings. Make the ground first
	 * (build.create_terrain). Undoable piece by piece.
	 *
	 * @param Layout - organic or grid.
	 * @param Location - Centre of the town (cm).
	 * @param Size - organic: width of the area (cm).
	 * @param Junctions - organic: how many junctions.
	 * @param SideStreets - organic: dead-end side streets.
	 * @param Cols - grid: blocks across.
	 * @param Rows - grid: blocks down.
	 * @param Block - grid: street spacing (cm).
	 * @param Buildings - At most this many buildings (0 for roads only).
	 * @param Trees - About this many trees: street trees on the verges and woods between the roads (0 for none; needs the
	 *                RuntimeFoliage trees).
	 * @param Seed - Layout seed.
	 * @param bLights - Street and window lights on (night).
	 * @return What was made.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location"), Category = "Road")
	static FRoadToolTown Generate(const FVector& Location, const FString& Layout = TEXT("organic"), float Size = 90000.f, int32 Junctions = 12,
		int32 SideStreets = 8, int32 Cols = 4, int32 Rows = 3, float Block = 10000.f, int32 Buildings = 150, int32 Trees = 1500, int32 Seed = 1, bool bLights = false);
};
