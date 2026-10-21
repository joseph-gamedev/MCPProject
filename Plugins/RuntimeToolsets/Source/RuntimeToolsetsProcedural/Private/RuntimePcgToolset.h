#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimePcgToolset.generated.h"

class AActor;

USTRUCT()
struct FPcgToolPinInfo
{
	GENERATED_BODY()

	/** Pin label, used by connect_node_pins. */
	UPROPERTY()
	FString Name;

	/** Data types the pin accepts or produces (Point, Spline, Surface, Param, Any...). */
	UPROPERTY()
	FString Type;

	/** Whether the pin accepts several connections. */
	UPROPERTY()
	bool bMultipleConnections = true;
};

USTRUCT()
struct FPcgToolEdgeInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString SrcNode;

	UPROPERTY()
	FString SrcPin;

	UPROPERTY()
	FString DestNode;

	UPROPERTY()
	FString DestPin;
};

USTRUCT()
struct FPcgToolNodeInfo
{
	GENERATED_BODY()

	/** Unique node name inside the graph ("input" and "output" are the graph's own nodes). */
	UPROPERTY()
	FString Name;

	/** Native node type ("Spline Sampler", "Surface Sampler", ...). */
	UPROPERTY()
	FString NodeType;

	UPROPERTY()
	bool bEnabled = true;

	/** Parameters that differ from the node type's defaults. */
	UPROPERTY()
	FRuntimeToolJson ParamOverrides;

	UPROPERTY()
	TArray<FPcgToolPinInfo> InputPins;

	UPROPERTY()
	TArray<FPcgToolPinInfo> OutputPins;
};

USTRUCT()
struct FPcgToolGraphStructure
{
	GENERATED_BODY()

	/** Graph name (pass it as `graph` to other tools). */
	UPROPERTY()
	FString Name;

	/** Node list, including the "input" and "output" nodes. */
	UPROPERTY()
	TArray<FPcgToolNodeInfo> Nodes;

	UPROPERTY()
	TArray<FPcgToolEdgeInfo> Edges;

	/** Graph parameters and their default values. */
	UPROPERTY()
	FRuntimeToolJson GraphParams;
};

USTRUCT()
struct FPcgToolGraphSchema
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** JSON schema of the graph parameters (what set_graph_instance_params accepts). */
	UPROPERTY()
	FRuntimeToolJson GraphParamsSchema;

	UPROPERTY()
	TArray<FPcgToolPinInfo> InputPins;

	UPROPERTY()
	TArray<FPcgToolPinInfo> OutputPins;
};

USTRUCT()
struct FPcgToolNodeSchema
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Description;

	/** JSON schema of the node parameters (what add_node / update_node accept in `params`). */
	UPROPERTY()
	FRuntimeToolJson PropertiesSchema;

	UPROPERTY()
	TArray<FPcgToolPinInfo> InputPins;

	UPROPERTY()
	TArray<FPcgToolPinInfo> OutputPins;
};

UENUM()
enum class EPcgToolParamType : uint8
{
	Bool,
	Int32,
	Int64,
	Float,
	Double,
	Name,
	String,
	Vector,
	Vector2D,
	Rotator,
	Transform,
	Color,
	/** Asset path (a static mesh, material...). */
	SoftObjectPath,
};

USTRUCT()
struct FPcgToolParamDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	EPcgToolParamType Type = EPcgToolParamType::Float;

	/** An array of Type instead of a single value. */
	UPROPERTY()
	bool bArray = false;

	/** Default value (omit for the type's zero value). */
	UPROPERTY()
	FRuntimeToolJson DefaultValue;
};

USTRUCT()
struct FPcgToolGraphInstanceInfo
{
	GENERATED_BODY()

	/** The actor running the graph (a graph instance volume or the actor it was attached to). */
	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	/** Graph name. */
	UPROPERTY()
	FString Graph;
};

USTRUCT()
struct FPcgToolExecutionMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FString Message;

	/** Warning or Error. */
	UPROPERTY()
	FString Severity;
};

USTRUCT()
struct FPcgToolExecutionResult
{
	GENERATED_BODY()

	/** Warnings and errors emitted while executing (empty on a clean run). */
	UPROPERTY()
	TArray<FPcgToolExecutionMessage> Messages;

	/** Data items reaching the graph output. */
	UPROPERTY()
	int32 OutputData = 0;

	/** Points reaching the graph output. */
	UPROPERTY()
	int32 OutputPoints = 0;
};

UENUM()
enum class EPcgScatterMode : uint8
{
	/** On or beside the spline (roadside trees, fence posts, lamps). */
	Along,
	/** Fill a closed spline (forests, fields, rubble). */
	Inside,
};

UENUM()
enum class EPcgScatterSide : uint8
{
	Both,
	Left,
	Right,
};

USTRUCT()
struct FPcgScatterItem
{
	GENERATED_BODY()

	/** Node with a builder mesh to copy (build.create_assembly / create_primitive); keep it out of sight. */
	UPROPERTY()
	TObjectPtr<AActor> Template = nullptr;

	/** Relative chance of being picked. */
	UPROPERTY()
	float Weight = 1.f;

	/** Random uniform scale range [min, max]. */
	UPROPERTY()
	FVector2D Scale = FVector2D(1.0, 1.0);

	/** Face along the spline (fences, lamps). */
	UPROPERTY()
	bool bAlign = false;

	/** Random rotation around Z (ignored when aligned unless also set). */
	UPROPERTY()
	bool bRandomYaw = true;

	/** Lift above the ground (cm). */
	UPROPERTY()
	float ZOffset = 0.f;

	/** Block movement and traces. */
	UPROPERTY()
	bool bCollision = false;
};

USTRUCT()
struct FPcgScatterExclusion
{
	GENERATED_BODY()

	/** Spline node to keep clear of (a road, a river). */
	UPROPERTY()
	TObjectPtr<AActor> Spline = nullptr;

	/** Distance from the spline that stays empty (cm). */
	UPROPERTY()
	float Clearance = 300.f;
};

USTRUCT()
struct FPcgScatterCount
{
	GENERATED_BODY()

	UPROPERTY()
	FString Template;

	UPROPERTY()
	int32 Instances = 0;
};

USTRUCT()
struct FPcgScatterResult
{
	GENERATED_BODY()

	/** The scatter node (delete or undo to remove everything it placed). */
	UPROPERTY()
	TObjectPtr<AActor> Scatter = nullptr;

	/** Points produced by the PCG graph. */
	UPROPERTY()
	int32 PcgPoints = 0;

	/** Points removed by noise thinning and exclusions. */
	UPROPERTY()
	int32 FilteredOut = 0;

	/** Instances placed. */
	UPROPERTY()
	int32 Instances = 0;

	UPROPERTY()
	TArray<FPcgScatterCount> Items;
};

/**
 * Procedural Content Generation: build and run PCG graphs (the same node-by-node workflow as Epic's editor PCG toolset)
 * and scatter builder objects along or inside splines.
 * Workflow: create_graph -> list_native_nodes / get_native_node_schema -> add_node -> connect_node_pins
 * -> spawn_graph_instance (on a volume or an existing actor such as a spline) -> execute_graph_instance -> get_node_data_view.
 * For the common case (trees, rocks, fences, lamps along or inside a spline) use scatter.
 */
UCLASS()
class URuntimePcgToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	// Graphs ---------------------------------------------------------------------------------------------------------

	/**
	 * Creates a new, empty PCG graph (in memory for this session).
	 *
	 * @param Name - Unique graph name, e.g. "ForestScatter".
	 * @return The graph structure (only the input and output nodes).
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Graph")
	static FPcgToolGraphStructure CreateGraph(const FString& Name);

	/**
	 * Lists graphs: the ones created in this session and PCG graph assets packaged with the project (usable as subgraphs).
	 *
	 * @return Graph names and asset paths, sorted.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Graph")
	static TArray<FString> ListGraphs();

	/**
	 * Returns the complete structure of a graph: nodes (with parameter overrides and pins), edges and graph parameters.
	 *
	 * @param Graph - Graph name or asset path.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Graph")
	static FPcgToolGraphStructure GetGraphStructure(const FString& Graph);

	/**
	 * Adds graph parameters that each graph instance can override (set_graph_instance_params).
	 *
	 * @param Graph - Graph name.
	 * @param Params - Parameter definitions {name, type, array, default_value}.
	 * @return True when all parameters were added.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|GraphParams")
	static bool SetGraphParams(const FString& Graph, const TArray<FPcgToolParamDefinition>& Params);

	/**
	 * Removes graph parameters.
	 *
	 * @param Graph - Graph name.
	 * @param ParamNames - Names of the parameters to remove.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|GraphParams")
	static bool RemoveGraphParams(const FString& Graph, const TArray<FName>& ParamNames);

	/**
	 * Returns the schema of a graph's parameters and its input/output pins.
	 *
	 * @param Graph - Graph name or asset path.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|GraphParams")
	static FPcgToolGraphSchema GetGraphSchema(const FString& Graph);

	// Graph instances ------------------------------------------------------------------------------------------------

	/**
	 * Lists every actor in the world that runs a PCG graph.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "PCG|GraphInstance")
	static TArray<FPcgToolGraphInstanceInfo> ListGraphInstances();

	/**
	 * Runs a graph in the world: on a new volume (its transform is the area samplers fill; default 50 m wide) or on an
	 * existing actor (e.g. a spline node, so "Get Spline Data" reads its spline). Undoable.
	 *
	 * @param Graph - Graph name or asset path.
	 * @param Name - Name of the new volume node.
	 * @param Transform - Volume transform {location, rotation, scale}; scale 1 = 1 m box (default scale [50,50,10] covers 50 x 50 m).
	 * @param Params - Graph parameter overrides, e.g. {"density": 0.3}.
	 * @param OnActor - Attach to this actor instead of spawning a volume.
	 * @return The actor running the graph.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Params,Transform"), Category = "PCG|GraphInstance")
	static FPcgToolGraphInstanceInfo SpawnGraphInstance(const FString& Graph, const FRuntimeToolJson& Params, const FTransform& Transform,
		const FString& Name = TEXT("PCGVolume"), AActor* OnActor = nullptr);

	/**
	 * Executes the graph on an actor and waits for it to finish.
	 *
	 * @param Instance - Actor running the graph (from spawn_graph_instance).
	 * @return Messages emitted while executing and how much data reached the output.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "PCG|GraphInstance")
	static FPcgToolExecutionResult ExecuteGraphInstance(AActor* Instance);

	/**
	 * Gets the graph parameter values of an instance.
	 *
	 * @param Instance - Actor running the graph.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "PCG|GraphInstanceParams")
	static FRuntimeToolJson GetGraphInstanceParams(AActor* Instance);

	/**
	 * Overrides graph parameters on one instance (execute again to see the result).
	 *
	 * @param Instance - Actor running the graph.
	 * @param Params - {"param_name": value, ...}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "PCG|GraphInstanceParams")
	static bool SetGraphInstanceParams(AActor* Instance, const FRuntimeToolJson& Params);

	/**
	 * Resets instance parameters back to the graph's defaults.
	 *
	 * @param Instance - Actor running the graph.
	 * @param ParamNames - Parameters to reset.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "PCG|GraphInstanceParams")
	static bool ResetGraphInstanceParams(AActor* Instance, const TArray<FName>& ParamNames);

	// Nodes ----------------------------------------------------------------------------------------------------------

	/**
	 * Lists native PCG node types.
	 *
	 * @param bCommonOnly - Only the commonly used nodes. Use false only if the user asks for more.
	 * @return Node type names for add_node, sorted.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static TArray<FString> ListNativeNodes(bool bCommonOnly = true);

	/**
	 * Returns a node type's parameters (JSON schema) and pins.
	 *
	 * @param NodeName - Node type, e.g. "Surface Sampler".
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static FPcgToolNodeSchema GetNativeNodeSchema(const FString& NodeName);

	/**
	 * Adds a native node to a graph.
	 *
	 * @param Graph - Graph name.
	 * @param NativeNodeType - Node type from list_native_nodes, e.g. "Surface Sampler".
	 * @param NodeName - Unique name for the node in this graph.
	 * @param Params - Non-default parameters, e.g. {"points_per_squared_meter": 0.05}. See get_native_node_schema.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Params"), Category = "PCG|Nodes")
	static FPcgToolNodeInfo AddNode(const FString& Graph, const FString& NativeNodeType, const FString& NodeName, const FRuntimeToolJson& Params);

	/**
	 * Adds a node that runs another graph.
	 *
	 * @param Graph - Graph to modify.
	 * @param Subgraph - Graph to run inside it (name or asset path, see list_graphs).
	 * @param NodeName - Unique name for the node.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static FPcgToolNodeInfo AddSubgraphNode(const FString& Graph, const FString& Subgraph, const FString& NodeName);

	/**
	 * Changes a node's parameters.
	 *
	 * @param Graph - Graph name.
	 * @param Node - Node name.
	 * @param Params - Parameters to set.
	 * @param bEnabled - Enable or disable the node.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Params"), Category = "PCG|Nodes")
	static FPcgToolNodeInfo UpdateNode(const FString& Graph, const FString& Node, const FRuntimeToolJson& Params, bool bEnabled = true);

	/**
	 * Returns a node's type, parameter overrides and pins.
	 *
	 * @param Graph - Graph name or asset path.
	 * @param Node - Node name.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static FPcgToolNodeInfo GetNodeInfo(const FString& Graph, const FString& Node);

	/**
	 * Removes a node and its edges.
	 *
	 * @param Graph - Graph name.
	 * @param Node - Node name.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static bool RemoveNode(const FString& Graph, const FString& Node);

	/**
	 * Connects an output pin to an input pin.
	 *
	 * @param Graph - Graph name.
	 * @param FromNode - Source node name ("input" for the graph input).
	 * @param FromPin - Output pin label, e.g. "Out".
	 * @param ToNode - Destination node name ("output" for the graph output).
	 * @param ToPin - Input pin label, e.g. "In", "Spline", "Surface".
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static bool ConnectNodePins(const FString& Graph, const FString& FromNode, const FString& FromPin, const FString& ToNode, const FString& ToPin);

	/**
	 * Removes the edge between two pins.
	 *
	 * @param Graph - Graph name.
	 * @param FromNode - Source node name.
	 * @param FromPin - Output pin label.
	 * @param ToNode - Destination node name.
	 * @param ToPin - Input pin label.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCG|Nodes")
	static bool DisconnectNodePins(const FString& Graph, const FString& FromNode, const FString& FromPin, const FString& ToNode, const FString& ToPin);

	/**
	 * Executes the instance's graph and returns the data a node produces on one pin (points as rows).
	 *
	 * @param Instance - Actor running the graph.
	 * @param Node - Node to inspect.
	 * @param PinLabel - Output pin to read.
	 * @param StartIndex - First element, inclusive.
	 * @param EndIndex - Last element, exclusive; -1 = up to 100 elements.
	 * @return {data:[{type, count, attributes, elements:[{position, rotation, scale, density, seed}]}]}.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "PCG|Data")
	static FRuntimeToolJson GetNodeDataView(AActor* Instance, const FString& Node, const FString& PinLabel = TEXT("Out"), int32 StartIndex = 0,
		int32 EndIndex = -1);

	// Scatter --------------------------------------------------------------------------------------------------------

	/**
	 * Scatters copies of builder objects along a spline (both sides of a road, a fence line) or inside a closed spline
	 * (a forest, a field) with a PCG graph (spline or surface sampler, jitter, self pruning), then noise thinning,
	 * exclusion zones and ground snapping. Rendered as GPU instances (thousands are cheap). Undoable; same seed, same result.
	 *
	 * @param Spline - Spline node (spline.create_spline).
	 * @param Items - What to place: {template, weight, scale:[min,max], align, random_yaw, z_offset, collision}.
	 * @param Mode - along: on or beside the spline; inside: fill a closed spline.
	 * @param Spacing - Sample spacing in cm.
	 * @param Offset - along: lateral band [min,max] in cm from the spline (e.g. [500,1200] keeps trees off a road); [0,0] = on the line.
	 * @param Side - along with an offset: which side of the spline.
	 * @param Jitter - Random position offset in cm (-1 = 35% of spacing).
	 * @param MinDistance - Minimum gap between objects in cm (-1 = 60% of spacing).
	 * @param Keep - Share of points kept, clumped by noise (1 = all).
	 * @param NoiseScale - Clump size for keep, in cm.
	 * @param Exclude - Keep clear of these splines: {spline, clearance}.
	 * @param bGround - Snap to the ground below.
	 * @param Seed - Random seed.
	 * @param Name - Node name.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Exclude,Offset"), Category = "PCG|Scatter")
	static FPcgScatterResult Scatter(AActor* Spline, const TArray<FPcgScatterItem>& Items, const TArray<FPcgScatterExclusion>& Exclude,
		const FVector2D& Offset, EPcgScatterMode Mode = EPcgScatterMode::Along, float Spacing = 400.f,
		EPcgScatterSide Side = EPcgScatterSide::Both, float Jitter = -1.f, float MinDistance = -1.f, float Keep = 1.f, float NoiseScale = 3000.f,
		bool bGround = true, int32 Seed = 1, const FString& Name = TEXT("Scatter"));
};
