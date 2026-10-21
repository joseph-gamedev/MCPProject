#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "Blueprint/UserWidget.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"

namespace
{
	AActor* PresentationNodeActor(FToolTestHarness& H, const FString& Id)
	{
		const FRuntimeSceneNode* N = H.GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}

	const TCHAR* DefaultSystem = TEXT("/Niagara/DefaultAssets/DefaultSystem.DefaultSystem");
}

// ---------------------------------------------------------------------------------------------------------------------
// UI (needs a real viewport and local player: runs in the packaged game / PIE, skipped headless)
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FUiToolsetSpec, "RuntimeToolsets.Toolsets.Ui", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	bool bLive = false;
END_DEFINE_SPEC(FUiToolsetSpec)

void FUiToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		bLive = H->SetupLiveWorld();
		if (!bLive)
		{
			H->Setup();
		}
	});
	AfterEach([this]() { H.Reset(); });

	It("needs a local player in an isolated world", [this]()
	{
		if (bLive)
		{
			return;
		}
		AddInfo(TEXT("No live game world (headless editor): checking errors only"));
		H->CallFails(TEXT("ui.create_hud_text"), TEXT("{\"text\":\"hi\"}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("ui.set_text"), TEXT("{\"target\":\"widget:Nope\",\"text\":\"x\"}"), ERuntimeToolError::NotFound);
	});

	It("creates HUD text, edits it, hides it and undo removes it", [this]()
	{
		if (!bLive)
		{
			AddInfo(TEXT("Skipped: needs a live game world"));
			return;
		}
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("ui.create_hud_text"), TEXT("{\"text\":\"Hello agent\",\"position\":[0.5,0.2],\"size\":32,\"name\":\"RtTestHud\"}"));
		const FString Target = R->GetStringField(TEXT("target"));
		TestEqual(TEXT("target"), Target, FString(TEXT("widget:RtTestHud")));
		TestTrue(TEXT("listed"), H->CallOk(TEXT("ui.list_widgets"))->GetArrayField(TEXT("widgets")).Num() >= 1);
		const TSharedRef<FJsonObject> Found = H->CallOk(TEXT("ui.find_widget"), TEXT("{\"text\":\"Hello*\"}"));
		TestEqual(TEXT("found by text"), static_cast<int32>(Found->GetNumberField(TEXT("total"))), 1);
		const TSharedRef<FJsonObject> Tree = H->CallOk(TEXT("ui.list_widget_tree"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Target));
		TestTrue(TEXT("canvas root"), Tree->GetObjectField(TEXT("root"))->GetStringField(TEXT("class")) == TEXT("CanvasPanel"));

		H->CallOk(TEXT("ui.set_text"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"Changed\"}"), *Target));
		TestEqual(TEXT("text changed"), static_cast<int32>(H->CallOk(TEXT("ui.find_widget"), TEXT("{\"text\":\"Changed\"}"))->GetNumberField(TEXT("total"))), 1);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("text restored"), static_cast<int32>(H->CallOk(TEXT("ui.find_widget"), TEXT("{\"text\":\"Hello agent\"}"))->GetNumberField(TEXT("total"))), 1);

		H->CallOk(TEXT("ui.set_widget_visibility"), FString::Printf(TEXT("{\"target\":\"%s\",\"visibility\":\"hidden\"}"), *Target));
		H->CallOk(TEXT("history.undo"));
		H->CallOk(TEXT("ui.remove_widget"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Target));
		H->CallFails(TEXT("ui.set_text"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"x\"}"), *Target), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("history.undo"));
		H->CallOk(TEXT("ui.list_widget_tree"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Target));
		H->CallOk(TEXT("history.undo")); // undo create
		H->CallFails(TEXT("ui.list_widget_tree"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Target), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.click_button"), TEXT("{\"target\":\"widget:NoButton\"}"), ERuntimeToolError::NotFound);
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// VFX
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FVfxToolsetSpec, "RuntimeToolsets.Toolsets.Vfx", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FVfxToolsetSpec)

void FVfxToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("spawns a system as a node, deactivates and undoes", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("vfx.spawn_system"), FString::Printf(TEXT("{\"system\":\"%s\",\"location\":[0,0,100],\"name\":\"fx\"}"), DefaultSystem));
		const FString Id = R->GetStringField(TEXT("id"));
		AActor* A = PresentationNodeActor(*H, Id);
		UNiagaraComponent* C = A ? A->FindComponentByClass<UNiagaraComponent>() : nullptr;
		if (!TestNotNull(TEXT("niagara component"), C))
		{
			return;
		}
		TestNotNull(TEXT("asset set"), C->GetAsset());
		H->CallOk(TEXT("vfx.list_user_parameters"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id));
		H->CallOk(TEXT("vfx.deactivate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id));
		H->CallOk(TEXT("vfx.activate"), FString::Printf(TEXT("{\"target\":\"%s\",\"reset\":true}"), *Id));
		H->CallFails(TEXT("vfx.set_user_variable"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Nope\",\"value\":1}"), *Id), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("history.undo"), TEXT("{\"count\":3}"));
		TestTrue(TEXT("removed"), H->GetGraph()->Find(Id)->bSoftDeleted);
	});

	It("validates inputs", [this]()
	{
		H->CallFails(TEXT("vfx.spawn_system"), TEXT("{\"system\":\"/Game/NoSuch.NoSuch\"}"), ERuntimeToolError::NotFound);
		const FString Cube = H->SpawnCube();
		H->CallFails(TEXT("vfx.activate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("vfx.list_active"));
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------------------------------------------------

BEGIN_DEFINE_SPEC(FLightingToolsetSpec, "RuntimeToolsets.Toolsets.Lighting", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	ADirectionalLight* Sun = nullptr;
	AExponentialHeightFog* Fog = nullptr;
END_DEFINE_SPEC(FLightingToolsetSpec)

void FLightingToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
		Sun = nullptr;
		Fog = nullptr;
	});
	AfterEach([this]() { H.Reset(); });

	It("reports missing lights", [this]()
	{
		H->CallFails(TEXT("lighting.set_sun"), TEXT("{\"intensity\":5}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("lighting.set_fog"), TEXT("{\"density\":0.1}"), ERuntimeToolError::NotFound);
		TestEqual(TEXT("empty environment"), RuntimeJson::Keys(H->CallOk(TEXT("lighting.get_environment"))).Num(), 0);
	});

	Describe("with a sun and fog", [this]()
	{
		BeforeEach([this]()
		{
			Sun = Cast<ADirectionalLight>(PresentationNodeActor(*H, H->CallOk(TEXT("actors.spawn_actor"), TEXT("{\"class\":\"/Script/Engine.DirectionalLight\"}"))->GetStringField(TEXT("id"))));
			Fog = Cast<AExponentialHeightFog>(PresentationNodeActor(*H, H->CallOk(TEXT("actors.spawn_actor"), TEXT("{\"class\":\"/Script/Engine.ExponentialHeightFog\"}"))->GetStringField(TEXT("id"))));
		});

		It("sets the sun and undo restores", [this]()
		{
			const float Before = Sun->GetLightComponent()->Intensity;
			H->CallOk(TEXT("lighting.set_sun"), TEXT("{\"intensity\":3.5,\"temperature\":4000,\"rotation\":[-30,45,0]}"));
			TestEqual(TEXT("intensity"), Sun->GetLightComponent()->Intensity, 3.5f);
			TestTrue(TEXT("rotation"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, -30.0, 0.01));
			H->CallOk(TEXT("history.undo"));
			TestEqual(TEXT("restored"), Sun->GetLightComponent()->Intensity, Before);
		});

		It("maps time of day to sun elevation", [this]()
		{
			H->CallOk(TEXT("lighting.set_time_of_day"), TEXT("{\"hour\":12}"));
			TestTrue(TEXT("noon overhead"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, -90.0, 0.5));
			H->CallOk(TEXT("lighting.set_time_of_day"), TEXT("{\"hour\":9}"));
			// Sinusoidal day curve: 9:00 is a quarter of the way from sunrise to noon -> 90 * sin(45 deg).
			TestTrue(TEXT("morning elevation"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, -90.0 * FMath::Sin(PI / 4.0), 0.5));
		});

		It("sets fog and post process; a mood is one undo step", [this]()
		{
			H->CallOk(TEXT("lighting.set_fog"), TEXT("{\"density\":0.2,\"volumetric\":true}"));
			TestEqual(TEXT("density"), Fog->GetComponent()->FogDensity, 0.2f);
			H->CallOk(TEXT("history.undo"));

			const TSharedRef<FJsonObject> P = H->CallOk(TEXT("lighting.set_post_process"), TEXT("{\"saturation\":0.5,\"vignette\":0.6}"));
			TestTrue(TEXT("volume created"), P->GetBoolField(TEXT("created_volume")));
			APostProcessVolume* Volume = nullptr;
			for (TActorIterator<APostProcessVolume> It(H->GetWorld()); It; ++It)
			{
				Volume = *It;
			}
			if (TestNotNull(TEXT("volume"), Volume))
			{
				TestEqual(TEXT("saturation"), static_cast<float>(Volume->Settings.ColorSaturation.X), 0.5f);
			}
			H->CallOk(TEXT("history.undo"));

			const float FogBefore = Fog->GetComponent()->FogDensity;
			const double PitchBefore = Sun->GetActorRotation().Pitch;
			H->CallOk(TEXT("lighting.apply_mood"), TEXT("{\"mood\":\"foggy\"}"));
			TestEqual(TEXT("foggy density"), Fog->GetComponent()->FogDensity, 0.25f);
			H->CallOk(TEXT("history.undo"));
			TestEqual(TEXT("fog back"), Fog->GetComponent()->FogDensity, FogBefore);
			TestTrue(TEXT("sun back"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, PitchBefore, 0.01));
			TestTrue(TEXT("environment reported"), H->CallOk(TEXT("lighting.get_environment"))->HasField(TEXT("sun")));
		});
	});
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FLabelToolsetSpec, "RuntimeToolsets.Toolsets.Ui.Labels", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
END_DEFINE_SPEC(FLabelToolsetSpec)

void FLabelToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("places a label above its target, follows it, and can be edited and undone", [this]()
	{
		const FString Tower = H->SpawnCube(TEXT("\"location\":[1000,0,0],\"scale\":[1,1,3]"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("ui.create_label"), FString::Printf(TEXT("{\"text\":\"Tower\",\"target\":\"%s\",\"offset\":[0,0,50]}"), *Tower));
		const FString Label = R->GetStringField(TEXT("id"));
		AActor* LabelActor = PresentationNodeActor(*H, Label);
		TestTrue(TEXT("above the top"), LabelActor->GetActorLocation().Equals(FVector(1000, 0, 200), 0.5));
		TestEqual(TEXT("child of target"), H->GetGraph()->Find(Label)->ParentId, Tower);
		H->CallOk(TEXT("scene.move_node"), FString::Printf(TEXT("{\"target\":\"%s\",\"offset\":[0,500,0]}"), *Tower));
		TestTrue(TEXT("follows"), LabelActor->GetActorLocation().Equals(FVector(1000, 500, 200), 0.5));
		H->CallOk(TEXT("ui.set_label"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"Big Tower\"}"), *Label));
		TestTrue(TEXT("text in source"), H->GetGraph()->Find(Label)->Source.Contains(TEXT("Big Tower")));
		H->CallOk(TEXT("history.undo"));
		TestFalse(TEXT("text undone"), H->GetGraph()->Find(Label)->Source.Contains(TEXT("Big Tower")));
		H->CallFails(TEXT("ui.create_label"), TEXT("{\"text\":\"x\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("ui.set_label"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"x\"}"), *Tower), ERuntimeToolError::InvalidArguments);
	});
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

BEGIN_DEFINE_SPEC(FStyleToolsetSpec, "RuntimeToolsets.Toolsets.Style", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;
	float Mpc(const TCHAR* Name)
	{
		UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/RuntimeToolsets/Materials/MPC_RT_Style.MPC_RT_Style"));
		return C ? UKismetMaterialLibrary::GetScalarParameterValue(H->GetWorld(), C, FName(Name)) : -1.f;
	}
	APostProcessVolume* StyleVolume()
	{
		for (TActorIterator<APostProcessVolume> It(H->GetWorld()); It; ++It)
		{
			if (It->Tags.Contains(FName(TEXT("RuntimeToolsetsStyle"))))
			{
				return *It;
			}
		}
		return nullptr;
	}
END_DEFINE_SPEC(FStyleToolsetSpec)

void FStyleToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("lists the built-in presets", [this]()
	{
		TArray<FString> Names;
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("style.list_presets"));
		for (const TSharedPtr<FJsonValue>& V : R->GetArrayField(TEXT("presets")))
		{
			Names.Add(V->AsObject()->GetStringField(TEXT("name")));
		}
		for (const TCHAR* N : {TEXT("realistic"), TEXT("stylized"), TEXT("lowpoly"), TEXT("toon"), TEXT("retro"), TEXT("noir")})
		{
			TestTrue(FString::Printf(TEXT("has %s"), N), Names.Contains(N));
		}
	});

	It("applies material switches and the post pass, tweaks, and undo restores", [this]()
	{
		H->CallOk(TEXT("style.apply"), TEXT(R"J({"preset":"lowpoly","lighting_enabled":false})J"));
		TestEqual(TEXT("faceted"), Mpc(TEXT("Faceted")), 1.f);
		TestEqual(TEXT("flat colours"), Mpc(TEXT("Pattern")), 0.f);
		H->CallOk(TEXT("style.apply"), TEXT(R"J({"preset":"toon","lighting_enabled":false})J"));
		APostProcessVolume* V = StyleVolume();
		TestNotNull(TEXT("style volume"), V);
		if (V)
		{
			TestTrue(TEXT("post pass on"), V->Settings.WeightedBlendables.Array.Num() == 1 && V->Settings.WeightedBlendables.Array[0].Weight > 0.5f);
		}
		H->CallOk(TEXT("style.apply"), TEXT(R"J({"material":{"Saturation":0.5},"lighting_enabled":false})J")); // tweak on top of toon
		TestEqual(TEXT("tweak"), Mpc(TEXT("Saturation")), 0.5f);
		TestEqual(TEXT("still toon"), H->CallOk(TEXT("style.get"))->GetObjectField(TEXT("style"))->GetObjectField(TEXT("post"))->GetNumberField(TEXT("Toon")), 1.0);
		H->CallOk(TEXT("history.undo"));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("back to lowpoly"), Mpc(TEXT("Faceted")), 1.f);
		H->CallOk(TEXT("style.apply"), TEXT(R"J({"preset":"realistic"})J"));
		if (V)
		{
			TestEqual(TEXT("realistic skips the post pass"), V->Settings.WeightedBlendables.Array[0].Weight, 0.f);
		}
	});

	It("defines presets and rejects unknown ones", [this]()
	{
		H->CallFails(TEXT("style.apply"), TEXT(R"J({"preset":"nope"})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("style.define_preset"), TEXT(R"J({"name":"comic","based_on":"toon","post":{"OutlineWidth":3}})J"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("style.apply"), TEXT(R"J({"preset":"comic","lighting_enabled":false})J"));
		TestEqual(TEXT("override kept"), R->GetObjectField(TEXT("style"))->GetObjectField(TEXT("post"))->GetNumberField(TEXT("OutlineWidth")), 3.0);
		TestEqual(TEXT("base kept"), R->GetObjectField(TEXT("style"))->GetObjectField(TEXT("post"))->GetNumberField(TEXT("Toon")), 1.0);
	});
}

#endif
