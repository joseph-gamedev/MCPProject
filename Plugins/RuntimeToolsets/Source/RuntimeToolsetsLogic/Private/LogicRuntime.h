#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "LogicExpr.h"

class SWidget;
class ACameraActor;
class ACharacter;
class APawn;
class APlayerController;
struct FRuntimeSceneNode;
class UWorld;
class UGameViewportClient;

/** "When <event> [matching target/key/name] and <condition>, run <actions>." */
struct FLogicRule
{
	FString Name;
	FString On;        // start, click, right_click, key, button, timer, var_changed, event, tick, touch, fall
	FString Target;    // click/touch: node id / name / path or "tag:<tag>"
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
	/** kind "bar": value / max expressions, fill color, delayed ghost bar. */
	FString Value;
	FString MaxValue;
	FLinearColor Fill = FLinearColor(0.85f, 0.15f, 0.12f, 1.f);
	bool bGhost = true;

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
	/** Platformer player (spawn_player args); null when there is none. */
	TSharedPtr<FJsonObject> PlayerDef;
	/** Runtime copy of PlayerDef that rules can change ({"player":"set"}); reset on start. */
	TSharedPtr<FJsonObject> PlayerLive;
	const TSharedPtr<FJsonObject>& PD() const { return PlayerLive.IsValid() ? PlayerLive : PlayerDef; }
	/** Spawners, loot tables and interactables. */
	TMap<FString, TSharedPtr<FJsonObject>> SpawnerDefs;
	TMap<FString, TSharedPtr<FJsonObject>> LootDefs;
	TMap<FString, TSharedPtr<FJsonObject>> InteractDefs;
	/** Node id -> motion {type, axis, distance, speed, points, loop, spin, amplitude, period, face, origin, origin_rot}. */
	TMap<FString, TSharedPtr<FJsonObject>> Motions;
	/** Node id -> blocks movement. */
	TMap<FString, bool> Solid;
	/** Pool name -> {template: node id, size}. */
	TMap<FString, TSharedPtr<FJsonObject>> Pools;
	/** Combat definitions. Health/hit zones keyed by target spec ("player", "tag:enemy", node id). */
	TMap<FString, TSharedPtr<FJsonObject>> HealthDefs;
	TMap<FString, TSharedPtr<FJsonObject>> HitZoneDefs;
	TMap<FString, TSharedPtr<FJsonObject>> WeaponDefs;
	/** Owner spec ("player" or target spec) -> weapon name. */
	TMap<FString, FString> EquipDefs;
	/** AI brains: name -> {states, start, vision, hearing, ...}; target spec -> {brain, patrol, wander_radius}. */
	TMap<FString, TSharedPtr<FJsonObject>> BrainDefs;
	TMap<FString, TSharedPtr<FJsonObject>> BrainAssign;
	/** In-world HUD: enemy bars, damage numbers, alert icons, hit flash. */
	TSharedPtr<FJsonObject> HudDef;
	/** Feedback presets: name -> {steps:[{type, delay, ...}]}. */
	TMap<FString, TSharedPtr<FJsonObject>> FeedbackDefs;

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

	// ---- play: player, touch, motion
	bool SpawnPlayer(FString& OutError);
	void RemovePlayer();
	void RespawnPlayer();
	bool HasPlayer() const;
	ACharacter* GetPlayer() const;
	void ApplySolid();
	bool HideNode(const FString& Id, bool bHide);
	/** Resolves "n12", "a/b", a node name or "tag:x" (all nodes with that tag). */
	bool ResolveNodes(const TArray<FString>& Targets, TArray<FString>& OutIds, FString& OutError) const;
	bool MakeTouchEvent(const FString& NodeId, bool bFromAbove, bool bFromBelow, FLogicEvent& Out) const;
	TSharedRef<FJsonObject> PlayerJson() const;

	// ---- combat
	bool ApplyDamage(const FString& Victim, double Amount, const FString& Type, const FString& Instigator, const FVector& Push);
	void Heal(const FString& Victim, double Amount);
	/** Fires Owner's weapon (or the named one) toward AimPoint / along AimDir. */
	bool FireWeapon(const FString& Owner, const FString& WeaponName, const FVector* AimPointIn, FString& OutError);
	void RebuildHealths(bool bReset);
	int32 CallWorldFunction(const FString& Name, TArray<FLogicValue>& Args, FLogicValue& Out, FString& OutError) const;
	bool OwnerLocation(const FString& Key, FVector& Out) const;
	FLogicValue HealthMap() const;
	FLogicValue AmmoMap() const;
	FString EquippedWeapon(const FString& OwnerKey) const;
	bool IsTopDown() const;
	bool IsFlight() const;
	bool IsGrid() const;
	/** Best target for a homing weapon in front of Owner (within range and angle). */
	FString LockTarget(const FString& Owner, const FVector& From, const FVector& Dir, double Range, double AngleDeg, const FString& Hits) const;
	void ResetAgentAt(const FString& Id, const FVector& At, const FRotator& Rot);
	void ResetHealth(const FString& Id);
	void DropLoot(const FString& Name, const FVector& At);
	bool SaveSlot(const FString& Slot, const TArray<FString>& Names, FString& OutError) const;
	bool LoadSlot(const FString& Slot, FString& OutError);
	void SetSpawnerActive(const FString& Name, bool bActive, bool bReset);
	FString CurrentInteract;
	FVector FlightVelocity = FVector::ZeroVector;
	double FlightSpeed = 0.0;
	int32 NumProjectiles() const { return Projectiles.Num(); }

	// ---- AI brains and game feel
	static bool ValidateBrain(const TSharedPtr<FJsonObject>& Def, FString& OutError);
	bool CreateBrainVisuals(const FString& NodeId, const TSharedPtr<FJsonObject>& Brain, const FString& Session, FString& OutError);
	void ResetBrains();
	void HearNoise(const FVector& At, double Radius);
	/** Bounds of an actor and its visible attachments, ignoring effect helpers (vision cones, held guns). */
	static FBox GameBox(const AActor* Actor);
	struct FFloatText
	{
		FVector World = FVector::ZeroVector;
		FString Text;
		FLinearColor Color = FLinearColor::White;
		double Age = 0.0;
	};
	struct FBrainAgent
	{
		FString Node;
		FString Brain;
		FString State;
		double TimeInState = 0.0;
		FVector Home = FVector::ZeroVector;
		double Yaw = 0.0;
		TArray<FVector> Patrol;
		int32 PatrolIndex = 0;
		int32 PatrolDir = 1;
		FVector LastKnown = FVector::ZeroVector;
		bool bHasLastKnown = false;
		double SinceSeen = 1e9;
		bool bSees = false;
		bool bHeard = false;
		bool bAtGoal = false;
		bool bMoving = false;
		double Phase = 0.0;
		double Roll = 0.0;
		FVector Wander = FVector::ZeroVector;
		double NextWander = 0.0;
		double NextPerceive = 0.0;
		double NextDecide = 0.0;
		FString Cone;
		FString Icon;
		FLinearColor ConeColor = FLinearColor::Black;
		FQuat Rot = FQuat::Identity;
		double Speed = 0.0;
		double Bank = 0.0;
		double OrbitPhase = 0.0;
		/** backend "ai": real AI agent (navmesh movement, AI perception). */
		bool bAiTried = false;
		FVector AiGoal = FVector(1e9);
	};
	const TMap<FString, FBrainAgent>& GetAgents() const { return Agents; }
	void PlayFeedback(const FString& Name, const FVector& At, const FString& Target);
	void TickFeedback(double RealDt);
	FVector ShakeOffset() const;
	double TimeScale() const { return SlowmoLeft > 0.0 ? SlowmoScale : 1.0; }
	struct FPendingFeedback
	{
		TSharedPtr<FJsonObject> Step;
		FVector At = FVector::ZeroVector;
		FString Target;
		double Delay = 0.0;
	};
	friend class SLogicHud;

	// ---- tweens and pools
	static const TArray<FString>& EaseNames();
	static double Ease(const FString& Name, double T);
	static bool ValidateTween(const TSharedPtr<FJsonObject>& Spec, FString& OutError);
	bool StartTween(const FString& NodeId, const TSharedPtr<FJsonObject>& Spec, FString& OutError);
	void TickTweens(double Dt);
	bool HasTweens() const { return Tweens.Num() > 0; }
	int32 NumTweens() const { return Tweens.Num(); }
	bool IsTweening(const FString& NodeId) const;
	bool EnsurePool(const FString& Name, FString& OutError);
	FString AcquireFromPool(const FString& Name, const FVector& At, FString& OutError);
	void ReleaseToPool(const FString& NodeId);
	int32 PoolFree(const FString& Name) const;

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
	friend class FLogicHudAccess;
	void TickPlay(double Dt);
	void TickPlayerInput(APlayerController* PC);
	void ApplyPlayerInputMode(APlayerController* PC);
	void TickMotion();
	void TickTouch();
	void TickCamera(double Dt, bool bSnap);
	bool IsHidden(const FString& NodeId) const;
	static bool DirectMatch(const FRuntimeSceneNode& Node, const FString& Target, const class FRuntimeSceneGraph& Graph);
	void ResetPlay();
	struct FTween
	{
		FString NodeId;
		TSharedPtr<FJsonObject> Spec;
		double Duration = 0.3;
		double Delay = 0.0;
		double Elapsed = 0.0;
		FString EaseName = TEXT("out_quad");
		int32 Cycles = 1; // -1 forever
		bool bYoyo = false;
		bool bForward = true;
		bool bStarted = false;
		FString Then;
		FString Emit;
		bool bLoc = false, bRot = false, bScale = false, bColor = false;
		FVector FromLoc = FVector::ZeroVector, ToLoc = FVector::ZeroVector, FromScale = FVector::OneVector, ToScale = FVector::OneVector;
		FRotator FromRot = FRotator::ZeroRotator, ToRot = FRotator::ZeroRotator;
		FLinearColor FromColor = FLinearColor::White, ToColor = FLinearColor::White;
	};
	void ActivateTween(FTween& T, AActor* A);
	void ApplyTween(const FTween& T, AActor* A, double Alpha);
	void ResetTweens();
	void ResetPools();
	void TickPlayerAnim(double Dt);
	struct FHealthState
	{
		double Max = 10.0;
		double Current = 10.0;
		double InvulnUntil = -1.0;
		bool bDead = false;
		TSharedPtr<FJsonObject> Def;
	};
	struct FWeaponState
	{
		double NextShot = 0.0;
		int32 Mag = -1;
		double ReloadUntil = -1.0;
		int32 ReloadAmount = 0;
		int32 BurstLeft = 0;
		double NextBurst = 0.0;
		FVector BurstAim = FVector::ZeroVector;
	};
	struct FProjectile
	{
		FString Node;
		FVector Pos = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		double Speed = 2000.0;
		double Life = 2.0;
		double Damage = 1.0;
		double Knockback = 0.0;
		double Radius = 12.0;
		FString Type;
		FString Owner;
		FString Hits;
		double Homing = 0.0;
		FString Target;
	};
	void TickCombat(double Dt);
	void TickProjectiles(double Dt);
	void TickHitZones();
	void TickWeapons();
	void TickFlight(double Dt);
	void TickFlightAgent(FBrainAgent& Ag, AActor* A, const TSharedPtr<FJsonObject>& Brain, const TSharedPtr<FJsonObject>& St, double Dt,
		const FVector& PlayerLoc, bool bPlayerAlive);
	void TickSpawners(double Dt);
	void TickInteract();
	void ResetExtras();
	struct FSpawnerState
	{
		bool bActive = false;
		int32 Wave = 0;
		int32 SpawnedInWave = 0;
		int32 SpawnedTotal = 0;
		double Next = 0.0;
		bool bWaitingNextWave = false;
		TArray<FString> Alive;
	};
	TMap<FString, FSpawnerState> Spawners;
	TSet<FString> InteractUsed;
	TMap<FString, double> InteractCooldown;
	// flight input (set by PollInput, integrated in TickFlight)
	double FlightPitchIn = 0.0, FlightYawIn = 0.0, FlightRollIn = 0.0, FlightThrottle = 0.6;
	bool bFlightBoost = false;
	// dash and grid
	double DashReadyAt = 0.0;
	double DashUntil = 0.0;
	FVector GridFrom = FVector::ZeroVector, GridTo = FVector::ZeroVector;
	double GridT = 1.0;
	double GridNextInput = 0.0;
	void TickBrains(double Dt);
	void TickFeel(double Dt);
	void ChangeBrainState(FBrainAgent& Agent, const FString& To);
	bool BrainSteer(FBrainAgent& Agent, AActor* A, const FVector& Goal, double Speed, double Dt);
	// Real AI backend (LogicAi.cpp): brains with backend "ai" drive an AI controller on the navmesh and use AI perception.
	class AAIController* EnsureAiAgent(FBrainAgent& Agent, AActor* A, const TSharedPtr<FJsonObject>& Brain);
	void PerceiveAi(FBrainAgent& Agent, class AAIController* Controller);
	bool MoveAi(FBrainAgent& Agent, class AAIController* Controller, AActor* Body, const FVector& Goal, bool bHasGoal, double Speed, const FString& Face, const FVector* FaceTarget);
	void ReportAiNoise(const FVector& At, double Radius, AActor* Instigator, const TCHAR* Tag);
	/** "player" or a node id -> actor. */
	AActor* KeyActor(const FString& Key) const;
	void ResetCombat();
	void SpawnShots(const FString& Owner, const TSharedPtr<FJsonObject>& W, const FVector& Muzzle, const FVector& Dir);
	bool VictimMatches(const FString& Key, const FString& Filter) const;
	bool VictimBox(const FString& Key, FBox& Out) const;
	void CollectOwnerActors(const FString& Owner, TArray<AActor*>& Out) const;
	bool StartReload(const FString& Owner, const TSharedPtr<FJsonObject>& W, FWeaponState& S);
	struct FLimb
	{
		TWeakObjectPtr<AActor> Actor;
		int32 Kind = 0; // 0 left leg, 1 right leg, 2 left arm, 3 right arm
		double Pitch = 0.0;
	};

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

	TWeakObjectPtr<ACharacter> Player;
	TWeakObjectPtr<AActor> PlayerBody;
	FString PlayerBodyNode;
	TArray<FString> PlayerBodyNodes;
	TArray<FLimb> Limbs;
	double AnimPhase = 0.0;
	double Squash = 0.0;
	double Invulnerable = 0.0;
	/** Player feet height on the previous frame (stomp detection). */
	double PrevFeetZ = TNumericLimits<double>::Max();
	bool bWasGrounded = true;
	TArray<FTween> Tweens;
	TMap<FString, FTransform> TweenOrigins;
	TMap<FString, TArray<FString>> PoolMembers;
	TMap<FString, FString> PoolOf;
	TSet<FString> PoolBusy;
	TArray<FString> PoolOrder;
	bool bOwnBody = false;
	TWeakObjectPtr<ACameraActor> PlayerCamera;
	TWeakObjectPtr<APawn> PreviousPawn;
	FVector SpawnPoint = FVector::ZeroVector;
	FVector Checkpoint = FVector::ZeroVector;
	FVector CamPos = FVector::ZeroVector;
	double PlayTime = 0.0;
	float Facing = 1.f;
	bool bFrozen = false;
	bool bFell = false;
	TSet<FString> Touching;
	/** Node id -> play time it was last touched (no re-touch within 0.35 s, e.g. a bumped block bouncing back). */
	TMap<FString, double> LastTouch;
	TMap<FString, FHealthState> Healths;
	TMap<FString, FWeaponState> WeaponStates;
	TArray<FProjectile> Projectiles;
	TMap<FString, TMap<FString, double>> ZoneTimes;
	TMap<FString, FBrainAgent> Agents;
	TArray<FFloatText> FloatTexts;
	double HitFlash = 0.0;
	FLinearColor FlashColor = FLinearColor(0.9f, 0.05f, 0.02f);
	TArray<FPendingFeedback> PendingFeedback;
	double ShakeTime = 0.0;
	double ShakeAmp = 0.0;
	double SlowmoLeft = 0.0;
	double SlowmoScale = 1.0;
	double Recoil = 0.0;
	FString GunNode;
	mutable TMap<FString, double> GhostValues;
	FVector AimPoint = FVector::ZeroVector;
	FVector AimDir = FVector(1, 0, 0);
	TSet<FString> Hidden;

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
