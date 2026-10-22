#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	AActor* PresentationNodeActor(FToolTestHarness& H, const FString& Id)
	{
		const FRuntimeSceneNode* N = H.GetGraph()->Find(Id);
		return N ? N->Actor.Get() : nullptr;
	}

	FString PresentationSpawn(FToolTestHarness& H, const TCHAR* Class)
	{
		return H.CallOk(TEXT("actors.spawn_actor"), FString::Printf(TEXT("{\"class\":\"%s\"}"), Class))->GetStringField(TEXT("id"));
	}

	const TCHAR* DefaultSystem = TEXT("/Niagara/DefaultAssets/DefaultSystem.DefaultSystem");
}

// ---------------------------------------------------------------------------------------------------------------------
// UI (screen widgets need a real viewport and local player: runs in the packaged game / PIE; headless checks errors and
// edits a free-standing widget tree)
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

	It("reports errors for every tool without a player or widget", [this]()
	{
		if (bLive)
		{
			return;
		}
		AddInfo(TEXT("No live game world (headless editor): checking errors only"));
		H->CallFails(TEXT("ui.create_hud_text"), TEXT("{\"text\":\"hi\"}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("ui.create_hud_text"), TEXT("{\"text\":\"hi\",\"position\":[2,0]}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("ui.create_widget"), TEXT("{\"class\":\"/Game/UI/WBP_Nope\"}"), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("ui.set_text"), TEXT("{\"target\":\"widget:Nope\",\"text\":\"x\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.list_widget_tree"), TEXT("{\"target\":\"widget:Nope\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.get_widget_description"), TEXT("{\"target\":\"widget:Nope\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.add_widget"), TEXT("{\"parent\":\"widget:Nope\",\"widget_class\":\"TextBlock\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.move_widget"), TEXT("{\"widget\":\"widget:Nope\",\"new_parent\":\"widget:Nope2\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.wrap_widget"), TEXT("{\"widget\":\"widget:Nope\",\"wrapper_class\":\"Border\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.remove_widget"), TEXT("{\"target\":\"widget:Nope\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.set_widget_visibility"), TEXT("{\"target\":\"widget:Nope\",\"visibility\":\"Hidden\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.set_widget_visibility"), TEXT("{\"target\":\"widget:Nope\",\"visibility\":\"sideways\"}"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("ui.set_brush"), TEXT("{\"target\":\"widget:Nope\",\"tint\":[1,0,0]}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.click_button"), TEXT("{\"target\":\"widget:NoButton\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("ui.show_activity"), TEXT("{\"visible\":true}"), ERuntimeToolError::InvalidArguments);
		TestEqual(TEXT("nothing on screen"), static_cast<int32>(H->CallOk(TEXT("ui.list_widgets"))->GetNumberField(TEXT("total"))), 0);
		TestEqual(TEXT("nothing found"), static_cast<int32>(H->CallOk(TEXT("ui.find_widget"), TEXT("{\"class\":\"Button\"}"))->GetNumberField(TEXT("total"))), 0);
	});

	It("edits a widget tree: add, text, move, wrap, visibility, remove, with undo", [this]()
	{
		TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>(GetTransientPackage()));
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RtBox"));
		UVerticalBox* Other = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RtOther"));
		Tree->RootWidget = Box;
		Box->AddChild(Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RtFirst")));
		Box->AddChild(Other);
		const FString BoxPath = Box->GetPathName();
		const FString OtherPath = Other->GetPathName();

		const TSharedRef<FJsonObject> Added = H->CallOk(TEXT("ui.add_widget"),
			FString::Printf(TEXT("{\"parent\":\"%s\",\"widget_class\":\"TextBlock\",\"name\":\"RtTitle\",\"text\":\"Title\",\"child_index\":0}"), *BoxPath));
		const FString Title = Added->GetStringField(TEXT("target"));
		TestEqual(TEXT("inserted first"), Box->GetChildIndex(Cast<UWidget>(StaticFindObject(UWidget::StaticClass(), nullptr, *Title))), 0);
		TestEqual(TEXT("three children"), Box->GetChildrenCount(), 3);
		H->CallFails(TEXT("ui.add_widget"), FString::Printf(TEXT("{\"parent\":\"%s\",\"widget_class\":\"TextBlock\",\"name\":\"RtTitle\"}"), *BoxPath), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("ui.add_widget"), FString::Printf(TEXT("{\"parent\":\"%s\",\"widget_class\":\"VerticalBox\"}"), *Title), ERuntimeToolError::InvalidArguments);

		const TSharedRef<FJsonObject> Desc = H->CallOk(TEXT("ui.get_widget_description"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *BoxPath));
		const FString Text = Desc->GetStringField(TEXT("description"));
		TestTrue(TEXT("index-aligned lines"), Text.Contains(TEXT("[1] TextBlock RtTitle text=\"Title\"")));
		TestEqual(TEXT("widgets aligned"), Desc->GetArrayField(TEXT("widgets"))[1]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("RtTitle")));
		const TSharedRef<FJsonObject> Tree2 = H->CallOk(TEXT("ui.list_widget_tree"), FString::Printf(TEXT("{\"target\":\"%s\",\"depth\":0}"), *BoxPath));
		TestTrue(TEXT("depth cut"), Tree2->GetBoolField(TEXT("truncated")));

		H->CallOk(TEXT("ui.set_text"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"Changed\"}"), *Title));
		H->CallFails(TEXT("ui.set_text"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"x\"}"), *BoxPath), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("ui.move_widget"), FString::Printf(TEXT("{\"widget\":\"%s\",\"new_parent\":\"%s\"}"), *Title, *OtherPath));
		TestEqual(TEXT("moved"), Other->GetChildrenCount(), 1);
		H->CallFails(TEXT("ui.move_widget"), FString::Printf(TEXT("{\"widget\":\"%s\",\"new_parent\":\"%s\"}"), *OtherPath, *OtherPath), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("moved back to index 0"), Cast<UTextBlock>(Box->GetChildAt(0))->GetText().ToString(), FString(TEXT("Changed")));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("text undone"), Cast<UTextBlock>(Box->GetChildAt(0))->GetText().ToString(), FString(TEXT("Title")));

		const TSharedRef<FJsonObject> Wrapped = H->CallOk(TEXT("ui.wrap_widget"), FString::Printf(TEXT("{\"widget\":\"%s\",\"wrapper_class\":\"Border\",\"name\":\"RtFrame\"}"), *Title));
		TestEqual(TEXT("wrapper in place"), Box->GetChildAt(0)->GetName(), FString(TEXT("RtFrame")));
		TestEqual(TEXT("wrapper parent"), Wrapped->GetStringField(TEXT("class")), FString(TEXT("Border")));
		H->CallFails(TEXT("ui.wrap_widget"), FString::Printf(TEXT("{\"widget\":\"%s\",\"wrapper_class\":\"Border\"}"), *BoxPath), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("unwrapped"), Box->GetChildAt(0)->GetName(), FString(TEXT("RtTitle")));

		H->CallOk(TEXT("ui.set_widget_visibility"), FString::Printf(TEXT("{\"target\":\"%s\",\"visibility\":\"Collapsed\"}"), *Title));
		TestEqual(TEXT("collapsed"), Box->GetChildAt(0)->GetVisibility(), ESlateVisibility::Collapsed);
		H->CallOk(TEXT("history.undo"));
		H->CallFails(TEXT("ui.set_brush"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Title), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("ui.click_button"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Title), ERuntimeToolError::InvalidArguments);

		H->CallOk(TEXT("ui.remove_widget"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Title));
		TestEqual(TEXT("removed"), Box->GetChildrenCount(), 2);
		H->CallFails(TEXT("ui.remove_widget"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *BoxPath), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("restored at index 0"), Box->GetChildAt(0)->GetName(), FString(TEXT("RtTitle")));
		H->CallOk(TEXT("history.undo")); // undo add
		TestEqual(TEXT("add undone"), Box->GetChildrenCount(), 2);
		TestTrue(TEXT("activity"), H->CallOk(TEXT("ui.show_activity"))->HasField(TEXT("enabled")));
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

		H->CallOk(TEXT("ui.set_widget_visibility"), FString::Printf(TEXT("{\"target\":\"%s\",\"visibility\":\"Hidden\"}"), *Target));
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

	It("spawns a system as a node, reads it, deactivates and undoes", [this]()
	{
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("vfx.spawn_system"), FString::Printf(TEXT("{\"system\":\"%s\",\"location\":[0,0,100],\"name\":\"fx\"}"), DefaultSystem));
		const FString Id = R->GetStringField(TEXT("id"));
		TestEqual(TEXT("name"), R->GetStringField(TEXT("name")), FString(TEXT("fx")));
		AActor* A = PresentationNodeActor(*H, Id);
		UNiagaraComponent* C = A ? A->FindComponentByClass<UNiagaraComponent>() : nullptr;
		if (!TestNotNull(TEXT("niagara component"), C))
		{
			return;
		}
		TestNotNull(TEXT("asset set"), C->GetAsset());
		const TSharedRef<FJsonObject> Summary = H->CallOk(TEXT("vfx.get_summary"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id));
		TestTrue(TEXT("emitters listed"), Summary->GetArrayField(TEXT("emitters")).Num() >= 1);
		TestTrue(TEXT("user variables read"), H->CallOk(TEXT("vfx.get_user_variables"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id))->HasField(TEXT("returnValue")));
		H->CallFails(TEXT("vfx.get_user_variable"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Nope\"}"), *Id), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("vfx.deactivate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Id));
		H->CallOk(TEXT("vfx.activate"), FString::Printf(TEXT("{\"target\":\"%s\",\"reset\":true}"), *Id));
		H->CallFails(TEXT("vfx.set_user_variable"), FString::Printf(TEXT("{\"target\":\"%s\",\"name\":\"Nope\",\"value\":1}"), *Id), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("history.undo"), TEXT("{\"count\":3}"));
		TestTrue(TEXT("removed"), H->GetGraph()->Find(Id)->bSoftDeleted);
	});

	It("swaps the system on a node and undo restores it", [this]()
	{
		const FString Id = H->CallOk(TEXT("vfx.spawn_system"), FString::Printf(TEXT("{\"system\":\"%s\"}"), DefaultSystem))->GetStringField(TEXT("id"));
		UNiagaraComponent* C = PresentationNodeActor(*H, Id)->FindComponentByClass<UNiagaraComponent>();
		UNiagaraSystem* Before = C->GetAsset();
		const TCHAR* Other = TEXT("/Niagara/DefaultAssets/Templates/Systems/Fountain.Fountain");
		if (!LoadObject<UNiagaraSystem>(nullptr, Other))
		{
			AddInfo(TEXT("Fountain template not available: checking the failure case only"));
		}
		else
		{
			H->CallOk(TEXT("vfx.set_system"), FString::Printf(TEXT("{\"target\":\"%s\",\"system\":\"%s\"}"), *Id, Other));
			TestNotEqual(TEXT("swapped"), C->GetAsset(), Before);
			TestTrue(TEXT("node source follows"), H->GetGraph()->Find(Id)->Source.Contains(TEXT("Fountain")));
			H->CallOk(TEXT("history.undo"));
			TestEqual(TEXT("restored"), C->GetAsset(), Before);
		}
		H->CallFails(TEXT("vfx.set_system"), FString::Printf(TEXT("{\"target\":\"%s\",\"system\":\"/Game/NoSuch.NoSuch\"}"), *Id), ERuntimeToolError::NotFound);
	});

	It("snaps to the ground and plays one-shots", [this]()
	{
		H->SpawnCube(TEXT("\"location\":[0,0,0],\"scale\":[4,4,1]"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("vfx.spawn_system"), FString::Printf(TEXT("{\"system\":\"%s\",\"location\":[0,0,800],\"snap_to_ground\":true}"), DefaultSystem));
		if (R->GetBoolField(TEXT("snapped_to_ground")))
		{
			TestTrue(TEXT("on the cube"), R->GetArrayField(TEXT("location"))[2]->AsNumber() < 200.0);
		}
		else
		{
			AddWarning(TEXT("no collision under the spawn point in the test world"));
		}
		const TSharedRef<FJsonObject> Shot = H->CallOk(TEXT("vfx.spawn_one_shot"), FString::Printf(TEXT("{\"system\":\"%s\",\"location\":[100,0,0]}"), DefaultSystem));
		TestEqual(TEXT("one-shot system"), Shot->GetStringField(TEXT("system")), FString(DefaultSystem));
		H->CallFails(TEXT("vfx.spawn_one_shot"), TEXT("{\"system\":\"/Game/NoSuch.NoSuch\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("vfx.spawn_one_shot"), FString::Printf(TEXT("{\"system\":\"%s\",\"user_variables\":{\"Nope\":1}}"), DefaultSystem), ERuntimeToolError::NotFound);
		// Isolated test worlds do not tick Niagara, so effects may not report active here; check the list shape only.
		const TSharedRef<FJsonObject> Active = H->CallOk(TEXT("vfx.list_active"), TEXT("{\"limit\":1}"));
		TestTrue(TEXT("list cut at limit"), Active->GetArrayField(TEXT("active")).Num() <= 1);
	});

	It("validates inputs", [this]()
	{
		H->CallFails(TEXT("vfx.spawn_system"), TEXT("{\"system\":\"/Game/NoSuch.NoSuch\"}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("vfx.spawn_system"), FString::Printf(TEXT("{\"system\":\"%s\",\"user_variables\":{\"Nope\":1}}"), DefaultSystem), ERuntimeToolError::NotFound);
		const FString Cube = H->SpawnCube();
		// A cube has no Niagara component: the reference does not resolve to one.
		H->CallFails(TEXT("vfx.activate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("vfx.deactivate"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("vfx.get_summary"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("vfx.get_user_variables"), FString::Printf(TEXT("{\"target\":\"%s\"}"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("vfx.list_active"), TEXT("{\"all\":true}"), ERuntimeToolError::InvalidArguments);
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

	It("reports missing lighting parts", [this]()
	{
		H->CallFails(TEXT("lighting.set_sun"), TEXT("{\"intensity\":5}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("lighting.set_sky"), TEXT("{\"intensity\":1}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("lighting.set_fog"), TEXT("{\"density\":0.1}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("lighting.set_atmosphere"), TEXT("{\"mie_scattering_scale\":0.02}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("lighting.set_clouds"), TEXT("{\"layer_height_km\":10}"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("lighting.set_time_of_day"), TEXT("{\"hour\":10}"), ERuntimeToolError::NotFound);
		const TSharedRef<FJsonObject> Env = H->CallOk(TEXT("lighting.get_environment"));
		for (const TCHAR* Part : {TEXT("sun"), TEXT("sky_light"), TEXT("atmosphere"), TEXT("clouds"), TEXT("fog"), TEXT("post_process")})
		{
			TestTrue(FString::Printf(TEXT("%s absent"), Part), Env->GetObjectField(Part)->GetStringField(TEXT("actor")).IsEmpty());
		}
		TestEqual(TEXT("exposure mode"), Env->GetObjectField(TEXT("post_process"))->GetStringField(TEXT("exposure_mode")), FString(TEXT("ProjectDefault")));
		H->CallFails(TEXT("lighting.get_environment"), TEXT("{\"tiers\":1}"), ERuntimeToolError::InvalidArguments);
	});

	Describe("with a sun and fog", [this]()
	{
		BeforeEach([this]()
		{
			Sun = Cast<ADirectionalLight>(PresentationNodeActor(*H, PresentationSpawn(*H, TEXT("/Script/Engine.DirectionalLight"))));
			Fog = Cast<AExponentialHeightFog>(PresentationNodeActor(*H, PresentationSpawn(*H, TEXT("/Script/Engine.ExponentialHeightFog"))));
		});

		It("sets the sun and undo restores", [this]()
		{
			const float Before = Sun->GetLightComponent()->Intensity;
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("lighting.set_sun"), TEXT("{\"intensity\":3.5,\"temperature\":4000,\"rotation\":[-30,45,0],\"soft_source_angle\":50}"));
			TestEqual(TEXT("intensity"), Sun->GetLightComponent()->Intensity, 3.5f);
			TestTrue(TEXT("rotation"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, -30.0, 0.01));
			TestEqual(TEXT("soft angle"), CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->LightSourceSoftAngle, 50.f);
			TestEqual(TEXT("elevation"), R->GetNumberField(TEXT("elevation")), 30.0);
			H->CallOk(TEXT("history.undo"));
			TestEqual(TEXT("restored"), Sun->GetLightComponent()->Intensity, Before);
			H->CallFails(TEXT("lighting.set_sun"), TEXT("{\"temperature\":100}"), ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("lighting.set_sun"), FString::Printf(TEXT("{\"target\":\"%s\",\"intensity\":1}"), *(TEXT("actor:") + Fog->GetName())), ERuntimeToolError::InvalidArguments);
		});

		It("maps time of day to sun elevation", [this]()
		{
			H->CallOk(TEXT("lighting.set_time_of_day"), TEXT("{\"hour\":12}"));
			TestTrue(TEXT("noon overhead"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, -90.0, 0.5));
			const TSharedRef<FJsonObject> R = H->CallOk(TEXT("lighting.set_time_of_day"), TEXT("{\"hour\":9,\"yaw\":120}"));
			// Sinusoidal day curve: 9:00 is a quarter of the way from sunrise to noon -> 90 * sin(45 deg).
			TestTrue(TEXT("morning elevation"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, -90.0 * FMath::Sin(PI / 4.0), 0.5));
			TestTrue(TEXT("yaw"), FMath::IsNearlyEqual(Sun->GetActorRotation().Yaw, 120.0, 0.5));
			TestEqual(TEXT("hour echoed"), R->GetNumberField(TEXT("hour")), 9.0);
			H->CallFails(TEXT("lighting.set_time_of_day"), TEXT("{\"hour\":30}"), ERuntimeToolError::InvalidArguments);
		});

		It("sets fog and post process; a mood is one undo step", [this]()
		{
			H->CallOk(TEXT("lighting.set_fog"), TEXT("{\"density\":0.2,\"volumetric\":true}"));
			TestEqual(TEXT("density"), Fog->GetComponent()->FogDensity, 0.2f);
			TestTrue(TEXT("volumetric"), Fog->GetComponent()->bEnableVolumetricFog != 0);
			H->CallOk(TEXT("history.undo"));
			H->CallFails(TEXT("lighting.set_fog"), TEXT("{\"density\":20}"), ERuntimeToolError::InvalidArguments);

			const TSharedRef<FJsonObject> P = H->CallOk(TEXT("lighting.set_post_process"), TEXT("{\"saturation\":0.5,\"vignette\":0.6,\"exposure_min\":-1,\"exposure_max\":3}"));
			TestTrue(TEXT("volume created"), P->GetBoolField(TEXT("created_volume")));
			TestEqual(TEXT("auto exposure"), P->GetStringField(TEXT("exposure_mode")), FString(TEXT("Auto")));
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
			H->CallFails(TEXT("lighting.set_post_process"), TEXT("{\"exposure_min\":5,\"exposure_max\":1}"), ERuntimeToolError::InvalidArguments);
			H->CallFails(TEXT("lighting.set_post_process"), TEXT("{\"saturation\":5}"), ERuntimeToolError::InvalidArguments);

			const float FogBefore = Fog->GetComponent()->FogDensity;
			const double PitchBefore = Sun->GetActorRotation().Pitch;
			H->CallOk(TEXT("lighting.apply_mood"), TEXT("{\"mood\":\"foggy\"}"));
			TestEqual(TEXT("foggy density"), Fog->GetComponent()->FogDensity, 0.2f);
			H->CallOk(TEXT("history.undo"));
			TestEqual(TEXT("fog back"), Fog->GetComponent()->FogDensity, FogBefore);
			TestTrue(TEXT("sun back"), FMath::IsNearlyEqual(Sun->GetActorRotation().Pitch, PitchBefore, 0.01));
			TestFalse(TEXT("environment reported"), H->CallOk(TEXT("lighting.get_environment"))->GetObjectField(TEXT("sun"))->GetStringField(TEXT("actor")).IsEmpty());
			H->CallFails(TEXT("lighting.apply_mood"), TEXT("{\"mood\":\"stormy\"}"), ERuntimeToolError::InvalidArguments);
		});

		It("sets sky light, atmosphere and clouds", [this]()
		{
			PresentationSpawn(*H, TEXT("/Script/Engine.SkyLight"));
			H->CallOk(TEXT("lighting.set_sky"), TEXT("{\"intensity\":1.5}"));
			H->CallFails(TEXT("lighting.set_sky"), TEXT("{\"intensity\":-1}"), ERuntimeToolError::InvalidArguments);

			AActor* Atmo = PresentationNodeActor(*H, PresentationSpawn(*H, TEXT("/Script/Engine.SkyAtmosphere")));
			USkyAtmosphereComponent* A = Atmo ? Atmo->FindComponentByClass<USkyAtmosphereComponent>() : nullptr;
			if (TestNotNull(TEXT("atmosphere"), A))
			{
				const float MieBefore = A->MieScatteringScale;
				H->CallOk(TEXT("lighting.set_atmosphere"), TEXT("{\"mie_scattering_scale\":0.03,\"rayleigh_scattering\":[0.45,0.56,0.75]}"));
				TestEqual(TEXT("mie"), A->MieScatteringScale, 0.03f);
				H->CallOk(TEXT("history.undo"));
				TestEqual(TEXT("mie back"), A->MieScatteringScale, MieBefore);
				H->CallFails(TEXT("lighting.set_atmosphere"), TEXT("{\"mie_anisotropy\":2}"), ERuntimeToolError::InvalidArguments);
			}

			AActor* CloudActor = PresentationNodeActor(*H, PresentationSpawn(*H, TEXT("/Script/Engine.VolumetricCloud")));
			UVolumetricCloudComponent* C = CloudActor ? CloudActor->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
			if (TestNotNull(TEXT("clouds"), C))
			{
				const float HeightBefore = C->LayerHeight;
				const TSharedRef<FJsonObject> R = H->CallOk(TEXT("lighting.set_clouds"), TEXT("{\"layer_height_km\":12,\"bottom_altitude_km\":1,\"cast_shadows\":true}"));
				TestEqual(TEXT("layer height"), C->LayerHeight, 12.f);
				TestTrue(TEXT("cloud shadows on the sun"), R->GetBoolField(TEXT("cast_shadows")));
				const TSharedRef<FJsonObject> Env = H->CallOk(TEXT("lighting.get_environment"));
				TestEqual(TEXT("clouds reported"), Env->GetObjectField(TEXT("clouds"))->GetNumberField(TEXT("layer_height_km")), 12.0);
				H->CallOk(TEXT("history.undo"));
				TestEqual(TEXT("layer back"), C->LayerHeight, HeightBefore);
				const FString CoverageParam = Env->GetObjectField(TEXT("clouds"))->GetStringField(TEXT("coverage_parameter"));
				if (CoverageParam.IsEmpty())
				{
					H->CallFails(TEXT("lighting.set_clouds"), TEXT("{\"coverage\":0.6}"), ERuntimeToolError::NotFound);
				}
				else
				{
					UMaterialInterface* MatBefore = C->GetMaterial();
					const TSharedRef<FJsonObject> Cov = H->CallOk(TEXT("lighting.set_clouds"), TEXT("{\"coverage\":0.6}"));
					TestTrue(TEXT("coverage set"), FMath::IsNearlyEqual(Cov->GetNumberField(TEXT("coverage")), 0.6, 0.001));
					TestNotEqual(TEXT("dynamic instance, engine asset untouched"), C->GetMaterial(), MatBefore);
					H->CallOk(TEXT("history.undo"));
					TestEqual(TEXT("material back"), C->GetMaterial(), MatBefore);
				}
				H->CallFails(TEXT("lighting.set_clouds"), TEXT("{\"coverage\":3}"), ERuntimeToolError::InvalidArguments);
			}
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
		TestEqual(TEXT("text"), R->GetStringField(TEXT("text")), FString(TEXT("Tower")));
		AActor* LabelActor = PresentationNodeActor(*H, Label);
		TestTrue(TEXT("above the top"), LabelActor->GetActorLocation().Equals(FVector(1000, 0, 200), 0.5));
		TestEqual(TEXT("child of target"), H->GetGraph()->Find(Label)->ParentId, Tower);
		H->CallOk(TEXT("scene.move_node"), FString::Printf(TEXT("{\"target\":\"%s\",\"offset\":[0,500,0]}"), *Tower));
		TestTrue(TEXT("follows"), LabelActor->GetActorLocation().Equals(FVector(1000, 500, 200), 0.5));
		H->CallOk(TEXT("ui.set_label"), FString::Printf(TEXT("{\"target\":\"%s\",\"text\":\"Big Tower\"}"), *Label));
		TestTrue(TEXT("text in source"), H->GetGraph()->Find(Label)->Source.Contains(TEXT("Big Tower")));
		H->CallOk(TEXT("history.undo"));
		TestFalse(TEXT("text undone"), H->GetGraph()->Find(Label)->Source.Contains(TEXT("Big Tower")));
		const TSharedRef<FJsonObject> At = H->CallOk(TEXT("ui.create_label"), TEXT("{\"text\":\"Here\",\"location\":[0,0,0]}"));
		TestTrue(TEXT("at location + default offset"), PresentationNodeActor(*H, At->GetStringField(TEXT("id")))->GetActorLocation().Equals(FVector(0, 0, 100), 0.5));
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
		H->CallFails(TEXT("style.list_presets"), TEXT("{\"filter\":\"toon\"}"), ERuntimeToolError::InvalidArguments);
	});

	It("applies material switches and the post pass, tweaks, and undo restores", [this]()
	{
		TestFalse(TEXT("nothing applied yet"), H->CallOk(TEXT("style.get"))->GetBoolField(TEXT("applied")));
		H->CallFails(TEXT("style.get"), TEXT("{\"name\":\"x\"}"), ERuntimeToolError::InvalidArguments);
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
		H->CallFails(TEXT("style.apply"), TEXT(R"J({"post":[1,2]})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("style.apply"), TEXT(R"J({"preset":"realistic"})J"));
		if (V)
		{
			TestEqual(TEXT("realistic skips the post pass"), V->Settings.WeightedBlendables.Array[0].Weight, 0.f);
		}
	});

	It("defines presets and rejects unknown ones", [this]()
	{
		H->CallFails(TEXT("style.apply"), TEXT(R"J({"preset":"nope"})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("style.define_preset"), TEXT(R"J({"name":"comic","based_on":"nope"})J"), ERuntimeToolError::InvalidArguments);
		H->CallOk(TEXT("style.define_preset"), TEXT(R"J({"name":"comic","based_on":"toon","post":{"OutlineWidth":3}})J"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("style.apply"), TEXT(R"J({"preset":"comic","lighting_enabled":false})J"));
		TestEqual(TEXT("override kept"), R->GetObjectField(TEXT("style"))->GetObjectField(TEXT("post"))->GetNumberField(TEXT("OutlineWidth")), 3.0);
		TestEqual(TEXT("base kept"), R->GetObjectField(TEXT("style"))->GetObjectField(TEXT("post"))->GetNumberField(TEXT("Toon")), 1.0);
	});
}

#endif
