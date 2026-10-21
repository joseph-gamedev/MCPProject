// Flight: an arcade jet for the player, 3D dogfighting brains, and target locking for homing weapons.
#include "LogicTuning.h"
#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	double Num(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, double Default)
	{
		double V = Default;
		if (J.IsValid())
		{
			J->TryGetNumberField(Field, V);
		}
		return V;
	}

	FString Str(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Default = FString())
	{
		FString V = Default;
		if (J.IsValid())
		{
			J->TryGetStringField(Field, V);
		}
		return V;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& J, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return J.IsValid() && J->TryGetObjectField(Field, O) ? *O : nullptr;
	}

	/** Turns From toward To by at most MaxDeg. */
	FVector TurnToward(const FVector& From, const FVector& To, double MaxDeg)
	{
		const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(From, To), -1.0, 1.0)));
		if (Angle <= MaxDeg || Angle < 1e-3)
		{
			return To;
		}
		const FQuat Q = FQuat::FindBetweenNormals(From, To);
		return FQuat::Slerp(FQuat::Identity, Q, MaxDeg / Angle).RotateVector(From).GetSafeNormal();
	}
}

bool FLogicWorld::IsFlight() const
{
	return PD().IsValid() && Str(PD(), TEXT("mode")) == TEXT("flight");
}

bool FLogicWorld::IsGrid() const
{
	return PD().IsValid() && Num(PD(), TEXT("grid"), 0.0) > 0.0;
}

FString FLogicWorld::LockTarget(const FString& Owner, const FVector& From, const FVector& Dir, double Range, double AngleDeg, const FString& Hits) const
{
	FString Best;
	double BestAngle = AngleDeg;
	for (const TPair<FString, FHealthState>& H : Healths)
	{
		FBox B;
		if (H.Key == Owner || H.Value.bDead || !VictimMatches(H.Key, Hits) || !VictimBox(H.Key, B))
		{
			continue;
		}
		const FVector To = B.GetCenter() - From;
		const double Dist = To.Size();
		if (Dist > Range || Dist < 1.0)
		{
			continue;
		}
		const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, To / Dist), -1.0, 1.0)));
		if (Angle < BestAngle)
		{
			BestAngle = Angle;
			Best = H.Key;
		}
	}
	return Best;
}

void FLogicWorld::TickFlight(double Dt)
{
	ACharacter* C = Player.Get();
	if (!C || !IsFlight() || Dt <= 0.0)
	{
		return;
	}
	if (bFrozen)
	{
		FlightVelocity = FVector::ZeroVector;
		return;
	}
	const TSharedPtr<FJsonObject>& D = PD();
	const double Cruise = Num(D, TEXT("speed"), 4500.0);
	const double MinSpeed = Num(D, TEXT("min_speed"), Cruise * 0.45);
	const double MaxSpeed = Num(D, TEXT("max_speed"), Cruise * 1.6);
	const double Turn = Num(D, TEXT("turn"), 70.0);
	const double RollRate = Num(D, TEXT("roll_rate"), 160.0);
	FQuat Q = C->GetActorQuat();
	// Local rotations: pitch about right, yaw about up, roll about forward.
	const double Pitch = FMath::Clamp(FlightPitchIn, -1.5, 1.5) * Turn * Dt;
	const double Yaw = FMath::Clamp(FlightYawIn, -1.5, 1.5) * Turn * 0.75 * Dt;
	const double Roll = FMath::Clamp(FlightRollIn, -1.0, 1.0) * RollRate * Dt;
	Q = Q * FQuat(FRotator(Pitch, Yaw, Roll));
	// Banked turns: rolling also turns the nose; with no roll input the jet levels out toward the bank of the yaw input.
	FRotator R = Q.Rotator();
	if (FMath::Abs(FlightRollIn) < 0.05)
	{
		const double WantRoll = FMath::Clamp(FlightYawIn, -1.0, 1.0) * LogicTuning::Get(TEXT("flight.auto_bank"));
		R.Roll = FMath::FInterpTo(R.Roll, WantRoll, Dt, 2.0);
	}
	R.Yaw += FMath::Sin(FMath::DegreesToRadians(R.Roll)) * Turn * LogicTuning::Get(TEXT("flight.bank_turn")) * Dt;
	Q = R.Quaternion();
	const double Target = bFlightBoost ? MaxSpeed : FMath::Lerp(MinSpeed, Cruise, FMath::Clamp(FlightThrottle, 0.0, 1.0));
	FlightSpeed = FlightSpeed <= 0.0 ? Cruise : FMath::FInterpTo(FlightSpeed, Target, Dt, bFlightBoost ? 1.2 : 0.8);
	FlightVelocity = Q.GetForwardVector() * FlightSpeed;
	FHitResult Hit;
	C->SetActorLocationAndRotation(C->GetActorLocation() + FlightVelocity * Dt, Q, true, &Hit);
	if (Hit.bBlockingHit && bRunning)
	{
		// Crash: ground, terrain or a solid object.
		FLogicEvent E;
		E.On = TEXT("crash");
		E.Data.M->Add(TEXT("location"), FLogicValue::MakeList({FLogicValue::MakeNumber(Hit.ImpactPoint.X), FLogicValue::MakeNumber(Hit.ImpactPoint.Y),
			FLogicValue::MakeNumber(Hit.ImpactPoint.Z)}));
		Queue.Add(E);
		if (Healths.Contains(TEXT("player")))
		{
			Invulnerable = 0.0;
			Healths[TEXT("player")].InvulnUntil = -1.0;
			ApplyDamage(TEXT("player"), Num(D, TEXT("crash_damage"), 1e6), TEXT("crash"), TEXT("ground"), FVector::ZeroVector);
		}
		else
		{
			RespawnPlayer();
		}
	}
}

void FLogicWorld::TickFlightAgent(FBrainAgent& Ag, AActor* A, const TSharedPtr<FJsonObject>& Brain, const TSharedPtr<FJsonObject>& St, double Dt,
	const FVector& PlayerLoc, bool bPlayerAlive)
{
	const TSharedPtr<FJsonObject> F = Obj(Brain, TEXT("flight"));
	const double Speed = Num(St, TEXT("speed"), Num(F, TEXT("speed"), 4000.0));
	const double Turn = Num(St, TEXT("turn_rate"), Num(F, TEXT("turn_rate"), 55.0));
	const double MinAlt = Num(F, TEXT("min_altitude"), Ag.Home.Z - 2500.0);
	const FVector Loc = A->GetActorLocation();
	FVector Fwd = Ag.Rot.GetForwardVector();
	const FString Move = Str(St, TEXT("move"), TEXT("patrol"));
	FVector Goal = Loc + Fwd * 2000.0;
	if (Move == TEXT("chase") && bPlayerAlive)
	{
		// Lead pursuit; break off when about to collide.
		const double Dist = FVector::Dist(Loc, PlayerLoc);
		Goal = PlayerLoc + FlightVelocity * FMath::Clamp(Dist / FMath::Max(Speed, 1.0), 0.0, 1.5) * 0.6;
		if (Dist < Num(St, TEXT("keep_distance"), 900.0))
		{
			Goal = Loc + Fwd * 3000.0 + FVector(0, 0, 1200.0) + Ag.Rot.GetRightVector() * 1500.0;
		}
	}
	else if (Move == TEXT("flee") && bPlayerAlive)
	{
		Goal = Loc + (Loc - PlayerLoc).GetSafeNormal() * 4000.0 + FVector(0, 0, 800.0) + Ag.Rot.GetRightVector() * 1500.0 * FMath::Sin(Ag.TimeInState * 1.3);
	}
	else if (Move == TEXT("to_last_known") && Ag.bHasLastKnown)
	{
		Goal = Ag.LastKnown;
		Ag.bAtGoal = FVector::Dist(Loc, Goal) < 1500.0;
	}
	else if (Move == TEXT("home"))
	{
		Goal = Ag.Home;
	}
	else if (Move == TEXT("patrol") || Move == TEXT("wander") || Move == TEXT("stop") || Move == TEXT("strafe"))
	{
		// Orbit the post (or follow patrol points as an orbit centre).
		const double Radius = Num(St, TEXT("radius"), Num(F, TEXT("orbit_radius"), 4000.0));
		Ag.OrbitPhase += Dt * Speed / FMath::Max(Radius, 100.0);
		const FVector Centre = Ag.Patrol.Num() > 0 ? Ag.Patrol[0] : Ag.Home;
		Goal = Centre + FVector(FMath::Cos(Ag.OrbitPhase + 0.6), FMath::Sin(Ag.OrbitPhase + 0.6), 0) * Radius;
	}
	FVector Want = (Goal - Loc).GetSafeNormal();
	if (Loc.Z < MinAlt)
	{
		Want.Z = FMath::Max(Want.Z, 0.6);
		Want.Normalize();
	}
	const double PrevYaw = Fwd.Rotation().Yaw;
	Fwd = TurnToward(Fwd, Want, Turn * Dt);
	const double YawRate = FRotator::NormalizeAxis(Fwd.Rotation().Yaw - PrevYaw) / FMath::Max(Dt, 1e-4);
	Ag.Bank = FMath::FInterpTo(Ag.Bank, FMath::Clamp(YawRate * 1.2, -70.0, 70.0), Dt, 3.0);
	FRotator R = Fwd.Rotation();
	R.Roll = Ag.Bank;
	Ag.Rot = R.Quaternion();
	Ag.Speed = FMath::FInterpTo(Ag.Speed <= 0.0 ? Speed : Ag.Speed, Speed, Dt, 1.0);
	A->SetActorLocationAndRotation(Loc + Fwd * Ag.Speed * Dt, Ag.Rot);
	Ag.Yaw = R.Yaw;
	Ag.bMoving = true;
	// Guns: fire when the player is in front.
	bool bFire = false;
	St->TryGetBoolField(TEXT("fire"), bFire);
	if (bFire && Ag.bSees && bPlayerAlive)
	{
		const FVector To = (PlayerLoc - A->GetActorLocation()).GetSafeNormal();
		const double Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Fwd, To), -1.0, 1.0)));
		if (Off < Num(St, TEXT("fire_angle"), 12.0) && FVector::Dist(PlayerLoc, A->GetActorLocation()) < Num(St, TEXT("fire_range"), 9000.0))
		{
			const double Dist = FVector::Dist(PlayerLoc, A->GetActorLocation());
			const FVector Lead = PlayerLoc + FlightVelocity * (Dist / 15000.0);
			FString Err;
			FireWeapon(Ag.Node, Str(St, TEXT("weapon")), &Lead, Err);
		}
	}
}

void FLogicWorld::ResetAgentAt(const FString& Id, const FVector& At, const FRotator& Rot)
{
	FBrainAgent* Ag = Agents.Find(Id);
	if (!Ag)
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Brain = BrainDefs.Find(Ag->Brain);
	Ag->Home = At;
	Ag->Yaw = Rot.Yaw;
	Ag->Rot = Rot.Quaternion();
	Ag->Speed = 0.0;
	Ag->Bank = 0.0;
	Ag->TimeInState = 0.0;
	Ag->bSees = false;
	Ag->bHeard = false;
	Ag->bHasLastKnown = false;
	Ag->SinceSeen = 1e9;
	Ag->OrbitPhase = Random.FRand() * 6.28;
	if (Ag->Patrol.Num() > 0)
	{
		const FVector Offset = Ag->Patrol.Num() > 1 ? Ag->Patrol[1] - Ag->Patrol[0] : FVector::ZeroVector;
		Ag->Patrol = {At};
		if (!Offset.IsNearlyZero())
		{
			Ag->Patrol.Add(At + Offset);
		}
	}
	Ag->State = Brain ? Str(*Brain, TEXT("start")) : Ag->State;
	if (Ag->State.IsEmpty() && Brain)
	{
		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if ((*Brain)->TryGetArrayField(TEXT("states"), States) && States->Num() > 0)
		{
			Ag->State = Str((*States)[0]->AsObject(), TEXT("name"));
		}
	}
	Ag->Icon.Reset();
}

void FLogicWorld::ResetHealth(const FString& Id)
{
	if (FHealthState* H = Healths.Find(Id))
	{
		H->Current = H->Max;
		H->bDead = false;
		H->InvulnUntil = -1.0;
	}
}
