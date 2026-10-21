#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeCaptureToolset.generated.h"

/** Image encoding. */
UENUM()
enum class ERuntimeImageFormat : uint8
{
	/** JPEG (small; default). */
	Jpg,
	/** PNG (lossless, larger). */
	Png,
};

/** Size of a captured image; the image itself is attached to the result. */
USTRUCT()
struct FRuntimeImageInfo
{
	GENERATED_BODY()

	/** Width of the attached image in px. */
	UPROPERTY()
	int32 W = 0;

	/** Height of the attached image in px. */
	UPROPERTY()
	int32 H = 0;

	/** Width rendered before downscaling. */
	UPROPERTY()
	int32 SourceW = 0;

	/** Height rendered before downscaling. */
	UPROPERTY()
	int32 SourceH = 0;
};

/** Pending screenshot (completes on the next rendered frame). */
UCLASS()
class URuntimeCaptureImageResult : public URuntimeToolAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FRuntimeImageInfo& InValue)
	{
		Value = InValue;
		return Complete();
	}

	UPROPERTY()
	FRuntimeImageInfo Value;
};

/**
 * Capture: images of the running game for visual checks. Each tool attaches one JPEG or PNG image to its result.
 * Workflow: screenshot to see exactly what the player sees (UI optional); scene_capture to look from any camera pose
 * (e.g. above or behind something you built) without moving the player; capture_render_target to read a render target.
 */
UCLASS()
class URuntimeCaptureToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Captures the game viewport as an image on the next rendered frame (async). Fails without a game viewport
	 * (null RHI, dedicated server): use scene_capture there.
	 *
	 * @param bShowUi - Include the UI (HUD, widgets).
	 * @param MaxDim - Longest side of the returned image in px, 64-4096 (capped by project settings).
	 * @param Format - Jpg or Png.
	 * @param Quality - JPEG quality 1-100 (0 = project default).
	 * @return Image size {w, h, source_w, source_h}; the image is attached.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Capture")
	static URuntimeCaptureImageResult* Screenshot(bool bShowUi = false, int32 MaxDim = 768, ERuntimeImageFormat Format = ERuntimeImageFormat::Jpg,
		int32 Quality = 0);

	/**
	 * Reads a render target, or a scene capture's render target, as an image.
	 * Fails if the target is neither, or the render target cannot be read.
	 *
	 * @param Target - Render target object path, or a SceneCapture2D actor / component.
	 * @param MaxDim - Longest side of the returned image in px, 64-4096.
	 * @param Format - Jpg or Png.
	 * @param Quality - JPEG quality 1-100 (0 = project default).
	 * @return Image size {w, h, source_w, source_h}; the image is attached.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Capture")
	static FRuntimeImageInfo CaptureRenderTarget(UObject* Target, int32 MaxDim = 768, ERuntimeImageFormat Format = ERuntimeImageFormat::Jpg,
		int32 Quality = 0);

	/**
	 * Renders the scene from any camera pose (default: the player camera) without moving the player.
	 * Fails when rendering is disabled (null RHI).
	 *
	 * @param Location - Camera position [x, y, z] in cm.
	 * @param Rotation - Camera rotation [pitch, yaw, roll] in degrees.
	 * @param LookAt - Point to look at [x, y, z] (overrides rotation).
	 * @param Fov - Horizontal field of view in degrees, 5-170.
	 * @param Width - Render width in px, 64-4096.
	 * @param Height - Render height in px, 64-4096.
	 * @param MaxDim - Longest side of the returned image in px.
	 * @param Format - Jpg or Png.
	 * @param Quality - JPEG quality 1-100 (0 = project default).
	 * @return Image size {w, h, source_w, source_h}; the image is attached.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Location,Rotation,LookAt"), Category = "Capture")
	static FRuntimeImageInfo SceneCapture(const FVector& Location, const FRotator& Rotation, const FVector& LookAt, float Fov = 90.f, int32 Width = 768,
		int32 Height = 432, int32 MaxDim = 768, ERuntimeImageFormat Format = ERuntimeImageFormat::Jpg, int32 Quality = 0);
};
