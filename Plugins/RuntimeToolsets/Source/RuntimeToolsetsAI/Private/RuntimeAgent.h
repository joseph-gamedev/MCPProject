#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "Perception/AIPerceptionTypes.h"
#include "RuntimeAgent.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;

/** Perception and movement settings of an AI agent. */
USTRUCT()
struct FRuntimeAgentSettings
{
	GENERATED_BODY()

	/** Walking speed in cm/s. */
	UPROPERTY()
	float MaxSpeed = 450.f;

	/** How far the agent sees (cm). 0 disables sight. */
	UPROPERTY()
	float SightRadius = 2500.f;

	/** Distance at which a seen target is lost again (cm, at least sight radius). */
	UPROPERTY()
	float LoseSightRadius = 3000.f;

	/** Half angle of the view cone in degrees (90 = 180 degree field of view). */
	UPROPERTY()
	float SightHalfAngle = 60.f;

	/** How far the agent hears noise events (cm). 0 disables hearing. */
	UPROPERTY()
	float HearingRange = 2000.f;

	/** Seconds a perceived stimulus is remembered. */
	UPROPERTY()
	float MemorySeconds = 5.f;

	/** Turn rate in degrees per second. */
	UPROPERTY()
	float TurnRate = 360.f;
};

/** AI controller of runtime agents: AI perception (sight and hearing) configured at runtime. */
UCLASS()
class ARuntimeAgentController : public AAIController
{
	GENERATED_BODY()

public:
	ARuntimeAgentController();

	/** Applies sight/hearing ranges (re-registers the senses). */
	void ApplySettings(const FRuntimeAgentSettings& Settings);

	/** Perceives friend, neutral and enemy alike (runtime agents have no teams by default). */
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override { return ETeamAttitude::Hostile; }

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;
};

/**
 * Body of a runtime agent: a character (capsule + character movement) that an AI controller drives along the navmesh.
 * A builder object made into an agent is attached to it, so it moves, turns and is perceived with it.
 */
UCLASS()
class ARuntimeAgentCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARuntimeAgentCharacter(const FObjectInitializer& ObjectInitializer);

	/** The visual actor this agent carries (null when the agent is a plain character). */
	UPROPERTY()
	TWeakObjectPtr<AActor> Body;

	UPROPERTY()
	FRuntimeAgentSettings Settings;
};

namespace RuntimeAgents
{
	/** The agent pawn for a reference: the pawn itself, the agent carrying an actor, or a pawn with an AI controller. */
	APawn* FindAgent(AActor* Actor);
	AAIController* ControllerOf(AActor* Actor);
	/** Makes an actor an AI agent (wraps non-pawns in an ARuntimeAgentCharacter). */
	APawn* MakeAgent(AActor* Actor, const FRuntimeAgentSettings& Settings, FString& OutError);
	/** Reverses MakeAgent (detaches the body, destroys the wrapper). */
	void RemoveAgent(APawn* Agent);
}
