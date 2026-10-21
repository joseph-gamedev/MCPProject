#pragma once

#include "RuntimeToolset.h"

/**
 * Art style: one call restyles the whole scene (realistic, stylized, low poly, toon, retro, noir, ...).
 * A style drives three layers: global material switches for every builder material (MPC_RT_Style: flat shading,
 * procedural detail, colour steps, saturation, tint, rim light), a style post-process (M_PP_Style: toon bands, outlines,
 * painterly filter, pixelation, posterize, grading) plus post settings, and lighting (mood, sun, sky, fog).
 * Presets are data: built in, overridable/extendable from Config/RuntimeToolsets/StylePresets.json or style.define_preset.
 */
class FStyleToolset final : public IRuntimeToolset
{
public:
	virtual FName GetName() const override { return TEXT("style"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Art style for the whole scene: realistic, stylized, lowpoly, toon, retro, noir presets or custom mixes (materials, post-process, lighting)");
	}
	virtual FString GetVersion() const override { return TEXT("1.0.0"); }
	virtual void RegisterTools(FRuntimeToolsetBuilder& B) override;
};
