#include "RuntimeFluidComponents.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"

URuntimeFluidFloat::URuntimeFluidFloat()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	Phase = FMath::FRandRange(0.f, 2.f * PI);
}

void URuntimeFluidFloat::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UPrimitiveComponent* P = GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	if (!P || !P->IsSimulatingPhysics())
	{
		return;
	}
	Phase += DeltaTime * 1.3f;
	const FBox B = P->Bounds.GetBox();
	const float Height = FMath::Max(1.f, static_cast<float>(B.GetSize().Z));
	const float Surface = WaterLevel + Bob * FMath::Sin(Phase);
	const float Under = FMath::Clamp(static_cast<float>(Surface - B.Min.Z) / Height, 0.f, 1.f);
	if (Under <= 0.f)
	{
		return;
	}
	const float Mass = P->GetMass();
	const float G = FMath::Abs(GetWorld()->GetGravityZ());
	P->AddForce(FVector(0.f, 0.f, Mass * G * Buoyancy * Under));
	P->AddForce(-P->GetPhysicsLinearVelocity() * Mass * Drag * Under);
	P->AddTorqueInRadians(-P->GetPhysicsAngularVelocityInRadians() * Mass * Drag * Under * FMath::Square(Height * 0.25f));
}

URuntimeFluidHeightCapture::URuntimeFluidHeightCapture()
{
	PrimaryComponentTick.bCanEverTick = true;
	ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureSource = SCS_SceneDepth;
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	bAlwaysPersistRenderingState = true;
	PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	ShowFlags.SetAtmosphere(false);
	ShowFlags.SetFog(false);
	ShowFlags.SetParticles(false);
	ShowFlags.SetTranslucency(false);
	ShowFlags.SetSkeletalMeshes(false);
}

void URuntimeFluidHeightCapture::Refresh()
{
	if (!Sim || !GetWorld())
	{
		return;
	}
	// the water, its effects and characters are not ground
	HiddenActors.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<APawn>() || It->Tags.ContainsByPredicate([](const FName& T) { return T.ToString().StartsWith(TEXT("RTFluid")); }))
		{
			HiddenActors.Add(*It);
			TArray<AActor*> Attached;
			It->GetAttachedActors(Attached, true, true);
			HiddenActors.Append(Attached);
		}
	}
	// the simulation reads altitude as (camera height - depth)
	for (TFieldIterator<FProperty> It(Sim->GetClass()); It; ++It)
	{
		if (It->GetAuthoredName() == TEXT("ExternalHeightDataNullPoint"))
		{
			It->ImportText_Direct(*FString::SanitizeFloat(GetComponentLocation().Z), It->ContainerPtrToValuePtr<void>(Sim), Sim, PPF_None);
			break;
		}
	}
	CaptureScene();
	Last = GetComponentLocation();
	LastRot = GetComponentQuat();
	Since = 0.f;
}

void URuntimeFluidHeightCapture::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Since += DeltaTime;
	if (Since >= Interval && (FVector::DistSquared(GetComponentLocation(), Last) > FMath::Square(20.f) || !GetComponentQuat().Equals(LastRot, 1e-3)))
	{
		Refresh();
	}
}

URuntimeFluidTrailFeed::URuntimeFluidTrailFeed()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void URuntimeFluidTrailFeed::FindTerrainMaterials(UWorld* World, TArray<UMaterialInstanceDynamic*>& Out)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		for (UActorComponent* C : TInlineComponentArray<UActorComponent*>(*It))
		{
			UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C);
			if (!P)
			{
				continue;
			}
			for (int32 i = 0; i < P->GetNumMaterials(); ++i)
			{
				UTexture* Tex = nullptr;
				UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(P->GetMaterial(i));
				if (M && M->GetTextureParameterValue(FName(TEXT("TrailTex")), Tex))
				{
					Out.AddUnique(M);
				}
			}
		}
	}
}

void URuntimeFluidTrailFeed::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Sim || !GetOwner())
	{
		return;
	}
	Since += DeltaTime;
	if (Since > 2.f)   // terrain made or replaced since
	{
		Since = 0.f;
		TArray<UMaterialInstanceDynamic*> Found;
		FindTerrainMaterials(GetWorld(), Found);
		Targets.Reset();
		for (UMaterialInstanceDynamic* M : Found)
		{
			Targets.Add(M);
		}
	}
	UTexture* RT = nullptr;
	for (TFieldIterator<FObjectProperty> It(Sim->GetClass()); It; ++It)
	{
		if (It->GetAuthoredName() == TEXT("RT_Painter"))
		{
			RT = Cast<UTexture>(It->GetObjectPropertyValue_InContainer(Sim));
			break;
		}
	}
	if (!RT)
	{
		return;
	}
	const FVector C = GetOwner()->GetActorLocation();
	const FLinearColor Min(C.X - Size.X * 0.5, C.Y - Size.Y * 0.5, 0.f), Extent(Size.X, Size.Y, 1.f);
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : Targets)
	{
		if (M.IsValid())
		{
			M->SetTextureParameterValue(TEXT("TrailTex"), RT);
			M->SetVectorParameterValue(TEXT("TrailMin"), Min);
			M->SetVectorParameterValue(TEXT("TrailSize"), Extent);
			M->SetScalarParameterValue(TEXT("TrailStrength"), 1.f);
		}
	}
}

void URuntimeFluidTrailFeed::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& M : Targets)
	{
		if (M.IsValid())
		{
			M->SetScalarParameterValue(TEXT("TrailStrength"), 0.f);
		}
	}
	Super::EndPlay(Reason);
}
