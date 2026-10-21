#include "RuntimeCaptureToolset.h"
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

namespace
{
	struct FEncodeOptions
	{
		int32 MaxDim = 768;
		FString Format = TEXT("jpg");
		int32 Quality = 80;
	};

	FEncodeOptions MakeOptions(int32 MaxDim, ERuntimeImageFormat Format, int32 Quality)
	{
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		FEncodeOptions O;
		O.MaxDim = FMath::Clamp(MaxDim, 64, FMath::Max(64, S.CaptureMaxDimension));
		O.Format = Format == ERuntimeImageFormat::Png ? TEXT("png") : TEXT("jpg");
		O.Quality = Quality > 0 ? FMath::Clamp(Quality, 1, 100) : S.CaptureJpegQuality;
		return O;
	}

	/** Encodes pixels and attaches the image to the invocation. Returns false (with OutError) on failure. */
	bool AttachImage(FToolInvocation& Inv, TArray<FColor>&& Pixels, int32 W, int32 H, const FEncodeOptions& O, FRuntimeImageInfo& OutInfo, FString& OutError)
	{
		FToolAttachment Attachment;
		if (!RuntimeImage::Encode(MoveTemp(Pixels), W, H, O.MaxDim, O.Format, O.Quality, Attachment, OutError))
		{
			return false;
		}
		OutInfo.W = Attachment.Width;
		OutInfo.H = Attachment.Height;
		OutInfo.SourceW = W;
		OutInfo.SourceH = H;
		Inv.AddAttachment(MoveTemp(Attachment));
		return true;
	}

	FRuntimeImageInfo FinishSync(TArray<FColor>&& Pixels, int32 W, int32 H, const FEncodeOptions& O)
	{
		FRuntimeImageInfo Info;
		FString Err;
		if (!AttachImage(*RuntimeToolCall::Current(), MoveTemp(Pixels), W, H, O, Info, Err))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::Internal, Err);
		}
		return Info;
	}
}

URuntimeCaptureImageResult* URuntimeCaptureToolset::Screenshot(bool bShowUi, int32 MaxDim, ERuntimeImageFormat Format, int32 Quality)
{
	if (!FApp::CanEverRender() || !GEngine || !GEngine->GameViewport)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("no game viewport (null RHI or server)"), TEXT("Use capture.scene_capture"));
		return nullptr;
	}
	URuntimeCaptureImageResult* Result = NewObject<URuntimeCaptureImageResult>();
	const FEncodeOptions Options = MakeOptions(MaxDim, Format, Quality);
	struct FState
	{
		FDelegateHandle Handle;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	TSharedRef<FToolInvocation> Inv = RuntimeToolCall::Current()->AsShared();
	TWeakObjectPtr<URuntimeCaptureImageResult> WeakResult = Result;
	State->Handle = UGameViewportClient::OnScreenshotCaptured().AddLambda([Inv, State, WeakResult, Options](int32 W, int32 H, const TArray<FColor>& Colors)
	{
		// Copy captures before removing: removal frees this lambda.
		const TSharedRef<FToolInvocation> Invocation = Inv;
		const TWeakObjectPtr<URuntimeCaptureImageResult> R = WeakResult;
		const FEncodeOptions O = Options;
		TArray<FColor> Copy = Colors;
		UGameViewportClient::OnScreenshotCaptured().Remove(State->Handle);
		if (!R.IsValid() || Invocation->IsComplete())
		{
			return;
		}
		FRuntimeImageInfo Info;
		FString Err;
		if (AttachImage(*Invocation, MoveTemp(Copy), W, H, O, Info, Err))
		{
			R->SetValue(Info);
		}
		else
		{
			R->SetError(Err);
		}
	});
	Result->OnCancelled = [State]() { UGameViewportClient::OnScreenshotCaptured().Remove(State->Handle); };
	FScreenshotRequest::RequestScreenshot(bShowUi, false);
	return Result;
}

FRuntimeImageInfo URuntimeCaptureToolset::CaptureRenderTarget(UObject* Target, int32 MaxDim, ERuntimeImageFormat Format, int32 Quality)
{
	UTextureRenderTarget* RT = Cast<UTextureRenderTarget>(Target);
	if (!RT)
	{
		if (const USceneCaptureComponent2D* Capture = Cast<USceneCaptureComponent2D>(Target))
		{
			RT = Capture->TextureTarget;
		}
		else if (const ASceneCapture2D* CaptureActor = Cast<ASceneCapture2D>(Target))
		{
			RT = CaptureActor->GetCaptureComponent2D()->TextureTarget;
		}
	}
	if (!RT)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments,
			FString::Printf(TEXT("%s is a %s, not a render target or scene capture with one"), *Target->GetName(), *Target->GetClass()->GetName()),
			TEXT("Use capture.scene_capture to render from a camera pose"));
		return {};
	}
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	FString Err;
	if (!RuntimeImage::ReadRenderTarget(RT, Pixels, W, H, Err))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, Err);
		return {};
	}
	return FinishSync(MoveTemp(Pixels), W, H, MakeOptions(MaxDim, Format, Quality));
}

FRuntimeImageInfo URuntimeCaptureToolset::SceneCapture(const FVector& Location, const FRotator& Rotation, const FVector& LookAt, float Fov, int32 Width,
	int32 Height, int32 MaxDim, ERuntimeImageFormat Format, int32 Quality)
{
	if (!FApp::CanEverRender())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("rendering is disabled (null RHI)"));
		return {};
	}
	FToolInvocation& Inv = *RuntimeToolCall::Current();
	UWorld* World = RuntimeToolCall::GetWorld();
	FVector CamLocation = FVector::ZeroVector;
	FRotator CamRotation = FRotator::ZeroRotator;
	if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		CamLocation = PC->PlayerCameraManager->GetCameraLocation();
		CamRotation = PC->PlayerCameraManager->GetCameraRotation();
	}
	if (Inv.Has(TEXT("location")))
	{
		CamLocation = Location;
	}
	if (Inv.Has(TEXT("rotation")))
	{
		CamRotation = Rotation;
	}
	if (Inv.Has(TEXT("look_at")))
	{
		CamRotation = (LookAt - CamLocation).Rotation();
	}
	Width = FMath::Clamp(Width, 64, 4096);
	Height = FMath::Clamp(Height, 64, 4096);

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), CamLocation, CamRotation, Params);
	if (!CaptureActor)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, TEXT("could not spawn a scene capture"));
		return {};
	}
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	RT->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
	RT->UpdateResourceImmediate(true);

	USceneCaptureComponent2D* Comp = CaptureActor->GetCaptureComponent2D();
	Comp->TextureTarget = RT;
	Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Comp->bCaptureEveryFrame = false;
	Comp->bCaptureOnMovement = false;
	Comp->FOVAngle = FMath::Clamp(Fov, 5.f, 170.f);
	Comp->CaptureScene();

	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	FString Err;
	const bool bRead = RuntimeImage::ReadRenderTarget(RT, Pixels, W, H, Err);
	CaptureActor->Destroy();
	RT->ReleaseResource();
	if (!bRead)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, Err);
		return {};
	}
	return FinishSync(MoveTemp(Pixels), W, H, MakeOptions(MaxDim, Format, Quality));
}
