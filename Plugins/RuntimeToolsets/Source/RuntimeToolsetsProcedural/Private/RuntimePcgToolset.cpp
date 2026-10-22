#include "RuntimePcgToolset.h"
#include "Engine/StaticMesh.h"
#include "PcgScatter.h"
#include "SplineCore.h"
#include "RuntimeCommand.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolInvocation.h"
#include "RuntimeToolReflection.h"
#include "RuntimeToolsetsCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/BoxComponent.h"
#include "Data/PCGBasePointData.h"
#include "PCGSubgraph.h"
#include "EngineUtils.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/OutputDeviceRedirector.h"
#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FName InstanceTag(TEXT("RTPcgInstance"));

	// ---- graphs ----

	TMap<FString, TStrongObjectPtr<UPCGGraph>>& Graphs()
	{
		static TMap<FString, TStrongObjectPtr<UPCGGraph>> Map;
		return Map;
	}

	FString GraphName(const UPCGGraph* G)
	{
		for (const TPair<FString, TStrongObjectPtr<UPCGGraph>>& P : Graphs())
		{
			if (P.Value.Get() == G)
			{
				return P.Key;
			}
		}
		return G ? G->GetPathName() : FString();
	}

	TArray<FString> AssetGraphPaths()
	{
		TArray<FString> Out;
		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AR.Get().GetAssetsByClass(UPCGGraph::StaticClass()->GetClassPathName(), Assets, true);
		for (const FAssetData& A : Assets)
		{
			Out.Add(A.GetObjectPathString());
		}
		return Out;
	}

	/** Graph by session name or asset path; fails the call with the known names if missing. */
	UPCGGraph* FindGraph(const FString& Ref, bool bFail = true)
	{
		if (const TStrongObjectPtr<UPCGGraph>* G = Graphs().Find(Ref))
		{
			return G->Get();
		}
		if (Ref.Contains(TEXT("/")))
		{
			if (UPCGGraph* G = LoadObject<UPCGGraph>(nullptr, *Ref, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return G;
			}
		}
		for (const FString& Path : AssetGraphPaths())
		{
			if (FPaths::GetBaseFilename(Path) == Ref || Path.EndsWith(TEXT(".") + Ref))
			{
				return LoadObject<UPCGGraph>(nullptr, *Path);
			}
		}
		if (bFail)
		{
			TArray<FString> Names;
			Graphs().GetKeys(Names);
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no graph '%s'"), *Ref),
				Names.Num() ? TEXT("Graphs: ") + FString::Join(Names, TEXT(", ")) : TEXT("Create one with pcg.create_graph"));
		}
		return nullptr;
	}

	bool IsSessionGraph(const UPCGGraph* G)
	{
		return GraphName(G) != G->GetPathName();
	}

	// ---- node types ----

	FString SplitWords(const FString& In)
	{
		FString Out;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			const bool bBreak = i > 0 && FChar::IsUpper(C)
				&& (FChar::IsLower(In[i - 1]) || FChar::IsDigit(In[i - 1]) || (i + 1 < In.Len() && FChar::IsLower(In[i + 1]) && FChar::IsUpper(In[i - 1])));
			if (bBreak)
			{
				Out += TEXT(' ');
			}
			Out += C;
		}
		return Out;
	}

	FString Normalize(const FString& In)
	{
		FString Out;
		for (TCHAR C : In)
		{
			if (FChar::IsAlnum(C))
			{
				Out += FChar::ToLower(C);
			}
		}
		return Out;
	}

	struct FNodeType
	{
		FString Title;
		UClass* Class = nullptr;
	};

	/** Native node types: title from the settings' default node name, else from the class name. */
	const TArray<FNodeType>& NodeTypes()
	{
		static TArray<FNodeType> Types;
		if (Types.Num() == 0)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* C = *It;
				if (!C->IsChildOf(UPCGSettings::StaticClass()) || C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden)
					|| C->GetName().StartsWith(TEXT("SKEL_")) || C->GetName().StartsWith(TEXT("REINST_")))
				{
					continue;
				}
				const UPCGSettings* CDO = GetDefault<UPCGSettings>(C);
#if WITH_EDITOR
				FString Title = CDO->GetDefaultNodeName().IsNone() ? FString() : SplitWords(CDO->GetDefaultNodeName().ToString());
#else
				FString Title; // node display names are editor-only data; fall back to the class name below
#endif
				if (Title.IsEmpty())
				{
					FString N = C->GetName();
					N.RemoveFromStart(TEXT("PCG"));
					N.RemoveFromEnd(TEXT("Settings"));
					Title = SplitWords(N);
				}
				Types.Add({Title, C});
			}
			Types.Sort([](const FNodeType& A, const FNodeType& B) { return A.Title < B.Title; });
		}
		return Types;
	}

	const FNodeType* FindNodeType(const FString& Name)
	{
		const FString Want = Normalize(Name);
		for (const FNodeType& T : NodeTypes())
		{
			if (Normalize(T.Title) == Want)
			{
				return &T;
			}
		}
		for (const FNodeType& T : NodeTypes())
		{
			FString N = T.Class->GetName();
			N.RemoveFromStart(TEXT("PCG"));
			N.RemoveFromEnd(TEXT("Settings"));
			if (Normalize(N) == Want)
			{
				return &T;
			}
		}
		return nullptr;
	}

	// The common set Epic's PCG toolset offers first (PCGToolsetLibraryCore.h).
	const TCHAR* CommonNodes[] = {TEXT("Add Attribute"), TEXT("Attribute Maths Op"), TEXT("Attribute Noise"), TEXT("Attribute Partition"),
		TEXT("Attribute Remap"), TEXT("Attribute Select"), TEXT("Bounds Modifier"), TEXT("Branch"), TEXT("Combine Points"), TEXT("Copy Attributes"),
		TEXT("Copy Points"), TEXT("Create Points"), TEXT("Create Points Grid"), TEXT("Create Points Sphere"), TEXT("Create Spline"),
		TEXT("Create Surface From Spline"), TEXT("Delete Attributes"), TEXT("Density Filter"), TEXT("Difference"), TEXT("Distance"), TEXT("Duplicate Point"),
		TEXT("Extents Modifier"), TEXT("Filter Attribute Elements"), TEXT("Get Actor Data"), TEXT("Get Landscape Data"), TEXT("Get Spline Control Points"),
		TEXT("Get Spline Data"), TEXT("Get Volume Data"), TEXT("Intersection"), TEXT("Loop"), TEXT("Make Concrete"), TEXT("Merge Points"),
		TEXT("Normal To Density"), TEXT("Offset Spline"), TEXT("Point From Mesh"), TEXT("Projection"), TEXT("Random Choice"), TEXT("Select"),
		TEXT("Self Pruning"), TEXT("Spatial Noise"), TEXT("Spawn Actor"), TEXT("Spawn Spline Mesh"), TEXT("Spline Direction"), TEXT("Spline Sampler"),
		TEXT("Split Points"), TEXT("Static Mesh Spawner"), TEXT("Subdivide Spline"), TEXT("Subgraph"), TEXT("Surface Sampler"), TEXT("Switch"),
		TEXT("To Point"), TEXT("Transform Points"), TEXT("Union"), TEXT("Volume Sampler"), TEXT("World Raycast"), TEXT("World Ray Hit Query")};

	/** Editable node parameters: properties of the settings class below UPCGSettings, plus Seed. */
	TArray<FProperty*> NodeProperties(UClass* SettingsClass)
	{
		TArray<FProperty*> Out;
		for (TFieldIterator<FProperty> It(SettingsClass); It; ++It)
		{
			const bool bOwn = It->GetOwnerClass() != UPCGSettings::StaticClass() && It->GetOwnerClass()->IsChildOf(UPCGSettings::StaticClass());
			const bool bSeed = It->GetFName() == TEXT("Seed");
			if ((bOwn || bSeed) && It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditConst))
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	// ---- nodes ----

	UPCGNode* FindNode(UPCGGraph* G, const FString& Name, bool bFail = true)
	{
		if (Name.Equals(TEXT("input"), ESearchCase::IgnoreCase))
		{
			return G->GetInputNode();
		}
		if (Name.Equals(TEXT("output"), ESearchCase::IgnoreCase))
		{
			return G->GetOutputNode();
		}
		for (UPCGNode* N : G->GetNodes())
		{
			if (N && N->GetAuthoredTitleName().ToString() == Name)
			{
				return N;
			}
		}
		if (bFail)
		{
			TArray<FString> Names = {TEXT("input"), TEXT("output")};
			for (UPCGNode* N : G->GetNodes())
			{
				if (N && N->HasAuthoredTitle())
				{
					Names.Add(N->GetAuthoredTitleName().ToString());
				}
			}
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no node '%s' in %s"), *Name, *GraphName(G)),
				TEXT("Nodes: ") + FString::Join(Names, TEXT(", ")));
		}
		return nullptr;
	}

	FString NodeNameOf(const UPCGGraph* G, const UPCGNode* N)
	{
		if (N == G->GetInputNode())
		{
			return TEXT("input");
		}
		if (N == G->GetOutputNode())
		{
			return TEXT("output");
		}
		return N->HasAuthoredTitle() ? N->GetAuthoredTitleName().ToString() : N->GetName();
	}

	void SetNodeName(UPCGNode* N, const FString& Name)
	{
		// UPCGNode::SetNodeTitle is editor-only; the title property itself exists at runtime.
		if (FNameProperty* P = FindFProperty<FNameProperty>(UPCGNode::StaticClass(), TEXT("NodeTitle")))
		{
			P->SetPropertyValue_InContainer(N, FName(*Name));
		}
	}

	TArray<FPcgToolPinInfo> Pins(const TArray<FPCGPinProperties>& Props)
	{
		TArray<FPcgToolPinInfo> Out;
		for (const FPCGPinProperties& P : Props)
		{
			FPcgToolPinInfo& I = Out.AddDefaulted_GetRef();
			I.Name = P.Label.ToString();
			I.Type = P.AllowedTypes.ToString();
			I.bMultipleConnections = P.AllowsMultipleConnections();
		}
		return Out;
	}

	FString NodeTypeTitle(const UPCGSettings* S)
	{
		for (const FNodeType& T : NodeTypes())
		{
			if (T.Class == S->GetClass())
			{
				return T.Title;
			}
		}
		return S->GetClass()->GetName();
	}

	FPcgToolNodeInfo NodeInfo(const UPCGGraph* G, const UPCGNode* N)
	{
		FPcgToolNodeInfo I;
		I.Name = NodeNameOf(G, N);
		I.InputPins = Pins(N->InputPinProperties());
		I.OutputPins = Pins(N->OutputPinProperties());
		if (const UPCGSettings* S = N->GetSettings())
		{
			I.NodeType = (N == G->GetInputNode() || N == G->GetOutputNode()) ? I.Name : NodeTypeTitle(S);
			I.bEnabled = S->bEnabled;
			TSharedRef<FJsonObject> Overrides = MakeShared<FJsonObject>();
			const UPCGSettings* CDO = GetDefault<UPCGSettings>(S->GetClass());
			for (FProperty* P : NodeProperties(S->GetClass()))
			{
				if (!P->Identical_InContainer(S, CDO) && P->GetFName() != TEXT("Seed"))
				{
					Overrides->SetField(RuntimeToolReflection::JsonName(P), RuntimeToolReflection::PropertyToJson(P, P->ContainerPtrToValuePtr<void>(S)));
				}
			}
			if (const UPCGBaseSubgraphSettings* Sub = Cast<UPCGBaseSubgraphSettings>(S); Sub && Sub->GetSubgraph())
			{
				Overrides->SetStringField(TEXT("subgraph"), GraphName(Sub->GetSubgraph()));
			}
			I.ParamOverrides = FRuntimeToolJson::Make(Overrides);
		}
		return I;
	}

	bool ApplyParams(UPCGSettings* S, const FRuntimeToolJson& Params)
	{
		if (!Params.IsSet())
		{
			return true;
		}
		const TSharedPtr<FJsonObject> O = Params.AsObject();
		if (!O.IsValid())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("params must be an object"));
			return false;
		}
		TArray<FProperty*> Props = NodeProperties(S->GetClass());
		RuntimeToolReflection::FConvertContext Ctx;
		Ctx.World = RuntimeToolCall::GetWorld();
		Ctx.Graph = RuntimeToolCall::GetGraph();
		Ctx.Outer = S;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : O->Values)
		{
			FProperty* Found = nullptr;
			for (FProperty* P : Props)
			{
				if (RuntimeToolReflection::JsonName(P) == KV.Key || P->GetName() == KV.Key)
				{
					Found = P;
					break;
				}
			}
			if (!Found)
			{
				TArray<FString> Names;
				for (FProperty* P : Props)
				{
					Names.Add(RuntimeToolReflection::JsonName(P));
				}
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("params.%s: unknown parameter"), *KV.Key),
					TEXT("Parameters: ") + FString::Join(Names, TEXT(", ")));
				return false;
			}
			FString Err;
			if (!RuntimeToolReflection::JsonToProperty(KV.Value, Found, Found->ContainerPtrToValuePtr<void>(S), Ctx, Err))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("params.%s: %s"), *KV.Key, *Err));
				return false;
			}
		}
		return true;
	}

	// ---- graph parameters ----

	TSharedRef<FJsonObject> BagValues(const FInstancedPropertyBag* Bag)
	{
		if (!Bag || !Bag->GetPropertyBagStruct() || !Bag->GetValue().GetMemory())
		{
			return MakeShared<FJsonObject>();
		}
		return RuntimeToolReflection::StructToJson(Bag->GetPropertyBagStruct(), Bag->GetValue().GetMemory());
	}

	TSharedRef<FJsonObject> BagSchema(const FInstancedPropertyBag* Bag)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("type"), TEXT("object"));
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		if (Bag && Bag->GetPropertyBagStruct())
		{
			for (TFieldIterator<FProperty> It(Bag->GetPropertyBagStruct()); It; ++It)
			{
				// Bag property names are the user's names; keep them as typed.
				Props->SetObjectField(It->GetName(), RuntimeToolReflection::PropertySchema(*It));
			}
		}
		S->SetObjectField(TEXT("properties"), Props);
		return S;
	}

	bool SetBagValues(FInstancedPropertyBag& Bag, const TSharedPtr<FJsonObject>& Values, TFunctionRef<void(const FProperty*)> OnSet)
	{
		RuntimeToolReflection::FConvertContext Ctx;
		Ctx.World = RuntimeToolCall::GetWorld();
		Ctx.Graph = RuntimeToolCall::GetGraph();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Values->Values)
		{
			const FPropertyBagPropertyDesc* D = Bag.FindPropertyDescByName(FName(*KV.Key));
			if (!D || !D->CachedProperty)
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("no graph parameter '%s'"), *KV.Key), TEXT("See pcg.get_graph_schema"));
				return false;
			}
			FString Err;
			if (!RuntimeToolReflection::JsonToProperty(KV.Value, D->CachedProperty, D->CachedProperty->ContainerPtrToValuePtr<void>(Bag.GetMutableValue().GetMemory()), Ctx, Err))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s: %s"), *KV.Key, *Err));
				return false;
			}
			OnSet(D->CachedProperty);
		}
		return true;
	}

	// ---- instances ----

	UPCGComponent* InstanceComponent(AActor* A, bool bFail = true)
	{
		UPCGComponent* C = A ? A->FindComponentByClass<UPCGComponent>() : nullptr;
		if (!C && bFail)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("instance has no PCG graph"), TEXT("Use pcg.spawn_graph_instance first"));
		}
		return C;
	}

	UPCGComponent* AddGraphComponent(AActor* A, UPCGGraph* G, const TSharedPtr<FJsonObject>& Params)
	{
		UPCGComponent* C = NewObject<UPCGComponent>(A, MakeUniqueObjectName(A, UPCGComponent::StaticClass(), TEXT("PCG")));
		C->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		C->RegisterComponent();
		A->AddInstanceComponent(C);
		C->SetGraphLocal(G);
		if (Params.IsValid() && C->GetGraphInstance())
		{
			if (FInstancedPropertyBag* Bag = C->GetGraphInstance()->GetMutableUserParametersStruct_Unsafe())
			{
				UPCGGraphInstance* GI = C->GetGraphInstance();
				SetBagValues(*Bag, Params, [GI](const FProperty* P) { GI->UpdatePropertyOverride(P, true); });
			}
		}
		return C;
	}

	/** Captures LogPCG warnings and errors while a graph runs. */
	class FPcgMessageCapture final : public FOutputDevice
	{
	public:
		FPcgMessageCapture() { GLog->AddOutputDevice(this); }
		virtual ~FPcgMessageCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == TEXT("LogPCG") && Verbosity <= ELogVerbosity::Warning && Messages.Num() < 50)
			{
				FPcgToolExecutionMessage& M = Messages.AddDefaulted_GetRef();
				M.Message = V;
				M.Severity = Verbosity <= ELogVerbosity::Error ? TEXT("Error") : TEXT("Warning");
			}
		}
		TArray<FPcgToolExecutionMessage> Messages;
	};

	FPcgToolExecutionResult Execute(UPCGComponent* C)
	{
		FPcgToolExecutionResult R;
		FPcgMessageCapture Capture;
		FString Err;
		if (!RuntimePcg::Run(C->GetWorld(), C, Err))
		{
			FPcgToolExecutionMessage& M = R.Messages.AddDefaulted_GetRef();
			M.Message = Err;
			M.Severity = TEXT("Error");
		}
		R.Messages.Append(Capture.Messages);
		for (const FPCGTaggedData& D : C->GetGeneratedGraphOutput().TaggedData)
		{
			++R.OutputData;
			if (const UPCGBasePointData* P = Cast<UPCGBasePointData>(D.Data))
			{
				R.OutputPoints += P->GetNumPoints();
			}
		}
		return R;
	}

	TSharedRef<FJsonObject> DataView(const FPCGTaggedData& D, int32 Start, int32 End)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("type"), D.Data ? D.Data->GetClass()->GetName().Replace(TEXT("PCG"), TEXT("")) : TEXT("None"));
		if (D.Tags.Num() > 0)
		{
			J->SetField(TEXT("tags"), RuntimeJson::Strings(D.Tags.Array()));
		}
		const UPCGBasePointData* P = Cast<UPCGBasePointData>(D.Data);
		if (!P)
		{
			return J;
		}
		const int32 N = P->GetNumPoints();
		J->SetNumberField(TEXT("count"), N);
		if (const UPCGMetadata* M = P->ConstMetadata())
		{
			TArray<FName> Names;
			TArray<EPCGMetadataTypes> Types;
			M->GetAttributes(Names, Types);
			TArray<FString> S;
			for (const FName& Nm : Names)
			{
				S.Add(Nm.ToString());
			}
			J->SetField(TEXT("attributes"), RuntimeJson::Strings(S));
		}
		const auto Tr = P->GetConstTransformValueRange();
		const auto De = P->GetConstDensityValueRange();
		const auto Se = P->GetConstSeedValueRange();
		const int32 Last = End < 0 ? FMath::Min(N, Start + 100) : FMath::Min(N, End);
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 i = FMath::Max(0, Start); i < Last; ++i)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("index"), i);
			Row->SetField(TEXT("position"), RuntimeJson::Vec(Tr[i].GetLocation()));
			Row->SetField(TEXT("rotation"), RuntimeJson::Rot(Tr[i].Rotator()));
			Row->SetField(TEXT("scale"), RuntimeJson::Vec(Tr[i].GetScale3D()));
			Row->SetNumberField(TEXT("density"), De[i]);
			Row->SetNumberField(TEXT("seed"), Se[i]);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		J->SetArrayField(TEXT("elements"), Rows);
		return J;
	}

	void RegisterSelectorConverter()
	{
		// PCG attribute selectors as strings ("$Density", "MyAttribute"), like the PCG editor shows them.
		RuntimeToolReflection::FStructConverter C;
		C.ToJson = [](const void* Data) -> TSharedPtr<FJsonValue>
		{
			return MakeShared<FJsonValueString>(static_cast<const FPCGAttributePropertySelector*>(Data)->ToString());
		};
		C.FromJson = [](const TSharedPtr<FJsonValue>& J, void* Data, FString& OutError)
		{
			FString S;
			if (!J->TryGetString(S) || !static_cast<FPCGAttributePropertySelector*>(Data)->Update(S))
			{
				OutError = TEXT("expected an attribute name like \"$Density\" or \"MyAttribute\"");
				return false;
			}
			return true;
		};
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("string"));
		Schema->SetStringField(TEXT("description"), TEXT("Attribute or property: \"$Density\", \"$Position\", \"$Scale\", \"MyAttribute\""));
		C.Schema = Schema;
		RuntimeToolReflection::RegisterStructConverter(FPCGAttributePropertySelector::StaticStruct(), MoveTemp(C), true);
	}
}

namespace RuntimePcg
{
	void RegisterConverters()
	{
		RegisterSelectorConverter();
	}

	void UnregisterConverters()
	{
		RuntimeToolReflection::UnregisterStructConverter(FPCGAttributePropertySelector::StaticStruct());
	}

	/** Recreates a graph instance volume (undo/redo). */
	AActor* SpawnInstanceFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
	{
		const TSharedPtr<FJsonObject> R = RuntimeJson::Parse(Source.Mid(8));
		UPCGGraph* G = R.IsValid() ? FindGraph(R->GetStringField(TEXT("graph")), false) : nullptr;
		if (!G)
		{
			OutError = TEXT("the instance's graph no longer exists");
			return nullptr;
		}
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), Transform, P);
		UBoxComponent* Box = NewObject<UBoxComponent>(A, TEXT("Volume"));
		Box->SetBoxExtent(FVector(50.0)); // 1 m box at scale 1
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		A->SetRootComponent(Box);
		Box->RegisterComponent();
		A->AddInstanceComponent(Box);
		A->SetActorTransform(Transform);
		A->Tags.Add(InstanceTag);
		const TSharedPtr<FJsonObject>* Params = nullptr;
		R->TryGetObjectField(TEXT("params"), Params);
		AddGraphComponent(A, G, Params ? *Params : nullptr);
		return A;
	}
}

// ---- graphs -------------------------------------------------------------------------------------------------------------

FPcgToolGraphStructure URuntimePcgToolset::CreateGraph(const FString& Name)
{
	if (Name.IsEmpty() || Graphs().Contains(Name))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, Name.IsEmpty() ? TEXT("name is empty") : FString::Printf(TEXT("graph '%s' exists"), *Name));
		return {};
	}
	UPCGGraph* G = NewObject<UPCGGraph>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UPCGGraph::StaticClass(), FName(*Name)), RF_Transient);
	Graphs().Add(Name, TStrongObjectPtr<UPCGGraph>(G));
	return GetGraphStructure(Name);
}

TArray<FString> URuntimePcgToolset::ListGraphs()
{
	TArray<FString> Out;
	Graphs().GetKeys(Out);
	Out.Append(AssetGraphPaths());
	Out.Sort();
	return Out;
}

FPcgToolGraphStructure URuntimePcgToolset::GetGraphStructure(const FString& Graph)
{
	FPcgToolGraphStructure S;
	UPCGGraph* G = FindGraph(Graph);
	if (!G)
	{
		return S;
	}
	S.Name = GraphName(G);
	TArray<UPCGNode*> Nodes = {G->GetInputNode(), G->GetOutputNode()};
	Nodes.Append(G->GetNodes());
	for (UPCGNode* N : Nodes)
	{
		if (!N)
		{
			continue;
		}
		S.Nodes.Add(NodeInfo(G, N));
		for (const UPCGPin* Pin : N->GetOutputPins())
		{
			for (const UPCGEdge* E : Pin->Edges)
			{
				if (E && E->InputPin && E->OutputPin && E->InputPin->Node == N)
				{
					FPcgToolEdgeInfo& Info = S.Edges.AddDefaulted_GetRef();
					Info.SrcNode = NodeNameOf(G, N);
					Info.SrcPin = E->InputPin->Properties.Label.ToString();
					Info.DestNode = NodeNameOf(G, E->OutputPin->Node);
					Info.DestPin = E->OutputPin->Properties.Label.ToString();
				}
			}
		}
	}
	S.GraphParams = FRuntimeToolJson::Make(BagValues(G->GetUserParametersStruct()));
	return S;
}

bool URuntimePcgToolset::SetGraphParams(const FString& Graph, const TArray<FPcgToolParamDefinition>& Params)
{
	UPCGGraph* G = FindGraph(Graph);
	if (!G)
	{
		return false;
	}
	TArray<FPropertyBagPropertyDesc> Descs;
	for (const FPcgToolParamDefinition& P : Params)
	{
		EPropertyBagPropertyType T = EPropertyBagPropertyType::Float;
		const UObject* TypeObject = nullptr;
		switch (P.Type)
		{
		case EPcgToolParamType::Bool: T = EPropertyBagPropertyType::Bool; break;
		case EPcgToolParamType::Int32: T = EPropertyBagPropertyType::Int32; break;
		case EPcgToolParamType::Int64: T = EPropertyBagPropertyType::Int64; break;
		case EPcgToolParamType::Float: T = EPropertyBagPropertyType::Float; break;
		case EPcgToolParamType::Double: T = EPropertyBagPropertyType::Double; break;
		case EPcgToolParamType::Name: T = EPropertyBagPropertyType::Name; break;
		case EPcgToolParamType::String: T = EPropertyBagPropertyType::String; break;
		case EPcgToolParamType::Vector: T = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FVector>::Get(); break;
		case EPcgToolParamType::Vector2D: T = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FVector2D>::Get(); break;
		case EPcgToolParamType::Rotator: T = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FRotator>::Get(); break;
		case EPcgToolParamType::Transform: T = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FTransform>::Get(); break;
		case EPcgToolParamType::Color: T = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FLinearColor>::Get(); break;
		case EPcgToolParamType::SoftObjectPath: T = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FSoftObjectPath>::Get(); break;
		}
		Descs.Add(FPropertyBagPropertyDesc(P.Name, P.bArray ? EPropertyBagContainerType::Array : EPropertyBagContainerType::None, T, TypeObject));
	}
	if (G->AddUserParameters(Descs) != EPropertyBagAlterationResult::Success)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("could not add the parameters (duplicate or invalid names?)"));
		return false;
	}
	bool bOk = true;
	G->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag)
	{
		TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
		for (const FPcgToolParamDefinition& P : Params)
		{
			if (P.DefaultValue.IsSet())
			{
				Defaults->SetField(P.Name.ToString(), P.DefaultValue.Value);
			}
		}
		bOk = SetBagValues(Bag, Defaults, [](const FProperty*) {});
	});
	return bOk;
}

bool URuntimePcgToolset::RemoveGraphParams(const FString& Graph, const TArray<FName>& ParamNames)
{
	UPCGGraph* G = FindGraph(Graph);
	if (!G)
	{
		return false;
	}
	G->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag) { Bag.RemovePropertiesByName(ParamNames); });
	return true;
}

FPcgToolGraphSchema URuntimePcgToolset::GetGraphSchema(const FString& Graph)
{
	FPcgToolGraphSchema S;
	UPCGGraph* G = FindGraph(Graph);
	if (!G)
	{
		return S;
	}
	S.Name = GraphName(G);
	S.GraphParamsSchema = FRuntimeToolJson::Make(BagSchema(G->GetUserParametersStruct()));
	S.InputPins = Pins(G->GetInputNode()->OutputPinProperties());
	S.OutputPins = Pins(G->GetOutputNode()->InputPinProperties());
	return S;
}

// ---- instances ----------------------------------------------------------------------------------------------------------

TArray<FPcgToolGraphInstanceInfo> URuntimePcgToolset::ListGraphInstances()
{
	TArray<FPcgToolGraphInstanceInfo> Out;
	for (TActorIterator<AActor> It(RuntimeToolCall::GetWorld()); It; ++It)
	{
		TArray<UPCGComponent*> Comps;
		It->GetComponents(Comps);
		for (UPCGComponent* C : Comps)
		{
			FPcgToolGraphInstanceInfo& I = Out.AddDefaulted_GetRef();
			I.Actor = *It;
			I.Graph = C->GetGraph() ? GraphName(C->GetGraph()) : FString();
		}
	}
	return Out;
}

FPcgToolGraphInstanceInfo URuntimePcgToolset::SpawnGraphInstance(const FString& Graph, const FRuntimeToolJson& Params, const FTransform& Transform,
	const FString& Name, AActor* OnActor)
{
	FPcgToolGraphInstanceInfo Info;
	UPCGGraph* G = FindGraph(Graph);
	if (!G)
	{
		return Info;
	}
	if (Params.IsSet() && !Params.AsObject().IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("params must be an object"));
		return Info;
	}
	Info.Graph = GraphName(G);
	if (OnActor)
	{
		TWeakObjectPtr<AActor> Weak = OnActor;
		TWeakObjectPtr<UPCGGraph> WeakG = G;
		TSharedPtr<FJsonObject> P = Params.AsObject();
		TSharedPtr<TWeakObjectPtr<UPCGComponent>> Added = MakeShared<TWeakObjectPtr<UPCGComponent>>();
		auto Do = [Weak, WeakG, P, Added](FRuntimeCommandContext&, FString& Err)
		{
			if (!Weak.IsValid() || !WeakG.IsValid())
			{
				Err = TEXT("actor or graph is gone");
				return false;
			}
			*Added = AddGraphComponent(Weak.Get(), WeakG.Get(), P);
			return true;
		};
		auto Undo = [Added](FRuntimeCommandContext&)
		{
			if (UPCGComponent* C = Added->Get())
			{
				C->Cleanup(true);
				C->DestroyComponent();
			}
			return true;
		};
		if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("attach pcg graph"), Do, Undo)))
		{
			Info.Actor = OnActor;
		}
		return Info;
	}
	FTransform T = Transform;
	if (!RuntimeToolCall::Current()->Has(TEXT("transform")))
	{
		T.SetScale3D(FVector(50.0, 50.0, 10.0));
	}
	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("graph"), Info.Graph);
	if (Params.IsSet())
	{
		R->SetObjectField(TEXT("params"), Params.AsObject());
	}
	const FString Source = TEXT("pcginst:") + RuntimeJson::Write(R);
	auto Spawn = [Source, T, Name](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
	{
		return RuntimePcg::SpawnInstanceFromSource(Ctx.World, Source, T, Name, Err);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("spawn pcg graph instance"), Spawn, Name, FString(), ERuntimeNodeKind::Actor, Source,
		TArray<FName>{TEXT("pcg")});
	FCreateNodeCommand* Raw = Cmd.Get();
	if (RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		if (const FRuntimeSceneNode* N = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId()))
		{
			Info.Actor = N->Actor.Get();
		}
	}
	return Info;
}

FPcgToolExecutionResult URuntimePcgToolset::ExecuteGraphInstance(AActor* Instance)
{
	UPCGComponent* C = InstanceComponent(Instance);
	return C ? Execute(C) : FPcgToolExecutionResult();
}

FRuntimeToolJson URuntimePcgToolset::GetGraphInstanceParams(AActor* Instance)
{
	UPCGComponent* C = InstanceComponent(Instance);
	return C && C->GetGraphInstance() ? FRuntimeToolJson::Make(BagValues(C->GetGraphInstance()->GetUserParametersStruct())) : FRuntimeToolJson();
}

bool URuntimePcgToolset::SetGraphInstanceParams(AActor* Instance, const FRuntimeToolJson& Params)
{
	UPCGComponent* C = InstanceComponent(Instance);
	if (!C || !C->GetGraphInstance())
	{
		return false;
	}
	if (!Params.AsObject().IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("params must be an object"));
		return false;
	}
	UPCGGraphInstance* GI = C->GetGraphInstance();
	FInstancedPropertyBag* Bag = GI->GetMutableUserParametersStruct_Unsafe();
	return Bag && SetBagValues(*Bag, Params.AsObject(), [GI](const FProperty* P) { GI->UpdatePropertyOverride(P, true); });
}

bool URuntimePcgToolset::ResetGraphInstanceParams(AActor* Instance, const TArray<FName>& ParamNames)
{
	UPCGComponent* C = InstanceComponent(Instance);
	if (!C || !C->GetGraphInstance())
	{
		return false;
	}
	UPCGGraphInstance* GI = C->GetGraphInstance();
	const FInstancedPropertyBag* Bag = GI->GetUserParametersStruct();
	for (const FName& N : ParamNames)
	{
		if (const FPropertyBagPropertyDesc* D = Bag ? Bag->FindPropertyDescByName(N) : nullptr; D && D->CachedProperty)
		{
			GI->UpdatePropertyOverride(D->CachedProperty, false);
		}
	}
	return true;
}

// ---- nodes --------------------------------------------------------------------------------------------------------------

TArray<FString> URuntimePcgToolset::ListNativeNodes(bool bCommonOnly)
{
	TArray<FString> Out;
	if (bCommonOnly)
	{
		for (const TCHAR* N : CommonNodes)
		{
			if (const FNodeType* T = FindNodeType(N))
			{
				Out.AddUnique(T->Title);
			}
		}
	}
	else
	{
		for (const FNodeType& T : NodeTypes())
		{
			Out.AddUnique(T.Title);
		}
	}
	Out.Sort();
	return Out;
}

FPcgToolNodeSchema URuntimePcgToolset::GetNativeNodeSchema(const FString& NodeName)
{
	FPcgToolNodeSchema S;
	const FNodeType* T = FindNodeType(NodeName);
	if (!T)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no node type '%s'"), *NodeName), TEXT("See pcg.list_native_nodes"));
		return S;
	}
	S.Name = T->Title;
#if WITH_METADATA
	S.Description = T->Class->GetMetaData(TEXT("ToolTip"));
#endif
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	const UPCGSettings* CDO = GetDefault<UPCGSettings>(T->Class);
	for (FProperty* P : NodeProperties(T->Class))
	{
		TSharedRef<FJsonObject> PS = RuntimeToolReflection::PropertySchema(P);
		PS->SetField(TEXT("default"), RuntimeToolReflection::PropertyToJson(P, P->ContainerPtrToValuePtr<void>(CDO)));
		Props->SetObjectField(RuntimeToolReflection::JsonName(P), PS);
	}
	Schema->SetObjectField(TEXT("properties"), Props);
	S.PropertiesSchema = FRuntimeToolJson::Make(Schema);
	S.InputPins = Pins(CDO->AllInputPinProperties());
	S.OutputPins = Pins(CDO->AllOutputPinProperties());
	return S;
}

FPcgToolNodeInfo URuntimePcgToolset::AddNode(const FString& Graph, const FString& NativeNodeType, const FString& NodeName, const FRuntimeToolJson& Params)
{
	UPCGGraph* G = FindGraph(Graph);
	if (!G)
	{
		return {};
	}
	if (!IsSessionGraph(G))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("packaged graph assets are read-only"), TEXT("Create a graph with pcg.create_graph and add it as a subgraph"));
		return {};
	}
	const FNodeType* T = FindNodeType(NativeNodeType);
	if (!T)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no node type '%s'"), *NativeNodeType), TEXT("See pcg.list_native_nodes"));
		return {};
	}
	if (NodeName.IsEmpty() || FindNode(G, NodeName, false))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("node name '%s' is empty or taken"), *NodeName));
		return {};
	}
	UPCGSettings* S = nullptr;
	UPCGNode* N = G->AddNodeOfType(T->Class, S);
	SetNodeName(N, NodeName);
	if (!ApplyParams(S, Params))
	{
		G->RemoveNode(N);
		return {};
	}
	return NodeInfo(G, N);
}

FPcgToolNodeInfo URuntimePcgToolset::AddSubgraphNode(const FString& Graph, const FString& Subgraph, const FString& NodeName)
{
	UPCGGraph* G = FindGraph(Graph);
	UPCGGraph* Sub = G ? FindGraph(Subgraph) : nullptr;
	if (!Sub)
	{
		return {};
	}
	if (Sub == G)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("a graph cannot contain itself"));
		return {};
	}
	if (NodeName.IsEmpty() || FindNode(G, NodeName, false))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("node name '%s' is empty or taken"), *NodeName));
		return {};
	}
	UPCGSettings* S = nullptr;
	UPCGNode* N = G->AddNodeOfType(UPCGSubgraphSettings::StaticClass(), S);
	Cast<UPCGSubgraphSettings>(S)->SetSubgraph(Sub);
	SetNodeName(N, NodeName);
	return NodeInfo(G, N);
}

FPcgToolNodeInfo URuntimePcgToolset::UpdateNode(const FString& Graph, const FString& Node, const FRuntimeToolJson& Params, bool bEnabled)
{
	UPCGGraph* G = FindGraph(Graph);
	UPCGNode* N = G ? FindNode(G, Node) : nullptr;
	if (!N || !N->GetSettings())
	{
		return {};
	}
	if (!ApplyParams(N->GetSettings(), Params))
	{
		return {};
	}
	N->GetSettings()->bEnabled = bEnabled;
	return NodeInfo(G, N);
}

FPcgToolNodeInfo URuntimePcgToolset::GetNodeInfo(const FString& Graph, const FString& Node)
{
	UPCGGraph* G = FindGraph(Graph);
	UPCGNode* N = G ? FindNode(G, Node) : nullptr;
	return N ? NodeInfo(G, N) : FPcgToolNodeInfo();
}

bool URuntimePcgToolset::RemoveNode(const FString& Graph, const FString& Node)
{
	UPCGGraph* G = FindGraph(Graph);
	UPCGNode* N = G ? FindNode(G, Node) : nullptr;
	if (!N)
	{
		return false;
	}
	if (N == G->GetInputNode() || N == G->GetOutputNode())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, TEXT("the input and output nodes cannot be removed"));
		return false;
	}
	G->RemoveNode(N);
	return true;
}

static bool CheckPin(const UPCGGraph* G, const UPCGNode* N, const FString& Pin, bool bOutput)
{
	const TArray<FPCGPinProperties> Props = bOutput ? N->OutputPinProperties() : N->InputPinProperties();
	TArray<FString> Labels;
	for (const FPCGPinProperties& P : Props)
	{
		if (P.Label.ToString() == Pin)
		{
			return true;
		}
		Labels.Add(P.Label.ToString());
	}
	RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s has no %s pin '%s'"), *NodeNameOf(G, N), bOutput ? TEXT("output") : TEXT("input"), *Pin),
		TEXT("Pins: ") + FString::Join(Labels, TEXT(", ")));
	return false;
}

bool URuntimePcgToolset::ConnectNodePins(const FString& Graph, const FString& FromNode, const FString& FromPin, const FString& ToNode, const FString& ToPin)
{
	UPCGGraph* G = FindGraph(Graph);
	UPCGNode* A = G ? FindNode(G, FromNode) : nullptr;
	UPCGNode* B = A ? FindNode(G, ToNode) : nullptr;
	if (!B || !CheckPin(G, A, FromPin, true) || !CheckPin(G, B, ToPin, false))
	{
		return false;
	}
	G->AddEdge(A, FName(*FromPin), B, FName(*ToPin));
	return true;
}

bool URuntimePcgToolset::DisconnectNodePins(const FString& Graph, const FString& FromNode, const FString& FromPin, const FString& ToNode, const FString& ToPin)
{
	UPCGGraph* G = FindGraph(Graph);
	UPCGNode* A = G ? FindNode(G, FromNode) : nullptr;
	UPCGNode* B = A ? FindNode(G, ToNode) : nullptr;
	if (!B)
	{
		return false;
	}
	if (!G->RemoveEdge(A, FName(*FromPin), B, FName(*ToPin)))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, TEXT("no such edge"), TEXT("See pcg.get_graph_structure"));
		return false;
	}
	return true;
}

FRuntimeToolJson URuntimePcgToolset::GetNodeDataView(AActor* Instance, const FString& Node, const FString& PinLabel, int32 StartIndex, int32 EndIndex)
{
	UPCGComponent* C = InstanceComponent(Instance);
	UPCGGraph* G = C ? C->GetGraph() : nullptr;
	if (!G)
	{
		if (C)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("the instance has no graph"));
		}
		return {};
	}
	UPCGNode* N = FindNode(G, Node);
	if (!N || !CheckPin(G, N, PinLabel, true))
	{
		return {};
	}
	// Runtime has no per-node inspection: route the pin to the graph output for one run, then restore the graph.
	UPCGNode* Out = G->GetOutputNode();
	const FName OutPin = Out->InputPinProperties().Num() > 0 ? Out->InputPinProperties()[0].Label : FName(TEXT("Out"));
	struct FSaved { UPCGNode* From; FName FromPin; };
	TArray<FSaved> Saved;
	if (UPCGPin* P = Out->GetInputPin(OutPin))
	{
		for (const UPCGEdge* E : P->Edges)
		{
			if (E && E->InputPin)
			{
				Saved.Add({E->InputPin->Node, E->InputPin->Properties.Label});
			}
		}
	}
	for (const FSaved& S : Saved)
	{
		G->RemoveEdge(S.From, S.FromPin, Out, OutPin);
	}
	const bool bDirect = N != Out;
	if (bDirect)
	{
		G->AddEdge(N, FName(*PinLabel), Out, OutPin);
	}
	const FPcgToolExecutionResult Run = Execute(C);
	TArray<TSharedPtr<FJsonValue>> Data;
	for (const FPCGTaggedData& D : C->GetGeneratedGraphOutput().TaggedData)
	{
		Data.Add(MakeShared<FJsonValueObject>(DataView(D, StartIndex, EndIndex)));
	}
	if (bDirect)
	{
		G->RemoveEdge(N, FName(*PinLabel), Out, OutPin);
	}
	for (const FSaved& S : Saved)
	{
		G->AddEdge(S.From, S.FromPin, Out, OutPin);
	}
	Execute(C); // back to the graph's real output
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetArrayField(TEXT("data"), Data);
	if (Run.Messages.Num() > 0)
	{
		TArray<FString> M;
		for (const FPcgToolExecutionMessage& X : Run.Messages)
		{
			M.Add(X.Severity + TEXT(": ") + X.Message);
		}
		J->SetField(TEXT("messages"), RuntimeJson::Strings(M));
	}
	return FRuntimeToolJson::Make(J);
}

// ---- scatter ------------------------------------------------------------------------------------------------------------

FPcgScatterResult URuntimePcgToolset::Scatter(AActor* Spline, const TArray<FPcgScatterItem>& Items, const TArray<FPcgScatterExclusion>& Exclude,
	const FVector2D& Offset, EPcgScatterMode Mode, float Spacing, EPcgScatterSide Side, float Jitter, float MinDistance, float Keep, float NoiseScale,
	bool bGround, int32 Seed, const FString& Name, AActor* Surface)
{
	FPcgScatterResult Result;
	const FRuntimeSceneNode* SplineNode = RuntimeToolCall::NodeOf(Spline);
	if (!SplineNode || !RuntimeSplines::FindSpline(Spline))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("spline must be a spline node"), TEXT("Create one with spline.create_spline"));
		return Result;
	}
	if (Items.Num() == 0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("items needs at least one {template}"));
		return Result;
	}
	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("spline"), SplineNode->Id);
	R->SetStringField(TEXT("mode"), Mode == EPcgScatterMode::Inside ? TEXT("inside") : TEXT("along"));
	R->SetNumberField(TEXT("spacing"), Spacing);
	if (Jitter >= 0.f)
	{
		R->SetNumberField(TEXT("jitter"), Jitter);
	}
	if (MinDistance >= 0.f)
	{
		R->SetNumberField(TEXT("min_distance"), MinDistance);
	}
	R->SetNumberField(TEXT("keep"), Keep);
	R->SetNumberField(TEXT("noise_scale"), NoiseScale);
	R->SetNumberField(TEXT("seed"), Seed);
	R->SetField(TEXT("offset"), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Offset.X), MakeShared<FJsonValueNumber>(Offset.Y)}));
	R->SetStringField(TEXT("side"), Side == EPcgScatterSide::Left ? TEXT("left") : Side == EPcgScatterSide::Right ? TEXT("right") : TEXT("both"));
	R->SetBoolField(TEXT("ground"), bGround || Surface != nullptr);
	if (Surface)
	{
		const FRuntimeSceneNode* SurfaceNode = RuntimeToolCall::NodeOf(Surface);
		R->SetStringField(TEXT("surface"), SurfaceNode ? SurfaceNode->Id : FString::Printf(TEXT("actor:%s"), *Surface->GetName()));
	}
	TArray<TSharedPtr<FJsonValue>> ItemJson;
	for (const FPcgScatterItem& It : Items)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		if (!It.Asset.IsEmpty())
		{
			if (!LoadObject<UStaticMesh>(nullptr, *It.Asset))
			{
				RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no static mesh asset '%s'"), *It.Asset),
					TEXT("level.find_assets {\"asset_class\":\"StaticMesh\"} lists packaged meshes"));
				return Result;
			}
			J->SetStringField(TEXT("asset"), It.Asset);
		}
		else
		{
			const FRuntimeSceneNode* T = RuntimeToolCall::NodeOf(It.Template);
			if (!T)
			{
				RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("each item needs a template node (a build.* object) or an asset path"));
				return Result;
			}
			J->SetStringField(TEXT("template"), T->Id);
		}
		J->SetNumberField(TEXT("weight"), It.Weight);
		J->SetField(TEXT("scale"), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(It.Scale.X), MakeShared<FJsonValueNumber>(It.Scale.Y)}));
		J->SetBoolField(TEXT("align"), It.bAlign);
		J->SetBoolField(TEXT("random_yaw"), It.bRandomYaw);
		J->SetNumberField(TEXT("z_offset"), It.ZOffset);
		J->SetBoolField(TEXT("collision"), It.bCollision);
		ItemJson.Add(MakeShared<FJsonValueObject>(J));
	}
	R->SetArrayField(TEXT("items"), ItemJson);
	TArray<TSharedPtr<FJsonValue>> ExJson;
	for (const FPcgScatterExclusion& X : Exclude)
	{
		const FRuntimeSceneNode* XN = RuntimeToolCall::NodeOf(X.Spline);
		if (!XN || !RuntimeSplines::FindSpline(X.Spline))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("each exclude needs a spline node"));
			return Result;
		}
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("spline"), XN->Id);
		J->SetNumberField(TEXT("clearance"), X.Clearance);
		ExJson.Add(MakeShared<FJsonValueObject>(J));
	}
	R->SetArrayField(TEXT("exclude"), ExJson);

	const FString Source = TEXT("scatter:") + RuntimeJson::Write(R);
	auto Spawn = [Source, Name](FRuntimeCommandContext& Ctx, FString& Err) -> AActor*
	{
		return RuntimePcg::SpawnScatterFromSource(Ctx.World, Source, FTransform::Identity, Name, Err);
	};
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(TEXT("pcg scatter"), Spawn, Name, FString(), ERuntimeNodeKind::Actor, Source, TArray<FName>{TEXT("scatter")});
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return Result;
	}
	const FRuntimeSceneNode* N = RuntimeToolCall::GetGraph()->Find(Raw->GetNodeId());
	Result.Scatter = N ? N->Actor.Get() : nullptr;
	if (const TSharedPtr<FJsonObject> S = RuntimePcg::ScatterStats(Result.Scatter))
	{
		Result.PcgPoints = S->GetIntegerField(TEXT("pcg_points"));
		Result.FilteredOut = S->GetIntegerField(TEXT("filtered_out"));
		Result.Instances = S->GetIntegerField(TEXT("instances"));
		for (const TSharedPtr<FJsonValue>& V : S->GetArrayField(TEXT("items")))
		{
			FPcgScatterCount& C = Result.Items.AddDefaulted_GetRef();
			C.Template = V->AsObject()->GetStringField(TEXT("template"));
			C.Instances = V->AsObject()->GetIntegerField(TEXT("instances"));
		}
	}
	return Result;
}
