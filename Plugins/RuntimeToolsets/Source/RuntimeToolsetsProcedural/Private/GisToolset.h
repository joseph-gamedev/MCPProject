#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "Subsystems/WorldSubsystem.h"
#include "GisToolset.generated.h"

class AActor;

/** How create_plots lays out streets and blocks. */
UENUM()
enum class EGisLayout : uint8
{
	/** Streets by recursive splitting, slightly irregular: most real towns. */
	Blocks,
	/** A regular grid of streets (Manhattan, Barcelona, new towns). */
	Grid,
	/** Strongly irregular, angled streets (old towns, villages). */
	Organic,
	/** Ring roads and spokes around the centre (Paris Etoile, Moscow, planned capitals). */
	Radial,
};

USTRUCT()
struct FGisPlotSummary
{
	GENERATED_BODY()

	/** Plot id, e.g. "P12": pass it to get_plot, set_plot or the kit's uk.gis.build("P12", ...). */
	UPROPERTY()
	FString Id;

	/** Area in square metres. */
	UPROPERTY()
	float AreaM2 = 0.f;

	/** Centre of the plot (cm). */
	UPROPERTY()
	FVector Centroid = FVector::ZeroVector;

	/** Land use: residential, commercial, mixed, industrial, civic, green (or your own). */
	UPROPERTY()
	FString Zoning;

	/** Corners of the polygon. */
	UPROPERTY()
	int32 Vertices = 0;

	/** Edges on a street (frontage). */
	UPROPERTY()
	int32 StreetEdges = 0;

	/** Node id of the building placed on it ("" = empty plot). */
	UPROPERTY()
	FString Building;

	/** Block the plot belongs to. */
	UPROPERTY()
	FString Block;
};

USTRUCT()
struct FGisEdge
{
	GENERATED_BODY()

	UPROPERTY()
	FVector A = FVector::ZeroVector;

	UPROPERTY()
	FVector B = FVector::ZeroVector;

	/** Length in cm. */
	UPROPERTY()
	float Length = 0.f;

	/** True when the edge faces a street (a frontage: put the building's front here). */
	UPROPERTY()
	bool bStreet = false;

	/** Yaw (degrees) of the direction looking out of the plot across this edge. */
	UPROPERTY()
	float OutwardYaw = 0.f;
};

USTRUCT()
struct FGisPlotInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	/** Corners counter-clockwise seen from above (cm, z = ground). */
	UPROPERTY()
	TArray<FVector> Polygon;

	/** One per corner: edge i runs from corner i to corner i+1. */
	UPROPERTY()
	TArray<FGisEdge> Edges;

	/** Index of the main frontage: the longest street edge (or the longest edge when none faces a street). */
	UPROPERTY()
	int32 Front = 0;

	UPROPERTY()
	float AreaM2 = 0.f;

	UPROPERTY()
	FVector Centroid = FVector::ZeroVector;

	/** Smallest rotated rectangle around the plot: centre, size [along, across] cm, yaw of the long side. */
	UPROPERTY()
	FVector ObbCenter = FVector::ZeroVector;

	UPROPERTY()
	FVector2D ObbSize = FVector2D::ZeroVector;

	UPROPERTY()
	float ObbYaw = 0.f;

	/** Area / rectangle area: 1 = a rectangle, lower = irregular. */
	UPROPERTY()
	float Rectangularity = 0.f;

	UPROPERTY()
	bool bConvex = true;

	UPROPERTY()
	FString Zoning;

	/** Attributes (imported GIS fields, height, levels, name, your own). */
	UPROPERTY()
	FRuntimeToolJson Props;

	UPROPERTY()
	FString Building;

	UPROPERTY()
	FString Block;
};

USTRUCT()
struct FGisCreateResult
{
	GENERATED_BODY()

	/** Plots created. */
	UPROPERTY()
	int32 Plots = 0;

	/** Street blocks created. */
	UPROPERTY()
	int32 Blocks = 0;

	/** Area covered in square metres. */
	UPROPERTY()
	float AreaM2 = 0.f;

	/** Plots per zoning. */
	UPROPERTY()
	TMap<FString, int32> Zoning;

	/** The first plots (all of them with list_plots). */
	UPROPERTY()
	TArray<FGisPlotSummary> Sample;
};

USTRUCT()
struct FGisPlotList
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Total = 0;

	UPROPERTY()
	TArray<FGisPlotSummary> Plots;
};

USTRUCT()
struct FGisIds
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FString> Ids;
};

USTRUCT()
struct FGisOrigin
{
	GENERATED_BODY()

	/** Whether a geographic origin is set (real-world data). */
	UPROPERTY()
	bool bSet = false;

	/** Latitude of world (0,0) in degrees. */
	UPROPERTY()
	double Latitude = 0.0;

	/** Longitude of world (0,0) in degrees. */
	UPROPERTY()
	double Longitude = 0.0;

	/** How map axes map to the world: +X east, +Y south (north is -Y, so maps are not mirrored), 1 m = 100 cm. */
	UPROPERTY()
	FString Axes = TEXT("+X east, -Y north, cm");
};

/** One parcel of land. */
struct FGisPlot
{
	FString Id;
	TArray<FVector2D> Poly;       // counter-clockwise in math terms (positive signed area)
	TArray<bool> Street;          // edge i (i -> i+1) faces a street
	double Z = 0.0;
	FString Zoning;
	TSharedPtr<FJsonObject> Props;
	FString Block;
	FString Building;
};

/** Plots, blocks and streets of the current world (cleared with the world). */
UCLASS()
class UGisSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	TArray<FGisPlot> Plots;
	TArray<TArray<FVector2D>> Blocks;
	TArray<TArray<FVector2D>> Areas;      // street areas (the land the blocks were cut from)
	double AreaZ = 0.0;
	int32 NextPlot = 1;
	int32 NextBlock = 1;
	FGisOrigin Origin;
	FString VisNode;                      // scene node showing plots and streets
	TWeakObjectPtr<AActor> Labels;        // id labels (own actor when there is no plot drawing to carry them)
	TArray<TWeakObjectPtr<USceneComponent>> LabelParts;   // id labels on the plot drawing's node (cleared and undone with it)
	bool bShowPlots = true;
	bool bShowLabels = true;
	bool bShowStreets = true;
	int32 MaxLabels = 2000;

	FGisPlot* Find(const FString& Id);
};

/**
 * GIS: land plots (parcels) of any polygon shape. Generate random street blocks and lots, add your own polygons or real
 * GIS data (the Python kit's uk.gis loads GeoJSON and GeoRegions files and projects them here), see
 * them with zoning colours and id labels, query them, and put buildings on them (uk.gis.build("P12", style=...)).
 * World axes: +X east, -Y north, cm. Plots are data: scene.clear_all keeps them (gis.delete_plots all=true clears).
 */
UCLASS()
class URuntimeGisToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	/**
	 * Generates random land: streets cut an area into blocks, blocks into lots (plots) with street frontage, each with a
	 * zoning. Shown with zoning colours and id labels. E.g. {"size":[40000,30000],"layout":"blocks","seed":4}.
	 *
	 * @param Area - Land polygon [[x,y,z],...] in cm (convex works best). Empty: a rectangle of `size` around `center`.
	 * @param Center - Centre of the default rectangle.
	 * @param Size - Size [x,y] cm of the default rectangle.
	 * @param Layout - Street pattern: blocks (most towns), grid (Manhattan), organic (old town), radial (rings and spokes).
	 * @param StreetWidth - Street width cm (avenues near the first cuts are 1.4x wider).
	 * @param BlockSize - Typical block [long, short] cm (city 8000x5000, suburb 15000x6000, old town 4000x3000).
	 * @param LotWidth - Lot frontage range [min, max] cm along the street (townhouses 600-900, apartments 1500-3000).
	 * @param LotDepth - Lot depth cm (0 = half the block, as deep as it gets).
	 * @param Zoning - Land-use weights, e.g. {"residential":5,"commercial":2,"mixed":2,"industrial":1,"green":1,"civic":0.5}.
	 * @param bCenterCommercial - Commercial and mixed use cluster in the centre, homes and industry outside.
	 * @param Seed - Random seed (-1 = a new layout every call).
	 * @param bReplace - Delete existing plots first.
	 * @return Plots and blocks created, area, plots per zoning and the first plots.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Area,Zoning"), Category = "GIS")
	static FGisCreateResult CreatePlots(const TArray<FVector>& Area, const FRuntimeToolJson& Zoning, FVector Center = FVector::ZeroVector,
		FVector2D Size = FVector2D(40000.f, 30000.f), EGisLayout Layout = EGisLayout::Blocks, float StreetWidth = 1200.f,
		FVector2D BlockSize = FVector2D(8000.f, 5000.f), FVector2D LotWidth = FVector2D(1200.f, 2400.f), float LotDepth = 0.f,
		bool bCenterCommercial = true, int32 Seed = -1, bool bReplace = true);

	/**
	 * Adds plots from your own polygons (any shape, e.g. from real GIS data projected to cm).
	 *
	 * @param Plots - [{polygon:[[x,y,z],...], id?, zoning?, props?:{...}, street_edges?:[edge indices], kind?: plot|area|block}, ...].
	 *   Edge i runs corner i -> i+1. kind area = land the plots sit in (drawn under them), block = a street block.
	 * @param bReplace - Delete existing plots first.
	 * @param bRedraw - Redraw the map now (false while sending a big import in several calls: redraw on the last).
	 * @return The ids of the new plots.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisIds AddPlots(const TArray<FRuntimeToolJson>& Plots, bool bReplace = false, bool bRedraw = true);

	/**
	 * Lists plots (summary: id, area, centre, zoning, building).
	 *
	 * @param Zoning - Only this land use ("" = all).
	 * @param bOnlyEmpty - Only plots without a building.
	 * @param MinAreaM2 - Only plots at least this big.
	 * @param Limit - Maximum plots returned (pages of ~1000 stay under the answer size cap).
	 * @param Offset - Skip this many matching plots (paging through big imports).
	 * @return Total matching and the plots.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisPlotList ListPlots(const FString& Zoning = TEXT(""), bool bOnlyEmpty = false, float MinAreaM2 = 0.f, int32 Limit = 500, int32 Offset = 0);

	/**
	 * Everything about one plot: polygon, edges with street frontage, front edge, area, rotated bounding box, attributes.
	 *
	 * @param Id - Plot id, e.g. "P12".
	 * @return The plot.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisPlotInfo GetPlot(const FString& Id);

	/**
	 * The plot under a world position.
	 *
	 * @param Location - World position (z ignored).
	 * @return The plot. Fails with not_found when no plot is there.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisPlotInfo FindPlot(FVector Location);

	/**
	 * Changes a plot's zoning or attributes, or records the building standing on it.
	 *
	 * @param Id - Plot id.
	 * @param Zoning - New land use ("" = keep).
	 * @param Props - Attributes to merge in (null removes a field).
	 * @param Building - The building node now on the plot.
	 * @param bClearBuilding - Forget the building (it was deleted or moved).
	 * @return The plot summary.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Props"), Category = "GIS")
	static FGisPlotSummary SetPlot(const FString& Id, const FRuntimeToolJson& Props, const FString& Zoning = TEXT(""), AActor* Building = nullptr,
		bool bClearBuilding = false);

	/**
	 * Cuts a plot into smaller lots along its main street frontage (the original plot is removed).
	 *
	 * @param Id - Plot id.
	 * @param Parts - Number of lots (0 = from lot_width).
	 * @param LotWidth - Frontage range [min, max] cm when parts is 0.
	 * @param Seed - Random seed (-1 = random).
	 * @return The new plot ids.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisIds SubdividePlot(const FString& Id, int32 Parts = 0, FVector2D LotWidth = FVector2D(1000.f, 2000.f), int32 Seed = -1);

	/**
	 * Merges plots into one (their outline: the convex hull when they do not share edges exactly).
	 *
	 * @param Ids - Plots to merge (2 or more).
	 * @return The new plot id.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisIds MergePlots(const TArray<FString>& Ids);

	/**
	 * Deletes plots (their buildings stay; delete those with scene.delete_node).
	 *
	 * @param Ids - Plot ids to delete.
	 * @param bAll - Delete every plot, block and street area.
	 * @return The deleted ids.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Ids"), Category = "GIS")
	static FGisIds DeletePlots(const TArray<FString>& Ids, bool bAll = false);

	/**
	 * Shows or hides the plot map: zoning-coloured plots, streets and id labels (hide them once buildings stand).
	 *
	 * @param bPlots - Coloured plot surfaces.
	 * @param bLabels - Plot id labels.
	 * @param bStreets - Street, land and block surfaces.
	 * @param MaxLabels - Labels are skipped above this many plots (thousands of labels cannot be read and cost frame time).
	 * @return The plots shown.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisPlotList ShowPlots(bool bPlots = true, bool bLabels = true, bool bStreets = true, int32 MaxLabels = 2000);

	/**
	 * Sets the real-world position of world (0,0) for geographic data (the kit's uk.gis loaders set it for you).
	 *
	 * @param Latitude - Degrees.
	 * @param Longitude - Degrees.
	 * @return The origin.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisOrigin SetOrigin(double Latitude, double Longitude);

	/**
	 * The real-world position of world (0,0), if one is set.
	 *
	 * @return The origin.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FGisOrigin GetOrigin();

	/**
	 * Exports the plots as a GeoJSON FeatureCollection (longitude/latitude when an origin is set, else metres).
	 *
	 * @param bLonLat - Geographic coordinates (needs an origin); false = local metres (+X east, +Y north).
	 * @param Limit - Features per call (answers are size-capped: page big exports; the kit's uk.gis.export joins them).
	 * @param Offset - Skip this many plots.
	 * @return The FeatureCollection (plus "total": all plots).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "GIS")
	static FRuntimeToolJson ExportGeoJson(bool bLonLat = true, int32 Limit = 300, int32 Offset = 0);
};
