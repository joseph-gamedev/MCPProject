#include "RuntimeSceneTypes.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolCall.h"
#include "GameFramework/Actor.h"

namespace RuntimeSceneTypes
{
	FRuntimeNodeInfo MakeNodeInfo(const FString& NodeId)
	{
		FRuntimeNodeInfo Info;
		Info.Id = NodeId;
		FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
		if (const FRuntimeSceneNode* Node = Graph ? Graph->Find(NodeId) : nullptr)
		{
			Info.Name = Node->Name;
			Info.Path = Graph->GetPath(NodeId);
			if (const AActor* A = Node->Actor.Get())
			{
				Info.Actor = A->GetName();
			}
		}
		return Info;
	}

	FRuntimeTransformResult MakeTransformResult(const AActor* Actor)
	{
		FRuntimeTransformResult R;
		if (Actor)
		{
			R.Target = RuntimeToolCall::MakeReference(Actor);
			R.Location = Actor->GetActorLocation();
			R.Rotation = Actor->GetActorRotation();
			R.Scale = Actor->GetActorScale3D();
		}
		return R;
	}
}
