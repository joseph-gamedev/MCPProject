#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class SWidget;
class UGameViewportClient;

/**
 * Game-side feedback while an agent works:
 *  - an activity panel (top-left): pulsing dot, the agent's status line, current step, step count, elapsed time
 *    and the last few actions; it shows "Done" briefly and hides when idle.
 *  - distance-based fly speed: the player's flying pawn speeds up far from geometry and slows down close to it.
 */
class FRuntimeToolsetsActivity
{
public:
	static FRuntimeToolsetsActivity& Get();

	void Startup();
	void Shutdown();

	/** Status line set by the agent (ui.set_status). Empty clears it. */
	void SetStatus(const FString& Text);

	struct FFlySpeed
	{
		bool bAuto = true;
		double Multiplier = 1.0;
		double MinSpeed = 300.0;
		double MaxSpeed = 80000.0;
		double FixedSpeed = 1200.0;
	};
	FFlySpeed& FlySpeed() { return Fly; }
	double GetCurrentFlySpeed() const { return CurrentSpeed; }

	bool IsPanelEnabled() const { return bPanelEnabled; }
	void SetPanelEnabled(bool bEnabled) { bPanelEnabled = bEnabled; }

private:
	void OnActivity(const FString& Tool, bool bStarted, bool bOk, double Seconds);
	bool Tick(float DeltaTime);
	void TickFlySpeed(float DeltaTime);
	void EnsureOverlay();

	FText DotText() const;
	FText HeadlineText() const;
	FText StatusText() const;
	FText FeedText() const;
	FSlateColor DotColor() const;
	float PanelOpacity() const;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle ActivityHandle;
	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;

	FFlySpeed Fly;
	double CurrentSpeed = 0.0;

	bool bPanelEnabled = false; // off unless the agent is asked to show it (ui.show_activity)
	FString Status;
	FString CurrentTool;
	int32 InFlight = 0;
	int32 Steps = 0;
	int32 Errors = 0;
	double RunStart = 0.0;
	double LastActivity = -1000.0;
	double Now = 0.0;
	TArray<FString> Feed;
};
