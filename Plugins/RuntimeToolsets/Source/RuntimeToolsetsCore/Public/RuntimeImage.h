#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolTypes.h"

class UTextureRenderTarget;
class USceneCaptureComponent2D;

/** Image helpers shared by capture tools. */
namespace RuntimeImage
{
	/** Downscales to MaxDim (longest side), forces opaque alpha and encodes as "jpg" or "png". */
	RUNTIMETOOLSETSCORE_API bool Encode(TArray<FColor> Pixels, int32 Width, int32 Height, int32 MaxDim, const FString& Format, int32 Quality, FToolAttachment& Out,
		FString& OutError);

	/** Reads a render target back to the CPU (flushes rendering) as 8-bit sRGB pixels. */
	RUNTIMETOOLSETSCORE_API bool ReadRenderTarget(UTextureRenderTarget* RenderTarget, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, FString& OutError);

	/** One-off scene captures have no eye adaptation history: give them the scene's locked exposure (from the
	 *  highest-priority unbound post process volume) as fixed manual exposure, so they match the game view. */
	RUNTIMETOOLSETSCORE_API void MatchSceneExposure(USceneCaptureComponent2D* Capture);
}
