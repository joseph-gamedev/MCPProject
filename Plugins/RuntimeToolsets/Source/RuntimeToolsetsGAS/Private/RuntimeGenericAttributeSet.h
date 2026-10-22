#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "NativeGameplayTags.h"
#include "RuntimeGenericAttributeSet.generated.h"

/** Native tags shipped with the gas toolset (always present in the tag table). */
namespace RuntimeGasTags
{
	/** SetByCaller key the built-in effect ability fills with the gameplay event magnitude. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCallerMagnitude);
	/** Loose tag the generic attribute set adds while Health is 0. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(StateDead);
	/** Generic "hit" gameplay event. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(EventHit);
}

/**
 * Generic attribute set the gas toolset adds to any actor (gas.add_ability_system): vitals, resources and a Damage
 * meta-attribute. Damage is consumed when an instant (or periodic) effect executes: it is subtracted from Shield first,
 * the rest from Health, then reset to 0. Health, Stamina and Mana are clamped to [0, Max*] (when the max is > 0);
 * Shield and Speed never go below 0. While Health is 0 the owner carries the loose tag Gas.State.Dead.
 */
UCLASS()
class URuntimeGenericAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Vitals")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals")
	FGameplayAttributeData Shield;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, Shield)

	UPROPERTY(BlueprintReadOnly, Category = "Resources")
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "Resources")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, MaxStamina)

	UPROPERTY(BlueprintReadOnly, Category = "Resources")
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, Mana)

	UPROPERTY(BlueprintReadOnly, Category = "Resources")
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, MaxMana)

	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	FGameplayAttributeData Speed;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, Speed)

	/** Meta-attribute: incoming damage, consumed by PostGameplayEffectExecute (Shield first, then Health). */
	UPROPERTY(BlueprintReadOnly, Category = "Meta")
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS_BASIC(URuntimeGenericAttributeSet, Damage)

	/** Default initial values (Health 100, MaxHealth 100, Shield 0, Stamina 100, ..., Speed 600, Damage 0). */
	static const TMap<FName, float>& Defaults();

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

private:
	void Clamp(const FGameplayAttribute& Attribute, float& NewValue) const;
};
