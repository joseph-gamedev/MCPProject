#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeGasToolset.generated.h"

class AActor;

/** The runtime base and current value of one gameplay attribute (Epic: FRuntimeAttributeValue). */
USTRUCT()
struct FRuntimeGasAttributeValue
{
	GENERATED_BODY()

	/** Short attribute name, e.g. "Health". */
	UPROPERTY()
	FString AttributeName;

	/** Attribute name with its set class, e.g. "RuntimeGenericAttributeSet.Health". */
	UPROPERTY()
	FString FullName;

	/** Attribute set class that owns the attribute, e.g. "RuntimeGenericAttributeSet". */
	UPROPERTY()
	FString SetClassName;

	/** Permanent base value, unaffected by temporary (duration) modifiers. */
	UPROPERTY()
	float BaseValue = 0.f;

	/** Current value after all active modifiers. */
	UPROPERTY()
	float CurrentValue = 0.f;
};

/** A gameplay effect active on an ability system (Epic: FActiveEffectInfo, plus the handle). */
USTRUCT()
struct FRuntimeGasActiveEffect
{
	GENERATED_BODY()

	/** Effect name: the gas.define_effect name, or the GameplayEffect class name (e.g. "GE_Burning"). */
	UPROPERTY()
	FString EffectName;

	/** Active handle id; pass it to gas.remove_effect. */
	UPROPERTY()
	int32 Handle = -1;

	/** Current stack count (1 for non-stacking effects). */
	UPROPERTY()
	int32 StackCount = 1;

	/** Total duration in seconds; -1 = infinite. */
	UPROPERTY()
	float TotalDuration = 0.f;

	/** Remaining duration in seconds; -1 = infinite. */
	UPROPERTY()
	float RemainingDuration = 0.f;

	/** Period in seconds (0 = not periodic). */
	UPROPERTY()
	float Period = 0.f;

	/** Tags granted to the owner while the effect is active. */
	UPROPERTY()
	TArray<FString> GrantedTags;
};

/** An ability granted to an ability system (Epic: FGrantedAbilityInfo, plus tags and trigger). */
USTRUCT()
struct FRuntimeGasGrantedAbility
{
	GENERATED_BODY()

	/** Ability class name, e.g. "GA_Sprint" or "RuntimeGasEffectAbility". */
	UPROPERTY()
	FString AbilityName;

	/** Level the ability was granted at. */
	UPROPERTY()
	int32 Level = 1;

	/** True while at least one instance is executing. */
	UPROPERTY()
	bool bIsActive = false;

	/** Ability tags (class asset tags plus tags given by gas.give_ability), usable with gas.try_activate_ability and gas.cancel_abilities. */
	UPROPERTY()
	TArray<FString> Tags;

	/** Effect the built-in effect ability applies (empty for other abilities). */
	UPROPERTY()
	FString Effect;

	/** Gameplay event tags that activate the ability (class triggers and gas.give_ability trigger_event). */
	UPROPERTY()
	TArray<FString> TriggerEvents;
};

/** A gameplay attribute of an attribute set class (Epic: FGameplayAttributeInfo). */
USTRUCT()
struct FRuntimeGasAttributeInfo
{
	GENERATED_BODY()

	/** Short attribute name, e.g. "Health". */
	UPROPERTY()
	FString AttributeName;

	/** Attribute name with its set class, e.g. "RuntimeGenericAttributeSet.Health". */
	UPROPERTY()
	FString FullName;

	/** Attribute set class name. */
	UPROPERTY()
	FString SetClassName;
};

/** An attribute set class (Epic: FAttributeSetClassInfo). */
USTRUCT()
struct FRuntimeGasAttributeSetClass
{
	GENERATED_BODY()

	/** Class name, e.g. "RuntimeGenericAttributeSet". */
	UPROPERTY()
	FString ClassName;

	/** Object path of the class (Blueprint classes end in _C). */
	UPROPERTY()
	FString ClassPath;

	/** Attributes defined on the set. */
	UPROPERTY()
	TArray<FRuntimeGasAttributeInfo> Attributes;
};

/** Ability system of an actor after gas.add_ability_system. */
USTRUCT()
struct FRuntimeGasSystemInfo
{
	GENERATED_BODY()

	/** The actor's node id (or actor:Name). */
	UPROPERTY()
	FString Id;

	/** Actor name. */
	UPROPERTY()
	FString Name;

	/** True if a new AbilitySystemComponent was added (false: the actor already had one). */
	UPROPERTY()
	bool bAddedComponent = false;

	/** Attribute values after initialization, sorted by full name. */
	UPROPERTY()
	TArray<FRuntimeGasAttributeValue> Attributes;
};

/** How long a gameplay effect lasts. */
UENUM()
enum class ERuntimeGasDurationPolicy : uint8
{
	/** Executes once and changes base values permanently (damage, healing, buying). */
	Instant,
	/** Active for `duration` seconds (buffs, debuffs, damage over time with a period). */
	HasDuration,
	/** Active until removed (auras, equipment, states). */
	Infinite,
};

/** How a modifier combines with the attribute. */
UENUM()
enum class ERuntimeGasModOp : uint8
{
	/** Adds the magnitude (negative to subtract). */
	Add,
	/** Multiplies by the magnitude (1.5 = +50%). */
	Multiply,
	/** Divides by the magnitude. */
	Divide,
	/** Replaces the value. */
	Override,
	/** Adds after multiplication and division. */
	AddFinal,
};

/** How repeated applications of the same effect stack. */
UENUM()
enum class ERuntimeGasStacking : uint8
{
	/** Every application is a separate active effect. */
	None,
	/** One stack per source (instigator). */
	AggregateBySource,
	/** One stack on the target, whoever applies it. */
	AggregateByTarget,
};

/** One attribute modifier of an effect. */
USTRUCT()
struct FRuntimeGasModifier
{
	GENERATED_BODY()

	/** Attribute name, e.g. "Health", "Damage" or "MySet.Armor". */
	UPROPERTY()
	FString Attribute;

	/** add (default), multiply, divide, override or add_final. */
	UPROPERTY()
	ERuntimeGasModOp Op = ERuntimeGasModOp::Add;

	/** Fixed magnitude (scaled by nothing; ignored when set_by_caller is given). */
	UPROPERTY()
	float Magnitude = 0.f;

	/** Optional SetByCaller tag: the magnitude is supplied at apply time (gas.apply_effect set_by_caller, or the event magnitude via Gas.SetByCaller.Magnitude). */
	UPROPERTY()
	FString SetByCaller;
};

/** A runtime-defined effect. */
USTRUCT()
struct FRuntimeGasEffectInfo
{
	GENERATED_BODY()

	/** Effect name used by gas.apply_effect and gas.give_ability. */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	ERuntimeGasDurationPolicy DurationPolicy = ERuntimeGasDurationPolicy::Instant;

	/** Seconds (HasDuration only). */
	UPROPERTY()
	float Duration = 0.f;

	/** Seconds between executions (0 = not periodic). */
	UPROPERTY()
	float Period = 0.f;

	UPROPERTY()
	TArray<FRuntimeGasModifier> Modifiers;

	UPROPERTY()
	TArray<FString> GrantedTags;

	UPROPERTY()
	TArray<FString> ApplicationRequiredTags;

	UPROPERTY()
	ERuntimeGasStacking Stacking = ERuntimeGasStacking::None;

	/** Maximum stacks (0 = unlimited). */
	UPROPERTY()
	int32 StackLimit = 0;

	/** Gameplay cue tags triggered by the effect. */
	UPROPERTY()
	TArray<FString> Cues;
};

/** Result of gas.apply_effect. */
USTRUCT()
struct FRuntimeGasApplyResult
{
	GENERATED_BODY()

	/** Effect name. */
	UPROPERTY()
	FString Effect;

	/** True if the effect was applied (false: blocked by tag requirements or immunity). */
	UPROPERTY()
	bool bApplied = false;

	/** Active handle id for duration/infinite effects (pass to gas.remove_effect); -1 for instant effects. */
	UPROPERTY()
	int32 Handle = -1;

	/** Attribute values after the application, sorted by full name. */
	UPROPERTY()
	TArray<FRuntimeGasAttributeValue> Attributes;

	/** Tags owned after the application, sorted. */
	UPROPERTY()
	TArray<FString> Tags;
};

/** Result of gas.remove_effect. */
USTRUCT()
struct FRuntimeGasRemoveResult
{
	GENERATED_BODY()

	/** Number of active effects removed. */
	UPROPERTY()
	int32 Removed = 0;

	/** Attribute values after the removal, sorted by full name. */
	UPROPERTY()
	TArray<FRuntimeGasAttributeValue> Attributes;

	/** Tags owned after the removal, sorted. */
	UPROPERTY()
	TArray<FString> Tags;
};

/** A gameplay ability class. */
USTRUCT()
struct FRuntimeGasAbilityClass
{
	GENERATED_BODY()

	/** Class name, e.g. "GA_Dash" (pass to gas.give_ability). */
	UPROPERTY()
	FString Name;

	/** Class path, e.g. "/Game/Abilities/GA_Dash.GA_Dash_C". */
	UPROPERTY()
	FString Path;

	/** Ability asset tags. */
	UPROPERTY()
	TArray<FString> Tags;

	/** Gameplay event tags that trigger the ability (from the class). */
	UPROPERTY()
	TArray<FString> TriggerEvents;
};

/** Result of gas.give_ability. */
USTRUCT()
struct FRuntimeGasGiveResult
{
	GENERATED_BODY()

	/** Granted ability class name. */
	UPROPERTY()
	FString AbilityName;

	/** Ability tags (class and given). */
	UPROPERTY()
	TArray<FString> Tags;

	/** Event tags that activate it. */
	UPROPERTY()
	TArray<FString> TriggerEvents;
};

/** Result of gas.try_activate_ability. */
USTRUCT()
struct FRuntimeGasActivateResult
{
	GENERATED_BODY()

	/** True if at least one ability activated. */
	UPROPERTY()
	bool bActivated = false;

	/** Abilities that matched and were tried. */
	UPROPERTY()
	TArray<FString> Tried;

	/** Attribute values afterwards (abilities usually change them), sorted by full name. */
	UPROPERTY()
	TArray<FRuntimeGasAttributeValue> Attributes;
};

/** Result of gas.send_gameplay_event. */
USTRUCT()
struct FRuntimeGasEventResult
{
	GENERATED_BODY()

	/** Number of abilities the event activated. */
	UPROPERTY()
	int32 TriggeredAbilities = 0;

	/** Attribute values afterwards, sorted by full name. */
	UPROPERTY()
	TArray<FRuntimeGasAttributeValue> Attributes;

	/** Tags owned afterwards, sorted. */
	UPROPERTY()
	TArray<FString> Tags;
};

/** A gameplay cue tag. */
USTRUCT()
struct FRuntimeGasCueInfo
{
	GENERATED_BODY()

	/** Cue tag, e.g. "GameplayCue.Character.Death". */
	UPROPERTY()
	FString Tag;

	/** Object path of the GameplayCueNotify handling it (empty when none is registered). */
	UPROPERTY()
	FString NotifyPath;
};

/**
 * Gameplay Ability System at runtime: inspect and drive attributes, gameplay effects, abilities, gameplay events,
 * loose tags and cues on any actor, including builder objects with no ability system (Epic GASToolsets inspection,
 * plus runtime drivers).
 *
 * Workflow:
 * 1. gas.add_ability_system on the actor (adds an AbilitySystemComponent and the generic attribute set: Health,
 *    MaxHealth, Shield, Stamina, MaxStamina, Mana, MaxMana, Speed, Damage). Actors that already own an ability system
 *    (project characters) work directly.
 * 2. gas.define_effect for each buff/debuff/damage (no assets needed), then gas.apply_effect / gas.remove_effect.
 * 3. Optional abilities: gas.give_ability (a project ability class, or effect = a defined effect for the built-in
 *    effect ability), then gas.try_activate_ability or gas.send_gameplay_event (trigger_event).
 * 4. Inspect with gas.get_attribute_values, gas.get_active_effects, gas.get_active_tags, gas.get_granted_abilities.
 * Tags must exist in the gameplay tag table; unknown tags fail with close matches.
 */
UCLASS()
class URuntimeGasToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("1.0.0"); }

	// ---- Inspection (Epic AbilitySystemInspectorToolset) ----

	/**
	 * Returns the base and current values of every gameplay attribute on the actor's ability system.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12" or "actor:BP_Hero_C_0".
	 * @return Attribute values sorted by full name. Fails if the actor has no ability system (see gas.add_ability_system).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AbilitySystem")
	static TArray<FRuntimeGasAttributeValue> GetAttributeValues(AActor* Target);

	/**
	 * Returns the gameplay effects active on the actor's ability system.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @return Active effects (name, handle, stacks, total/remaining duration, period, granted tags) sorted by effect name.
	 *   Fails if the actor has no ability system.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AbilitySystem")
	static TArray<FRuntimeGasActiveEffect> GetActiveEffects(AActor* Target);

	/**
	 * Returns the abilities granted to the actor's ability system.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @return Granted abilities (name, level, active, tags, effect, trigger events) sorted by ability name.
	 *   Fails if the actor has no ability system.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AbilitySystem")
	static TArray<FRuntimeGasGrantedAbility> GetGrantedAbilities(AActor* Target);

	/**
	 * Returns the gameplay tags owned by the actor's ability system (loose tags, effect-granted tags, ...).
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @return Sorted tag names. Fails if the actor has no ability system.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "AbilitySystem")
	static TArray<FString> GetActiveTags(AActor* Target);

	/**
	 * Returns the attribute set classes available (native and loaded Blueprint), with their attributes.
	 * Use it to find attribute names for gas.define_effect modifiers.
	 *
	 * @return Attribute set classes sorted by class name.
	 */
	UFUNCTION(meta = (AICallable), Category = "AttributeSets")
	static TArray<FRuntimeGasAttributeSetClass> FindAttributeSetClasses();

	// ---- Setup and attributes ----

	/**
	 * Gives an actor an ability system: adds an AbilitySystemComponent (unless it already has one), initializes its
	 * actor info and adds the generic attribute set (Health, MaxHealth, Shield, Stamina, MaxStamina, Mana, MaxMana,
	 * Speed, Damage) with initial values. Works on builder objects and any other actor. Undoable.
	 * Damage is a meta-attribute: an instant or periodic effect that adds to it subtracts from Shield first, then Health.
	 * While Health is 0 the actor has the tag Gas.State.Dead.
	 *
	 * @param Target - The actor, e.g. "n12".
	 * @param Attributes - Initial values overriding the defaults (Health 100, MaxHealth 100, Shield 0, Stamina 100,
	 *   MaxStamina 100, Mana 100, MaxMana 100, Speed 600), e.g. {"Health": 250, "MaxHealth": 250, "Shield": 50}.
	 *   A Health above MaxHealth raises MaxHealth too.
	 * @return The actor (id, name), whether a component was added, and the attribute values.
	 *   Fails if the actor already has the generic attribute set, or on an unknown attribute name.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Attributes"), Category = "AbilitySystem")
	static FRuntimeGasSystemInfo AddAbilitySystem(AActor* Target, const TMap<FString, float>& Attributes);

	/**
	 * Sets the base value of an attribute (permanent; active modifiers still apply on top). Undoable.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Attribute - Attribute name, e.g. "Health" or "RuntimeGenericAttributeSet.MaxMana".
	 * @param Value - New base value, e.g. 75. The generic set clamps Health/Stamina/Mana to [0, max].
	 * @return The attribute's base and current value afterwards. Fails on an unknown attribute (lists the actor's attributes).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "AbilitySystem")
	static FRuntimeGasAttributeValue SetAttributeBase(AActor* Target, const FString& Attribute, float Value);

	// ---- Effects ----

	/**
	 * Defines a gameplay effect at runtime (a transient GameplayEffect; no asset needed), stored by name for
	 * gas.apply_effect and gas.give_ability. Typical uses: damage (instant, Damage +N), heal (instant, Health +N), poison
	 * (has_duration 5 s, period 1, Damage +4), speed buff (has_duration 10 s, Speed multiply 1.5, granted tag), stun
	 * (has_duration 2 s, granted tag State.Stunned).
	 *
	 * @param Name - Effect name, e.g. "Poison".
	 * @param DurationPolicy - instant, has_duration or infinite.
	 * @param Modifiers - Attribute changes, e.g. [{"attribute":"Damage","op":"add","magnitude":4}] or
	 *   [{"attribute":"Damage","set_by_caller":"Gas.SetByCaller.Magnitude"}].
	 * @param GrantedTags - Tags the target owns while the effect is active (duration/infinite only), e.g. ["Gas.State.Dead"].
	 * @param ApplicationRequiredTags - Tags the target must own for the effect to apply.
	 * @param Cues - Gameplay cue tags to trigger (GameplayCue.*).
	 * @param Duration - Seconds, for has_duration (e.g. 5).
	 * @param Period - Seconds between executions for periodic effects (0 = not periodic), e.g. 1.
	 * @param Stacking - none (each application separate), aggregate_by_source or aggregate_by_target.
	 * @param StackLimit - Maximum stacks when stacking (0 = unlimited).
	 * @param bReplace - Replace an existing definition with the same name (effects already active keep the old one).
	 * @return The stored definition. Fails on unknown attributes or tags (with candidates), an existing name without
	 *   replace, or a has_duration effect without a positive duration.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "Modifiers,GrantedTags,ApplicationRequiredTags,Cues"), Category = "Effects")
	static FRuntimeGasEffectInfo DefineEffect(const FString& Name, ERuntimeGasDurationPolicy DurationPolicy, const TArray<FRuntimeGasModifier>& Modifiers,
		const TArray<FString>& GrantedTags, const TArray<FString>& ApplicationRequiredTags, const TArray<FString>& Cues, float Duration = 0.f, float Period = 0.f,
		ERuntimeGasStacking Stacking = ERuntimeGasStacking::None, int32 StackLimit = 0, bool bReplace = false);

	/**
	 * Lists the effects defined with gas.define_effect.
	 *
	 * @return Definitions sorted by name.
	 */
	UFUNCTION(meta = (AICallable), Category = "Effects")
	static TArray<FRuntimeGasEffectInfo> ListEffects();

	/**
	 * Applies a gameplay effect to the actor's ability system. Undoable (removes it again, or restores the base values an
	 * instant effect changed).
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Effect - A gas.define_effect name (e.g. "Poison") or a GameplayEffect class path (e.g. "/Game/GE/GE_Burn.GE_Burn_C").
	 * @param SetByCaller - Magnitudes for SetByCaller modifiers, e.g. {"Gas.SetByCaller.Magnitude": 25}.
	 * @param Level - Effect level.
	 * @param Instigator - Optional actor that caused it (shown in the effect context; drives aggregate_by_source stacking).
	 * @return Whether it applied, the active handle (-1 for instant), and attributes and tags afterwards.
	 *   Fails on an unknown effect (lists defined effects) or a missing SetByCaller magnitude.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "SetByCaller"), Category = "Effects")
	static FRuntimeGasApplyResult ApplyEffect(AActor* Target, const FString& Effect, const TMap<FString, float>& SetByCaller, float Level = 1.f,
		AActor* Instigator = nullptr);

	/**
	 * Removes active gameplay effects from the actor. Undoable (re-applies them with their full duration).
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Effect - Active handle id from gas.apply_effect or gas.get_active_effects (e.g. "3"), or an effect name
	 *   (removes every active instance of it, e.g. "Poison").
	 * @param Stacks - Stacks to remove (-1 = all).
	 * @return Number removed, attributes and tags afterwards. Fails if nothing matches (lists active effects).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Effects")
	static FRuntimeGasRemoveResult RemoveEffect(AActor* Target, const FString& Effect, int32 Stacks = -1);

	// ---- Abilities ----

	/**
	 * Lists the gameplay ability classes that are loaded or registered (native, Blueprint, packaged).
	 *
	 * @param Filter - Optional case-insensitive substring of the name, e.g. "dash".
	 * @return Ability classes (name, path, tags, trigger events) sorted by name.
	 */
	UFUNCTION(meta = (AICallable), Category = "Abilities")
	static TArray<FRuntimeGasAbilityClass> ListAbilityClasses(const FString& Filter = TEXT(""));

	/**
	 * Grants an ability to the actor's ability system. Either a project ability class, or the built-in effect ability:
	 * leave ability_class empty and pass effect, and the ability applies that effect to its owner when activated.
	 * Undoable.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param AbilityClass - Ability class name or path from gas.list_ability_classes, e.g. "GA_Dash"; empty for the built-in effect ability.
	 * @param Effect - For the built-in ability: a gas.define_effect name, e.g. "Hit".
	 * @param TriggerEvent - Optional gameplay event tag that activates the ability (gas.send_gameplay_event), e.g. "Gas.Event.Hit".
	 *   The event magnitude reaches the effect as SetByCaller Gas.SetByCaller.Magnitude.
	 * @param AbilityTags - Extra tags on this grant, usable with gas.try_activate_ability and gas.cancel_abilities.
	 * @param Level - Ability level.
	 * @return Granted ability name, tags and trigger events. Fails on an unknown class (lists candidates), effect or tag.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "AbilityTags"), Category = "Abilities")
	static FRuntimeGasGiveResult GiveAbility(AActor* Target, const TArray<FString>& AbilityTags, const FString& AbilityClass = TEXT(""),
		const FString& Effect = TEXT(""), const FString& TriggerEvent = TEXT(""), int32 Level = 1);

	/**
	 * Tries to activate granted abilities, by class name or by tag. Activation runs the ability's own checks (costs,
	 * cooldowns, blocked tags); activated false is a normal result, not an error.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Ability - A granted ability class name (e.g. "GA_Dash") or an ability tag (e.g. "Ability.Dash").
	 * @return Whether one activated, which abilities were tried, and attribute values afterwards.
	 *   Fails if no granted ability matches (lists granted abilities and their tags).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Abilities")
	static FRuntimeGasActivateResult TryActivateAbility(AActor* Target, const FString& Ability);

	/**
	 * Cancels active abilities with any of the tags, or all active abilities when tags is empty.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Tags - Ability tags, e.g. ["Ability.Dash"]; empty = all.
	 * @return Number of abilities that were active and are no longer.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Tags"), Category = "Abilities")
	static int32 CancelAbilities(AActor* Target, const TArray<FString>& Tags);

	/**
	 * Sends a gameplay event to the actor (UAbilitySystemBlueprintLibrary::SendGameplayEventToActor): activates abilities
	 * triggered by the event tag (or a parent tag) and notifies listeners.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param EventTag - Event tag, e.g. "Gas.Event.Hit".
	 * @param Magnitude - Event magnitude, e.g. 25 (the built-in effect ability passes it as SetByCaller Gas.SetByCaller.Magnitude).
	 * @param Instigator - Optional actor that caused the event.
	 * @return Number of abilities triggered, attributes and tags afterwards. Fails on an unknown tag (with close matches).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Abilities")
	static FRuntimeGasEventResult SendGameplayEvent(AActor* Target, const FString& EventTag, float Magnitude = 0.f, AActor* Instigator = nullptr);

	// ---- Tags and cues ----

	/**
	 * Adds loose gameplay tags to the actor's ability system (states such as stunned or on fire that no effect owns).
	 * Undoable.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Tags - Existing tags, e.g. ["Gas.State.Dead"].
	 * @return The owned tags afterwards, sorted. Fails on an unknown tag (with close matches).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Tags")
	static TArray<FString> AddLooseTags(AActor* Target, const TArray<FString>& Tags);

	/**
	 * Removes loose gameplay tags from the actor's ability system (tags granted by effects stay until the effect ends).
	 * Undoable.
	 *
	 * @param Target - Actor with an ability system, e.g. "n12".
	 * @param Tags - Tags to remove, e.g. ["Gas.State.Dead"].
	 * @return The owned tags afterwards, sorted. Fails on an unknown tag.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Tags")
	static TArray<FString> RemoveLooseTags(AActor* Target, const TArray<FString>& Tags);

	/**
	 * Lists gameplay cue tags and the notify registered for each (Epic: ListCues + GetCueInfo).
	 *
	 * @param ParentTag - Only cues under this tag, e.g. "GameplayCue.Character"; empty = all under GameplayCue.
	 * @return Cues sorted by tag.
	 */
	UFUNCTION(meta = (AICallable), Category = "Cues")
	static TArray<FRuntimeGasCueInfo> ListCues(const FString& ParentTag = TEXT(""));

	/**
	 * Executes a gameplay cue on the actor, non-replicated (Epic: ExecuteCueOnSelectedActor, on any actor). Needs a
	 * cue notify for the tag (see gas.list_cues) or an actor that handles cues itself.
	 *
	 * @param Target - The actor, e.g. "n12".
	 * @param CueTag - Cue tag, e.g. "GameplayCue.Hit.Spark".
	 * @param Location - Optional world location for the cue (default: the actor's location).
	 * @param NormalizedMagnitude - 0..1 magnitude passed to the cue.
	 * @return True when dispatched. Fails if the tag is unknown or not under GameplayCue, or nothing handles it.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld, AutoCreateRefTerm = "Location"), Category = "Cues")
	static bool ExecuteCue(AActor* Target, const FString& CueTag, const FVector& Location, float NormalizedMagnitude = 1.f);
};

namespace RuntimeGas
{
	/** Releases the effect registry (module shutdown). */
	void ResetRegistry();
}
