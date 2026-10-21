#include "InputToolset.h"
#include "RuntimeToolHelpers.h"
#include "Containers/Ticker.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"

using namespace RuntimeTools;

namespace
{
	APlayerController* RequireLocalPlayer(FToolInvocation& Inv)
	{
		APlayerController* PC = Inv.GetWorld()->GetFirstPlayerController();
		if (!PC || !PC->GetLocalPlayer() || !PC->PlayerInput)
		{
			Inv.Fail(ERuntimeToolError::WrongState, TEXT("no local player with input"));
			return nullptr;
		}
		return PC;
	}

	UEnhancedInputLocalPlayerSubsystem* RequireEnhancedInput(FToolInvocation& Inv, APlayerController*& OutPC)
	{
		OutPC = RequireLocalPlayer(Inv);
		if (!OutPC)
		{
			return nullptr;
		}
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(OutPC->GetLocalPlayer());
		if (!Subsystem)
		{
			Inv.Fail(ERuntimeToolError::Unsupported, TEXT("Enhanced Input is not active for the local player"));
		}
		return Subsystem;
	}

	template <typename T>
	T* LoadInputAsset(FToolInvocation& Inv, const TCHAR* Arg, const TCHAR* What)
	{
		const FString Path = Inv.GetString(Arg);
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		bool bAllowed = S.AllowedObjectRoots.Num() == 0;
		for (const FString& Root : S.AllowedObjectRoots)
		{
			bAllowed |= Path.StartsWith(Root);
		}
		if (!bAllowed)
		{
			Inv.Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("'%s' is outside AllowedObjectRoots"), *Path));
			return nullptr;
		}
		T* Asset = LoadObject<T>(nullptr, *Path);
		if (!Asset)
		{
			Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("%s '%s' not found (is it cooked?)"), What, *Path));
		}
		return Asset;
	}

	bool PressKey(APlayerController* PC, const FKey& Key, EInputEvent Event, double Value = 1.0)
	{
		return PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, static_cast<float>(Value)));
	}

	/** Reads a bool / number / [x,y] / [x,y,z] into an action value. */
	bool ReadActionValue(const TSharedPtr<FJsonValue>& V, FInputActionValue& Out)
	{
		if (!V.IsValid() || V->IsNull())
		{
			Out = FInputActionValue(true);
			return true;
		}
		if (V->Type == EJson::Boolean)
		{
			Out = FInputActionValue(V->AsBool());
			return true;
		}
		if (V->Type == EJson::Number)
		{
			Out = FInputActionValue(EInputActionValueType::Axis1D, FVector(V->AsNumber(), 0, 0));
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (V->TryGetArray(Arr) && (Arr->Num() == 2 || Arr->Num() == 3))
		{
			FVector Axis(0);
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				Axis[i] = (*Arr)[i]->AsNumber();
			}
			Out = FInputActionValue(Arr->Num() == 2 ? EInputActionValueType::Axis2D : EInputActionValueType::Axis3D, Axis);
			return true;
		}
		return false;
	}

	/** Runs Each() every tick for Duration seconds, then Finish(), then completes the call. */
	void HoldFor(FToolInvocation& Inv, double Duration, TFunction<void()> Each, TFunction<void()> Finish, TSharedRef<FJsonObject> Out)
	{
		TSharedRef<FToolInvocation> Self = Inv.AsShared();
		const double End = FPlatformTime::Seconds() + Duration;
		TSharedRef<TFunction<void()>> FinishRef = MakeShared<TFunction<void()>>(MoveTemp(Finish));
		FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, End, Each, FinishRef, Out](float)
		{
			if (Self->IsComplete())
			{
				return false;
			}
			if (FPlatformTime::Seconds() < End)
			{
				if (Each)
				{
					Each();
				}
				return true;
			}
			(*FinishRef)();
			Self->Succeed(Out);
			return false;
		}));
		Inv.SetCancelHandler([Handle, FinishRef]()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			(*FinishRef)(); // never leave a key held down
		});
		Inv.Defer();
	}
}

static void InjectKey(FToolInvocation& Inv)
{
	APlayerController* PC = RequireLocalPlayer(Inv);
	if (!PC)
	{
		return;
	}
	const FString KeyName = Inv.GetString(TEXT("key"));
	const FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown key '%s'"), *KeyName), TEXT("Use UE key names: W, SpaceBar, LeftMouseButton, Gamepad_FaceButton_Bottom, MouseX"));
		return;
	}
	const FString Event = Inv.GetString(TEXT("event"), TEXT("tap"));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("key"), Key.GetFName().ToString());
	Out->SetStringField(TEXT("event"), Event);
	Out->SetBoolField(TEXT("undoable"), false);
	Inv.RecordIrreversible(FString::Printf(TEXT("key %s %s"), *KeyName, *Event));

	if (Event == TEXT("axis"))
	{
		Out->SetBoolField(TEXT("handled"), PressKey(PC, Key, IE_Axis, Inv.GetNumber(TEXT("value"), 1.0)));
		Inv.Succeed(Out);
		return;
	}
	if (Event == TEXT("press") || Event == TEXT("release"))
	{
		Out->SetBoolField(TEXT("handled"), PressKey(PC, Key, Event == TEXT("press") ? IE_Pressed : IE_Released));
		Inv.Succeed(Out);
		return;
	}
	// tap / hold: press now, release after the duration (at least one frame so the game sees it).
	TWeakObjectPtr<APlayerController> Weak = PC;
	Out->SetBoolField(TEXT("handled"), PressKey(PC, Key, IE_Pressed));
	HoldFor(Inv, FMath::Max(0.03, Inv.GetNumber(TEXT("duration"), 0.1)), nullptr,
		[Weak, Key]()
		{
			if (Weak.IsValid())
			{
				PressKey(Weak.Get(), Key, IE_Released);
			}
		},
		Out);
}

static void InjectAction(FToolInvocation& Inv)
{
	APlayerController* PC = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = RequireEnhancedInput(Inv, PC);
	if (!Subsystem)
	{
		return;
	}
	UInputAction* Action = LoadInputAsset<UInputAction>(Inv, TEXT("action"), TEXT("input action"));
	if (!Action)
	{
		return;
	}
	FInputActionValue Value;
	if (!ReadActionValue(Inv.GetValue(TEXT("value")), Value))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("value must be bool, number, [x,y] or [x,y,z]"));
		return;
	}
	Inv.RecordIrreversible(FString::Printf(TEXT("action %s"), *Action->GetName()));
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("action"), Action->GetPathName());
	Out->SetBoolField(TEXT("undoable"), false);
	// Injected input lasts one frame, so re-inject every tick for the duration.
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Weak = Subsystem;
	TWeakObjectPtr<UInputAction> WeakAction = Action;
	auto Inject = [Weak, WeakAction, Value]()
	{
		if (Weak.IsValid() && WeakAction.IsValid())
		{
			Weak->InjectInputForAction(WeakAction.Get(), Value, {}, {});
		}
	};
	Inject();
	HoldFor(Inv, FMath::Max(0.0, Inv.GetNumber(TEXT("duration"), 0.0)), Inject, []() {}, Out);
}

static void ChangeMappingContext(FToolInvocation& Inv, bool bAdd)
{
	APlayerController* PC = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = RequireEnhancedInput(Inv, PC);
	if (!Subsystem)
	{
		return;
	}
	UInputMappingContext* Context = LoadInputAsset<UInputMappingContext>(Inv, TEXT("context"), TEXT("mapping context"));
	if (!Context)
	{
		return;
	}
	int32 OldPriority = 0;
	const bool bHad = Subsystem->HasMappingContext(Context, OldPriority);
	if (!bAdd && !bHad)
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("mapping context is not applied"));
		return;
	}
	const int32 Priority = static_cast<int32>(Inv.GetInt(TEXT("priority"), 0));
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Weak = Subsystem;
	TSharedPtr<TStrongObjectPtr<UInputMappingContext>> Ctx = MakeShared<TStrongObjectPtr<UInputMappingContext>>(Context);
	auto Apply = [Weak, Ctx](bool bPresent, int32 InPriority)
	{
		if (!Weak.IsValid())
		{
			return false;
		}
		Weak->RemoveMappingContext(Ctx->Get());
		if (bPresent)
		{
			Weak->AddMappingContext(Ctx->Get(), InPriority);
		}
		return true;
	};
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("context"), Context->GetPathName());
	Delta->SetBoolField(TEXT("applied"), bAdd);
	auto DoFn = [Apply, bAdd, Priority](FRuntimeCommandContext&, FString& Error)
	{
		if (!Apply(bAdd, Priority))
		{
			Error = TEXT("input subsystem no longer exists");
			return false;
		}
		return true;
	};
	auto UndoFn = [Apply, bHad, OldPriority](FRuntimeCommandContext&) { return Apply(bHad, OldPriority); };
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(bAdd ? TEXT("add mapping context") : TEXT("remove mapping context"), DoFn, UndoFn, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

void FInputToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	B.Tool(TEXT("inject_key"), TEXT("Simulate a key/button for the local player: tap (press+release), press, release or axis."))
		.Args(FToolSchema()
			.Add(TEXT("key"), FToolParam::String(TEXT("UE key name: W, SpaceBar, LeftMouseButton, Gamepad_FaceButton_Bottom"), 64).Required())
			.Add(TEXT("event"), FToolParam::Enum(TEXT("Event"), {TEXT("tap"), TEXT("press"), TEXT("release"), TEXT("axis")}).Default(TEXT("tap")))
			.Add(TEXT("duration"), FToolParam::Number(TEXT("Seconds to hold for tap"), 0.0, 30.0).Default(0.1))
			.Add(TEXT("value"), FToolParam::Number(TEXT("Axis value for event=axis"))))
		.Irreversible().RequiresWorld()
		.Handle(&InjectKey);

	B.Tool(TEXT("inject_action"), TEXT("Trigger an Enhanced Input action with a value, optionally held for a duration."))
		.Args(FToolSchema()
			.Add(TEXT("action"), FToolParam::String(TEXT("Input action path, e.g. /Game/Input/IA_Jump.IA_Jump"), 512).Required())
			.Add(TEXT("value"), FToolParam::Any(TEXT("true, a number, [x,y] or [x,y,z] (default true)")))
			.Add(TEXT("duration"), FToolParam::Number(TEXT("Seconds to keep injecting"), 0.0, 30.0).Default(0.0)))
		.Irreversible().RequiresWorld()
		.Handle(&InjectAction);

	B.Tool(TEXT("add_mapping_context"), TEXT("Apply an Enhanced Input mapping context to the local player."))
		.Args(FToolSchema()
			.Add(TEXT("context"), FToolParam::String(TEXT("Mapping context path"), 512).Required())
			.Add(TEXT("priority"), FToolParam::Integer(TEXT("Priority"), -1000, 1000).Default(0)))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { ChangeMappingContext(Inv, true); });

	B.Tool(TEXT("remove_mapping_context"), TEXT("Remove an Enhanced Input mapping context from the local player."))
		.Args(FToolSchema().Add(TEXT("context"), FToolParam::String(TEXT("Mapping context path"), 512).Required()))
		.Undoable().RequiresWorld()
		.Handle([](FToolInvocation& Inv) { ChangeMappingContext(Inv, false); });
}
