#include "RuntimeInputToolset.h"
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

namespace
{
	APlayerController* RequireLocalPlayer()
	{
		APlayerController* PC = RuntimeToolCall::GetWorld()->GetFirstPlayerController();
		if (!PC || !PC->GetLocalPlayer() || !PC->PlayerInput)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no local player with input in this world"),
				TEXT("Input injection needs a running game or PIE session with a local player controller"));
			return nullptr;
		}
		return PC;
	}

	UEnhancedInputLocalPlayerSubsystem* RequireEnhancedInput()
	{
		APlayerController* PC = RequireLocalPlayer();
		if (!PC)
		{
			return nullptr;
		}
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		if (!Subsystem)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::Unsupported, TEXT("Enhanced Input is not active for the local player"),
				TEXT("Use input.inject_key for games with legacy input bindings"));
		}
		return Subsystem;
	}

	template <typename T>
	T* InputLoadAsset(const FString& Path, const TCHAR* What)
	{
		const URuntimeToolsetsSettings& S = URuntimeToolsetsSettings::Get();
		bool bAllowed = S.AllowedObjectRoots.Num() == 0;
		for (const FString& Root : S.AllowedObjectRoots)
		{
			bAllowed |= Path.StartsWith(Root);
		}
		if (!bAllowed)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotAllowed, FString::Printf(TEXT("'%s' is outside AllowedObjectRoots"), *Path),
				TEXT("Allowed roots: ") + FString::Join(S.AllowedObjectRoots, TEXT(", ")));
			return nullptr;
		}
		UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		T* Asset = Cast<T>(Loaded);
		if (!Asset)
		{
			RuntimeToolCall::Fail(Loaded ? ERuntimeToolError::InvalidArguments : ERuntimeToolError::NotFound,
				Loaded ? FString::Printf(TEXT("'%s' is a %s, expected a %s"), *Path, *Loaded->GetClass()->GetName(), *T::StaticClass()->GetName())
					   : FString::Printf(TEXT("%s '%s' not found"), What, *Path),
				TEXT("Use the full object path (/Game/Input/IA_Jump.IA_Jump) of an asset cooked into the build; level.find_assets lists them"));
		}
		return Asset;
	}

	bool InputPressKey(APlayerController* PC, const FKey& Key, EInputEvent Event, double Value = 1.0)
	{
		return PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, static_cast<float>(Value)));
	}

	/** Reads a bool / number / [x,y] / [x,y,z] into an action value. */
	bool InputReadActionValue(const TSharedPtr<FJsonValue>& V, FInputActionValue& Out)
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
				double D = 0.0;
				if (!(*Arr)[i]->TryGetNumber(D))
				{
					return false;
				}
				Axis[i] = D;
			}
			Out = FInputActionValue(Arr->Num() == 2 ? EInputActionValueType::Axis2D : EInputActionValueType::Axis3D, Axis);
			return true;
		}
		return false;
	}

	/**
	 * Runs Each() every tick for Duration seconds, then Finish() and Complete(). If the call is cancelled or times out,
	 * Finish() still runs (never leave a key held down).
	 */
	void InputHoldFor(URuntimeToolAsyncResult* Result, double Duration, TFunction<void()> Each, TFunction<void()> Finish, TFunction<void()> Complete)
	{
		TWeakObjectPtr<URuntimeToolAsyncResult> Weak = Result;
		const double End = FPlatformTime::Seconds() + Duration;
		TSharedRef<TFunction<void()>> FinishRef = MakeShared<TFunction<void()>>(MoveTemp(Finish));
		FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, End, Each, FinishRef, Complete](float)
		{
			if (!Weak.IsValid() || Weak->bIsComplete)
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
			if (*FinishRef)
			{
				(*FinishRef)();
			}
			Complete();
			return false;
		}));
		Result->OnCancelled = [Handle, FinishRef]()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			if (*FinishRef)
			{
				(*FinishRef)();
			}
		};
		RuntimeToolCall::SetTimeout(Duration + 10.0);
	}

	FInputToolMappingResult ChangeMappingContext(const FString& Path, bool bAdd, int32 Priority)
	{
		FInputToolMappingResult Result;
		UEnhancedInputLocalPlayerSubsystem* Subsystem = RequireEnhancedInput();
		if (!Subsystem)
		{
			return Result;
		}
		UInputMappingContext* Context = InputLoadAsset<UInputMappingContext>(Path, TEXT("mapping context"));
		if (!Context)
		{
			return Result;
		}
		int32 OldPriority = 0;
		const bool bHad = Subsystem->HasMappingContext(Context, OldPriority);
		if (!bAdd && !bHad)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("mapping context '%s' is not applied"), *Path),
				TEXT("Nothing to remove; add it with input.add_mapping_context"));
			return Result;
		}
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
		TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
		Delta->SetStringField(TEXT("context"), Context->GetPathName());
		Delta->SetBoolField(TEXT("applied"), bAdd);
		auto DoFn = [Apply, bAdd, Priority](FRuntimeCommandContext&, FString& OutError)
		{
			if (!Apply(bAdd, Priority))
			{
				OutError = TEXT("input subsystem no longer exists");
				return false;
			}
			return true;
		};
		auto UndoFn = [Apply, bHad, OldPriority](FRuntimeCommandContext&) { return Apply(bHad, OldPriority); };
		if (RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(bAdd ? TEXT("add mapping context") : TEXT("remove mapping context"), DoFn, UndoFn, Delta)))
		{
			Result.Context = Context->GetPathName();
			Result.bApplied = bAdd;
		}
		return Result;
	}
}

URuntimeInputToolKeyAsyncResult* URuntimeInputToolset::InjectKey(const FString& Key, EInputToolKeyEvent Event, float Duration, float Value)
{
	APlayerController* PC = RequireLocalPlayer();
	if (!PC)
	{
		return nullptr;
	}
	const FKey InputKey(*Key);
	if (!InputKey.IsValid())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("unknown key '%s'"), *Key),
			TEXT("Use UE key names: W, SpaceBar, LeftShift, LeftMouseButton, Gamepad_FaceButton_Bottom, MouseX"));
		return nullptr;
	}
	if (Duration < 0.f || Duration > 30.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("duration %g is out of range"), Duration), TEXT("Use 0 .. 30 seconds"));
		return nullptr;
	}
	URuntimeInputToolKeyAsyncResult* Result = NewObject<URuntimeInputToolKeyAsyncResult>();
	FInputToolKeyResult Out;
	Out.Key = InputKey.GetFName().ToString();
	Out.Event = Event;
	RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("key %s %s"), *Key, *UEnum::GetValueAsString(Event)));

	switch (Event)
	{
	case EInputToolKeyEvent::Axis:
		Out.bHandled = InputPressKey(PC, InputKey, IE_Axis, Value);
		Result->SetValue(Out);
		return Result;
	case EInputToolKeyEvent::Press:
	case EInputToolKeyEvent::Release:
		Out.bHandled = InputPressKey(PC, InputKey, Event == EInputToolKeyEvent::Press ? IE_Pressed : IE_Released);
		Result->SetValue(Out);
		return Result;
	default:
		break;
	}
	// Tap: press now, release after the duration (at least one frame so the game sees it).
	Out.bHandled = InputPressKey(PC, InputKey, IE_Pressed);
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	TWeakObjectPtr<URuntimeInputToolKeyAsyncResult> WeakResult = Result;
	InputHoldFor(Result, FMath::Max(0.03, static_cast<double>(Duration)), nullptr,
		[WeakPC, InputKey]()
		{
			if (WeakPC.IsValid())
			{
				InputPressKey(WeakPC.Get(), InputKey, IE_Released);
			}
		},
		[WeakResult, Out]()
		{
			if (WeakResult.IsValid())
			{
				WeakResult->SetValue(Out);
			}
		});
	return Result;
}

URuntimeInputToolActionAsyncResult* URuntimeInputToolset::InjectAction(const FString& Action, const FRuntimeToolJson& Value, float Duration)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = RequireEnhancedInput();
	if (!Subsystem)
	{
		return nullptr;
	}
	if (Duration < 0.f || Duration > 30.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("duration %g is out of range"), Duration), TEXT("Use 0 .. 30 seconds"));
		return nullptr;
	}
	UInputAction* InputAction = InputLoadAsset<UInputAction>(Action, TEXT("input action"));
	if (!InputAction)
	{
		return nullptr;
	}
	FInputActionValue ActionValue;
	if (!InputReadActionValue(Value.Value, ActionValue))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("value must be true/false, a number, [x,y] or [x,y,z]"),
			FString::Printf(TEXT("%s expects a %s value"), *InputAction->GetName(), *UEnum::GetValueAsString(InputAction->ValueType)));
		return nullptr;
	}
	RuntimeToolCall::Current()->RecordIrreversible(FString::Printf(TEXT("action %s"), *InputAction->GetName()));
	URuntimeInputToolActionAsyncResult* Result = NewObject<URuntimeInputToolActionAsyncResult>();
	TSharedRef<FInputToolActionResult> Out = MakeShared<FInputToolActionResult>();
	Out->Action = InputAction->GetPathName();
	// Injected input lasts one frame, so re-inject every tick for the duration.
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Weak = Subsystem;
	TWeakObjectPtr<UInputAction> WeakAction = InputAction;
	auto Inject = [Weak, WeakAction, ActionValue, Out]()
	{
		if (Weak.IsValid() && WeakAction.IsValid())
		{
			Weak->InjectInputForAction(WeakAction.Get(), ActionValue, {}, {});
			++Out->Frames;
		}
	};
	Inject();
	if (Duration <= 0.f)
	{
		Result->SetValue(*Out);
		return Result;
	}
	TWeakObjectPtr<URuntimeInputToolActionAsyncResult> WeakResult = Result;
	InputHoldFor(Result, Duration, Inject, nullptr,
		[WeakResult, Out]()
		{
			if (WeakResult.IsValid())
			{
				WeakResult->SetValue(*Out);
			}
		});
	return Result;
}

FInputToolMappingResult URuntimeInputToolset::AddMappingContext(const FString& Context, int32 Priority)
{
	if (Priority < -1000 || Priority > 1000)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("priority %d is out of range"), Priority), TEXT("Use -1000 .. 1000"));
		return FInputToolMappingResult();
	}
	return ChangeMappingContext(Context, true, Priority);
}

FInputToolMappingResult URuntimeInputToolset::RemoveMappingContext(const FString& Context)
{
	return ChangeMappingContext(Context, false, 0);
}
