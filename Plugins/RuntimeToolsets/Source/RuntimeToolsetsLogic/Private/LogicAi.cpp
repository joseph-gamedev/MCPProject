// Bridge from logic brains to Unreal's AI: brains with backend "ai" become AI agents (ai.create_agent), walk the navmesh
// through their AI controller, and sense with AI perception (sight and hearing) instead of the built-in cone and steering.
#include "LogicRuntime.h"
#include "LogicJsonHelpers.h"
#include "LogicTuning.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"

namespace
{
	TSharedPtr<FJsonObject> AiObj(const TSharedPtr<FJsonObject>& J, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return J.IsValid() && J->TryGetObjectField(Field, O) ? *O : nullptr;
	}
}

AActor* FLogicWorld::KeyActor(const FString& Key) const
{
	if (Key == TEXT("player"))
	{
		return Player.Get();
	}
	FRuntimeSceneGraph* G = IRuntimeToolsetsCore::Get().GetSceneGraph(World.Get());
	const FRuntimeSceneNode* N = G ? G->Find(Key) : nullptr;
	return N ? N->Actor.Get() : nullptr;
}

AAIController* FLogicWorld::EnsureAiAgent(FBrainAgent& Ag, AActor* A, const TSharedPtr<FJsonObject>& Brain)
{
	APawn* Pawn = Cast<APawn>(A->GetAttachParentActor());
	if (!Pawn && !Ag.bAiTried)
	{
		Ag.bAiTried = true;
		// Settings from the brain: vision cone -> AI sight, hearing radius -> AI hearing, fastest state -> max speed.
		const TSharedPtr<FJsonObject> Vision = AiObj(Brain, TEXT("vision"));
		const TSharedPtr<FJsonObject> Hearing = AiObj(Brain, TEXT("hearing"));
		double MaxSpeed = 300.0;
		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (Brain->TryGetArrayField(TEXT("states"), States))
		{
			for (const TSharedPtr<FJsonValue>& S : *States)
			{
				MaxSpeed = FMath::Max(MaxSpeed, Num(S->AsObject(), TEXT("speed"), 0.0));
			}
		}
		TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("max_speed"), MaxSpeed);
		Settings->SetNumberField(TEXT("sight_radius"), Vision.IsValid() ? Num(Vision, TEXT("radius"), 1200.0) : 0.0);
		Settings->SetNumberField(TEXT("lose_sight_radius"), Vision.IsValid() ? Num(Vision, TEXT("radius"), 1200.0) * 1.2 : 0.0);
		Settings->SetNumberField(TEXT("sight_half_angle"), Vision.IsValid() ? Num(Vision, TEXT("angle"), 90.0) / 2.0 : 60.0);
		Settings->SetNumberField(TEXT("hearing_range"), Num(Hearing, TEXT("radius"), 0.0));
		Settings->SetNumberField(TEXT("turn_rate"), Num(Brain, TEXT("turn_speed"), LogicTuning::Get(TEXT("brain.turn_speed"))));
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("target"), Ag.Node);
		Args->SetObjectField(TEXT("settings"), Settings);
		const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("ai.create_agent"), Args, FToolCallerContext(), 1);
		if (!R.IsOk())
		{
			FLogicEvent E;
			E.On = TEXT("ai");
			E.Name = Ag.Node;
			AddLog(E, {}, {FString::Printf(TEXT("brain %s on %s could not become an AI agent (%s); using built-in steering"), *Ag.Brain, *Ag.Node, *R.Message)});
			return nullptr;
		}
		Pawn = Cast<APawn>(A->GetAttachParentActor());
	}
	return Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
}

void FLogicWorld::PerceiveAi(FBrainAgent& Ag, AAIController* Ctrl)
{
	UAIPerceptionComponent* P = Ctrl->GetPerceptionComponent();
	ACharacter* C = Player.Get();
	const FHealthState* PlayerHealth = Healths.Find(TEXT("player"));
	Ag.bSees = false;
	if (!P || !C || (PlayerHealth && PlayerHealth->bDead) || bFrozen)
	{
		return;
	}
	FActorPerceptionBlueprintInfo Info;
	if (P->GetActorsPerception(C, Info))
	{
		const FAISenseID SightId = UAISense::GetSenseID<UAISense_Sight>();
		const FAISenseID HearingId = UAISense::GetSenseID<UAISense_Hearing>();
		for (const FAIStimulus& S : Info.LastSensedStimuli)
		{
			if (S.Type == SightId && S.IsActive() && S.GetAge() < 0.05f)
			{
				Ag.bSees = true;
			}
			// Heard recently (gunshots, footsteps): remember where.
			if (S.Type == HearingId && S.WasSuccessfullySensed() && S.GetAge() < LogicTuning::Get(TEXT("brain.perceive_interval")) * 2.0 + 0.1)
			{
				Ag.bHeard = true;
				Ag.LastKnown = S.StimulusLocation;
				Ag.bHasLastKnown = true;
			}
		}
	}
	// Noises from anything (alarms, explosions reported with ai.report_noise) count too.
	TArray<AActor*> Heard;
	P->GetCurrentlyPerceivedActors(UAISense_Hearing::StaticClass(), Heard);
	for (AActor* H : Heard)
	{
		FActorPerceptionBlueprintInfo HI;
		if (H != C && P->GetActorsPerception(H, HI))
		{
			for (const FAIStimulus& S : HI.LastSensedStimuli)
			{
				if (S.Type == UAISense::GetSenseID<UAISense_Hearing>() && S.WasSuccessfullySensed() && S.GetAge() < 0.5f)
				{
					Ag.bHeard = true;
					Ag.LastKnown = S.StimulusLocation;
					Ag.bHasLastKnown = true;
				}
			}
		}
	}
	if (Ag.bSees)
	{
		Ag.SinceSeen = 0.0;
		Ag.LastKnown = C->GetActorLocation();
		Ag.bHasLastKnown = true;
	}
}

bool FLogicWorld::MoveAi(FBrainAgent& Ag, AAIController* Ctrl, AActor* BodyActor, const FVector& BodyGoal, bool bHasGoal, double Speed, const FString& Face, const FVector* FaceTarget)
{
	ACharacter* Body = Cast<ACharacter>(Ctrl->GetPawn());
	// Brain goals are for the object's pivot; the capsule carrying it is offset from that pivot.
	FVector Goal = BodyGoal;
	if (Body && BodyActor)
	{
		const FVector Offset = Body->GetActorLocation() - BodyActor->GetActorLocation();
		Goal += FVector(Offset.X, Offset.Y, 0.0);
	}
	if (Body)
	{
		Body->GetCharacterMovement()->MaxWalkSpeed = static_cast<float>(Speed);
		// Face the target (aiming, watching) or the way it walks.
		const bool bFocus = Face == TEXT("target") && FaceTarget;
		Body->GetCharacterMovement()->bOrientRotationToMovement = !bFocus;
		Body->GetCharacterMovement()->bUseControllerDesiredRotation = bFocus;
		if (bFocus)
		{
			Ctrl->SetFocalPoint(*FaceTarget);
		}
		else
		{
			Ctrl->ClearFocus(EAIFocusPriority::Gameplay);
		}
		Ag.Yaw = Body->GetActorRotation().Yaw;
	}
	if (!bHasGoal)
	{
		if (Ctrl->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			Ctrl->StopMovement();
		}
		Ag.AiGoal = FVector(1e9);
		return false;
	}
	// Re-plan only when the goal moved noticeably or the agent stopped (paths are not free).
	if (FVector::Dist2D(Goal, Ag.AiGoal) > 120.0 || Ctrl->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		// Arrival is measured from the centre (not the capsule edge, UE's default) so the brain's waypoint and goal
		// checks, which use the object's position, see it arrive.
		FAIMoveRequest Req(Goal);
		Req.SetAcceptanceRadius(static_cast<float>(LogicTuning::Get(TEXT("brain.waypoint_reach")) * 0.4));
		Req.SetReachTestIncludesAgentRadius(false);
		Req.SetUsePathfinding(true);
		Req.SetAllowPartialPath(true);
		Req.SetProjectGoalLocation(true);
		Req.SetCanStrafe(true);
		const EPathFollowingRequestResult::Type R = Ctrl->MoveTo(Req);
		Ag.AiGoal = Goal;
		if (R == EPathFollowingRequestResult::AlreadyAtGoal)
		{
			return false;
		}
		if (R == EPathFollowingRequestResult::Failed)
		{
			return false;
		}
	}
	return Ctrl->GetMoveStatus() == EPathFollowingStatus::Moving || Ctrl->GetMoveStatus() == EPathFollowingStatus::Waiting;
}

void FLogicWorld::ReportAiNoise(const FVector& At, double Radius, AActor* Instigator, const TCHAR* Tag)
{
	if (UWorld* W = World.Get())
	{
		UAISense_Hearing::ReportNoiseEvent(W, At, 1.f, Instigator, static_cast<float>(Radius), FName(Tag));
	}
}
