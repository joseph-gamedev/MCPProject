#include "LogicRuntime.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeToolsetsLogic, Log, All);

namespace
{
	const TArray<FString>& EventNames()
	{
		static const TArray<FString> Names = {TEXT("start"), TEXT("click"), TEXT("right_click"), TEXT("key"), TEXT("button"), TEXT("timer"),
			TEXT("var_changed"), TEXT("event"), TEXT("tick")};
		return Names;
	}

	const TArray<FString>& ActionNames()
	{
		static const TArray<FString> Names = {TEXT("set"), TEXT("set_item"), TEXT("push"), TEXT("let"), TEXT("if"), TEXT("for"), TEXT("call"), TEXT("emit"),
			TEXT("timer"), TEXT("cancel_timer"), TEXT("set_ui"), TEXT("log"), TEXT("restart"), TEXT("stop")};
		return Names;
	}

	constexpr int32 MaxEventsPerFire = 256;
	constexpr int32 MaxActionsPerFire = 20000;
	constexpr int32 MaxLog = 100;

	FString ReadString(const TSharedPtr<FJsonObject>& J, const TCHAR* Field)
	{
		FString S;
		J->TryGetStringField(Field, S);
		return S;
	}

	bool ReadVec2(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!J->TryGetArrayField(Field, A) || A->Num() != 2)
		{
			return false;
		}
		Out = FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
		return true;
	}

	bool ReadColor(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, FLinearColor& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!J->TryGetArrayField(Field, A) || A->Num() < 3)
		{
			return false;
		}
		Out = FLinearColor((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), A->Num() > 3 ? (*A)[3]->AsNumber() : 1.0);
		return true;
	}

	TSharedPtr<FJsonValue> ColorJson(const FLinearColor& C)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(C.R), MakeShared<FJsonValueNumber>(C.G),
			MakeShared<FJsonValueNumber>(C.B), MakeShared<FJsonValueNumber>(C.A)});
	}

	TSharedPtr<FJsonValue> Vec2Json(const FVector2D& V)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y)});
	}

	bool CheckExpr(const FString& Expr, const FString& Where, FString& OutError)
	{
		FString Err;
		if (!LogicExpr::Parse(Expr, Err))
		{
			OutError = FString::Printf(TEXT("%s: '%s': %s"), *Where, *Expr, *Err);
			return false;
		}
		return true;
	}

	bool CheckTemplateValue(const TSharedPtr<FJsonValue>& V, const FString& Where, FString& OutError)
	{
		if (!V.IsValid())
		{
			return true;
		}
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString();
			if (S.StartsWith(TEXT("=")))
			{
				return CheckExpr(S.Mid(1), Where, OutError);
			}
			FString Err;
			if (!LogicExpr::CheckTemplate(S, Err))
			{
				OutError = Where + TEXT(": ") + Err;
				return false;
			}
			return true;
		}
		if (V->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
			{
				if (!CheckTemplateValue(Item, Where, OutError))
				{
					return false;
				}
			}
		}
		if (V->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : V->AsObject()->Values)
			{
				if (!CheckTemplateValue(P.Value, Where + TEXT(".") + P.Key, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CheckActions(const TArray<TSharedPtr<FJsonValue>>& Actions, const FString& Where, FString& OutError)
	{
		for (int32 i = 0; i < Actions.Num(); ++i)
		{
			const FString At = FString::Printf(TEXT("%s action %d"), *Where, i + 1);
			const TSharedPtr<FJsonObject>* A = nullptr;
			if (!Actions[i]->TryGetObject(A))
			{
				OutError = At + TEXT(": must be an object like {\"set\":\"score\",\"value\":\"score + 1\"}");
				return false;
			}
			FString Kind;
			for (const FString& Name : ActionNames())
			{
				if ((*A)->HasField(Name))
				{
					Kind = Name;
					break;
				}
			}
			if (Kind.IsEmpty())
			{
				OutError = At + TEXT(": unknown action; use one of ") + FString::Join(ActionNames(), TEXT(", "));
				return false;
			}
			for (const TCHAR* Field : {TEXT("value"), TEXT("index"), TEXT("in"), TEXT("delay")})
			{
				FString Expr;
				if ((*A)->TryGetStringField(Field, Expr) && !CheckExpr(Expr, At + TEXT(" ") + Field, OutError))
				{
					return false;
				}
			}
			if (Kind == TEXT("if"))
			{
				FString Expr;
				if (!(*A)->TryGetStringField(TEXT("if"), Expr) || !CheckExpr(Expr, At + TEXT(" if"), OutError))
				{
					OutError = OutError.IsEmpty() ? At + TEXT(": 'if' needs an expression string") : OutError;
					return false;
				}
				for (const TCHAR* Branch : {TEXT("then"), TEXT("else")})
				{
					const TArray<TSharedPtr<FJsonValue>>* Sub = nullptr;
					if ((*A)->TryGetArrayField(Branch, Sub) && !CheckActions(*Sub, At + TEXT(" ") + Branch, OutError))
					{
						return false;
					}
				}
			}
			if (Kind == TEXT("for"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Sub = nullptr;
				if (!(*A)->HasField(TEXT("in")) || !(*A)->TryGetArrayField(TEXT("do"), Sub))
				{
					OutError = At + TEXT(": 'for' needs \"in\" (a list or count) and \"do\" (actions)");
					return false;
				}
				if (!CheckActions(*Sub, At + TEXT(" do"), OutError))
				{
					return false;
				}
			}
			if ((Kind == TEXT("set") || Kind == TEXT("let") || Kind == TEXT("push")) && !(*A)->HasField(TEXT("value")))
			{
				OutError = At + FString::Printf(TEXT(": '%s' needs \"value\" (an expression; quote text like \"'X'\")"), *Kind);
				return false;
			}
			if (Kind == TEXT("set_item") && (!(*A)->HasField(TEXT("index")) || !(*A)->HasField(TEXT("value"))))
			{
				OutError = At + TEXT(": 'set_item' needs \"index\" and \"value\"");
				return false;
			}
			if (Kind == TEXT("call"))
			{
				const FString Tool = ReadString(*A, TEXT("call"));
				if (Tool.StartsWith(TEXT("logic.")) || Tool.StartsWith(TEXT("logic_")))
				{
					OutError = At + TEXT(": rules cannot call logic tools; use actions (set, timer, restart, ...)");
					return false;
				}
				if (!CheckTemplateValue((*A)->TryGetField(TEXT("args")), At + TEXT(" args"), OutError))
				{
					return false;
				}
			}
			if (Kind == TEXT("emit") && !CheckTemplateValue((*A)->TryGetField(TEXT("data")), At + TEXT(" data"), OutError))
			{
				return false;
			}
			for (const TCHAR* Field : {TEXT("log"), TEXT("text")})
			{
				FString T, Err;
				if ((*A)->TryGetStringField(Field, T) && !LogicExpr::CheckTemplate(T, Err))
				{
					OutError = At + TEXT(": ") + Err;
					return false;
				}
			}
		}
		return true;
	}
}

// ------------------------------------------------------------------ rule / ui definitions

TSharedRef<FJsonObject> FLogicRule::ToJson() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("name"), Name);
	J->SetStringField(TEXT("on"), On);
	if (!Target.IsEmpty()) J->SetStringField(TEXT("target"), Target);
	if (!Key.IsEmpty()) J->SetStringField(TEXT("key"), Key);
	if (!Filter.IsEmpty()) J->SetStringField(TEXT("filter"), Filter);
	if (!Condition.IsEmpty()) J->SetStringField(TEXT("condition"), Condition);
	J->SetArrayField(TEXT("actions"), Actions);
	if (!bEnabled) J->SetBoolField(TEXT("enabled"), false);
	return J;
}

bool FLogicRule::FromJson(const TSharedPtr<FJsonObject>& J, FLogicRule& Out, FString& OutError)
{
	if (!J.IsValid())
	{
		OutError = TEXT("rule must be an object");
		return false;
	}
	Out = FLogicRule();
	Out.Name = ReadString(J, TEXT("name"));
	Out.On = ReadString(J, TEXT("on")).ToLower();
	Out.Target = ReadString(J, TEXT("target"));
	Out.Key = ReadString(J, TEXT("key"));
	Out.Filter = ReadString(J, TEXT("filter"));
	Out.Condition = ReadString(J, TEXT("condition"));
	J->TryGetBoolField(TEXT("enabled"), Out.bEnabled);
	const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
	if (J->TryGetArrayField(TEXT("actions"), Actions))
	{
		Out.Actions = *Actions;
	}
	if (Out.Name.IsEmpty())
	{
		OutError = TEXT("rule needs a name");
		return false;
	}
	if (!EventNames().Contains(Out.On))
	{
		OutError = FString::Printf(TEXT("rule '%s': 'on' must be one of %s"), *Out.Name, *FString::Join(EventNames(), TEXT(", ")));
		return false;
	}
	return FLogicWorld::ValidateRule(Out, OutError);
}

TSharedRef<FJsonObject> FLogicUiItem::ToJson() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("name"), Name);
	J->SetStringField(TEXT("kind"), Kind);
	J->SetStringField(TEXT("text"), Text);
	J->SetField(TEXT("anchor"), Vec2Json(Anchor));
	if (!Size.IsZero()) J->SetField(TEXT("size"), Vec2Json(Size));
	J->SetField(TEXT("color"), ColorJson(Color));
	J->SetField(TEXT("background"), ColorJson(Background));
	J->SetNumberField(TEXT("font_size"), FontSize);
	if (!VisibleIf.IsEmpty()) J->SetStringField(TEXT("visible_if"), VisibleIf);
	return J;
}

bool FLogicUiItem::FromJson(const TSharedPtr<FJsonObject>& J, FLogicUiItem& Out, FString& OutError)
{
	Out = FLogicUiItem();
	Out.Name = ReadString(J, TEXT("name"));
	if (Out.Name.IsEmpty())
	{
		OutError = TEXT("ui item needs a name");
		return false;
	}
	FString Kind = ReadString(J, TEXT("kind"));
	Out.Kind = Kind.IsEmpty() ? TEXT("text") : Kind.ToLower();
	if (Out.Kind != TEXT("text") && Out.Kind != TEXT("button"))
	{
		OutError = TEXT("ui kind must be text or button");
		return false;
	}
	Out.Text = ReadString(J, TEXT("text"));
	ReadVec2(J, TEXT("anchor"), Out.Anchor);
	ReadVec2(J, TEXT("size"), Out.Size);
	ReadColor(J, TEXT("color"), Out.Color);
	if (!ReadColor(J, TEXT("background"), Out.Background) && Out.Kind == TEXT("button"))
	{
		Out.Background = FLinearColor(0.1f, 0.35f, 0.8f, 1.f);
	}
	double Font = Out.FontSize;
	J->TryGetNumberField(TEXT("font_size"), Font);
	Out.FontSize = FMath::Clamp(static_cast<int32>(Font), 6, 200);
	Out.VisibleIf = ReadString(J, TEXT("visible_if"));
	FString Err;
	if (!LogicExpr::CheckTemplate(Out.Text, Err))
	{
		OutError = FString::Printf(TEXT("ui '%s' text: %s"), *Out.Name, *Err);
		return false;
	}
	if (!Out.VisibleIf.IsEmpty() && !CheckExpr(Out.VisibleIf, TEXT("visible_if"), OutError))
	{
		return false;
	}
	return true;
}

// ------------------------------------------------------------------ world logic

FLogicWorld::FLogicWorld(UWorld* InWorld) : World(InWorld)
{
	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
}

FLogicWorld::~FLogicWorld()
{
	RemoveUi();
}

bool FLogicWorld::ValidateRule(const FLogicRule& Rule, FString& OutError)
{
	if (!Rule.Condition.IsEmpty() && !CheckExpr(Rule.Condition, FString::Printf(TEXT("rule '%s' condition"), *Rule.Name), OutError))
	{
		return false;
	}
	if (Rule.On == TEXT("key") && !Rule.Key.IsEmpty() && !FKey(*Rule.Key).IsValid())
	{
		OutError = FString::Printf(TEXT("rule '%s': unknown key '%s' (examples: R, SpaceBar, Enter, One, LeftMouseButton)"), *Rule.Name, *Rule.Key);
		return false;
	}
	return CheckActions(Rule.Actions, FString::Printf(TEXT("rule '%s'"), *Rule.Name), OutError);
}

TSharedRef<FJsonObject> FLogicWorld::ExportDefinitions() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
	for (const TPair<FString, FLogicValue>& P : InitialVars)
	{
		V->SetField(P.Key, P.Value.ToJson());
	}
	J->SetObjectField(TEXT("vars"), V);
	TArray<TSharedPtr<FJsonValue>> R;
	for (const FLogicRule& Rule : Rules)
	{
		R.Add(MakeShared<FJsonValueObject>(Rule.ToJson()));
	}
	J->SetArrayField(TEXT("rules"), R);
	TArray<TSharedPtr<FJsonValue>> U;
	for (const FLogicUiItem& Item : Ui)
	{
		U.Add(MakeShared<FJsonValueObject>(Item.ToJson()));
	}
	J->SetArrayField(TEXT("ui"), U);
	TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
	for (const TPair<FString, FLogicValue>& P : NodeData)
	{
		D->SetField(P.Key, P.Value.ToJson());
	}
	J->SetObjectField(TEXT("node_data"), D);
	J->SetStringField(TEXT("input_mode"), InputMode);
	J->SetBoolField(TEXT("lock_camera"), bLockCamera);
	return J;
}

bool FLogicWorld::ImportDefinitions(const TSharedPtr<FJsonObject>& J, FString& OutError)
{
	if (!J.IsValid())
	{
		OutError = TEXT("logic must be an object");
		return false;
	}
	TMap<FString, FLogicValue> NewVars;
	const TSharedPtr<FJsonObject>* V = nullptr;
	if (J->TryGetObjectField(TEXT("vars"), V))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*V)->Values)
		{
			NewVars.Add(P.Key, FLogicValue::FromJson(P.Value));
		}
	}
	TArray<FLogicRule> NewRules;
	const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
	if (J->TryGetArrayField(TEXT("rules"), R))
	{
		for (const TSharedPtr<FJsonValue>& Item : *R)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			FLogicRule Rule;
			if (!Item->TryGetObject(O) || !FLogicRule::FromJson(*O, Rule, OutError))
			{
				return false;
			}
			NewRules.Add(Rule);
		}
	}
	TArray<FLogicUiItem> NewUi;
	const TArray<TSharedPtr<FJsonValue>>* U = nullptr;
	if (J->TryGetArrayField(TEXT("ui"), U))
	{
		for (const TSharedPtr<FJsonValue>& Item : *U)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			FLogicUiItem Ui0;
			if (!Item->TryGetObject(O) || !FLogicUiItem::FromJson(*O, Ui0, OutError))
			{
				return false;
			}
			NewUi.Add(Ui0);
		}
	}
	TMap<FString, FLogicValue> NewData;
	const TSharedPtr<FJsonObject>* D = nullptr;
	if (J->TryGetObjectField(TEXT("node_data"), D))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*D)->Values)
		{
			NewData.Add(P.Key, FLogicValue::FromJson(P.Value));
		}
	}
	InitialVars = MoveTemp(NewVars);
	// Keep the current value of variables that still exist; new ones start at their initial value.
	TMap<FString, FLogicValue> Current;
	for (const TPair<FString, FLogicValue>& P : InitialVars)
	{
		const FLogicValue* Old = Vars.Find(P.Key);
		Current.Add(P.Key, Old ? *Old : P.Value.DeepCopy());
	}
	Vars = MoveTemp(Current);
	Rules = MoveTemp(NewRules);
	Ui = MoveTemp(NewUi);
	NodeData = MoveTemp(NewData);
	FString Mode;
	if (J->TryGetStringField(TEXT("input_mode"), Mode) && (Mode == TEXT("game") || Mode == TEXT("cursor")))
	{
		InputMode = Mode;
	}
	J->TryGetBoolField(TEXT("lock_camera"), bLockCamera);
	RefreshUi();
	return true;
}

FLogicScope FLogicWorld::MakeScope(FFrame& Frame)
{
	FLogicScope Scope;
	Scope.Random = &Random;
	Scope.Lookup = [this, &Frame](const FString& Name) -> const FLogicValue*
	{
		if (const FLogicValue* L = Frame.Locals.Find(Name))
		{
			return L;
		}
		if (Name == TEXT("event"))
		{
			return &Frame.EventValue;
		}
		if (const FLogicValue* V = Vars.Find(Name))
		{
			return V;
		}
		if (Name == TEXT("time"))
		{
			Frame.Locals.Add(TEXT("time"), FLogicValue::MakeNumber(Time));
			return Frame.Locals.Find(TEXT("time"));
		}
		return nullptr;
	};
	return Scope;
}

FLogicValue FLogicWorld::Evaluate(const FString& Expr, FString& OutError)
{
	FFrame Frame;
	FLogicScope Scope = MakeScope(Frame);
	const FLogicValue V = LogicExpr::Evaluate(Expr, Scope);
	OutError = Scope.Error;
	return V;
}

void FLogicWorld::Start()
{
	ClearPlayObjects();
	Vars.Reset();
	for (const TPair<FString, FLogicValue>& P : InitialVars)
	{
		Vars.Add(P.Key, P.Value.DeepCopy());
	}
	for (const TPair<FString, FLogicValue>& P : KeepOnRestart)
	{
		if (Vars.Contains(P.Key))
		{
			Vars.Add(P.Key, P.Value);
		}
	}
	KeepOnRestart.Reset();
	Timers.Reset();
	Queue.Reset();
	TextOverrides.Reset();
	VisibleOverrides.Reset();
	Time = 0.0;
	bRunning = true;
	ApplyInputMode();
	RefreshUi();
	FLogicEvent E;
	E.On = TEXT("start");
	Fire(MoveTemp(E));
}

void FLogicWorld::Stop()
{
	bRunning = false;
	Timers.Reset();
	Queue.Reset();
}

void FLogicWorld::ClearPlayObjects()
{
	if (PlayNodes.Num() == 0)
	{
		return;
	}
	FToolCallerContext Caller;
	Caller.SessionId = FLogicManager::Session();
	Caller.ClientName = TEXT("logic");
	for (const FString& Id : PlayNodes)
	{
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("target"), Id);
		IRuntimeToolsetsCore::Get().ExecuteInline(TEXT("scene.delete_node"), Args, Caller, 1);
	}
	PlayNodes.Reset();
	// Drop the play session's history so deleted objects are really destroyed.
	IRuntimeToolsetsCore::Get().EndSession(FLogicManager::Session());
}

void FLogicWorld::SetVar(const FString& Name, const FLogicValue& Value, bool bNotify)
{
	FLogicValue Old;
	if (const FLogicValue* Prev = Vars.Find(Name))
	{
		Old = *Prev;
	}
	Vars.Add(Name, Value.DeepCopy());
	if (!bNotify || !bRunning || Old.Equals(Value))
	{
		return;
	}
	const bool bListened = Rules.ContainsByPredicate([&Name](const FLogicRule& R) { return R.bEnabled && R.On == TEXT("var_changed") && (R.Filter.IsEmpty() || R.Filter == Name); });
	if (!bListened)
	{
		return;
	}
	FLogicEvent E;
	E.On = TEXT("var_changed");
	E.Name = Name;
	E.Data.M->Add(TEXT("value"), Value.DeepCopy());
	E.Data.M->Add(TEXT("old"), Old);
	Queue.Add(MoveTemp(E));
	if (!bProcessing)
	{
		ProcessQueue(nullptr);
	}
}

TSharedRef<FJsonObject> FLogicWorld::Fire(FLogicEvent&& Event)
{
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Queue.Add(MoveTemp(Event));
	if (!bProcessing)
	{
		ProcessQueue(&Report);
	}
	return Report;
}

FLogicValue FLogicWorld::FindNodeData(const FString& NodeId) const
{
	UWorld* W = World.Get();
	FRuntimeSceneGraph* Graph = W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	FString Id = NodeId;
	for (int32 Guard = 0; Graph && !Id.IsEmpty() && Guard < 64; ++Guard)
	{
		if (const FLogicValue* D = NodeData.Find(Id))
		{
			return *D;
		}
		const FRuntimeSceneNode* N = Graph->Find(Id);
		Id = N ? N->ParentId : FString();
	}
	return FLogicValue::MakeMap();
}

bool FLogicWorld::NodeMatches(const FString& NodeId, const FString& Target) const
{
	UWorld* W = World.Get();
	FRuntimeSceneGraph* Graph = W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	const bool bTag = Target.StartsWith(TEXT("tag:"));
	const FName Tag(bTag ? *Target.Mid(4) : TEXT(""));
	FString Id = NodeId;
	for (int32 Guard = 0; Graph && !Id.IsEmpty() && Guard < 64; ++Guard)
	{
		const FRuntimeSceneNode* N = Graph->Find(Id);
		if (!N)
		{
			return false;
		}
		if (bTag ? N->Tags.Contains(Tag) : (N->Id == Target || N->Name == Target || Graph->GetPath(N->Id) == Target))
		{
			return true;
		}
		Id = N->ParentId;
	}
	return false;
}

bool FLogicWorld::Matches(const FLogicRule& Rule, const FLogicEvent& E) const
{
	if (!Rule.bEnabled || Rule.On != E.On)
	{
		return false;
	}
	if (E.On == TEXT("click") || E.On == TEXT("right_click"))
	{
		return Rule.Target.IsEmpty() || NodeMatches(E.Target, Rule.Target);
	}
	if (E.On == TEXT("key"))
	{
		return Rule.Key.IsEmpty() || Rule.Key.Equals(E.Name, ESearchCase::IgnoreCase);
	}
	if (E.On == TEXT("start") || E.On == TEXT("tick"))
	{
		return true;
	}
	return Rule.Filter.IsEmpty() || Rule.Filter == E.Name;
}

void FLogicWorld::ProcessQueue(TSharedRef<FJsonObject>* OutReport)
{
	bProcessing = true;
	int32 Budget = MaxActionsPerFire;
	int32 Events = 0;
	TArray<TSharedPtr<FJsonValue>> ReportEvents;
	TArray<TSharedPtr<FJsonValue>> ReportErrors;
	TArray<FString> Logs;
	while (Queue.Num() > 0 && Events < MaxEventsPerFire)
	{
		const FLogicEvent E = Queue[0];
		Queue.RemoveAt(0);
		++Events;
		++EventCount;
		TArray<FString> Ran, Errors;
		const TArray<FLogicRule> Snapshot = Rules; // rules may not change mid-event, but be safe
		for (const FLogicRule& Rule : Snapshot)
		{
			if (!Matches(Rule, E))
			{
				continue;
			}
			FFrame Frame;
			Frame.Event = &E;
			Frame.Rule = Rule.Name;
			Frame.Logs = &Logs;
			Frame.EventValue = E.Data.DeepCopy();
			Frame.EventValue.M->Add(TEXT("on"), FLogicValue::MakeString(E.On));
			Frame.EventValue.M->FindOrAdd(TEXT("name")) = FLogicValue::MakeString(E.Name);
			Frame.EventValue.M->Add(TEXT("target"), FLogicValue::MakeString(E.Target));
			if (!Rule.Condition.IsEmpty())
			{
				FLogicScope Scope = MakeScope(Frame);
				const FLogicValue C = LogicExpr::Evaluate(Rule.Condition, Scope);
				if (!Scope.Error.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("%s condition: %s"), *Rule.Name, *Scope.Error));
					continue;
				}
				if (!C.Truthy())
				{
					continue;
				}
			}
			Ran.Add(Rule.Name);
			FString Err;
			if (!RunActions(Rule.Actions, Frame, Budget, Err))
			{
				Errors.Add(FString::Printf(TEXT("%s: %s"), *Rule.Name, *Err));
			}
		}
		if (E.On != TEXT("tick") || Errors.Num() > 0)
		{
			AddLog(E, Ran, Errors);
		}
		if (E.On != TEXT("tick") || Ran.Num() > 0)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("on"), E.On);
			if (!E.Name.IsEmpty()) J->SetStringField(TEXT("name"), E.Name);
			if (!E.Target.IsEmpty()) J->SetStringField(TEXT("target"), E.Target);
			TArray<TSharedPtr<FJsonValue>> R;
			for (const FString& S : Ran) R.Add(MakeShared<FJsonValueString>(S));
			J->SetArrayField(TEXT("rules"), R);
			ReportEvents.Add(MakeShared<FJsonValueObject>(J));
		}
		for (const FString& S : Errors)
		{
			ReportErrors.Add(MakeShared<FJsonValueString>(S));
			UE_LOG(LogRuntimeToolsetsLogic, Warning, TEXT("%s"), *S);
		}
	}
	if (Queue.Num() > 0)
	{
		ReportErrors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("more than %d events in one go; stopped (an event loop?)"), MaxEventsPerFire)));
		Queue.Reset();
	}
	bProcessing = false;
	if (OutReport)
	{
		(*OutReport)->SetArrayField(TEXT("events"), ReportEvents);
		(*OutReport)->SetArrayField(TEXT("errors"), ReportErrors);
		if (Logs.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> L;
			for (const FString& S : Logs) L.Add(MakeShared<FJsonValueString>(S));
			(*OutReport)->SetArrayField(TEXT("log"), L);
		}
	}
	if (bRestartPending)
	{
		bRestartPending = false;
		Start();
	}
}

bool FLogicWorld::EvalField(const TSharedPtr<FJsonObject>& Action, const TCHAR* Field, FFrame& Frame, FLogicValue& Out, FString& OutError)
{
	const TSharedPtr<FJsonValue> V = Action->TryGetField(Field);
	if (!V.IsValid())
	{
		OutError = FString::Printf(TEXT("missing \"%s\""), Field);
		return false;
	}
	if (V->Type != EJson::String)
	{
		Out = FLogicValue::FromJson(V);
		return true;
	}
	FLogicScope Scope = MakeScope(Frame);
	Out = LogicExpr::Evaluate(V->AsString(), Scope);
	if (!Scope.Error.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s '%s': %s"), Field, *V->AsString(), *Scope.Error);
		return false;
	}
	return true;
}

TSharedPtr<FJsonValue> FLogicWorld::Template(const TSharedPtr<FJsonValue>& V, FFrame& Frame, FString& OutError)
{
	if (!V.IsValid())
	{
		return V;
	}
	if (V->Type == EJson::String)
	{
		const FString S = V->AsString();
		FLogicScope Scope = MakeScope(Frame);
		if (S.StartsWith(TEXT("=")))
		{
			const FLogicValue R = LogicExpr::Evaluate(S.Mid(1), Scope);
			OutError = Scope.Error;
			return R.ToJson();
		}
		const FString T = LogicExpr::Interpolate(S, Scope);
		OutError = Scope.Error;
		return MakeShared<FJsonValueString>(T);
	}
	if (V->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
		{
			Items.Add(Template(Item, Frame, OutError));
			if (!OutError.IsEmpty())
			{
				return nullptr;
			}
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (V->Type == EJson::Object)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : V->AsObject()->Values)
		{
			O->SetField(P.Key, Template(P.Value, Frame, OutError));
			if (!OutError.IsEmpty())
			{
				return nullptr;
			}
		}
		return MakeShared<FJsonValueObject>(O);
	}
	return V;
}

bool FLogicWorld::RunActions(const TArray<TSharedPtr<FJsonValue>>& Actions, FFrame& Frame, int32& Budget, FString& OutError)
{
	for (int32 i = 0; i < Actions.Num() && !Frame.bStop; ++i)
	{
		if (--Budget < 0)
		{
			OutError = TEXT("too many actions in one event (an endless loop?)");
			return false;
		}
		const TSharedPtr<FJsonObject>* A = nullptr;
		if (!Actions[i]->TryGetObject(A))
		{
			OutError = FString::Printf(TEXT("action %d is not an object"), i + 1);
			return false;
		}
		FString Err;
		if (!RunAction(*A, Frame, Budget, Err))
		{
			OutError = FString::Printf(TEXT("action %d: %s"), i + 1, *Err);
			return false;
		}
	}
	return true;
}

bool FLogicWorld::RunAction(const TSharedPtr<FJsonObject>& A, FFrame& Frame, int32& Budget, FString& OutError)
{
	FString Name;
	if (A->TryGetStringField(TEXT("set"), Name))
	{
		FLogicValue V;
		if (!EvalField(A, TEXT("value"), Frame, V, OutError))
		{
			return false;
		}
		if (FLogicValue* Local = Frame.Locals.Find(Name))
		{
			*Local = V.DeepCopy();
			return true;
		}
		if (!Vars.Contains(Name))
		{
			OutError = FString::Printf(TEXT("unknown variable '%s' (define it with logic.define_var, or use let for a local)"), *Name);
			return false;
		}
		SetVar(Name, V, true);
		return true;
	}
	if (A->TryGetStringField(TEXT("let"), Name))
	{
		FLogicValue V;
		if (!EvalField(A, TEXT("value"), Frame, V, OutError))
		{
			return false;
		}
		Frame.Locals.Add(Name, V.DeepCopy());
		return true;
	}
	if (A->TryGetStringField(TEXT("set_item"), Name) || A->TryGetStringField(TEXT("push"), Name))
	{
		const bool bPush = A->HasField(TEXT("push"));
		FLogicValue* Target = Frame.Locals.Find(Name);
		const bool bGlobal = Target == nullptr;
		FLogicValue Old;
		if (bGlobal)
		{
			Target = Vars.Find(Name);
			Old = Target ? Target->DeepCopy() : FLogicValue();
		}
		if (!Target)
		{
			OutError = FString::Printf(TEXT("unknown variable '%s'"), *Name);
			return false;
		}
		FLogicValue Index, V;
		if ((!bPush && !EvalField(A, TEXT("index"), Frame, Index, OutError)) || !EvalField(A, TEXT("value"), Frame, V, OutError))
		{
			return false;
		}
		Target = bGlobal ? Vars.Find(Name) : Frame.Locals.Find(Name); // evaluation may not reallocate, but re-find to be safe
		if (bPush)
		{
			if (Target->Type != FLogicValue::EType::List)
			{
				OutError = FString::Printf(TEXT("push: '%s' is a %s, not a list"), *Name, Target->TypeName());
				return false;
			}
			Target->L->Add(V.DeepCopy());
		}
		else if (Target->Type == FLogicValue::EType::List)
		{
			const int32 I = static_cast<int32>(Index.N);
			if (Index.Type != FLogicValue::EType::Number || !Target->L->IsValidIndex(I))
			{
				OutError = FString::Printf(TEXT("set_item: index %s is outside '%s' (length %d)"), *Index.ToText(), *Name, Target->L->Num());
				return false;
			}
			(*Target->L)[I] = V.DeepCopy();
		}
		else if (Target->Type == FLogicValue::EType::Map)
		{
			Target->M->Add(Index.ToText(), V.DeepCopy());
		}
		else
		{
			OutError = FString::Printf(TEXT("set_item: '%s' is a %s, not a list or map"), *Name, Target->TypeName());
			return false;
		}
		if (bGlobal)
		{
			const FLogicValue NewValue = Target->DeepCopy();
			Vars.Add(Name, Old);
			SetVar(Name, NewValue, true);
		}
		return true;
	}
	if (A->HasField(TEXT("if")))
	{
		FLogicValue C;
		if (!EvalField(A, TEXT("if"), Frame, C, OutError))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Branch = nullptr;
		if (A->TryGetArrayField(C.Truthy() ? TEXT("then") : TEXT("else"), Branch))
		{
			return RunActions(*Branch, Frame, Budget, OutError);
		}
		return true;
	}
	if (A->TryGetStringField(TEXT("for"), Name))
	{
		FLogicValue Items;
		if (!EvalField(A, TEXT("in"), Frame, Items, OutError))
		{
			return false;
		}
		if (Items.Type == FLogicValue::EType::Number)
		{
			TArray<FLogicValue> Range;
			for (int32 i = 0; i < FMath::Clamp(static_cast<int32>(Items.N), 0, 10000); ++i)
			{
				Range.Add(FLogicValue::MakeNumber(i));
			}
			Items = FLogicValue::MakeList(MoveTemp(Range));
		}
		if (Items.Type != FLogicValue::EType::List)
		{
			OutError = FString::Printf(TEXT("for: 'in' must be a list or a count, got %s"), Items.TypeName());
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Body = nullptr;
		A->TryGetArrayField(TEXT("do"), Body);
		for (const FLogicValue& Item : *Items.L)
		{
			Frame.Locals.Add(Name, Item);
			if (Body && !RunActions(*Body, Frame, Budget, OutError))
			{
				return false;
			}
			if (Frame.bStop)
			{
				break;
			}
		}
		return true;
	}
	if (A->TryGetStringField(TEXT("call"), Name))
	{
		if (Name.StartsWith(TEXT("logic.")) || Name.StartsWith(TEXT("logic_")))
		{
			OutError = TEXT("rules cannot call logic tools");
			return false;
		}
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		if (const TSharedPtr<FJsonValue> Raw = A->TryGetField(TEXT("args")))
		{
			const TSharedPtr<FJsonValue> T = Template(Raw, Frame, OutError);
			if (!OutError.IsEmpty())
			{
				OutError = TEXT("args: ") + OutError;
				return false;
			}
			if (!T.IsValid() || T->Type != EJson::Object)
			{
				OutError = TEXT("args must be an object");
				return false;
			}
			Args = T->AsObject();
		}
		FToolCallerContext Caller;
		Caller.SessionId = FLogicManager::Session();
		Caller.ClientName = TEXT("logic");
		const FToolResult R = IRuntimeToolsetsCore::Get().ExecuteInline(Name, Args, Caller, 1);
		if (!R.IsOk())
		{
			OutError = FString::Printf(TEXT("%s failed: %s"), *Name, *R.Message);
			return false;
		}
		FString Id;
		if (R.Payload.IsValid() && R.Payload->TryGetStringField(TEXT("id"), Id) && (Name.Contains(TEXT("create")) || Name.Contains(TEXT("spawn")) || Name.Contains(TEXT("duplicate"))))
		{
			PlayNodes.Add(Id);
		}
		FString As;
		if (A->TryGetStringField(TEXT("as"), As))
		{
			Frame.Locals.Add(As, R.Payload.IsValid() ? FLogicValue::FromJson(MakeShared<FJsonValueObject>(R.Payload)) : FLogicValue::MakeMap());
		}
		return true;
	}
	if (A->TryGetStringField(TEXT("emit"), Name))
	{
		FLogicEvent E;
		E.On = TEXT("event");
		E.Name = Name;
		if (const TSharedPtr<FJsonValue> Raw = A->TryGetField(TEXT("data")))
		{
			const TSharedPtr<FJsonValue> T = Template(Raw, Frame, OutError);
			if (!OutError.IsEmpty())
			{
				return false;
			}
			const FLogicValue D = FLogicValue::FromJson(T);
			if (D.Type == FLogicValue::EType::Map)
			{
				E.Data = D;
			}
		}
		Queue.Add(MoveTemp(E));
		return true;
	}
	if (A->TryGetStringField(TEXT("timer"), Name))
	{
		FLogicValue Delay = FLogicValue::MakeNumber(1.0);
		if (A->HasField(TEXT("delay")) && !EvalField(A, TEXT("delay"), Frame, Delay, OutError))
		{
			return false;
		}
		bool bRepeat = false;
		A->TryGetBoolField(TEXT("repeat"), bRepeat);
		Timers.RemoveAll([&Name](const FTimer& T) { return T.Name == Name; });
		FTimer T;
		T.Name = Name;
		T.Interval = FMath::Max(0.0, Delay.N);
		T.Remaining = T.Interval;
		T.bRepeat = bRepeat && T.Interval > 0.0;
		Timers.Add(T);
		return true;
	}
	if (A->TryGetStringField(TEXT("cancel_timer"), Name))
	{
		Timers.RemoveAll([&Name](const FTimer& T) { return T.Name == Name; });
		return true;
	}
	if (A->TryGetStringField(TEXT("set_ui"), Name))
	{
		FString Text;
		if (A->TryGetStringField(TEXT("text"), Text))
		{
			TextOverrides.Add(Name, Text);
		}
		if (A->HasField(TEXT("visible")))
		{
			FLogicValue Visible;
			if (!EvalField(A, TEXT("visible"), Frame, Visible, OutError))
			{
				return false;
			}
			VisibleOverrides.Add(Name, Visible.Truthy());
		}
		return true;
	}
	if (A->TryGetStringField(TEXT("log"), Name))
	{
		FLogicScope Scope = MakeScope(Frame);
		const FString Text = LogicExpr::Interpolate(Name, Scope);
		UE_LOG(LogRuntimeToolsetsLogic, Log, TEXT("[%s] %s"), *Frame.Rule, *Text);
		if (Frame.Logs)
		{
			Frame.Logs->Add(FString::Printf(TEXT("%s: %s"), *Frame.Rule, *Text));
		}
		return true;
	}
	if (A->HasField(TEXT("restart")))
	{
		KeepOnRestart.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Keep = nullptr;
		if (A->TryGetArrayField(TEXT("keep"), Keep))
		{
			for (const TSharedPtr<FJsonValue>& K : *Keep)
			{
				if (const FLogicValue* V = Vars.Find(K->AsString()))
				{
					KeepOnRestart.Add(K->AsString(), V->DeepCopy());
				}
			}
		}
		bRestartPending = true;
		Frame.bStop = true;
		return true;
	}
	if (A->HasField(TEXT("stop")))
	{
		Frame.bStop = true;
		return true;
	}
	OutError = TEXT("unknown action; use one of ") + FString::Join(ActionNames(), TEXT(", "));
	return false;
}

void FLogicWorld::Step(double Seconds)
{
	if (!bRunning || Seconds <= 0.0)
	{
		return;
	}
	Time += Seconds;
	TArray<FString> Fired;
	for (int32 i = Timers.Num() - 1; i >= 0; --i)
	{
		FTimer& T = Timers[i];
		T.Remaining -= Seconds;
		int32 Guard = 0;
		while (T.Remaining <= 0.0 && Guard++ < 100)
		{
			Fired.Add(T.Name);
			if (!T.bRepeat)
			{
				break;
			}
			T.Remaining += T.Interval;
		}
		if (!T.bRepeat && T.Remaining <= 0.0)
		{
			Timers.RemoveAt(i);
		}
	}
	for (const FString& Name : Fired)
	{
		FLogicEvent E;
		E.On = TEXT("timer");
		E.Name = Name;
		Queue.Add(MoveTemp(E));
	}
	if (Rules.ContainsByPredicate([](const FLogicRule& R) { return R.bEnabled && R.On == TEXT("tick"); }))
	{
		FLogicEvent E;
		E.On = TEXT("tick");
		E.Data.M->Add(TEXT("dt"), FLogicValue::MakeNumber(Seconds));
		Queue.Add(MoveTemp(E));
	}
	if (Queue.Num() > 0 && !bProcessing)
	{
		ProcessQueue(nullptr);
	}
}

bool FLogicWorld::MakeClickEventForNode(const FString& NodeId, bool bRight, FLogicEvent& Out) const
{
	UWorld* W = World.Get();
	FRuntimeSceneGraph* Graph = W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	const FRuntimeSceneNode* Node = Graph ? Graph->Find(NodeId) : nullptr;
	if (!Node)
	{
		return false;
	}
	Out = FLogicEvent();
	Out.On = bRight ? TEXT("right_click") : TEXT("click");
	Out.Target = Node->Id;
	Out.Name = Node->Name;
	TArray<FLogicValue> Tags;
	for (const FName& T : Node->Tags)
	{
		Tags.Add(FLogicValue::MakeString(T.ToString()));
	}
	Out.Data.M->Add(TEXT("tags"), FLogicValue::MakeList(MoveTemp(Tags)));
	Out.Data.M->Add(TEXT("data"), FindNodeData(Node->Id));
	Out.Data.M->Add(TEXT("path"), FLogicValue::MakeString(Graph->GetPath(Node->Id)));
	if (AActor* A = Node->Actor.Get())
	{
		const FVector L = A->GetActorLocation();
		Out.Data.M->Add(TEXT("location"), FLogicValue::MakeList({FLogicValue::MakeNumber(L.X), FLogicValue::MakeNumber(L.Y), FLogicValue::MakeNumber(L.Z)}));
	}
	return true;
}

bool FLogicWorld::MakeClickEvent(AActor* Actor, const FVector& Location, bool bRight, FLogicEvent& Out) const
{
	UWorld* W = World.Get();
	FRuntimeSceneGraph* Graph = W ? IRuntimeToolsetsCore::Get().GetSceneGraph(W) : nullptr;
	for (AActor* A = Actor; Graph && A; A = A->GetAttachParentActor())
	{
		if (const FRuntimeSceneNode* Node = Graph->FindByActor(A))
		{
			if (!MakeClickEventForNode(Node->Id, bRight, Out))
			{
				return false;
			}
			Out.Data.M->Add(TEXT("hit"), FLogicValue::MakeList({FLogicValue::MakeNumber(Location.X), FLogicValue::MakeNumber(Location.Y), FLogicValue::MakeNumber(Location.Z)}));
			return true;
		}
	}
	return false;
}

void FLogicWorld::PollInput()
{
	UWorld* W = World.Get();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!bRunning || !PC || !PC->PlayerInput)
	{
		return;
	}
	for (const bool bRight : {false, true})
	{
		const TCHAR* On = bRight ? TEXT("right_click") : TEXT("click");
		if (!PC->WasInputKeyJustPressed(bRight ? EKeys::RightMouseButton : EKeys::LeftMouseButton)
			|| !Rules.ContainsByPredicate([On](const FLogicRule& R) { return R.bEnabled && R.On == On; }))
		{
			continue;
		}
		FHitResult Hit;
		if (PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit) && Hit.GetActor())
		{
			FLogicEvent E;
			if (MakeClickEvent(Hit.GetActor(), Hit.ImpactPoint, bRight, E))
			{
				Fire(MoveTemp(E));
			}
		}
	}
	TSet<FString> Keys;
	for (const FLogicRule& R : Rules)
	{
		if (R.bEnabled && R.On == TEXT("key") && !R.Key.IsEmpty())
		{
			Keys.Add(R.Key);
		}
	}
	for (const FString& K : Keys)
	{
		const FKey Key(*K);
		if (Key.IsValid() && PC->WasInputKeyJustPressed(Key))
		{
			FLogicEvent E;
			E.On = TEXT("key");
			E.Name = K;
			Fire(MoveTemp(E));
		}
	}
}

void FLogicWorld::ApplyInputMode()
{
	UWorld* W = World.Get();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	if (InputMode == TEXT("cursor"))
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
	PC->ResetIgnoreInputFlags();
	if (bLockCamera)
	{
		PC->SetIgnoreLookInput(true);
		PC->SetIgnoreMoveInput(true);
	}
}

void FLogicWorld::AddLog(const FLogicEvent& E, const TArray<FString>& RulesRun, const TArray<FString>& Errors)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("seq"), static_cast<double>(EventCount));
	J->SetNumberField(TEXT("time"), RuntimeJson::Round(Time));
	J->SetStringField(TEXT("on"), E.On);
	if (!E.Name.IsEmpty()) J->SetStringField(TEXT("name"), E.Name);
	if (!E.Target.IsEmpty()) J->SetStringField(TEXT("target"), E.Target);
	TArray<TSharedPtr<FJsonValue>> R, Er;
	for (const FString& S : RulesRun) R.Add(MakeShared<FJsonValueString>(S));
	for (const FString& S : Errors) Er.Add(MakeShared<FJsonValueString>(S));
	J->SetArrayField(TEXT("rules"), R);
	if (Er.Num() > 0) J->SetArrayField(TEXT("errors"), Er);
	Log.Add(J);
	if (Log.Num() > MaxLog)
	{
		Log.RemoveAt(0, Log.Num() - MaxLog);
	}
}

TSharedRef<FJsonObject> FLogicWorld::GetState(int32 LogLimit) const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetBoolField(TEXT("running"), bRunning);
	J->SetNumberField(TEXT("time"), RuntimeJson::Round(Time));
	TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
	for (const TPair<FString, FLogicValue>& P : Vars)
	{
		V->SetField(P.Key, P.Value.ToJson());
	}
	J->SetObjectField(TEXT("vars"), V);
	TArray<TSharedPtr<FJsonValue>> T;
	for (const FTimer& Timer : Timers)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Timer.Name);
		O->SetNumberField(TEXT("remaining"), RuntimeJson::Round(Timer.Remaining));
		O->SetBoolField(TEXT("repeat"), Timer.bRepeat);
		T.Add(MakeShared<FJsonValueObject>(O));
	}
	J->SetArrayField(TEXT("timers"), T);
	TArray<TSharedPtr<FJsonValue>> R;
	for (const FLogicRule& Rule : Rules)
	{
		R.Add(MakeShared<FJsonValueString>(Rule.Name + TEXT(" (") + Rule.On + TEXT(")")));
	}
	J->SetArrayField(TEXT("rules"), R);
	TArray<TSharedPtr<FJsonValue>> U;
	for (const FLogicUiItem& Item : Ui)
	{
		U.Add(MakeShared<FJsonValueString>(Item.Name));
	}
	J->SetArrayField(TEXT("ui"), U);
	J->SetNumberField(TEXT("play_objects"), PlayNodes.Num());
	J->SetStringField(TEXT("input_mode"), InputMode);
	TArray<TSharedPtr<FJsonValue>> L;
	for (int32 i = FMath::Max(0, Log.Num() - LogLimit); i < Log.Num(); ++i)
	{
		L.Add(MakeShared<FJsonValueObject>(Log[i].ToSharedRef()));
	}
	J->SetArrayField(TEXT("log"), L);
	return J;
}

// ------------------------------------------------------------------ UI overlay

void FLogicWorld::SetUiOverride(const FString& Name, const FString* Text, const bool* bVisible)
{
	if (Text)
	{
		TextOverrides.Add(Name, *Text);
	}
	if (bVisible)
	{
		VisibleOverrides.Add(Name, *bVisible);
	}
}

void FLogicWorld::RemoveUi()
{
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* VP = OverlayViewport.Get())
		{
			VP->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
	}
	Overlay.Reset();
	OverlayViewport.Reset();
}

void FLogicWorld::RefreshUi()
{
	RemoveUi();
	UWorld* W = World.Get();
	UGameViewportClient* VP = W ? W->GetGameViewport() : nullptr;
	if (!VP || Ui.Num() == 0 || !FSlateApplication::IsInitialized())
	{
		return;
	}
	TWeakPtr<FLogicWorld> Weak = AsShared();
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	for (const FLogicUiItem& Item : Ui)
	{
		const FString Name = Item.Name;
		const FString Text = Item.Text;
		const FString VisibleIf = Item.VisibleIf;
		auto TextFn = [Weak, Name, Text]() -> FText
		{
			TSharedPtr<FLogicWorld> S = Weak.Pin();
			if (!S)
			{
				return FText();
			}
			const FString* Ov = S->TextOverrides.Find(Name);
			FFrame Frame;
			FLogicScope Scope = S->MakeScope(Frame);
			return FText::FromString(LogicExpr::Interpolate(Ov ? *Ov : Text, Scope));
		};
		auto VisibleFn = [Weak, Name, VisibleIf]() -> EVisibility
		{
			TSharedPtr<FLogicWorld> S = Weak.Pin();
			if (!S)
			{
				return EVisibility::Collapsed;
			}
			if (const bool* Ov = S->VisibleOverrides.Find(Name))
			{
				return *Ov ? EVisibility::Visible : EVisibility::Collapsed;
			}
			if (!VisibleIf.IsEmpty())
			{
				FFrame Frame;
				FLogicScope Scope = S->MakeScope(Frame);
				return LogicExpr::Evaluate(VisibleIf, Scope).Truthy() ? EVisibility::Visible : EVisibility::Collapsed;
			}
			return EVisibility::Visible;
		};
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", Item.FontSize);
		TSharedRef<SWidget> Content = SNew(STextBlock).Font(Font).ColorAndOpacity(Item.Color).Justification(ETextJustify::Center).Text_Lambda(TextFn);
		TSharedPtr<SWidget> Widget;
		if (Item.Kind == TEXT("button"))
		{
			Widget = SNew(SButton)
				.ButtonColorAndOpacity(Item.Background)
				.ContentPadding(FMargin(18.f, 8.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility_Lambda(VisibleFn)
				.OnClicked_Lambda([Weak, Name]()
				{
					if (TSharedPtr<FLogicWorld> S = Weak.Pin(); S && S->IsRunning())
					{
						FLogicEvent E;
						E.On = TEXT("button");
						E.Name = Name;
						S->Fire(MoveTemp(E));
					}
					return FReply::Handled();
				})
				[Content];
		}
		else
		{
			Widget = SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Item.Background)
				.Padding(FMargin(14.f, 6.f))
				.Visibility_Lambda(VisibleFn)
				[Content];
		}
		if (!Item.Size.IsZero())
		{
			Widget = SNew(SBox).WidthOverride(Item.Size.X).HeightOverride(Item.Size.Y)[Widget.ToSharedRef()];
		}
		Canvas->AddSlot()
			.Anchors(FAnchors(Item.Anchor.X, Item.Anchor.Y))
			.Alignment(FVector2D(0.5, 0.5))
			.AutoSize(true)
			[Widget.ToSharedRef()];
	}
	Overlay = Canvas;
	OverlayViewport = VP;
	VP->AddViewportWidgetContent(Canvas, 60);
}

// ------------------------------------------------------------------ manager

FLogicManager& FLogicManager::Get()
{
	static FLogicManager Instance;
	return Instance;
}

void FLogicManager::Startup()
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FLogicManager::Tick));
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* W, bool, bool)
	{
		if (TSharedPtr<FLogicWorld>* L = Worlds.Find(W))
		{
			(*L)->RemoveUi();
			Worlds.Remove(W);
		}
	});
}

void FLogicManager::Shutdown()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	for (TPair<TWeakObjectPtr<UWorld>, TSharedPtr<FLogicWorld>>& P : Worlds)
	{
		P.Value->RemoveUi();
	}
	Worlds.Reset();
}

FLogicWorld& FLogicManager::For(UWorld* World)
{
	TSharedPtr<FLogicWorld>& L = Worlds.FindOrAdd(World);
	if (!L.IsValid())
	{
		L = MakeShared<FLogicWorld>(World);
	}
	return *L;
}

FLogicWorld* FLogicManager::Find(UWorld* World)
{
	TSharedPtr<FLogicWorld>* L = Worlds.Find(World);
	return L ? L->Get() : nullptr;
}

bool FLogicManager::Tick(float DeltaTime)
{
	for (auto It = Worlds.CreateIterator(); It; ++It)
	{
		UWorld* W = It->Key.Get();
		if (!W)
		{
			It.RemoveCurrent();
			continue;
		}
		TSharedPtr<FLogicWorld> L = It->Value;
		if (!L->IsRunning() || !W->IsGameWorld())
		{
			continue;
		}
		L->PollInput();
		L->Step(W->IsPaused() ? 0.0 : DeltaTime);
	}
	return true;
}
