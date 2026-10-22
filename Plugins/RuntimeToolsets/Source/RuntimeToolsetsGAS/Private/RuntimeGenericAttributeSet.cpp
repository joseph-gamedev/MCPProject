#include "RuntimeGenericAttributeSet.h"
#include "GameplayEffectExtension.h"

namespace RuntimeGasTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCallerMagnitude, "Gas.SetByCaller.Magnitude", "SetByCaller key filled with the gameplay event magnitude by the gas toolset's effect ability");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(StateDead, "Gas.State.Dead", "Added by the generic attribute set while Health is 0");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(EventHit, "Gas.Event.Hit", "Generic hit gameplay event");
}

const TMap<FName, float>& URuntimeGenericAttributeSet::Defaults()
{
	static const TMap<FName, float> Values = {{TEXT("Health"), 100.f}, {TEXT("MaxHealth"), 100.f}, {TEXT("Shield"), 0.f}, {TEXT("Stamina"), 100.f},
		{TEXT("MaxStamina"), 100.f}, {TEXT("Mana"), 100.f}, {TEXT("MaxMana"), 100.f}, {TEXT("Speed"), 600.f}, {TEXT("Damage"), 0.f}};
	return Values;
}

void URuntimeGenericAttributeSet::Clamp(const FGameplayAttribute& Attribute, float& NewValue) const
{
	auto ClampTo = [&NewValue](float Max)
	{
		NewValue = Max > 0.f ? FMath::Clamp(NewValue, 0.f, Max) : FMath::Max(0.f, NewValue);
	};
	if (Attribute == GetHealthAttribute())
	{
		ClampTo(MaxHealth.GetCurrentValue());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		ClampTo(MaxStamina.GetCurrentValue());
	}
	else if (Attribute == GetManaAttribute())
	{
		ClampTo(MaxMana.GetCurrentValue());
	}
	else if (Attribute == GetShieldAttribute() || Attribute == GetSpeedAttribute() || Attribute == GetMaxHealthAttribute()
		|| Attribute == GetMaxStaminaAttribute() || Attribute == GetMaxManaAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
	}
}

void URuntimeGenericAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	Clamp(Attribute, NewValue);
}

void URuntimeGenericAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	Clamp(Attribute, NewValue);
}

void URuntimeGenericAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}
	if (Attribute == GetHealthAttribute())
	{
		ASC->SetLooseGameplayTagCount(RuntimeGasTags::StateDead, NewValue <= 0.f ? 1 : 0);
	}
	else if (Attribute == GetMaxHealthAttribute() && MaxHealth.GetCurrentValue() > 0.f && Health.GetCurrentValue() > MaxHealth.GetCurrentValue())
	{
		ASC->SetNumericAttributeBase(GetHealthAttribute(), MaxHealth.GetCurrentValue());
	}
}

void URuntimeGenericAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	if (Data.EvaluatedData.Attribute != GetDamageAttribute())
	{
		return;
	}
	float Remaining = Damage.GetBaseValue();
	SetDamage(0.f);
	if (Remaining <= 0.f)
	{
		return;
	}
	const float Absorbed = FMath::Min(Shield.GetBaseValue(), Remaining);
	if (Absorbed > 0.f)
	{
		SetShield(Shield.GetBaseValue() - Absorbed);
		Remaining -= Absorbed;
	}
	if (Remaining > 0.f)
	{
		SetHealth(Health.GetBaseValue() - Remaining);
	}
}
