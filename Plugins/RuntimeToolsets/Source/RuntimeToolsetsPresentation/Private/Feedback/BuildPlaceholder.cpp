#include "BuildPlaceholder.h"
#include "RuntimeToolsetsCore.h"
#include "RuntimeToolsetsLabelActor.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr double GridSize = 3000.0; // cm
	constexpr int32 GridLines = 11;
	constexpr double ScanHeight = 1200.0;

	TSharedPtr<FJsonValue> Vec(double X, double Y, double Z)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(X), MakeShared<FJsonValueNumber>(Y), MakeShared<FJsonValueNumber>(Z)});
	}

	TSharedPtr<FJsonValue> Bar(double X, double Y, double SX, double SY, double SZ)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("shape"), TEXT("box"));
		P->SetField(TEXT("size"), Vec(SX, SY, SZ));
		P->SetField(TEXT("location"), Vec(X, Y, 0));
		return MakeShared<FJsonValueObject>(P);
	}

	/** A glowing assembly recipe as a "prim:" source. */
	FString GlowingAssembly(const TArray<TSharedPtr<FJsonValue>>& Parts, double Strength)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("shape"), TEXT("assembly"));
		R->SetArrayField(TEXT("parts"), Parts);
		R->SetField(TEXT("color"), Vec(0.05, 0.25, 0.6));
		R->SetField(TEXT("emissive"), Vec(0.25, 0.65, 1.0));
		R->SetNumberField(TEXT("emissive_strength"), Strength);
		R->SetBoolField(TEXT("repair_coplanar"), false);
		FString S;
		FJsonSerializer::Serialize(R, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S));
		return TEXT("prim:") + S;
	}

	AActor* SpawnGlow(UWorld* World, const FString& Source, const FVector& At, const TCHAR* Name)
	{
		FString Error;
		AActor* A = IRuntimeToolsetsCore::Get().SpawnFromSource(World, Source, FTransform(At), Name, Error);
		if (A)
		{
			A->SetActorEnableCollision(false);
			A->Tags.Add(FName(TEXT("RuntimeToolsetsPlaceholder")));
		}
		return A;
	}
}

bool FBuildPlaceholder::Spawn(UWorld* World)
{
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return false;
	}
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RuntimeToolsetsPlaceholder), false, PC->GetPawn());
	FHitResult Hit;
	// where the player looks, if that is close; otherwise on the ground 30 m ahead
	if (World->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * 6000.0, ECC_Visibility, Params))
	{
		At = Hit.ImpactPoint;
	}
	else
	{
		const FVector Ahead = Eye + FRotator(0.0, View.Yaw, 0.0).Vector() * 3000.0;
		At = World->LineTraceSingleByChannel(Hit, Ahead + FVector(0, 0, 50000), Ahead - FVector(0, 0, 50000), ECC_Visibility, Params)
			? Hit.ImpactPoint : FVector(Ahead.X, Ahead.Y, 0.0);
	}
	TArray<TSharedPtr<FJsonValue>> Lines;
	for (int32 i = 0; i < GridLines; ++i)
	{
		const double O = -GridSize / 2 + GridSize * i / (GridLines - 1);
		Lines.Add(Bar(O, 0, 6, GridSize, 3));
		Lines.Add(Bar(0, O, GridSize, 6, 3));
	}
	TArray<TSharedPtr<FJsonValue>> Frame = {Bar(-GridSize / 2, 0, 12, GridSize, 12), Bar(GridSize / 2, 0, 12, GridSize, 12),
		Bar(0, -GridSize / 2, GridSize, 12, 12), Bar(0, GridSize / 2, GridSize, 12, 12)};
	Grid = SpawnGlow(World, GlowingAssembly(Lines, 3.0), At + FVector(0, 0, 3), TEXT("DesignGrid"));
	Scan = SpawnGlow(World, GlowingAssembly(Frame, 6.0), At + FVector(0, 0, 10), TEXT("DesignScan"));
	return Grid.IsValid();
}

void FBuildPlaceholder::Update(bool bWanted, double DesigningSeconds, double Now)
{
	if (!bWanted)
	{
		Clear();
		bTried = false;
		return;
	}
	UWorld* World = IRuntimeToolsetsCore::IsAvailable() ? IRuntimeToolsetsCore::Get().FindWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (!Grid.IsValid())
	{
		if (bTried)
		{
			return; // one try per build (the world may have no player yet)
		}
		bTried = true;
		if (!Spawn(World))
		{
			return;
		}
		Started = Now;
		NextLabel = 0.0;
		UE_LOG(LogTemp, Display, TEXT("BuildTiming: placeholder shown after %.1f s"), DesigningSeconds);
	}
	if (Scan.IsValid())
	{
		// the scan frame sweeps up and down every 4 seconds
		const double T = FMath::Fmod(Now - Started, 4.0) / 4.0;
		const double H = (T < 0.5 ? T * 2.0 : 2.0 - T * 2.0) * ScanHeight;
		Scan->SetActorLocation(At + FVector(0, 0, 10.0 + H));
	}
}

void FBuildPlaceholder::Clear()
{
	for (AActor* A : {Grid.Get(), Scan.Get(), static_cast<AActor*>(Label.Get())})
	{
		if (A)
		{
			A->Destroy();
		}
	}
	Grid.Reset();
	Scan.Reset();
	Label.Reset();
}
