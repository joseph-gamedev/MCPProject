#include "CaptureToolset.h"
#include "RuntimeImage.h"
#include "Helpers/ToolsCommon.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "UnrealClient.h"

using namespace RuntimeTools;

static int32 ReadMaxDim(const FToolInvocation& Inv)
{
	const int32 Cap = URuntimeToolsetsSettings::Get().CaptureMaxDimension;
	return FMath::Clamp(static_cast<int32>(Inv.GetInt(TEXT("max_dim"), 768)), 64, FMath::Max(64, Cap));
}

static int32 ReadQuality(const FToolInvocation& Inv)
{
	return static_cast<int32>(Inv.GetInt(TEXT("quality"), URuntimeToolsetsSettings::Get().CaptureJpegQuality));
}

static void FinishImage(FToolInvocation& Inv, TArray<FColor>&& Pixels, int32 W, int32 H)
{
	FToolAttachment Attachment;
	FString Error;
	if (!RuntimeImage::Encode(MoveTemp(Pixels), W, H, ReadMaxDim(Inv), Inv.GetString(TEXT("format"), TEXT("jpg")), ReadQuality(Inv), Attachment, Error))
	{
		Inv.Fail(ERuntimeToolError::Internal, Error);
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("w"), Attachment.Width);
	Out->SetNumberField(TEXT("h"), Attachment.Height);
	Out->SetNumberField(TEXT("source_w"), W);
	Out->SetNumberField(TEXT("source_h"), H);
	Inv.AddAttachment(MoveTemp(Attachment));
	Inv.Succeed(Out);
}

static void Screenshot(FToolInvocation& Inv)
{
	if (!FApp::CanEverRender() || !GEngine || !GEngine->GameViewport)
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("no game viewport (null RHI or server)"), TEXT("Use capture.scene_capture"));
		return;
	}
	struct FState
	{
		FDelegateHandle Handle;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	TSharedRef<FToolInvocation> Self = Inv.AsShared();
	State->Handle = UGameViewportClient::OnScreenshotCaptured().AddLambda([Self, State](int32 W, int32 H, const TArray<FColor>& Colors)
	{
		// Copy captures before removing: removal frees this lambda.
		const TSharedRef<FToolInvocation> Invocation = Self;
		TArray<FColor> Copy = Colors;
		UGameViewportClient::OnScreenshotCaptured().Remove(State->Handle);
		if (Invocation->IsComplete())
		{
			return;
		}
		FinishImage(*Invocation, MoveTemp(Copy), W, H);
	});
	Inv.SetCancelHandler([State]() { UGameViewportClient::OnScreenshotCaptured().Remove(State->Handle); });
	FScreenshotRequest::RequestScreenshot(Inv.GetBool(TEXT("show_ui"), false), false);
	Inv.Defer();
}

static void CaptureRenderTarget(FToolInvocation& Inv)
{
	FResolvedTarget Target;
	if (!Inv.ResolveTarget(TEXT("target"), Target))
	{
		return;
	}
	UTextureRenderTarget* RT = Cast<UTextureRenderTarget>(Target.Object);
	if (!RT)
	{
		if (USceneCaptureComponent2D* Capture = Cast<USceneCaptureComponent2D>(Target.Object))
		{
			RT = Capture->TextureTarget;
		}
		else if (ASceneCapture2D* CaptureActor = Cast<ASceneCapture2D>(Target.Object))
		{
			RT = CaptureActor->GetCaptureComponent2D()->TextureTarget;
		}
	}
	if (!RT)
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a render target or scene capture"));
		return;
	}
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	FString Error;
	if (!RuntimeImage::ReadRenderTarget(RT, Pixels, W, H, Error))
	{
		Inv.Fail(ERuntimeToolError::Internal, Error);
		return;
	}
	FinishImage(Inv, MoveTemp(Pixels), W, H);
}

static void SceneCapture(FToolInvocation& Inv)
{
	if (!FApp::CanEverRender())
	{
		Inv.Fail(ERuntimeToolError::Unsupported, TEXT("rendering is disabled (null RHI)"));
		return;
	}
	UWorld* World = Inv.GetWorld();
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Location = PC->PlayerCameraManager->GetCameraLocation();
		Rotation = PC->PlayerCameraManager->GetCameraRotation();
	}
	Inv.GetVector(TEXT("location"), Location);
	Inv.GetRotator(TEXT("rotation"), Rotation);
	FVector LookAt;
	if (Inv.GetVector(TEXT("look_at"), LookAt))
	{
		Rotation = (LookAt - Location).Rotation();
	}
	const int32 Width = static_cast<int32>(Inv.GetInt(TEXT("width"), 768));
	const int32 Height = static_cast<int32>(Inv.GetInt(TEXT("height"), 432));

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), Location, Rotation, Params);
	if (!CaptureActor)
	{
		Inv.Fail(ERuntimeToolError::Internal, TEXT("could not spawn scene capture"));
		return;
	}
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	RT->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
	RT->UpdateResourceImmediate(true);

	USceneCaptureComponent2D* Comp = CaptureActor->GetCaptureComponent2D();
	Comp->TextureTarget = RT;
	Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Comp->bCaptureEveryFrame = false;
	Comp->bCaptureOnMovement = false;
	Comp->FOVAngle = static_cast<float>(Inv.GetNumber(TEXT("fov"), 90.0));
	Comp->CaptureScene();

	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	FString Error;
	const bool bRead = RuntimeImage::ReadRenderTarget(RT, Pixels, W, H, Error);
	CaptureActor->Destroy();
	RT->ReleaseResource();
	if (!bRead)
	{
		Inv.Fail(ERuntimeToolError::Internal, Error);
		return;
	}
	FinishImage(Inv, MoveTemp(Pixels), W, H);
}

void FCaptureToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Format = []() { return FToolParam::Enum(TEXT("Image format"), {TEXT("jpg"), TEXT("png")}).Default(TEXT("jpg")); };
	const auto MaxDim = []() { return FToolParam::Integer(TEXT("Longest side in px"), 64, 4096).Default(768); };
	const auto Quality = []() { return FToolParam::Integer(TEXT("JPEG quality"), 1, 100); };

	B.Tool(TEXT("screenshot"), TEXT("Capture the game viewport as an image (async, next frame)."))
		.Args(FToolSchema()
			.Add(TEXT("show_ui"), FToolParam::Boolean(TEXT("Include UI")).Default(false))
			.Add(TEXT("max_dim"), MaxDim())
			.Add(TEXT("format"), Format())
			.Add(TEXT("quality"), Quality()))
		.Async().RequiresWorld()
		.Handle(&Screenshot);

	B.Tool(TEXT("capture_render_target"), TEXT("Read a render target (or a scene capture's target) as an image."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Render target object or scene capture")).Required())
			.Add(TEXT("max_dim"), MaxDim())
			.Add(TEXT("format"), Format())
			.Add(TEXT("quality"), Quality()))
		.RequiresWorld()
		.Handle(&CaptureRenderTarget);

	B.Tool(TEXT("scene_capture"), TEXT("Render the scene from any camera pose. Defaults to the player camera."))
		.Args(FToolSchema()
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("Camera [x,y,z] cm")))
			.Add(TEXT("rotation"), FToolParam::Rotator(TEXT("Camera [pitch,yaw,roll]")))
			.Add(TEXT("look_at"), FToolParam::Vec3(TEXT("Point to look at (overrides rotation)")))
			.Add(TEXT("fov"), FToolParam::Number(TEXT("Horizontal FOV deg"), 5.0, 170.0).Default(90.0))
			.Add(TEXT("width"), FToolParam::Integer(TEXT("Render width"), 64, 4096).Default(768))
			.Add(TEXT("height"), FToolParam::Integer(TEXT("Render height"), 64, 4096).Default(432))
			.Add(TEXT("max_dim"), MaxDim())
			.Add(TEXT("format"), Format())
			.Add(TEXT("quality"), Quality()))
		.RequiresWorld()
		.Handle(&SceneCapture);
}
