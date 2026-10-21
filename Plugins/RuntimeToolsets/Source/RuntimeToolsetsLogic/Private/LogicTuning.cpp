#include "LogicTuning.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	struct FEntry
	{
		const TCHAR* Name;
		double Default;
		const TCHAR* Description;
		const TCHAR* Basis;
	};

	// Basis: "TDE" = TopDown Engine default, "scale" = real-world scale (1 unit = 1 cm), "feel" = tuned by playtesting.
	const TArray<FEntry>& Entries()
	{
		static const TArray<FEntry> List = {
			// AI brains
			{TEXT("brain.perceive_interval"), 0.1, TEXT("Seconds between vision checks per agent"), TEXT("feel: TDE scans every 1 s; 0.1 s makes guards react within a frame or two of you entering the cone")},
			{TEXT("brain.decide_interval"), 0.15, TEXT("Seconds between transition checks"), TEXT("feel: TDE randomises 0.5-1 s; shorter feels sharper in small arenas")},
			{TEXT("brain.near_notice"), 180.0, TEXT("Always notice the player within this distance (cm), whatever the cone"), TEXT("scale: about one body length")},
			{TEXT("brain.steer_probe"), 110.0, TEXT("How far ahead (cm) walkers check for walls"), TEXT("scale: a bit more than one step at walking speed")},
			{TEXT("brain.steer_radius"), 35.0, TEXT("Radius (cm) of the wall probe"), TEXT("scale: half a body width")},
			{TEXT("brain.waypoint_reach"), 40.0, TEXT("Distance (cm) at which a patrol point counts as reached"), TEXT("feel")},
			{TEXT("brain.goal_reach"), 70.0, TEXT("Distance (cm) at which the last known position counts as reached"), TEXT("feel")},
			{TEXT("brain.home_reach"), 60.0, TEXT("Distance (cm) at which an agent counts as home"), TEXT("feel")},
			{TEXT("brain.turn_speed"), 300.0, TEXT("Default turn speed (deg/s) when a brain sets none"), TEXT("feel: a quick but visible turn")},
			{TEXT("brain.waddle_roll"), 7.0, TEXT("Side-to-side tilt (deg) of walking agents"), TEXT("feel")},
			{TEXT("brain.waddle_bob"), 5.0, TEXT("Up-down bob (cm) of walking agents"), TEXT("feel")},
			// Combat feel
			{TEXT("combat.hit_punch_scale"), 1.15, TEXT("Scale an enemy swells to when hit"), TEXT("feel: TDE uses a scale-punch feedback")},
			{TEXT("combat.hit_punch_time"), 0.05, TEXT("Seconds of the hit punch (each way)"), TEXT("feel")},
			{TEXT("combat.squash_xy"), 1.4, TEXT("Width scale of the death squash"), TEXT("feel")},
			{TEXT("combat.squash_z"), 0.15, TEXT("Height scale of the death squash"), TEXT("feel")},
			{TEXT("combat.squash_time"), 0.25, TEXT("Seconds of the death squash"), TEXT("feel")},
			{TEXT("combat.fire_kick"), 14.0, TEXT("Distance (cm) a firing enemy kicks back"), TEXT("feel")},
			{TEXT("combat.player_invulnerable"), 1.0, TEXT("Default seconds the player is immune after a hit"), TEXT("TDE: DamageOnTouch gives 0.5 s; players get a little longer")},
			{TEXT("combat.respawn_invulnerable"), 1.5, TEXT("Seconds of immunity after a respawn"), TEXT("feel")},
			// Player animation
			{TEXT("anim.leg_swing"), 42.0, TEXT("Leg swing (deg) at full run"), TEXT("scale: a human stride is roughly +/-40 deg")},
			{TEXT("anim.arm_swing"), 36.0, TEXT("Arm swing (deg) at full run"), TEXT("scale")},
			{TEXT("anim.run_speed_ref"), 450.0, TEXT("Speed (cm/s) at which the walk cycle reaches full swing"), TEXT("scale: a brisk run")},
			{TEXT("anim.lean"), 7.0, TEXT("Forward lean (deg) at full run"), TEXT("feel")},
			{TEXT("anim.recoil"), 22.0, TEXT("Arm kick (deg) per shot"), TEXT("feel")},
			// Touch
			{TEXT("touch.retouch_cooldown"), 0.35, TEXT("Seconds before the same object can fire touch again"), TEXT("feel: longer than a bumped block's bounce")},
			{TEXT("touch.stomp_tolerance"), 12.0, TEXT("cm the feet may already be below an enemy's top and still count as a stomp"), TEXT("feel")},
			// Flight
			{TEXT("flight.bank_turn"), 0.55, TEXT("How much banking turns the nose (fraction of turn rate)"), TEXT("feel: arcade bank-and-yank")},
			{TEXT("flight.auto_bank"), 50.0, TEXT("Roll angle (deg) the jet banks to when yawing"), TEXT("feel")},
			{TEXT("flight.mouse_scale"), 0.6, TEXT("Steering per unit of mouse movement"), TEXT("feel")},
			{TEXT("flight.throttle_rate"), 0.7, TEXT("Throttle change per second while W/S is held"), TEXT("feel: idle to full in about 1.5 s")},
			{TEXT("flight.boost_fov"), 12.0, TEXT("Extra field of view (deg) while boosting"), TEXT("feel")},
			// HUD
			{TEXT("hud.enemy_bar_width"), 62.0, TEXT("Width (px) of health bars over enemies"), TEXT("feel")},
			{TEXT("hud.enemy_bar_height"), 9.0, TEXT("Height (px) of health bars over enemies"), TEXT("feel")},
			{TEXT("hud.flash_alpha"), 0.35, TEXT("Opacity of the red hit flash"), TEXT("feel")},
			{TEXT("hud.low_health"), 0.3, TEXT("Health fraction below which the screen edges pulse"), TEXT("feel: common in shooters")},
			{TEXT("hud.number_life"), 0.9, TEXT("Seconds damage numbers stay on screen"), TEXT("feel")},
			{TEXT("hud.number_rise"), 140.0, TEXT("cm per second damage numbers rise"), TEXT("feel")},
		};
		return List;
	}

	TMap<FString, double>& Overrides()
	{
		static TMap<FString, double> Map;
		return Map;
	}

	const FEntry* Find(const FString& Name)
	{
		return Entries().FindByPredicate([&Name](const FEntry& E) { return Name == E.Name; });
	}
}

namespace LogicTuning
{
	double Get(const TCHAR* Name)
	{
		if (const double* V = Overrides().Find(Name))
		{
			return *V;
		}
		const FEntry* E = Find(Name);
		ensureMsgf(E, TEXT("unknown tuning value %s"), Name);
		return E ? E->Default : 0.0;
	}

	bool Set(const FString& Name, double Value, FString& OutError)
	{
		if (!Find(Name))
		{
			OutError = FString::Printf(TEXT("unknown tuning value '%s' (see logic.get_tuning)"), *Name);
			return false;
		}
		Overrides().Add(Name, Value);
		return true;
	}

	void Reset()
	{
		Overrides().Reset();
		LoadConfig();
	}

	void LoadConfig()
	{
		if (!GConfig)
		{
			return;
		}
		for (const FEntry& E : Entries())
		{
			double V = 0.0;
			if (GConfig->GetDouble(TEXT("RuntimeToolsets.Tuning"), E.Name, V, GGameIni))
			{
				Overrides().Add(E.Name, V);
			}
		}
	}

	TSharedRef<FJsonObject> ToJson()
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		for (const FEntry& E : Entries())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("value"), Get(E.Name));
			O->SetNumberField(TEXT("default"), E.Default);
			O->SetStringField(TEXT("what"), E.Description);
			O->SetStringField(TEXT("basis"), E.Basis);
			J->SetObjectField(E.Name, O);
		}
		return J;
	}
}
