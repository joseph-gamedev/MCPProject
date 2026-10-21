#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "LogicExpr.h"

class SWidget;
class UWorld;
class UGameViewportClient;

/** "When <event> [matching target/key/name] and <condition>, run <actions>." */
struct FLogicRule
{
	FString Name;
	FString On;        // start, click, right_click, key, button, timer, var_changed, event, tick
	FString Target;    // click: node id / name / path or "tag:<tag>" (matches the clicked node or an ancestor)
	FString Key;       // key: key name ("R", "SpaceBar", "One")
	FString Filter;    // button / timer / var_changed / event: its name
	FString Condition; // optional expression
	TArray<TSharedPtr<FJsonValue>> Actions;
	bool bEnabled = true;

	TSharedRef<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& Json, FLogicRule& Out, FString& OutError);
};

/** On-screen button or text whose text is a template ("Turn: {turn}"). */
struct FLogicUiItem
{
	FString Name;
	FString Kind = TEXT("text"); // text | button
	FString Text;
	FVector2D Anchor = FVector2D(0.5, 0.06);
	FVector2D Size = FVector2D::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	FLinearColor Background = FLinearColor(0.f, 0.f, 0.f, 0.5f);
	int32 FontSize = 22;
	FString VisibleIf;

	TSharedRef<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& Json, FLogicUiItem& Out, FString& OutError);
};

struct FLogicEvent
{
	FString On;
	FString Name;    // key / button / timer / variable / custom event name
	FString Target;  // clicked node id
	FLogicValue Data = FLogicValue::MakeMap();
};

/** Game logic of one world: definitions (undoable, exportable) and the running state. */
class FLogicWorld : public TSharedFromThis<FLogicWorld>
{
public:
	explicit FLogicWorld(UWorld* InWorld);
	~FLogicWorld();

	// ---- definitions
	TMap<FString, FLogicValue> InitialVars;
	TArray<FLogicRule> Rules;
	TArray<FLogicUiItem> Ui;
	TMap<FString, FLogicValue> NodeData;
	FString InputMode = TEXT("game");
	bool bLockCamera = false;

	TSharedRef<FJsonObject> ExportDefinitions() const;
	bool ImportDefinitions(const TSharedPtr<FJsonObject>& Json, FString& OutError);
	/** Checks the expressions and action shapes of a rule. */
	static bool ValidateRule(const FLogicRule& Rule, FString& OutError);

	// ---- running state
	TMap<FString, FLogicValue> Vars;
	bool IsRunning() const { return bRunning; }
	/** Resets variables, removes objects spawned during play, clears timers, then fires "start". */
	void Start();
	void Stop();
	/** Queues an event and processes the queue. Returns the rules that ran and any errors. */
	TSharedRef<FJsonObject> Fire(FLogicEvent&& Event);
	void SetVar(const FString& Name, const FLogicValue& Value, bool bNotify);
	/** Advances timers (and fires tick rules). */
	void Step(double Seconds);
	/** Mouse clicks and keys of the local player. */
	void PollInput();
	void ApplyInputMode();
	FLogicValue Evaluate(const FString& Expr, FString& OutError);
	TSharedRef<FJsonObject> GetState(int32 LogLimit) const;
	/** Click event for an actor (the node that owns it), as the input poller builds it. */
	bool MakeClickEvent(AActor* Actor, const FVector& Location, bool bRight, FLogicEvent& Out) const;
	bool MakeClickEventForNode(const FString& NodeId, bool bRight, FLogicEvent& Out) const;

	// ---- UI overlay
	void RefreshUi();
	void RemoveUi();
	void SetUiOverride(const FString& Name, const FString* Text, const bool* bVisible);

	UWorld* GetWorld() const { return World.Get(); }

private:
	struct FTimer
	{
		FString Name;
		double Remaining = 0.0;
		double Interval = 0.0;
		bool bRepeat = false;
	};

	struct FFrame
	{
		const FLogicEvent* Event = nullptr;
		FLogicValue EventValue;
		TMap<FString, FLogicValue> Locals;
		FString Rule;
		bool bStop = false;
		TArray<FString>* Logs = nullptr;
	};

	bool Matches(const FLogicRule& Rule, const FLogicEvent& Event) const;
	bool NodeMatches(const FString& NodeId, const FString& Target) const;
	FLogicScope MakeScope(FFrame& Frame);
	bool RunActions(const TArray<TSharedPtr<FJsonValue>>& Actions, FFrame& Frame, int32& Budget, FString& OutError);
	bool RunAction(const TSharedPtr<FJsonObject>& Action, FFrame& Frame, int32& Budget, FString& OutError);
	TSharedPtr<FJsonValue> Template(const TSharedPtr<FJsonValue>& Value, FFrame& Frame, FString& OutError);
	bool EvalField(const TSharedPtr<FJsonObject>& Action, const TCHAR* Field, FFrame& Frame, FLogicValue& Out, FString& OutError);
	void ProcessQueue(TSharedRef<FJsonObject>* OutReport);
	void ClearPlayObjects();
	void AddLog(const FLogicEvent& Event, const TArray<FString>& RulesRun, const TArray<FString>& Errors);
	FLogicValue FindNodeData(const FString& NodeId) const;

	TWeakObjectPtr<UWorld> World;
	bool bRunning = false;
	bool bProcessing = false;
	bool bRestartPending = false;
	TArray<FTimer> Timers;
	TArray<FLogicEvent> Queue;
	TArray<FString> PlayNodes;
	/** Values carried over a restart ({"restart":true,"keep":[...]}). */
	TMap<FString, FLogicValue> KeepOnRestart;
	TArray<TSharedPtr<FJsonObject>> Log;
	int64 EventCount = 0;
	FRandomStream Random;
	double Time = 0.0;

	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;
	TMap<FString, FString> TextOverrides;
	TMap<FString, bool> VisibleOverrides;
};

/** One FLogicWorld per world; ticks the running ones. */
class FLogicManager
{
public:
	static FLogicManager& Get();
	void Startup();
	void Shutdown();
	FLogicWorld& For(UWorld* World);
	FLogicWorld* Find(UWorld* World);

	static const TCHAR* Session() { return TEXT("logic"); }

private:
	bool Tick(float DeltaTime);
	TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FLogicWorld>> Worlds;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle CleanupHandle;
};
