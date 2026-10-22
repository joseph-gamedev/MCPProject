#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeLogicToolset.generated.h"

class AActor;

// Enum values are shown in Epic's PascalCase; the old lower-case spellings ("out_quad", "right_click", "topdown") are
// accepted too, and definitions store them lower-case (that is what rules, logic.export and the guide use).

/** How the mouse behaves while the game runs. */
UENUM()
enum class ELogicInputMode : uint8
{
	/** Visible cursor; clicks hit objects and buttons (board and puzzle games). */
	Cursor,
	/** Normal play: mouse-look, movement. */
	Game,
};

/** Simulated input for logic.fire_event. */
UENUM()
enum class ELogicEventKind : uint8
{
	/** Left click on a node (needs target). */
	Click,
	/** Right click on a node (needs target). */
	RightClick,
	/** Key press (needs key). */
	Key,
	/** UI button press (needs name). */
	Button,
	/** Timer firing (needs name). */
	Timer,
	/** Custom event, as {"emit": name} sends it (needs name). */
	Event,
	/** The player touching a node (needs target; data {from_above, from_below}). */
	Touch,
	/** The player falling below kill_z. */
	Fall,
	/** The interact key at an interactable (target). */
	Interact,
	/** A flight player crashing into the ground. */
	Crash,
};

/** Easing curve of a tween. */
UENUM()
enum class ELogicEase : uint8
{
	Linear,
	InQuad,
	OutQuad,
	InOutQuad,
	InCubic,
	OutCubic,
	InOutCubic,
	InSine,
	OutSine,
	InOutSine,
	InBack,
	OutBack,
	InOutBack,
	OutElastic,
	InBounce,
	OutBounce,
};

/** Repetition of a tween. */
UENUM()
enum class ELogicTweenLoop : uint8
{
	/** Play once. */
	Once,
	/** There and back, `repeat` times. */
	Yoyo,
	/** Restart from the beginning, `repeat` times (-1 = forever). */
	Loop,
	/** There and back forever. */
	Pingpong,
};

/** What happens to a tweened node at the end. */
UENUM()
enum class ELogicTweenEnd : uint8
{
	None,
	Hide,
	Show,
	/** Return the node to its pool (logic.define_pool). */
	Release,
};

/** What happens when health reaches 0. */
UENUM()
enum class ELogicDeathAction : uint8
{
	Hide,
	/** Flatten, then hide. */
	Squash,
	/** Return to its pool (spawner enemies). */
	Release,
	/** Back to the start or the last checkpoint (the player's default). */
	Respawn,
	/** Stop the player. */
	Freeze,
	/** Nothing (rules on "died" decide). */
	None,
};

/** When a hit zone deals damage. */
UENUM()
enum class ELogicHitZoneMode : uint8
{
	/** Once each time something enters it. */
	Enter,
	/** Every `interval` seconds while something is inside. */
	Stay,
};

UENUM()
enum class ELogicWeaponMode : uint8
{
	/** Pooled bullets that fly (and can home). */
	Projectile,
	/** Instant ray. */
	Hitscan,
};

UENUM()
enum class ELogicSpreadMode : uint8
{
	/** Random direction within the spread. */
	Random,
	/** Shots spread evenly (shotgun fan). */
	Fan,
};

/** Kind of playable character. */
UENUM()
enum class ELogicPlayerMode : uint8
{
	/** Side-scroller: the level runs along +X, the camera looks from +Y. */
	Side,
	/** WASD on the ground, aim with the mouse, overhead camera. */
	Topdown,
	/** Arcade jet with a chase camera. */
	Flight,
};

/** Movement of a node while the game runs. */
UENUM()
enum class ELogicMotionType : uint8
{
	/** Only spin (if set). */
	None,
	/** Back and forth along an axis. */
	Patrol,
	/** Through points relative to the start. */
	Path,
	/** Up and down. */
	Bob,
};

// ---- Input structs ---------------------------------------------------------------------------------------------------

/** Look of the default bullet a weapon creates when it has no pool. */
/** How a brain moves and senses. */
UENUM()
enum class ELogicBrainBackend : uint8
{
	/** Built-in steering and vision cone. */
	Native,
	/** Unreal AI: AI controller on the navmesh, AI perception. */
	Ai,
};

USTRUCT()
struct FLogicBulletLook
{
	GENERATED_BODY()

	/** Diameter in cm. */
	UPROPERTY()
	float Size = 24.f;

	/** Glow color. */
	UPROPERTY()
	FLinearColor Color = FLinearColor(1.f, 0.8f, 0.2f);
};

/** What an AI brain sees. Without vision a brain is blind (it can still hear). */
USTRUCT()
struct FLogicBrainVision
{
	GENERATED_BODY()

	/** View distance in cm. */
	UPROPERTY()
	float Radius = 1200.f;

	/** Cone angle in degrees (total, not half). */
	UPROPERTY()
	float Angle = 90.f;

	/** Eye height above the pivot in cm. */
	UPROPERTY()
	float EyeHeight = 100.f;

	/** Always notice the player within this distance in cm, whatever the cone (default: the brain.near_notice tuning value). */
	UPROPERTY()
	float Near = 180.f;

	/** Draw the vision cone (yellow calm, orange searching, red seeing the player). */
	UPROPERTY()
	bool bShowCone = true;

	/** Cone opacity. */
	UPROPERTY()
	float Opacity = 0.22f;
};

/** What an AI brain hears. */
USTRUCT()
struct FLogicBrainHearing
{
	GENERATED_BODY()

	/** Hears the player's shots within this distance in cm (0 = deaf). */
	UPROPERTY()
	float Radius = 0.f;
};

/** Makes a brain fly in 3D (enemy jets) instead of walking. */
USTRUCT()
struct FLogicBrainFlight
{
	GENERATED_BODY()

	/** Cruise speed in cm/s (a state's speed overrides it). */
	UPROPERTY()
	float Speed = 4000.f;

	/** Turn rate in degrees per second (a state's turn_rate overrides it). */
	UPROPERTY()
	float TurnRate = 55.f;

	/** Pull up below this height in cm (default: 2500 below the start). */
	UPROPERTY()
	float MinAltitude = 0.f;

	/** Radius of the patrol orbit in cm. */
	UPROPERTY()
	float OrbitRadius = 4000.f;
};

/** Patrol route of an agent, relative to where it stands. Either an axis and distance, or points. */
USTRUCT()
struct FLogicPatrolRoute
{
	GENERATED_BODY()

	/** Direction to walk back and forth along. */
	UPROPERTY()
	FVector Axis = FVector(1.0, 0.0, 0.0);

	/** Distance along the axis in cm. */
	UPROPERTY()
	float Distance = 600.f;

	/** Waypoints [dx,dy,dz] relative to the start (overrides axis/distance). */
	UPROPERTY()
	TArray<FVector> Points;
};

/** A dash ability for the player (fires the "dash" event). */
USTRUCT()
struct FLogicDash
{
	GENERATED_BODY()

	/** Launch speed in cm/s. */
	UPROPERTY()
	float Speed = 2200.f;

	/** Seconds the dash lasts. */
	UPROPERTY()
	float Duration = 0.18f;

	/** Seconds before the next dash. */
	UPROPERTY()
	float Cooldown = 0.8f;

	/** Key (default: SpaceBar top-down, LeftControl side view). */
	UPROPERTY()
	FString Key;

	/** Immune to damage while dashing. */
	UPROPERTY()
	bool bInvulnerable = false;
};

/** Movement of nodes while the game runs (reset on restart). */
USTRUCT()
struct FLogicMotion
{
	GENERATED_BODY()

	UPROPERTY()
	ELogicMotionType Type = ELogicMotionType::None;

	/** Patrol: direction. */
	UPROPERTY()
	FVector Axis = FVector(1.0, 0.0, 0.0);

	/** Patrol: distance in cm. */
	UPROPERTY()
	float Distance = 400.f;

	/** Patrol and path: speed in cm/s. */
	UPROPERTY()
	float Speed = 150.f;

	/** Path: points [dx,dy,dz] relative to the start, at least 2. */
	UPROPERTY()
	TArray<FVector> Points;

	/** Path: loop back to the first point instead of going back and forth. */
	UPROPERTY()
	bool bLoop = false;

	/** Bob: height in cm. */
	UPROPERTY()
	float Amplitude = 30.f;

	/** Bob: seconds per cycle. */
	UPROPERTY()
	float Period = 2.f;

	/** Spin in degrees per second [pitch, yaw, roll]; works with any type. */
	UPROPERTY()
	FRotator Spin = FRotator::ZeroRotator;

	/** Turn around (yaw 180) when moving backwards. */
	UPROPERTY()
	bool bFace = false;

	/** Time offset in seconds (so a row of enemies does not move in sync). */
	UPROPERTY()
	float Offset = 0.f;

	/** Side-to-side tilt while moving: [degrees, cycles per second]. */
	UPROPERTY()
	FVector2D Waddle = FVector2D::ZeroVector;
};

/** One wave of a spawner. */
USTRUCT()
struct FLogicSpawnerWave
{
	GENERATED_BODY()

	/** Enemies in the wave. */
	UPROPERTY()
	int32 Count = 3;

	/** Seconds between spawns in this wave: n or [min, max] (default: the spawner's interval). */
	UPROPERTY()
	FRuntimeToolJson Interval;

	/** At most this many alive (default: the spawner's max_alive). */
	UPROPERTY()
	int32 MaxAlive = 5;
};

/** One entry of a loot table. */
USTRUCT()
struct FLogicLootEntry
{
	GENERATED_BODY()

	/** Pool to drop from, or "none" for a chance of no drop. */
	UPROPERTY()
	FString Pool;

	/** Relative chance. */
	UPROPERTY()
	float Weight = 1.f;
};

// ---- Results ---------------------------------------------------------------------------------------------------------

USTRUCT()
struct FLogicVarsResult
{
	GENERATED_BODY()

	/** Variable name -> value. */
	UPROPERTY()
	FRuntimeToolJson Vars;
};

USTRUCT()
struct FLogicValueResult
{
	GENERATED_BODY()

	/** The value (number, text, bool, list, map or null). */
	UPROPERTY()
	FRuntimeToolJson Value;
};

USTRUCT()
struct FLogicEvalResult
{
	GENERATED_BODY()

	/** The value (number, text, bool, list, map or null). */
	UPROPERTY()
	FRuntimeToolJson Value;

	/** number, string, bool, list, map or null. */
	UPROPERTY()
	FString Type;
};

USTRUCT()
struct FLogicDefineRulesResult
{
	GENERATED_BODY()

	/** Names of the rules added or replaced, in the order given. */
	UPROPERTY()
	TArray<FString> Defined;

	/** Number of rules now defined. */
	UPROPERTY()
	int32 Rules = 0;
};

USTRUCT()
struct FLogicRuleList
{
	GENERATED_BODY()

	/** Rule definitions in run order: {name, on, target, key, filter, condition, actions, enabled}. */
	UPROPERTY()
	TArray<FRuntimeToolJson> Rules;
};

/** A scene node. */
USTRUCT()
struct FLogicNodeRef
{
	GENERATED_BODY()

	/** Node id (n12). */
	UPROPERTY()
	FString Id;

	/** Node name. */
	UPROPERTY()
	FString Name;
};

USTRUCT()
struct FLogicDefineUiResult
{
	GENERATED_BODY()

	/** Number of UI items now defined. */
	UPROPERTY()
	int32 Items = 0;

	/** Whether there is a game viewport to draw on (false in headless runs: the items still work in tests). */
	UPROPERTY()
	bool bOnScreen = false;
};

USTRUCT()
struct FLogicInputModeResult
{
	GENERATED_BODY()

	/** cursor or game. */
	UPROPERTY()
	FString Mode;

	UPROPERTY()
	bool bLockCamera = false;

	/** Whether a player controller exists to apply the mode to. */
	UPROPERTY()
	bool bHasPlayer = false;
};

USTRUCT()
struct FLogicTimerInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** Seconds until it fires. */
	UPROPERTY()
	double Remaining = 0.0;

	UPROPERTY()
	bool bRepeat = false;
};

USTRUCT()
struct FLogicSpawnerState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	/** Current wave, from 1. */
	UPROPERTY()
	int32 Wave = 0;

	/** Spawned enemies still alive. */
	UPROPERTY()
	int32 Alive = 0;

	/** Spawned since the start. */
	UPROPERTY()
	int32 Spawned = 0;
};

/** The playable character (logic.spawn_player). */
USTRUCT()
struct FLogicPlayerState
{
	GENERATED_BODY()

	/** Whether the character is in the world (false before {"player":"spawn"} with auto_spawn false, or without a player). */
	UPROPERTY()
	bool bExists = false;

	/** side, topdown or flight (empty without a player). */
	UPROPERTY()
	FString Mode;

	/** How to play, for telling the user. */
	UPROPERTY()
	FString Controls;

	/** Feet position. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	bool bGrounded = false;

	UPROPERTY()
	bool bFrozen = false;

	UPROPERTY()
	bool bInvulnerable = false;

	/** cm/s (flight: airspeed). */
	UPROPERTY()
	double Speed = 0.0;

	UPROPERTY()
	double Altitude = 0.0;

	/** Flight throttle 0-1. */
	UPROPERTY()
	double Throttle = 0.0;

	UPROPERTY()
	bool bBoost = false;

	UPROPERTY()
	bool bDashReady = false;

	/** Node the homing secondary weapon is locked on (empty if none). */
	UPROPERTY()
	FString Lock;

	/** Where the player respawns. */
	UPROPERTY()
	FVector Checkpoint = FVector::ZeroVector;

	/** Whether the local player controller possesses the character. */
	UPROPERTY()
	bool bControlled = false;
};

/** Running state of the game. */
USTRUCT()
struct FLogicGameState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bRunning = false;

	/** Game time in seconds since start. */
	UPROPERTY()
	double Time = 0.0;

	/** Variable name -> current value. */
	UPROPERTY()
	FRuntimeToolJson Vars;

	UPROPERTY()
	TArray<FLogicTimerInfo> Timers;

	/** "name (on)" per rule, in run order. */
	UPROPERTY()
	TArray<FString> Rules;

	/** UI item names. */
	UPROPERTY()
	TArray<FString> Ui;

	/** Objects created by rules during play (removed on restart). */
	UPROPERTY()
	int32 PlayObjects = 0;

	/** cursor or game. */
	UPROPERTY()
	FString InputMode;

	/** The player (exists false without one). */
	UPROPERTY()
	FLogicPlayerState Player;

	/** Node ids hidden by rules (shown again on restart). */
	UPROPERTY()
	TArray<FString> Hidden;

	/** Running tweens. */
	UPROPERTY()
	int32 Tweens = 0;

	/** Bullets in flight. */
	UPROPERTY()
	int32 Projectiles = 0;

	/** Spawner name -> state. */
	UPROPERTY()
	TMap<FString, FLogicSpawnerState> Spawners;

	/** Interactable the player can use now (empty if none). */
	UPROPERTY()
	FString Interact;

	/** Agent node id -> "state" or "state (sees)". */
	UPROPERTY()
	TMap<FString, FString> Brains;

	/** "player" or node id -> current health. */
	UPROPERTY()
	TMap<FString, double> Health;

	/** Weapon owner -> rounds left in the magazine. */
	UPROPERTY()
	TMap<FString, int32> Ammo;

	/** Pool name -> free copies. */
	UPROPERTY()
	TMap<FString, int32> PoolsFree;

	/** Recent events, oldest first: {seq, time, on, name, target, rules:[...], errors:[...]} ("errors" only when something failed). */
	UPROPERTY()
	TArray<FRuntimeToolJson> Log;
};

/** One processed event. */
USTRUCT()
struct FLogicEventReport
{
	GENERATED_BODY()

	UPROPERTY()
	FString On;

	/** Key, button, timer or custom event name. */
	UPROPERTY()
	FString Name;

	/** Node id for click/touch/interact events. */
	UPROPERTY()
	FString Target;

	/** Rules whose condition held and that ran. */
	UPROPERTY()
	TArray<FString> Rules;
};

USTRUCT()
struct FLogicFireReport
{
	GENERATED_BODY()

	/** The fired event and every event it caused (emit, var_changed...), in order. */
	UPROPERTY()
	TArray<FLogicEventReport> Events;

	/** Rule errors ("rule: message"); empty when everything ran. */
	UPROPERTY()
	TArray<FString> Errors;

	/** Lines written by {"log": ...} actions. */
	UPROPERTY()
	TArray<FString> Log;

	/** Variables after the events. */
	UPROPERTY()
	FRuntimeToolJson Vars;
};

USTRUCT()
struct FLogicExport
{
	GENERATED_BODY()

	/** The whole definition: vars, rules, ui, node data, input mode, player, combat, brains, pools... (logic.import takes it back). */
	UPROPERTY()
	FRuntimeToolJson Logic;
};

USTRUCT()
struct FLogicImportResult
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Rules = 0;

	UPROPERTY()
	int32 Vars = 0;
};

USTRUCT()
struct FLogicHealthResult
{
	GENERATED_BODY()

	/** "player" or node id -> current health. */
	UPROPERTY()
	TMap<FString, double> Health;
};

USTRUCT()
struct FLogicDamageResult
{
	GENERATED_BODY()

	/** False when the target was invulnerable, dead or has no health. */
	UPROPERTY()
	bool bApplied = false;

	/** "player" or node id -> current health. */
	UPROPERTY()
	TMap<FString, double> Health;
};

USTRUCT()
struct FLogicWeaponResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Weapon;

	/** Pool the bullets come from (a generated "bullets_<name>" pool unless one was given). */
	UPROPERTY()
	FString Pool;
};

USTRUCT()
struct FLogicBrainResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Brain;
};

USTRUCT()
struct FLogicAssignBrainResult
{
	GENERATED_BODY()

	/** Nodes that got the brain. */
	UPROPERTY()
	int32 Agents = 0;
};

USTRUCT()
struct FLogicTuningValue
{
	GENERATED_BODY()

	/** Current value. */
	UPROPERTY()
	double Value = 0.0;

	/** Built-in default. */
	UPROPERTY()
	double Default = 0.0;

	/** What it controls. */
	UPROPERTY()
	FString What;

	/** Where the default comes from (feel, scale, TDE...). */
	UPROPERTY()
	FString Basis;
};

USTRUCT()
struct FLogicTuning
{
	GENERATED_BODY()

	/** Name -> value, sorted by area. */
	UPROPERTY()
	TMap<FString, FLogicTuningValue> Tuning;

	UPROPERTY()
	FString Note;
};

USTRUCT()
struct FLogicTweenResult
{
	GENERATED_BODY()

	/** Nodes that started tweening. */
	UPROPERTY()
	int32 Nodes = 0;

	/** Tweens running in the world. */
	UPROPERTY()
	int32 ActiveTweens = 0;
};

USTRUCT()
struct FLogicPoolResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Pool;

	/** Copies ready to spawn. */
	UPROPERTY()
	int32 Free = 0;
};

USTRUCT()
struct FLogicNodesResult
{
	GENERATED_BODY()

	/** Nodes affected. */
	UPROPERTY()
	int32 Nodes = 0;
};

USTRUCT()
struct FLogicSolidResult
{
	GENERATED_BODY()

	/** Nodes affected. */
	UPROPERTY()
	int32 Nodes = 0;

	UPROPERTY()
	bool bSolid = false;
};

/**
 * Game logic without code: turns a built scene into a playable game with variables, event rules (when click / key /
 * button / timer / touch / start ... and a condition, run actions), on-screen UI, a playable character (side-scroller,
 * top-down or flight), health, damage zones, weapons, AI brains (state machines with vision and hearing), game HUD,
 * feedback presets, tweens, object pools, spawners, loot, interactables and saves.
 * Workflow: get_guide (read it first) -> build the scene with build/actors tools -> set_data on clickable nodes ->
 * define_vars -> define_rules -> define_ui -> set_input_mode or spawn_player -> start -> test with fire_event / step and
 * read get_state (vars, event log, errors) -> fix rules until a full game works -> export to keep it.
 * Definitions are undoable (history.undo) and stored as the JSON passed to the define_* tools (what export returns).
 */
UCLASS()
class URuntimeLogicToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	// Guide -----------------------------------------------------------------------------------------------------------

	/**
	 * READ FIRST: how to turn a scene into a playable game: variables, rules and actions, expressions, UI, players,
	 * combat, AI, feedback, tweens, pools, spawners and typical rule sets for board games, platformers, shooters and
	 * dogfights.
	 *
	 * @param Guide - The guide text.
	 */
	UFUNCTION(meta = (AICallable), Category = "Logic|Guide")
	static void GetGuide(FString& Guide);

	// Variables -------------------------------------------------------------------------------------------------------

	/**
	 * Defines game variables and their starting values; start and the restart action reset them to these. Undoable.
	 *
	 * @param Vars - {name: starting value}: number, text, bool, list or map, e.g. {"board":["","",""], "turn":"X", "moves":0}.
	 *               Names start with a letter; "event" and "time" are reserved.
	 * @param bReplace - Remove all other variables first.
	 * @return Every defined variable with its starting value. Fails if a name is invalid.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Variables")
	static FLogicVarsResult DefineVars(const TMap<FString, FRuntimeToolJson>& Vars, bool bReplace = false);

	/**
	 * Changes a variable while the game runs and fires its var_changed rules (testing, or steering a running game).
	 *
	 * @param Name - Variable, e.g. "score".
	 * @param Value - New value as JSON, e.g. 10 or ["X","",""].
	 * @param Expr - Or an expression evaluated against the current variables, e.g. "score + 5".
	 * @return The new value. Fails if the variable is not defined or neither value nor expr is given.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Value,Expr"), Category = "Logic|Variables")
	static FLogicValueResult SetVar(const FString& Name, const FRuntimeToolJson& Value, const FString& Expr);

	/**
	 * Evaluates an expression against the current variables, to test conditions and functions before using them in rules.
	 *
	 * @param Expr - Expression, e.g. "winning_move(board, grid_lines(3,3,3), 'O', '')".
	 * @return The value and its type. Fails with the parse or evaluation error.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Variables")
	static FLogicEvalResult Eval(const FString& Expr);

	// Rules -----------------------------------------------------------------------------------------------------------

	/**
	 * Adds or replaces rules (by name): when <on> [target / key / filter] and <condition>, run <actions>. Undoable.
	 * Every expression and action is checked before anything changes.
	 *
	 * @param Rules - Rules: {name, on, target, key, filter, condition, actions:[...], enabled}, e.g.
	 *                {"name":"place","on":"click","target":"tag:cell","condition":"board[event.data.index] == ''",
	 *                "actions":[{"set_item":"board","index":"event.data.index","value":"turn"}]}. See get_guide for events and actions.
	 * @param bReplace - Remove all other rules first.
	 * @return The rule names defined and the total number of rules. Fails on the first invalid rule (nothing changes).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Rules")
	static FLogicDefineRulesResult DefineRules(const TArray<FRuntimeToolJson>& Rules, bool bReplace = false);

	/**
	 * Removes rules by name. Undoable.
	 *
	 * @param Names - Rule names, e.g. ["restart_key"].
	 * Fails if a rule does not exist (the error lists the defined rules).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Rules")
	static void RemoveRules(const TArray<FString>& Names);

	/**
	 * Lists all rules with their full definitions.
	 *
	 * @return Rules in run order.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Rules")
	static FLogicRuleList ListRules();

	/**
	 * Attaches data to a node and its children; click, touch and interact rules read it as event.data. Undoable.
	 *
	 * @param Target - The node, e.g. "n12" or "TicTacToe/cell_4".
	 * @param Data - Object, e.g. {"index": 4, "pos": [0,300,40]}.
	 * @return The node. Fails if the target is not a scene node or data is not an object.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Rules")
	static FLogicNodeRef SetData(AActor* Target, const FRuntimeToolJson& Data);

	// UI --------------------------------------------------------------------------------------------------------------

	/**
	 * Adds or replaces on-screen text, buttons and bars (by name). Text is a live template ("Score: {score}");
	 * buttons fire "button" rules. Undoable.
	 *
	 * @param Items - Items: {name, kind: text|button|bar, text, anchor:[x,y] 0-1, size:[w,h], color:[r,g,b,a], background,
	 *                font_size, visible_if; bar: value, max, fill, ghost}, e.g. {"name":"status","kind":"text",
	 *                "text":"{turn} to move","anchor":[0.5,0.06]}.
	 * @param bReplace - Remove all other items first.
	 * @return Number of items and whether they are drawn on a viewport. Fails on the first invalid item (nothing changes).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|UI")
	static FLogicDefineUiResult DefineUi(const TArray<FRuntimeToolJson>& Items, bool bReplace = false);

	/**
	 * Removes on-screen items by name. Undoable.
	 *
	 * @param Names - Item names, e.g. ["status"].
	 * Fails if an item does not exist (the error lists the defined items).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|UI")
	static void RemoveUi(const TArray<FString>& Names);

	/**
	 * Sets how the mouse works: cursor (visible cursor, clicks hit objects and buttons: board and puzzle games) or game
	 * (normal play). Undoable.
	 *
	 * @param Mode - cursor or game.
	 * @param bLockCamera - Stop mouse-look and movement (a fixed board view; frame it with camera tools).
	 * @return The applied mode and whether a player controller exists.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|UI")
	static FLogicInputModeResult SetInputMode(ELogicInputMode Mode = ELogicInputMode::Cursor, bool bLockCamera = false);

	// Running ---------------------------------------------------------------------------------------------------------

	/**
	 * Starts or restarts the game: resets variables, removes objects made during play, shows hidden nodes, clears
	 * timers, then runs "start" rules.
	 *
	 * @return The running state with the last 5 log entries (check them for errors).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Run")
	static FLogicGameState Start();

	/**
	 * Stops running rules, timers and movement. Definitions are kept; start runs the game again.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Run")
	static void Stop();

	/**
	 * Simulates player input to test the game: a click on a node, a key, a button press, a timer, a custom event, the
	 * player touching a node, falling, interacting or crashing. Runs the matching rules right away.
	 *
	 * @param On - Event kind, e.g. "click".
	 * @param Target - click, right_click, touch, interact: the node, e.g. "TicTacToe/cell_4".
	 * @param Key - key: key name, e.g. "R", "SpaceBar", "One".
	 * @param Name - button, timer, event: its name, e.g. "restart".
	 * @param Data - Event data (event.data in rules); touch: {"from_above": true} or {"from_below": true}.
	 * @return The events processed with the rules that ran, rule errors, log lines and the variables afterwards.
	 *         Fails if the game is not running or the target is not a scene node.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Data"), Category = "Logic|Run")
	static FLogicFireReport FireEvent(ELogicEventKind On, const FRuntimeToolJson& Data, AActor* Target = nullptr, const FString& Key = TEXT(""),
		const FString& Name = TEXT(""));

	/**
	 * Advances game time in 0.1 s steps: timers, tick rules, movement, physics of the player, AI, projectiles (testing).
	 *
	 * @param Seconds - Seconds to simulate, 0-600, e.g. 1.5.
	 * @return The running state with the last 10 log entries. Fails if the game is not running.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Run")
	static FLogicGameState Step(float Seconds = 1.f);

	/**
	 * Returns the running state: variables, timers, rules, UI, objects made in play, player, AI states, health, ammo,
	 * pools, spawners and the recent event log with errors.
	 *
	 * @param LogLimit - Log entries to return (newest), 0-100.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Run")
	static FLogicGameState GetState(int32 LogLimit = 20);

	/**
	 * Returns the whole game definition as JSON (vars, rules, ui, node data, input mode, player, combat, brains, pools,
	 * spawners...), to keep it or move it to another level with import.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Run")
	static FLogicExport Export();

	/**
	 * Replaces the game definition with one from export. Undoable.
	 *
	 * @param Logic - The "logic" object returned by export.
	 * @return Numbers of rules and variables. Fails if the definition is invalid (nothing changes).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Run")
	static FLogicImportResult Import(const FRuntimeToolJson& Logic);

	// Player and motion -----------------------------------------------------------------------------------------------

	/**
	 * Spawns the playable character with a follow camera. side: side-scroller (level along +X in the plane Y=0, camera
	 * on +Y; A/D run, Space jump). topdown: WASD on the ground, aim with the mouse. flight: arcade jet (mouse steers,
	 * A/D roll, W/S throttle, Shift boost). Do not use camera tools while a player exists. Undoable.
	 *
	 * @param Location - Start point of the feet [x,y,z], e.g. [0,0,100].
	 * @param Dash - Dash ability {speed, duration, cooldown, key, invulnerable} (fires "dash").
	 * @param Speed - Run speed cm/s (default 650; flight: cruise speed, default 4500).
	 * @param MinSpeed - Flight: slowest speed cm/s (default 45% of speed).
	 * @param MaxSpeed - Flight: boost speed cm/s (default 160% of speed).
	 * @param CameraDistance - cm (default: side 1700, top-down 1500, flight 1300).
	 * @param CameraHeight - cm above the player (default: side 250, flight 330).
	 * @param CameraLead - cm ahead in the facing direction (default: side 200, top-down 150).
	 * @param CameraPitch - Degrees (default: side -6, top-down 60 looking down).
	 * @param Fov - Field of view (default: 55, flight 70).
	 * @param KillZ - Falling below this height fires "fall" (default: start height - 1500; without a fall rule the player respawns).
	 * @param CrashDamage - Flight: damage when hitting the ground (default: lethal).
	 * @param Grid - Top-down: move cell by cell, cell size in cm (default: free movement).
	 * @param Mode - side, topdown or flight.
	 * @param Yaw - Start heading in degrees.
	 * @param Turn - Flight: pitch/yaw rate in degrees per second.
	 * @param RollRate - Flight: roll rate in degrees per second.
	 * @param MouseSensitivity - Flight: mouse steering scale.
	 * @param Secondary - Weapon fired with right mouse / left trigger / G (e.g. homing missiles).
	 * @param bAutoSpawn - Spawn now and on start; false = wait for the {"player":"spawn"} action.
	 * @param StepTime - Grid: seconds per step.
	 * @param ReloadKey - Key that reloads, e.g. "Q".
	 * @param Body - Your own character model node (feet at z=0, facing +X); default: a cap-and-overalls hero (flight: a jet).
	 * @param Radius - Collision radius in cm.
	 * @param Height - Collision height in cm.
	 * @param Jump - Jump velocity cm/s.
	 * @param JumpHold - Seconds holding jump adds height.
	 * @param Gravity - Gravity scale.
	 * @param AirControl - Steering in the air, 0-1.
	 * @return The player state and its controls. Fails if the body is not a scene node or spawning fails.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld,
		AutoCreateRefTerm = "Dash,Speed,MinSpeed,MaxSpeed,CameraDistance,CameraHeight,CameraLead,CameraPitch,Fov,KillZ,CrashDamage,Grid"),
		Category = "Logic|Player")
	static FLogicPlayerState SpawnPlayer(const FVector& Location, const FLogicDash& Dash, const float& Speed, const float& MinSpeed,
		const float& MaxSpeed, const float& CameraDistance, const float& CameraHeight, const float& CameraLead, const float& CameraPitch,
		const float& Fov, const float& KillZ, const float& CrashDamage, const float& Grid, ELogicPlayerMode Mode = ELogicPlayerMode::Side,
		float Yaw = 0.f, float Turn = 70.f, float RollRate = 160.f, float MouseSensitivity = 1.f, const FString& Secondary = TEXT(""),
		bool bAutoSpawn = true, float StepTime = 0.14f, const FString& ReloadKey = TEXT(""), AActor* Body = nullptr, float Radius = 38.f,
		float Height = 180.f, float Jump = 1250.f, float JumpHold = 0.22f, float Gravity = 2.8f, float AirControl = 0.85f);

	/**
	 * Removes the playable character and gives the view back. Undoable.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Player")
	static void RemovePlayer();

	/**
	 * Makes nodes move while the game runs: patrol back and forth, follow a path, bob, and/or spin. Movement restarts
	 * from the original pose on restart. Undoable.
	 *
	 * @param Targets - Node ids, paths, names or "tag:x", e.g. ["tag:goomba"].
	 * @param Motion - {type: patrol|path|bob|none, axis, distance, speed, points, loop, amplitude, period, spin, face, offset, waddle},
	 *                 e.g. {"type":"patrol","axis":[1,0,0],"distance":500,"speed":120,"face":true}.
	 * @return Number of nodes. Fails if a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Motion")
	static FLogicNodesResult SetMotion(const TArray<FString>& Targets, const FLogicMotion& Motion);

	/**
	 * Stops moving these nodes (set_motion). Undoable.
	 *
	 * @param Targets - Node ids, paths, names or "tag:x".
	 * Fails if a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Motion")
	static void ClearMotion(const TArray<FString>& Targets);

	/**
	 * Sets whether nodes block the player. Pickups and enemies: false (they still fire touch). Undoable.
	 *
	 * @param Targets - Node ids, paths, names or "tag:x", e.g. ["tag:coin", "tag:enemy"].
	 * @param bSolid - Blocks movement.
	 * @return Number of nodes and the value set. Fails if a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Motion")
	static FLogicSolidResult SetSolid(const TArray<FString>& Targets, bool bSolid);

	// Combat ----------------------------------------------------------------------------------------------------------

	/**
	 * Gives the player or nodes health: max, immunity after hits, resistances by damage type, what happens at 0.
	 * Rules on "damaged" / "died" (target "player" or "tag:x") react to it. Undoable.
	 *
	 * @param Targets - "player", node ids, paths, names or "tag:x", e.g. ["tag:enemy"].
	 * @param Resistances - {damage type: multiplier}, e.g. {"fire": 0.5, "explosion": 2}.
	 * @param Initial - Starting health (default: max).
	 * @param Invulnerable - Seconds immune after a hit (default: player 1, nodes 0).
	 * @param OnDeath - What happens at 0 (default: player respawn, nodes hide).
	 * @param Max - Max health.
	 * @param PointsVar - Variable that gets `points` on death, e.g. "score".
	 * @param Points - Points added to points_var on death.
	 * @param HitFeedback - Feedback preset played when hurt (define_feedback).
	 * @param DeathFeedback - Feedback preset played on death.
	 * @param Loot - Loot table dropped on death (define_loot).
	 * @return Current health of everything that has health. Fails if a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Resistances,Initial,Invulnerable,OnDeath"), Category = "Logic|Combat")
	static FLogicHealthResult DefineHealth(const TArray<FString>& Targets, const TMap<FString, float>& Resistances, const float& Initial,
		const float& Invulnerable, const ELogicDeathAction& OnDeath, float Max = 10.f, const FString& PointsVar = TEXT(""), float Points = 0.f,
		const FString& HitFeedback = TEXT(""), const FString& DeathFeedback = TEXT(""), const FString& Loot = TEXT(""));

	/**
	 * Makes nodes damage what overlaps them (spikes, lava, explosions, melee areas): once on entry or every interval
	 * while inside. Undoable.
	 *
	 * @param Targets - Zone nodes: ids, paths, names or "tag:x", e.g. ["tag:lava"].
	 * @param DamageMax - Random damage between damage and this (default: always damage).
	 * @param Damage - Damage per hit.
	 * @param Type - Damage type for resistances, e.g. "fire".
	 * @param Affects - "player", "any" or "tag:x".
	 * @param Mode - enter: once per entry; stay: every interval.
	 * @param Interval - Seconds between hits in stay mode.
	 * @param Knockback - Push the victim away (cm/s).
	 * Fails if a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "DamageMax"), Category = "Logic|Combat")
	static void DefineHitzone(const TArray<FString>& Targets, const float& DamageMax, float Damage = 1.f, const FString& Type = TEXT(""),
		const FString& Affects = TEXT("any"), ELogicHitZoneMode Mode = ELogicHitZoneMode::Enter, float Interval = 0.5f, float Knockback = 0.f);

	/**
	 * Defines a projectile or hitscan weapon: damage, fire rate, burst, magazine / reload / reserve ammo, shots per
	 * trigger, spread, bullets from a pool, homing missiles. Give it to the player or nodes with equip; rules fire it
	 * with {"fire": ...}. Undoable.
	 *
	 * @param Name - Weapon name, e.g. "blaster".
	 * @param Bullet - Look of the default bullet {size, color} (used when no pool is given).
	 * @param Mode - projectile or hitscan.
	 * @param Damage - Damage per hit.
	 * @param Type - Damage type, e.g. "laser".
	 * @param Rate - Shots per second.
	 * @param bAuto - Player: hold to keep firing.
	 * @param Burst - Shots per trigger pull.
	 * @param BurstGap - Seconds between burst shots.
	 * @param Magazine - Rounds per magazine (0 = unlimited).
	 * @param Reload - Reload seconds.
	 * @param bAutoReload - Reload when the magazine is empty.
	 * @param AmmoVar - Variable holding reserve ammo, e.g. "reserve".
	 * @param PerShot - Projectiles per shot (shotgun).
	 * @param Spread - Degrees either side.
	 * @param SpreadMode - random or fan.
	 * @param Speed - Projectile speed cm/s.
	 * @param Lifetime - Projectile seconds.
	 * @param Range - Hitscan range cm.
	 * @param Radius - Projectile hit radius cm.
	 * @param Knockback - Push on hit (cm/s).
	 * @param Muzzle - Spawn distance in front of the shooter (cm).
	 * @param Hits - What it can damage: "tag:enemy", "player" or "any" (default: anything but the shooter).
	 * @param Pool - Pool of bullet objects (define_pool); default: a generated glowing bullet.
	 * @param PoolSize - Size of the generated bullet pool.
	 * @param Feedback - Feedback preset played on each shot.
	 * @param Noise - How far brains hear the player's shots (cm).
	 * @param Homing - Missile turn rate deg/s (0 = straight).
	 * @param LockRange - Lock-on range cm.
	 * @param LockAngle - Lock-on cone degrees.
	 * @return The weapon and its bullet pool. Fails if the pool does not exist.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Bullet"), Category = "Logic|Combat")
	static FLogicWeaponResult DefineWeapon(const FString& Name, const FLogicBulletLook& Bullet, ELogicWeaponMode Mode = ELogicWeaponMode::Projectile,
		float Damage = 1.f, const FString& Type = TEXT(""), float Rate = 4.f, bool bAuto = false, int32 Burst = 1, float BurstGap = 0.08f,
		int32 Magazine = 0, float Reload = 1.5f, bool bAutoReload = true, const FString& AmmoVar = TEXT(""), int32 PerShot = 1, float Spread = 0.f,
		ELogicSpreadMode SpreadMode = ELogicSpreadMode::Random, float Speed = 2500.f, float Lifetime = 1.5f, float Range = 3000.f,
		float Radius = 12.f, float Knockback = 0.f, float Muzzle = 60.f, const FString& Hits = TEXT(""), const FString& Pool = TEXT(""),
		int32 PoolSize = 40, const FString& Feedback = TEXT(""), float Noise = 1600.f, float Homing = 0.f, float LockRange = 12000.f,
		float LockAngle = 25.f);

	/**
	 * Gives the player or nodes a weapon (an empty weapon takes it away). Undoable.
	 *
	 * @param Owner - "player", a node or "tag:x", e.g. "tag:turret".
	 * @param Weapon - Weapon name (define_weapon); empty = unequip.
	 * Fails if the weapon is not defined (the error lists the weapons) or the owner matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Combat")
	static void Equip(const FString& Owner, const FString& Weapon = TEXT(""));

	/**
	 * Damages the player or a node now, as a hit would (testing).
	 *
	 * @param Target - "player" or a node, e.g. "n12".
	 * @param Amount - Damage.
	 * @param Type - Damage type for resistances.
	 * @return Whether it applied and everyone's health. Fails if the target matches no node.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Combat")
	static FLogicDamageResult Damage(const FString& Target, float Amount = 1.f, const FString& Type = TEXT(""));

	// AI --------------------------------------------------------------------------------------------------------------

	/**
	 * Defines an AI brain: a state machine whose states set movement, facing, firing and an icon, with transitions by
	 * expressions (sees, heard, distance, time_in_state, since_seen, at_goal, at_home, has_last_known, health,
	 * health_pct, player_alive, self and all variables). Assign it with assign_brain. Undoable.
	 *
	 * @param Name - Brain name, e.g. "guard".
	 * @param States - States: {name, move: stop|patrol|chase|strafe|flee|to_last_known|home|wander, speed, keep_distance,
	 *                 radius, face: move|target|look_around, fire, fire_angle, fire_range, weapon, icon, turn_rate,
	 *                 transitions:[{when, to}]}, e.g. {"name":"patrol","move":"patrol","transitions":[{"when":"sees","to":"attack"}]}.
	 * @param Vision - What it sees; without vision the brain is blind.
	 * @param Hearing - What it hears.
	 * @param Flight - Make it a flying brain (enemy jets): patrol orbits, chase pursues, flee evades.
	 * @param TurnSpeed - Degrees per second (default: the brain.turn_speed tuning value).
	 * @param DecideEvery - Seconds between transition checks (default: the brain.decide_interval tuning value).
	 * @param Start - Start state (default: the first).
	 * @param Backend - Native: built-in steering and vision cone (no navmesh needed). Ai: a real AI agent - walks the navmesh
	 *                  around obstacles (call ai.build_navigation first) and senses with AI perception (sight, hearing of
	 *                  gunshots and ai.report_noise). Ground brains only.
	 * @return The brain name. Fails if a state, move mode or transition target is invalid.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Vision,Hearing,Flight,TurnSpeed,DecideEvery"), Category = "Logic|AI")
	static FLogicBrainResult DefineBrain(const FString& Name, const TArray<FRuntimeToolJson>& States, const FLogicBrainVision& Vision,
		const FLogicBrainHearing& Hearing, const FLogicBrainFlight& Flight, const float& TurnSpeed, const float& DecideEvery,
		const FString& Start = TEXT(""), ELogicBrainBackend Backend = ELogicBrainBackend::Native);

	/**
	 * Gives nodes an AI brain (with a patrol route) and draws their vision cones. Rules on "state" (filter: state name)
	 * react to state changes. Undoable.
	 *
	 * @param Targets - Node ids, paths, names or "tag:x", e.g. ["tag:guard"].
	 * @param Brain - Brain name (define_brain).
	 * @param Patrol - Route relative to the start: {axis:[1,0,0], distance} or {points:[[dx,dy,0],...]}.
	 * @return Number of agents. Fails if the brain is not defined (the error lists the brains) or a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Patrol"), Category = "Logic|AI")
	static FLogicAssignBrainResult AssignBrain(const TArray<FString>& Targets, const FString& Brain, const FLogicPatrolRoute& Patrol);

	/**
	 * Turns on world HUD effects: health bars over damaged enemies, ! / ? alert icons, floating damage numbers, a red
	 * hit flash, a low-health pulse and flight target markers. Undoable.
	 *
	 * @param bEnemyBars - Health bars over hurt enemies.
	 * @param bAlwaysBars - Show bars even at full health.
	 * @param bAlertIcons - Brain state icons.
	 * @param bDamageNumbers - Floating damage numbers.
	 * @param bHitFlash - Red screen flash when the player is hurt.
	 * @param bLowHealth - Pulsing red edges under 30% health.
	 * @param bMarkers - Target brackets with distance, missile lock and flight gun reticle.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|AI")
	static void SetHud(bool bEnemyBars = true, bool bAlwaysBars = false, bool bAlertIcons = true, bool bDamageNumbers = true, bool bHitFlash = true,
		bool bLowHealth = true, bool bMarkers = false);

	// Game feel -------------------------------------------------------------------------------------------------------

	/**
	 * Defines a named game-feel preset: steps of camera shake, screen flash, freeze-frame, slow motion, floating text,
	 * tween, pooled effect or event, each with a delay. Play it with the {"feedback": name} action or attach it to
	 * define_health / define_weapon. Undoable.
	 *
	 * @param Name - Preset name, e.g. "hit".
	 * @param Steps - Steps: {type: shake|flash|freeze|slowmo|text|tween|spawn|emit, delay, duration, intensity, scale, color,
	 *                text, pool, name, ...tween fields}, e.g. [{"type":"shake","duration":0.2},{"type":"flash","intensity":0.6}].
	 * Fails if a step type is unknown or a spawn step names a missing pool.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Feel")
	static void DefineFeedback(const FString& Name, const TArray<FRuntimeToolJson>& Steps);

	/**
	 * Plays a feedback preset now (testing).
	 *
	 * @param Name - Preset name (define_feedback).
	 * @param At - World location [x,y,z] (default: the player).
	 * @param Target - Node for tween steps.
	 * Fails if the preset is not defined (the error lists the presets).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "At"), Category = "Logic|Feel")
	static void PlayFeedback(const FString& Name, const FVector& At, const FString& Target = TEXT(""));

	/**
	 * Animates nodes smoothly: move / rotate / scale / color with easing, delay, yoyo or loop, and hide / show /
	 * release at the end. Works whether or not the game runs; the {"tween": ...} action does the same from rules.
	 *
	 * @param Targets - Node ids, paths, names or "tag:x".
	 * @param Location - Absolute end location.
	 * @param Move - Relative offset, e.g. [0,0,100].
	 * @param Rotation - Absolute end rotation.
	 * @param Rotate - Relative rotation (can exceed 360).
	 * @param Scale - End scale: a number or [x,y,z].
	 * @param Color - End base color (builder materials).
	 * @param Duration - Seconds per cycle.
	 * @param Delay - Seconds before starting.
	 * @param Ease - Easing curve.
	 * @param Loop - once, yoyo (there and back), loop (restart) or pingpong (forever).
	 * @param Repeat - Cycles for yoyo / loop (-1 = forever for loop).
	 * @param Then - At the end: none, hide, show or release (back to its pool).
	 * @param Emit - Custom event fired at the end (rules on "event").
	 * @param bAdd - Run alongside other tweens on the node instead of replacing them.
	 * @return Nodes started and tweens running. Fails if no property to animate is given or a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Location,Move,Rotation,Rotate,Scale,Color"), Category = "Logic|Feel")
	static FLogicTweenResult Tween(const TArray<FString>& Targets, const FVector& Location, const FVector& Move, const FRotator& Rotation,
		const FRotator& Rotate, const FRuntimeToolJson& Scale, const FLinearColor& Color, float Duration = 0.3f, float Delay = 0.f,
		ELogicEase Ease = ELogicEase::OutQuad, ELogicTweenLoop Loop = ELogicTweenLoop::Once, int32 Repeat = 1,
		ELogicTweenEnd Then = ELogicTweenEnd::None, const FString& Emit = TEXT(""), bool bAdd = false);

	// Pools and spawning ----------------------------------------------------------------------------------------------

	/**
	 * Creates an object pool: hidden copies of a template node that rules spawn ({"spawn": pool}) and release instead
	 * of creating objects (bullets, coins from blocks, effects, enemies). The template is hidden. Undoable.
	 *
	 * @param Name - Pool name, e.g. "coins".
	 * @param Template - Node to copy, e.g. "Level/coin".
	 * @param Size - Copies, 1-500.
	 * @return The pool and its free copies. Fails if the template is not a scene node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Spawning")
	static FLogicPoolResult DefinePool(const FString& Name, AActor* Template, int32 Size = 8);

	/**
	 * Spawns pooled enemies over time or in waves, at points or around the player. Events: wave_start, wave_cleared,
	 * waves_complete, spawned (filter: spawner name); action {"spawner": name, "op": "start|stop|reset"}. Undoable.
	 *
	 * @param Name - Spawner name, e.g. "bots".
	 * @param Pool - Pool to take enemies from (define_pool).
	 * @param Points - Spawn points [[x,y,z], ...].
	 * @param AroundPlayer - Or around the player: {radius: n or [min,max], height: n or [min,max]}.
	 * @param Interval - Seconds between spawns: n or [min,max] (default 1.5).
	 * @param Waves - Waves {count, interval, max_alive}; without waves it spawns up to total.
	 * @param Total - Stop after this many (no waves; default: unlimited).
	 * @param MaxAlive - At most this many alive.
	 * @param WaveGap - Seconds between waves.
	 * @param StartDelay - Seconds before the first spawn.
	 * @param bAutostart - Start with the game.
	 * @param bFacePlayer - Spawn facing the player.
	 * Fails if the pool does not exist or neither points nor around_player is given.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Points,AroundPlayer,Interval,Waves,Total"), Category = "Logic|Spawning")
	static void DefineSpawner(const FString& Name, const FString& Pool, const TArray<FVector>& Points, const FRuntimeToolJson& AroundPlayer,
		const FRuntimeToolJson& Interval, const TArray<FLogicSpawnerWave>& Waves, const int32& Total, int32 MaxAlive = 5, float WaveGap = 3.f,
		float StartDelay = 0.5f, bool bAutostart = true, bool bFacePlayer = true);

	/**
	 * Defines a weighted loot table of pooled pickups, dropped on death (define_health loot) or with {"drop_loot": name}. Undoable.
	 *
	 * @param Name - Table name, e.g. "crate".
	 * @param Entries - {pool, weight}; pool "none" = a chance of nothing, e.g. [{"pool":"coins","weight":3},{"pool":"none","weight":1}].
	 * @param Count - Drops: n or [min,max] (default 1).
	 * @param Scatter - Spread radius cm.
	 * Fails if an entry's pool does not exist (the error lists the pools).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Count"), Category = "Logic|Spawning")
	static void DefineLoot(const FString& Name, const TArray<FLogicLootEntry>& Entries, const FRuntimeToolJson& Count, float Scatter = 120.f);

	/**
	 * Makes nodes interactable (doors, switches, chests, terminals): a key prompt near the player and an "interact"
	 * event (or "interact_denied" while requires is false). Undoable.
	 *
	 * @param Targets - Node ids, paths, names or "tag:x".
	 * @param Prompt - Prompt text, e.g. "Open door".
	 * @param Key - Key name.
	 * @param Radius - Reach in cm.
	 * @param bOnce - Only once per game.
	 * @param Cooldown - Seconds between uses.
	 * @param Requires - Condition, e.g. "has('red_key')".
	 * @param LockedPrompt - Prompt while the condition is false.
	 * Fails if requires does not parse or a target matches no node.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Logic|Spawning")
	static void DefineInteractable(const TArray<FString>& Targets, const FString& Prompt = TEXT(""), const FString& Key = TEXT("E"), float Radius = 250.f,
		bool bOnce = false, float Cooldown = 0.3f, const FString& Requires = TEXT(""), const FString& LockedPrompt = TEXT(""));

	// Saves and tuning ------------------------------------------------------------------------------------------------

	/**
	 * Saves variables to a slot on disk (Saved/RuntimeToolsets/Saves); the {"save": ...} action does the same from rules.
	 *
	 * @param Slot - Slot name, e.g. "progress".
	 * @param Vars - Variables to save (default: all).
	 * Fails if the slot name is invalid, a variable does not exist or the file cannot be written.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Vars"), Category = "Logic|Saves")
	static void Save(const FString& Slot, const TArray<FString>& Vars);

	/**
	 * Loads variables from a save slot.
	 *
	 * @param Slot - Slot name.
	 * @return All variables after loading. Fails if the slot does not exist.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Logic|Saves")
	static FLogicVarsResult Load(const FString& Slot);

	/**
	 * Lists every internal constant of the game runtime (perception rates, steering, animation, HUD sizes) with its
	 * value, default, meaning and where it comes from. Gameplay parameters belong to the define_* tools.
	 */
	UFUNCTION(meta = (AICallable), Category = "Logic|Tuning")
	static FLogicTuning GetTuning();

	/**
	 * Changes internal constants at runtime (see get_tuning). Defaults can also be set in DefaultGame.ini
	 * [RuntimeToolsets.Tuning] name=value.
	 *
	 * @param Values - {name: number}, e.g. {"hud.flash_alpha": 0.5}.
	 * @param bReset - Back to the defaults and config first.
	 * @return All values after the change. Fails if a name is unknown (see get_tuning).
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Values"), Category = "Logic|Tuning")
	static FLogicTuning SetTuning(const TMap<FString, double>& Values, bool bReset = false);
};
