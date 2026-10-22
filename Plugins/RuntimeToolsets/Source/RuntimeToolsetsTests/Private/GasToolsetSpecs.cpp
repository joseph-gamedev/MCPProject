#include "ToolTestHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RuntimeJson.h"
#include "RuntimeToolsetsCore.h"

BEGIN_DEFINE_SPEC(FGasToolsetSpec, "RuntimeToolsets.Toolsets.Gas", RUNTIMETOOLSETS_TEST_FLAGS)
	TUniquePtr<FToolTestHarness> H;

	/** returnValue (object) of a reflected tool call. */
	TSharedPtr<FJsonObject> Ret(const TCHAR* Tool, const FString& Args)
	{
		return H->CallOk(Tool, Args)->GetObjectField(TEXT("returnValue"));
	}
	TArray<TSharedPtr<FJsonValue>> RetArray(const TCHAR* Tool, const FString& Args)
	{
		const TSharedRef<FJsonObject> R = H->CallOk(Tool, Args);
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return R->TryGetArrayField(TEXT("returnValue"), A) ? *A : TArray<TSharedPtr<FJsonValue>>();
	}
	/** Current (or base) value of an attribute in an attribute-value array; -1 if missing. */
	double Attr(const TArray<TSharedPtr<FJsonValue>>& Values, const TCHAR* Name, bool bBase = false)
	{
		for (const TSharedPtr<FJsonValue>& V : Values)
		{
			if (V->AsObject()->GetStringField(TEXT("attribute_name")) == Name)
			{
				return V->AsObject()->GetNumberField(bBase ? TEXT("base_value") : TEXT("current_value"));
			}
		}
		return -1.0;
	}
	double Attr(const FString& Target, const TCHAR* Name, bool bBase = false)
	{
		return Attr(RetArray(TEXT("gas.get_attribute_values"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Target)), Name, bBase);
	}
	bool HasString(const TArray<TSharedPtr<FJsonValue>>& Values, const TCHAR* S)
	{
		return Values.ContainsByPredicate([S](const TSharedPtr<FJsonValue>& V) { return V->AsString() == S; });
	}
	TArray<TSharedPtr<FJsonValue>> Tags(const FString& Target)
	{
		return RetArray(TEXT("gas.get_active_tags"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Target));
	}
	/** Spawns a cube with an ability system (extra attributes JSON object, e.g. {"Shield":20}). */
	FString GasCube(const TCHAR* Attributes = TEXT("{}"))
	{
		const FString Cube = H->SpawnCube();
		H->CallOk(TEXT("gas.add_ability_system"), FString::Printf(TEXT(R"J({"target":"%s","attributes":%s})J"), *Cube, Attributes));
		return Cube;
	}
END_DEFINE_SPEC(FGasToolsetSpec)

void FGasToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		H = MakeUnique<FToolTestHarness>(*this);
		H->Setup();
	});
	AfterEach([this]() { H.Reset(); });

	It("documents tools from the doc comments", [this]()
	{
		const TSharedPtr<FJsonObject> D = RuntimeJson::Parse(IRuntimeToolsetsCore::Get().DescribeToolset(TEXT("gas"), true));
		TSharedPtr<FJsonObject> Apply;
		for (const TSharedPtr<FJsonValue>& V : D->GetArrayField(TEXT("tools")))
		{
			if (V->AsObject()->GetStringField(TEXT("name")) == TEXT("gas.apply_effect"))
			{
				Apply = V->AsObject();
			}
		}
		TestTrue(TEXT("apply_effect exists"), Apply.IsValid());
		if (Apply.IsValid())
		{
			const TSharedPtr<FJsonObject> In = Apply->GetObjectField(TEXT("input_schema"));
			TestTrue(TEXT("param docs"), In->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("effect"))->HasField(TEXT("description")));
			TestFalse(TEXT("set_by_caller optional"),
				In->GetArrayField(TEXT("required")).ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return V->AsString() == TEXT("set_by_caller"); }));
		}
	});

	It("adds an ability system with the generic attribute set to a builder cube, undoably", [this]()
	{
		const FString Cube = H->SpawnCube();
		const FString T = FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube);
		H->CallFails(TEXT("gas.get_attribute_values"), T, ERuntimeToolError::WrongState);
		H->CallFails(TEXT("gas.add_ability_system"), FString::Printf(TEXT(R"J({"target":"%s","attributes":{"Mojo":3}})J"), *Cube), ERuntimeToolError::InvalidArguments);
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("gas.add_ability_system"), FString::Printf(TEXT(R"J({"target":"%s","attributes":{"Health":150,"Shield":20}})J"), *Cube));
		TestEqual(TEXT("id"), R->GetStringField(TEXT("id")), Cube);
		TestTrue(TEXT("component added"), R->GetBoolField(TEXT("added_component")));
		const TArray<TSharedPtr<FJsonValue>> Values = R->GetArrayField(TEXT("attributes"));
		TestEqual(TEXT("9 attributes"), Values.Num(), 9);
		TestEqual(TEXT("health"), Attr(Values, TEXT("Health")), 150.0);
		TestEqual(TEXT("max health raised"), Attr(Values, TEXT("MaxHealth")), 150.0);
		TestEqual(TEXT("shield"), Attr(Values, TEXT("Shield")), 20.0);
		TestEqual(TEXT("speed default"), Attr(Values, TEXT("Speed")), 600.0);
		H->CallFails(TEXT("gas.add_ability_system"), T, ERuntimeToolError::WrongState);

		const TArray<TSharedPtr<FJsonValue>> Sets = RetArray(TEXT("gas.find_attribute_set_classes"), TEXT("{}"));
		TestTrue(TEXT("generic set listed"), Sets.ContainsByPredicate([](const TSharedPtr<FJsonValue>& V)
			{ return V->AsObject()->GetStringField(TEXT("class_name")) == TEXT("RuntimeGenericAttributeSet") && V->AsObject()->GetArrayField(TEXT("attributes")).Num() == 9; }));

		const TSharedRef<FJsonObject> Set = H->CallOk(TEXT("gas.set_attribute_base"), FString::Printf(TEXT(R"J({"target":"%s","attribute":"MaxMana","value":250})J"), *Cube));
		TestEqual(TEXT("set base"), Set->GetNumberField(TEXT("base_value")), 250.0);
		H->CallFails(TEXT("gas.set_attribute_base"), FString::Printf(TEXT(R"J({"target":"%s","attribute":"Mojo","value":1})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("set undone"), Attr(Cube, TEXT("MaxMana")), 100.0);
		H->CallOk(TEXT("history.undo"));
		H->CallFails(TEXT("gas.get_attribute_values"), T, ERuntimeToolError::WrongState);
	});

	It("defines effects at runtime and applies instant and duration effects", [this]()
	{
		const FString Cube = GasCube(TEXT(R"J({"Shield":20})J"));
		H->CallOk(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestHit","duration_policy":"instant","replace":true,"modifiers":[{"attribute":"Damage","magnitude":30}]})J"));
		H->CallOk(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestHeal","duration_policy":"instant","replace":true,"modifiers":[{"attribute":"Health","magnitude":50}]})J"));
		const TSharedPtr<FJsonObject> Haste = Ret(TEXT("gas.define_effect"),
			TEXT(R"J({"name":"TestHaste","duration_policy":"has_duration","duration":10,"replace":true,"granted_tags":["Gas.Event.Hit"],"modifiers":[{"attribute":"Speed","op":"multiply","magnitude":1.5}]})J"));
		TestEqual(TEXT("full attribute name"), Haste->GetArrayField(TEXT("modifiers"))[0]->AsObject()->GetStringField(TEXT("attribute")), FString(TEXT("RuntimeGenericAttributeSet.Speed")));
		TestTrue(TEXT("listed"), RetArray(TEXT("gas.list_effects"), TEXT("{}")).ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return V->AsObject()->GetStringField(TEXT("name")) == TEXT("TestHaste"); }));

		// Instant: damage drains the shield first, then health; undo restores the base values.
		const TSharedRef<FJsonObject> Hit = H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHit"})J"), *Cube));
		TestTrue(TEXT("applied"), Hit->GetBoolField(TEXT("applied")));
		TestEqual(TEXT("instant has no handle"), Hit->GetNumberField(TEXT("handle")), -1.0);
		TestEqual(TEXT("shield absorbed"), Attr(Hit->GetArrayField(TEXT("attributes")), TEXT("Shield")), 0.0);
		TestEqual(TEXT("health took the rest"), Attr(Hit->GetArrayField(TEXT("attributes")), TEXT("Health")), 90.0);
		TestEqual(TEXT("damage reset"), Attr(Hit->GetArrayField(TEXT("attributes")), TEXT("Damage")), 0.0);
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("undo restores health"), Attr(Cube, TEXT("Health")), 100.0);
		TestEqual(TEXT("undo restores shield"), Attr(Cube, TEXT("Shield")), 20.0);
		H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHit"})J"), *Cube));
		TestEqual(TEXT("heal clamps to max"), Attr(H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHeal"})J"), *Cube))
			->GetArrayField(TEXT("attributes")), TEXT("Health")), 100.0);

		// Duration: modifies the current value and grants tags while active.
		const TSharedRef<FJsonObject> Buff = H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHaste"})J"), *Cube));
		const int32 Handle = static_cast<int32>(Buff->GetNumberField(TEXT("handle")));
		TestTrue(TEXT("active handle"), Handle >= 0);
		TestEqual(TEXT("speed current"), Attr(Buff->GetArrayField(TEXT("attributes")), TEXT("Speed")), 900.0);
		TestEqual(TEXT("speed base untouched"), Attr(Buff->GetArrayField(TEXT("attributes")), TEXT("Speed"), true), 600.0);
		TestTrue(TEXT("tag granted"), HasString(Buff->GetArrayField(TEXT("tags")), TEXT("Gas.Event.Hit")));
		const TArray<TSharedPtr<FJsonValue>> Active = RetArray(TEXT("gas.get_active_effects"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube));
		TestEqual(TEXT("one active effect"), Active.Num(), 1);
		if (Active.Num() == 1)
		{
			const TSharedPtr<FJsonObject> E = Active[0]->AsObject();
			TestEqual(TEXT("effect name"), E->GetStringField(TEXT("effect_name")), FString(TEXT("TestHaste")));
			TestEqual(TEXT("duration"), E->GetNumberField(TEXT("total_duration")), 10.0);
			TestTrue(TEXT("granted tags reported"), HasString(E->GetArrayField(TEXT("granted_tags")), TEXT("Gas.Event.Hit")));
		}

		// Removal by handle (undo re-applies), then by name.
		const TSharedRef<FJsonObject> Removed = H->CallOk(TEXT("gas.remove_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"%d"})J"), *Cube, Handle));
		TestEqual(TEXT("removed"), Removed->GetNumberField(TEXT("removed")), 1.0);
		TestEqual(TEXT("speed back"), Attr(Removed->GetArrayField(TEXT("attributes")), TEXT("Speed")), 600.0);
		TestFalse(TEXT("tag gone"), HasString(Removed->GetArrayField(TEXT("tags")), TEXT("Gas.Event.Hit")));
		H->CallOk(TEXT("history.undo"));
		TestEqual(TEXT("undo re-applies"), Attr(Cube, TEXT("Speed")), 900.0);
		TestEqual(TEXT("removed by name"), H->CallOk(TEXT("gas.remove_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHaste"})J"), *Cube))->GetNumberField(TEXT("removed")), 1.0);

		// Failures.
		const FString NoAsc = H->SpawnCube();
		H->CallFails(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHit"})J"), *NoAsc), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"NoSuchEffect"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.remove_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestHaste"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestHit","duration_policy":"instant","modifiers":[{"attribute":"Damage","magnitude":1}]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBad","duration_policy":"instant","replace":true,"modifiers":[{"attribute":"Mojo","magnitude":1}]})J"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBad","duration_policy":"has_duration","replace":true,"modifiers":[{"attribute":"Speed","magnitude":1}]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBad","duration_policy":"instant","replace":true,"granted_tags":["Gas.Event.Hit"],"modifiers":[{"attribute":"Speed","magnitude":1}]})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBad","duration_policy":"infinite","replace":true,"granted_tags":["Gas.Evnt.Hit"]})J"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBad","duration_policy":"infinite","replace":true})J"), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBad","duration_policy":"sometimes","replace":true,"modifiers":[{"attribute":"Speed","magnitude":1}]})J"), ERuntimeToolError::InvalidArguments);
	});

	It("routes damage through the shield, marks death and handles stacking and periods", [this]()
	{
		const FString Cube = GasCube(TEXT(R"J({"Health":50,"Shield":30})J"));
		H->CallOk(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestBigHit","duration_policy":"instant","replace":true,"modifiers":[{"attribute":"Damage","magnitude":100}]})J"));
		const TSharedRef<FJsonObject> R = H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestBigHit"})J"), *Cube));
		TestEqual(TEXT("shield gone"), Attr(R->GetArrayField(TEXT("attributes")), TEXT("Shield")), 0.0);
		TestEqual(TEXT("health clamped at 0"), Attr(R->GetArrayField(TEXT("attributes")), TEXT("Health")), 0.0);
		TestTrue(TEXT("dead tag"), HasString(R->GetArrayField(TEXT("tags")), TEXT("Gas.State.Dead")));
		H->CallOk(TEXT("gas.set_attribute_base"), FString::Printf(TEXT(R"J({"target":"%s","attribute":"Health","value":40})J"), *Cube));
		TestFalse(TEXT("revived"), HasString(Tags(Cube), TEXT("Gas.State.Dead")));

		// Stacking up to 2 and a periodic effect.
		H->CallOk(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestPoison","duration_policy":"has_duration","duration":5,"period":1,"stacking":"aggregate_by_target","stack_limit":2,"replace":true,"modifiers":[{"attribute":"Damage","magnitude":4}]})J"));
		for (int32 i = 0; i < 3; ++i)
		{
			H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestPoison"})J"), *Cube));
		}
		const TArray<TSharedPtr<FJsonValue>> Active = RetArray(TEXT("gas.get_active_effects"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube));
		TestEqual(TEXT("one stacked effect"), Active.Num(), 1);
		if (Active.Num() == 1)
		{
			TestEqual(TEXT("stack limit"), Active[0]->AsObject()->GetNumberField(TEXT("stack_count")), 2.0);
			TestEqual(TEXT("period"), Active[0]->AsObject()->GetNumberField(TEXT("period")), 1.0);
		}
		TestTrue(TEXT("damage over time does not raise health"), Attr(Cube, TEXT("Health")) <= 40.0);

		// SetByCaller magnitudes.
		H->CallOk(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestScaledHit","duration_policy":"instant","replace":true,"modifiers":[{"attribute":"Damage","set_by_caller":"Gas.SetByCaller.Magnitude"}]})J"));
		H->CallFails(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestScaledHit"})J"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestScaledHit","set_by_caller":{"Gas.Nope":1}})J"), *Cube), ERuntimeToolError::NotFound);
		const double Before = Attr(Cube, TEXT("Health"));
		H->CallOk(TEXT("gas.apply_effect"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestScaledHit","set_by_caller":{"Gas.SetByCaller.Magnitude":7}})J"), *Cube));
		TestEqual(TEXT("set by caller damage"), Attr(Cube, TEXT("Health")), Before - 7.0);
	});

	It("grants abilities, activates them by class, tag and gameplay event", [this]()
	{
		const FString Cube = GasCube();
		H->CallOk(TEXT("gas.define_effect"), TEXT(R"J({"name":"TestEventHit","duration_policy":"instant","replace":true,"modifiers":[{"attribute":"Damage","set_by_caller":"Gas.SetByCaller.Magnitude"}]})J"));
		TestTrue(TEXT("built-in ability listed"), RetArray(TEXT("gas.list_ability_classes"), TEXT(R"J({"filter":"RuntimeGas"})J")).ContainsByPredicate([](const TSharedPtr<FJsonValue>& V)
			{ return V->AsObject()->GetStringField(TEXT("name")) == TEXT("RuntimeGasEffectAbility"); }));
		const TSharedRef<FJsonObject> Given = H->CallOk(TEXT("gas.give_ability"),
			FString::Printf(TEXT(R"J({"target":"%s","effect":"TestEventHit","trigger_event":"Gas.Event.Hit","ability_tags":["Gas.Event.Hit"]})J"), *Cube));
		TestEqual(TEXT("ability"), Given->GetStringField(TEXT("ability_name")), FString(TEXT("RuntimeGasEffectAbility")));
		TestTrue(TEXT("trigger"), HasString(Given->GetArrayField(TEXT("trigger_events")), TEXT("Gas.Event.Hit")));
		const TArray<TSharedPtr<FJsonValue>> Granted = RetArray(TEXT("gas.get_granted_abilities"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube));
		TestTrue(TEXT("granted with effect"), Granted.Num() == 1 && Granted[0]->AsObject()->GetStringField(TEXT("effect")) == TEXT("TestEventHit"));

		const TSharedRef<FJsonObject> Ev = H->CallOk(TEXT("gas.send_gameplay_event"), FString::Printf(TEXT(R"J({"target":"%s","event_tag":"Gas.Event.Hit","magnitude":25})J"), *Cube));
		TestEqual(TEXT("event triggered the ability"), Ev->GetNumberField(TEXT("triggered_abilities")), 1.0);
		TestEqual(TEXT("event magnitude became damage"), Attr(Ev->GetArrayField(TEXT("attributes")), TEXT("Health")), 75.0);

		const TSharedRef<FJsonObject> ByClass = H->CallOk(TEXT("gas.try_activate_ability"), FString::Printf(TEXT(R"J({"target":"%s","ability":"RuntimeGasEffectAbility"})J"), *Cube));
		TestTrue(TEXT("activated by class"), ByClass->GetBoolField(TEXT("activated")));
		TestTrue(TEXT("activated by tag"), H->CallOk(TEXT("gas.try_activate_ability"), FString::Printf(TEXT(R"J({"target":"%s","ability":"Gas.Event.Hit"})J"), *Cube))->GetBoolField(TEXT("activated")));
		TestEqual(TEXT("nothing left active"), H->CallOk(TEXT("gas.cancel_abilities"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube))->GetNumberField(TEXT("returnValue")), 0.0);

		H->CallOk(TEXT("history.undo")); // give_ability
		TestEqual(TEXT("ungranted"), RetArray(TEXT("gas.get_granted_abilities"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube)).Num(), 0);
		TestEqual(TEXT("event does nothing now"), H->CallOk(TEXT("gas.send_gameplay_event"), FString::Printf(TEXT(R"J({"target":"%s","event_tag":"Gas.Event.Hit","magnitude":25})J"), *Cube))
			->GetNumberField(TEXT("triggered_abilities")), 0.0);

		H->CallFails(TEXT("gas.give_ability"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.give_ability"), FString::Printf(TEXT(R"J({"target":"%s","ability_class":"GA_DoesNotExist"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.give_ability"), FString::Printf(TEXT(R"J({"target":"%s","effect":"NoSuchEffect"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.give_ability"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestEventHit","trigger_event":"Gas.Evnt"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.try_activate_ability"), FString::Printf(TEXT(R"J({"target":"%s","ability":"Nope"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.send_gameplay_event"), FString::Printf(TEXT(R"J({"target":"%s","event_tag":"Gas.Evnt.Hit"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.cancel_abilities"), FString::Printf(TEXT(R"J({"target":"%s","tags":["Gas.Nope"]})J"), *Cube), ERuntimeToolError::NotFound);
		const FString NoAsc = H->SpawnCube();
		H->CallFails(TEXT("gas.give_ability"), FString::Printf(TEXT(R"J({"target":"%s","effect":"TestEventHit"})J"), *NoAsc), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("gas.send_gameplay_event"), FString::Printf(TEXT(R"J({"target":"%s","event_tag":"Gas.Event.Hit"})J"), *NoAsc), ERuntimeToolError::WrongState);
	});

	It("adds and removes loose tags, lists cues and rejects bad cues", [this]()
	{
		const FString Cube = GasCube();
		const TArray<TSharedPtr<FJsonValue>> Added = RetArray(TEXT("gas.add_loose_tags"), FString::Printf(TEXT(R"J({"target":"%s","tags":["Gas.Event.Hit"]})J"), *Cube));
		TestTrue(TEXT("added"), HasString(Added, TEXT("Gas.Event.Hit")));
		const FToolResult Bad = H->Call(TEXT("gas.add_loose_tags"), FString::Printf(TEXT(R"J({"target":"%s","tags":["Gas.Event.Hti"]})J"), *Cube));
		TestEqual(TEXT("unknown tag"), Bad.Error, ERuntimeToolError::NotFound);
		TestTrue(FString::Printf(TEXT("close matches in hint (%s)"), *Bad.Hint), Bad.Hint.Contains(TEXT("Gas.Event.Hit")));
		TestFalse(TEXT("removed"), HasString(RetArray(TEXT("gas.remove_loose_tags"), FString::Printf(TEXT(R"J({"target":"%s","tags":["Gas.Event.Hit"]})J"), *Cube)), TEXT("Gas.Event.Hit")));
		H->CallOk(TEXT("history.undo"));
		TestTrue(TEXT("undo restores"), HasString(Tags(Cube), TEXT("Gas.Event.Hit")));
		H->CallOk(TEXT("history.undo"));
		TestFalse(TEXT("undo add"), HasString(Tags(Cube), TEXT("Gas.Event.Hit")));
		H->CallFails(TEXT("gas.add_loose_tags"), FString::Printf(TEXT(R"J({"target":"%s","tags":[]})J"), *Cube), ERuntimeToolError::InvalidArguments);

		H->CallOk(TEXT("gas.list_cues"));
		H->CallFails(TEXT("gas.list_cues"), TEXT(R"J({"parent_tag":"GameplayCue.Nope.Nope"})J"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.execute_cue"), FString::Printf(TEXT(R"J({"target":"%s","cue_tag":"Gas.Event.Hit"})J"), *Cube), ERuntimeToolError::InvalidArguments);
		H->CallFails(TEXT("gas.execute_cue"), FString::Printf(TEXT(R"J({"target":"%s","cue_tag":"GameplayCue.Nope.Nope"})J"), *Cube), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.get_active_effects"), TEXT(R"J({"target":"n999999"})J"), ERuntimeToolError::NotFound);
		H->CallFails(TEXT("gas.get_granted_abilities"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *H->SpawnCube()), ERuntimeToolError::WrongState);
		H->CallFails(TEXT("gas.get_active_tags"), FString::Printf(TEXT(R"J({"target":"%s"})J"), *H->SpawnCube()), ERuntimeToolError::WrongState);
	});
}

#endif
