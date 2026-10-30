#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class SVerticalBox;
class SWidget;
class UGameViewportClient;
class IInputProcessor;
struct FRuntimeToolTrace;

/**
 * Debug layer: everything the tools do, live, on top of the game. Off by default; Shift+Alt+Z (or the command line
 * -RuntimeToolsetsDebug) shows it. While a call runs it shows which one and for how long, and between calls that the
 * agent is thinking, so a slow build never looks like nothing is happening. Records even while hidden.
 */
class FRuntimeDebugLayer
{
public:
	static FRuntimeDebugLayer& Get();

	void Startup();
	void Shutdown();
	void Toggle();
	bool IsShown() const { return bShown; }

	/** One call (nested calls have Depth > 0). */
	struct FEntry
	{
		FString Tool;
		FString Session;
		int32 Depth = 0;
		/** Indent on screen: wrapper calls (session_call_tool) are hidden, their inner call takes their place. */
		int32 Indent = 0;
		bool bWrapper = false;
		double StartedAt = 0.0;
		FDateTime Wall;
		double Seconds = 0.0;
		/** The agent's thinking before this call (pause since the previous call ended); -1 for inner calls. */
		double Think = -1.0;
		bool bRunning = true;
		bool bOk = true;
		FString Args;
		FString Detail; // result summary, or the error and its hint
	};

private:
	void OnTrace(const FRuntimeToolTrace& Trace);
	bool Tick(float DeltaTime);
	void EnsureWidget();
	void RebuildRows();
	void RefreshStats();
	/** While the layer is shown the mouse cursor is free (to drag, resize and scroll); hiding gives the game its mouse back. */
	void SetFreeCursor(bool bFree);

	FText StatusText() const;
	FText BuildTimerText() const;
	bool AnyRunning(const TSharedPtr<FEntry>& Except) const;
	FSlateColor StatusColor() const;

	TArray<TSharedPtr<FEntry>> Entries; // oldest first
	int32 Version = 0;
	int32 BuiltVersion = -1;
	bool bShown = false;

	int32 Calls = 0;
	int32 Errors = 0;
	double ToolSeconds = 0.0;
	double LastCallEnd = 0.0;
	double FirstCall = 0.0;
	// the current (or last) build: calls without a long pause between them
	double RunStart = 0.0;
	int32 RunCalls = 0;
	int32 RunErrors = 0;
	double RunThink = 0.0;
	double RunTools = 0.0;

	// refreshed twice a second while shown
	FString ObjectsText = TEXT("-");
	FString ActorsText = TEXT("-");
	FString MapText = TEXT("-");
	FString PerfLine;
	double NextStats = 0.0;
	float SmoothedFrameMs = 16.f;

	FVector2D PanelPosition = FVector2D(-1.0, -1.0); // first show: top right
	FVector2D PanelSize = FVector2D(720.0, 620.0);
	bool bCursorFreed = false;
	bool bSavedShowCursor = false;
	TWeakObjectPtr<class APlayerController> CursorController;

	TSharedPtr<SWidget> Widget;
	TSharedPtr<SVerticalBox> Rows;
	TWeakObjectPtr<UGameViewportClient> WidgetViewport;
	TSharedPtr<IInputProcessor> Input;
	FDelegateHandle TraceHandle;
	FTSTicker::FDelegateHandle TickHandle;
};
