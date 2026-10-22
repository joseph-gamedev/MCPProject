#include "RuntimeGasEffectAbility.h"
#include "RuntimeGenericAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

URuntimeGasEffectAbility::URuntimeGasEffectAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void URuntimeGasEffectAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const UGameplayEffect* Effect = Cast<UGameplayEffect>(GetCurrentSourceObject());
	if (ASC && Effect)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		if (TriggerEventData && TriggerEventData->Instigator)
		{
			AActor* Instigator = const_cast<AActor*>(TriggerEventData->Instigator.Get());
			Context.AddInstigator(Instigator, Instigator);
		}
		FGameplayEffectSpec Spec(Effect, Context, GetAbilityLevel(Handle, ActorInfo));
		Spec.SetSetByCallerMagnitude(RuntimeGasTags::SetByCallerMagnitude, TriggerEventData ? TriggerEventData->EventMagnitude : 0.f);
		ASC->ApplyGameplayEffectSpecToSelf(Spec);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
