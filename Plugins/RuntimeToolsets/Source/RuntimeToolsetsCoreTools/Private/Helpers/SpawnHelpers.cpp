#include "SpawnHelpers.h"
#include "RuntimeToolsetsGroupActor.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace RuntimeSpawn
{
	static const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const FName ColorParam(TEXT("Color"));

	const TArray<FString>& ShapeNames()
	{
		static const TArray<FString> Names = {TEXT("cube"), TEXT("sphere"), TEXT("cylinder"), TEXT("cone"), TEXT("plane")};
		return Names;
	}

	static UStaticMesh* LoadShapeMesh(const FString& Shape)
	{
		FString Asset;
		if (Shape == TEXT("cube")) Asset = TEXT("Cube");
		else if (Shape == TEXT("sphere")) Asset = TEXT("Sphere");
		else if (Shape == TEXT("cylinder")) Asset = TEXT("Cylinder");
		else if (Shape == TEXT("cone")) Asset = TEXT("Cone");
		else if (Shape == TEXT("plane")) Asset = TEXT("Plane");
		else return nullptr;
		const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), *Asset, *Asset);
		return LoadObject<UStaticMesh>(nullptr, *Path);
	}

	UClass* LoadSpawnClass(const FString& ClassPath, FString& OutError, bool* OutNotAllowed)
	{
		if (OutNotAllowed)
		{
			*OutNotAllowed = false;
		}
		const URuntimeToolsetsSettings& Settings = URuntimeToolsetsSettings::Get();
		FString Path = ClassPath;
		// Blueprint asset path without _C: /Game/BP_Door.BP_Door -> /Game/BP_Door.BP_Door_C
		if (Path.StartsWith(TEXT("/Game/")) && !Path.EndsWith(TEXT("_C")))
		{
			if (!Path.Contains(TEXT(".")))
			{
				Path += TEXT(".") + FPaths::GetBaseFilename(Path);
			}
			Path += TEXT("_C");
		}
		bool bAllowed = Settings.SpawnClassAllowRoots.Num() == 0;
		for (const FString& Root : Settings.SpawnClassAllowRoots)
		{
			bAllowed |= Path.StartsWith(Root);
		}
		if (!bAllowed)
		{
			OutError = FString::Printf(TEXT("class '%s' is outside SpawnClassAllowRoots"), *Path);
			if (OutNotAllowed)
			{
				*OutNotAllowed = true;
			}
			return nullptr;
		}
		UClass* Class = FSoftClassPath(Path).TryLoadClass<AActor>();
		if (!Class)
		{
			OutError = FString::Printf(TEXT("class '%s' not found (is it cooked?)"), *Path);
			return nullptr;
		}
		if (!Class->IsChildOf(AActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			OutError = FString::Printf(TEXT("'%s' is not a spawnable actor class"), *Path);
			if (OutNotAllowed)
			{
				*OutNotAllowed = true;
			}
			return nullptr;
		}
		return Class;
	}

	static FActorSpawnParameters MakeParams(UWorld* World, UClass* Class, const FString& BaseName)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		if (!BaseName.IsEmpty())
		{
			FString Clean = BaseName;
			for (TCHAR& C : Clean)
			{
				if (!FChar::IsAlnum(C) && C != TEXT('_'))
				{
					C = TEXT('_');
				}
			}
			Params.Name = MakeUniqueObjectName(World->PersistentLevel, Class, FName(*Clean));
		}
		return Params;
	}

	AActor* SpawnFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& BaseName, FString& OutError)
	{
		return IRuntimeToolsetsCore::Get().SpawnFromSource(World, Source, Transform, BaseName, OutError);
	}

	AActor* SpawnBuiltin(UWorld* World, const FString& Source, const FTransform& Transform, const FString& BaseName, FString& OutError)
	{
		if (!World)
		{
			OutError = TEXT("no world");
			return nullptr;
		}

		if (Source == TEXT("group"))
		{
			FActorSpawnParameters Params = MakeParams(World, ARuntimeToolsetsGroupActor::StaticClass(), BaseName);
			Params.bDeferConstruction = false;
			return World->SpawnActor<ARuntimeToolsetsGroupActor>(ARuntimeToolsetsGroupActor::StaticClass(), Transform, Params);
		}

		if (Source.StartsWith(TEXT("shape:")))
		{
			const FString Shape = Source.Mid(6);
			UStaticMesh* Mesh = LoadShapeMesh(Shape);
			if (!Mesh)
			{
				OutError = FString::Printf(TEXT("unknown shape '%s' or /Engine/BasicShapes not cooked"), *Shape);
				return nullptr;
			}
			FActorSpawnParameters Params = MakeParams(World, AStaticMeshActor::StaticClass(), BaseName.IsEmpty() ? Shape : BaseName);
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, Params);
			if (!Actor)
			{
				OutError = TEXT("spawn failed");
				return nullptr;
			}
			UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetStaticMesh(Mesh);
			if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath))
			{
				Comp->SetMaterial(0, Mat);
			}
			Actor->FinishSpawning(Transform);
			return Actor;
		}

		if (Source.StartsWith(TEXT("class:")))
		{
			UClass* Class = LoadSpawnClass(Source.Mid(6), OutError);
			if (!Class)
			{
				return nullptr;
			}
			FActorSpawnParameters Params = MakeParams(World, Class, BaseName);
			AActor* Actor = World->SpawnActor<AActor>(Class, Transform, Params);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("could not spawn %s"), *Class->GetName());
				return nullptr;
			}
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				Root->SetMobility(EComponentMobility::Movable);
			}
			Actor->FinishSpawning(Transform);
			return Actor;
		}

		OutError = FString::Printf(TEXT("unknown source '%s' (shape:<name>, class:<path>, group)"), *Source);
		return nullptr;
	}

	bool SetShapeColor(AActor* Actor, const FLinearColor& Color)
	{
		AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor);
		UStaticMeshComponent* Comp = SMA ? SMA->GetStaticMeshComponent() : nullptr;
		if (!Comp)
		{
			return false;
		}
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0));
		if (!MID)
		{
			MID = Comp->CreateDynamicMaterialInstance(0);
		}
		if (!MID)
		{
			return false;
		}
		MID->SetVectorParameterValue(ColorParam, Color);
		return true;
	}

	bool GetShapeColor(AActor* Actor, FLinearColor& OutColor)
	{
		AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor);
		UStaticMeshComponent* Comp = SMA ? SMA->GetStaticMeshComponent() : nullptr;
		UMaterialInstanceDynamic* MID = Comp ? Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)) : nullptr;
		return MID && MID->GetVectorParameterValue(FHashedMaterialParameterInfo(ColorParam), OutColor);
	}
}
