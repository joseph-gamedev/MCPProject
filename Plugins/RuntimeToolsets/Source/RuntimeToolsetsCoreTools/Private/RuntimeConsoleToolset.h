#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeConsoleToolset.generated.h"

USTRUCT()
struct FRuntimeCommandOutput
{
	GENERATED_BODY()

	/** Whether any handler recognised the command. */
	UPROPERTY()
	bool bHandled = false;

	/** Text the command printed (up to 4000 characters). */
	UPROPERTY()
	FString Output;

	/** True when the output was cut. */
	UPROPERTY()
	bool bTruncated = false;
};

/** One console variable. */
USTRUCT()
struct FRuntimeCVarInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Current value as text. */
	UPROPERTY()
	FString Value;

	UPROPERTY()
	bool bReadOnly = false;

	/** Help text (get_cvar only, up to 300 characters). */
	UPROPERTY()
	FString Help;
};

USTRUCT()
struct FRuntimeCVarList
{
	GENERATED_BODY()

	/** Number of matching variables (may exceed the list). */
	UPROPERTY()
	int32 Total = 0;

	/** Matches in console-manager order, at most `limit`. */
	UPROPERTY()
	TArray<FRuntimeCVarInfo> Cvars;
};

USTRUCT()
struct FRuntimeCVarChange
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Value after the change. */
	UPROPERTY()
	FString Value;

	/** Value before the change (undo restores it). */
	UPROPERTY()
	FString Previous;
};

/** Scalability levels: 0 low, 1 medium, 2 high, 3 epic, 4 cinematic. */
USTRUCT()
struct FRuntimeScalability
{
	GENERATED_BODY()

	/** Screen percentage. */
	UPROPERTY()
	float ResolutionScale = 100.f;

	UPROPERTY()
	int32 ViewDistance = 0;

	UPROPERTY()
	int32 AntiAliasing = 0;

	UPROPERTY()
	int32 Shadows = 0;

	UPROPERTY()
	int32 GlobalIllumination = 0;

	UPROPERTY()
	int32 Reflections = 0;

	UPROPERTY()
	int32 PostProcess = 0;

	UPROPERTY()
	int32 Textures = 0;

	UPROPERTY()
	int32 Effects = 0;

	UPROPERTY()
	int32 Foliage = 0;

	UPROPERTY()
	int32 Shading = 0;

	/** Lowest group level. */
	UPROPERTY()
	int32 Overall = 0;
};

USTRUCT()
struct FRuntimeUserSettings
{
	GENERATED_BODY()

	/** Screen resolution [width, height]. */
	UPROPERTY()
	FIntPoint Resolution = FIntPoint::ZeroValue;

	/** fullscreen, windowed_fullscreen or windowed. */
	UPROPERTY()
	FString WindowMode;

	UPROPERTY()
	bool bVsync = false;

	/** Frame rate cap (0 = none). */
	UPROPERTY()
	float FrameRateLimit = 0.f;

	UPROPERTY()
	bool bDynamicResolution = false;

	UPROPERTY()
	FRuntimeScalability Scalability;
};

/**
 * Console: console commands, console variables (cvars), scalability and user settings of the running game.
 * Workflow: list_cvars {prefix} to discover -> get_cvar for help text -> set_cvar (undoable); set_scalability for quality
 * presets; exec_command only for commands that have no cvar (it cannot be undone, and the project deny-list applies).
 */
UCLASS()
class URuntimeConsoleToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Runs one console command and returns what it printed. Fails for commands denied by project settings and for
	 * chained commands ("a|b").
	 *
	 * @param Command - Console command, e.g. "stat fps" or "r.ScreenPercentage 75".
	 * @return Whether it was handled and its captured output.
	 */
	UFUNCTION(meta = (AICallable, Irreversible), Category = "Console")
	static FRuntimeCommandOutput ExecCommand(const FString& Command);

	/**
	 * Reads one console variable with its help text. Fails if it does not exist (search with list_cvars).
	 *
	 * @param Name - Exact cvar name, e.g. "r.ScreenPercentage".
	 * @return Name, value, read-only flag and help.
	 */
	UFUNCTION(meta = (AICallable), Category = "Console")
	static FRuntimeCVarInfo GetCvar(const FString& Name);

	/**
	 * Lists console variables whose names start with a prefix.
	 *
	 * @param Prefix - At least 2 characters, e.g. "r.Shadow".
	 * @param Limit - Maximum variables returned (0 = project default).
	 * @return Total count and variables {name, value, read_only}.
	 */
	UFUNCTION(meta = (AICallable), Category = "Console")
	static FRuntimeCVarList ListCvars(const FString& Prefix, int32 Limit = 25);

	/**
	 * Sets a console variable. Undoable. Fails if it does not exist, is read-only or is denied by project settings.
	 *
	 * @param Name - Cvar name, e.g. "t.MaxFPS".
	 * @param Value - New value: number, bool or string, e.g. 60.
	 * @return Name, new value and previous value.
	 */
	UFUNCTION(meta = (AICallable, Undoable), Category = "Console")
	static FRuntimeCVarChange SetCvar(const FString& Name, const FRuntimeToolJson& Value);

	/**
	 * Sets the overall quality level and/or individual groups (0 low, 1 medium, 2 high, 3 epic, 4 cinematic); groups you
	 * omit keep their level, and groups override overall. Undoable. Fails for levels outside 0-4.
	 *
	 * @param Overall - All groups at once.
	 * @param ResolutionScale - Screen percentage, 10-200.
	 * @param ViewDistance - View distance level.
	 * @param AntiAliasing - Anti-aliasing level.
	 * @param Shadows - Shadow level.
	 * @param GlobalIllumination - Global illumination level.
	 * @param Reflections - Reflection level.
	 * @param PostProcess - Post-process level.
	 * @param Textures - Texture level.
	 * @param Effects - Effects level.
	 * @param Foliage - Foliage level.
	 * @param Shading - Shading level.
	 * @return The resulting scalability levels.
	 */
	UFUNCTION(meta = (AICallable, Undoable, AutoCreateRefTerm = "Overall,ResolutionScale,ViewDistance,AntiAliasing,Shadows,GlobalIllumination,Reflections,PostProcess,Textures,Effects,Foliage,Shading"), Category = "Console")
	static FRuntimeScalability SetScalability(const int32& Overall, const float& ResolutionScale, const int32& ViewDistance, const int32& AntiAliasing,
		const int32& Shadows, const int32& GlobalIllumination, const int32& Reflections, const int32& PostProcess, const int32& Textures,
		const int32& Effects, const int32& Foliage, const int32& Shading);

	/**
	 * Returns the game user settings: resolution, window mode, vsync, frame rate limit, dynamic resolution and scalability.
	 */
	UFUNCTION(meta = (AICallable), Category = "Console")
	static FRuntimeUserSettings GetUserSettings();
};
