#include "RuntimeImage.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Engine/TextureRenderTarget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace RuntimeImage
{
	bool Encode(TArray<FColor> Pixels, int32 Width, int32 Height, int32 MaxDim, const FString& Format, int32 Quality, FToolAttachment& Out,
		FString& OutError)
	{
		if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
		{
			OutError = TEXT("empty image");
			return false;
		}
		for (FColor& C : Pixels)
		{
			C.A = 255;
		}
		int32 W = Width;
		int32 H = Height;
		if (MaxDim > 0 && FMath::Max(W, H) > MaxDim)
		{
			const double Scale = static_cast<double>(MaxDim) / FMath::Max(W, H);
			const int32 DW = FMath::Max(1, FMath::RoundToInt(W * Scale));
			const int32 DH = FMath::Max(1, FMath::RoundToInt(H * Scale));
			TArray<FColor> Resized;
			Resized.SetNumUninitialized(DW * DH);
			FImageUtils::ImageResize(W, H, Pixels, DW, DH, Resized, false, true);
			Pixels = MoveTemp(Resized);
			W = DW;
			H = DH;
		}
		const bool bPng = Format.Equals(TEXT("png"), ESearchCase::IgnoreCase);
		const FImageView View(Pixels.GetData(), W, H, EGammaSpace::sRGB);
		Out.Bytes.Reset();
		if (!FImageUtils::CompressImage(Out.Bytes, bPng ? TEXT("png") : TEXT("jpg"), View, bPng ? 0 : FMath::Clamp(Quality, 1, 100)))
		{
			OutError = TEXT("image compression failed");
			return false;
		}
		Out.MimeType = bPng ? TEXT("image/png") : TEXT("image/jpeg");
		Out.Width = W;
		Out.Height = H;
		return true;
	}

	bool ReadRenderTarget(UTextureRenderTarget* RenderTarget, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, FString& OutError)
	{
		if (!RenderTarget)
		{
			OutError = TEXT("no render target");
			return false;
		}
		FImage Image;
		if (!FImageUtils::GetRenderTargetImage(RenderTarget, Image))
		{
			OutError = TEXT("render target readback failed");
			return false;
		}
		FImage Bgra;
		Image.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const TArrayView64<FColor> Px = Bgra.AsBGRA8();
		OutPixels.SetNumUninitialized(static_cast<int32>(Px.Num()));
		FMemory::Memcpy(OutPixels.GetData(), Px.GetData(), Px.Num() * sizeof(FColor));
		OutWidth = Bgra.SizeX;
		OutHeight = Bgra.SizeY;
		return true;
	}
}

void RuntimeImage::MatchSceneExposure(USceneCaptureComponent2D* Capture)
{
	UWorld* World = Capture ? Capture->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const APostProcessVolume* Best = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->bUnbound && It->bEnabled && It->Settings.bOverride_AutoExposureMinBrightness && (!Best || It->Priority > Best->Priority))
		{
			Best = *It;
		}
	}
	// Without a view state and the eye adaptation pass, captures render at exposure 1 whatever the settings say.
	Capture->bAlwaysPersistRenderingState = true;
	Capture->ShowFlags.SetEyeAdaptation(true);
	FPostProcessSettings& S = Capture->PostProcessSettings;
	Capture->PostProcessBlendWeight = 1.f;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_AutoExposureBias = true;
	if (!Best)
	{
		S.AutoExposureBias = 0.f;  // nothing locked: neutral exposure
		return;
	}
	// Min/max brightness are luminance unless the extended range is on (then they are EV100).
	static const IConsoleVariable* Extended = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
	const float Setting = Best->Settings.AutoExposureMinBrightness;
	const float Ev = Extended && Extended->GetInt() != 0 ? Setting : FMath::Log2(FMath::Max(Setting, 1e-6f) / 0.125f);
	const float Bias = Best->Settings.bOverride_AutoExposureBias ? Best->Settings.AutoExposureBias : 0.f;
	// Manual bias that matches the game view's histogram exposure at the same EV (calibrated against screenshots).
	S.AutoExposureBias = 1.85f - Ev + Bias;
}
