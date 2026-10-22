#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "RuntimeGasEffectAbility.generated.h"

/**
 * Built-in ability of the gas toolset: applies one gameplay effect to its owner and ends. The effect is the ability
 * spec's source object (gas.give_ability with effect = a defined effect name). When triggered by a gameplay event the
 * event magnitude is passed as SetByCaller Gas.SetByCaller.Magnitude, so one "damage" effect with a SetByCaller
 * modifier can deal the amount each event carries.
 */
UCLASS()
class URuntimeGasEffectAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	URuntimeGasEffectAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
