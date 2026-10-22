#include "GasSkills.h"

URuntimeGameplayAbilitySystemSkill::URuntimeGameplayAbilitySystemSkill()
{
	Description = TEXT("Apply when a task involves stats (health, shield, mana, stamina, speed), buffs and debuffs, damage over time, status tags "
		"(stunned, burning), abilities with costs, or a project that already uses Unreal's Gameplay Ability System (GAS).");
	Toolsets = {TEXT("gas")};
	Instructions = TEXT(R"SKILL(# Gameplay Ability System (gas toolset)

## GAS or logic health?
- logic.define_health / logic.damage: quick arcade health with hit zones, death and HUD bars. Use it for simple games
  where nothing but hit points matters.
- gas: real Unreal GAS. Use it when the project's characters already own an AbilitySystemComponent (their abilities,
  effects and attributes work directly), or when you need several stats, timed buffs/debuffs, stacking, status tags,
  damage over time or abilities. Do not give the same actor both systems for the same stat.

## Workflow
1. gas.add_ability_system on each actor (builder objects work). Adds the generic attribute set:
   Health, MaxHealth, Shield, Stamina, MaxStamina, Mana, MaxMana, Speed, and the Damage meta-attribute.
   Pass initial values: {"Health": 250, "MaxHealth": 250, "Shield": 50}. Project characters skip this step.
2. gas.define_effect for each gameplay rule, then gas.apply_effect (returns a handle for duration/infinite effects).
3. gas.get_attribute_values / gas.get_active_effects / gas.get_active_tags to verify; gas.remove_effect by handle or name.
4. Optional: gas.give_ability with effect = a defined effect and trigger_event = "Gas.Event.Hit", then
   gas.send_gameplay_event(magnitude 25) applies it with the event magnitude (SetByCaller Gas.SetByCaller.Magnitude).
   Project abilities: gas.list_ability_classes, gas.give_ability, gas.try_activate_ability, gas.cancel_abilities.

## Rules
- Damage goes through the Damage meta-attribute: instant effect, op add, positive magnitude. It drains Shield first,
  then Health; Health is clamped to [0, MaxHealth] and the actor gets Gas.State.Dead at 0.
- Heal: instant, Health add +N (clamped to MaxHealth). Never add to Health for damage; use Damage so shields work.
- Instant effects change base values permanently. Duration/infinite effects change current values and revert on removal.
- Damage over time: has_duration with period (Damage add per tick). Damage modifiers only work on instant or periodic effects.
- Multiply 1.5 = +50%; slow 0.5. Granted tags exist only while the effect is active.
- Tags must already exist in the gameplay tag table; unknown tags fail with close matches. Shipped tags: Gas.Event.Hit,
  Gas.State.Dead, Gas.SetByCaller.Magnitude.
- Stacking: aggregate_by_target with stack_limit for "up to 3 stacks of poison"; none = independent applications.
- gas.set_attribute_base for direct edits (level-up max health). Everything except activation and events is undoable.

## Typical numbers
Player: Health 100, Shield 0-50, Stamina 100, Speed 600. Grunt: Health 30-60. Boss: Health 500-2000, Shield 200.
Hit: 10-25 damage. Poison: 4 per second for 5 s. Heal pickup: +25. Speed buff: x1.5 for 8 s. Stun: 1.5-2 s tag.
)SKILL");
}
