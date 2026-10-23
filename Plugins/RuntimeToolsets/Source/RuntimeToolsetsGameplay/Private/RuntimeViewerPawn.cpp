#include "RuntimeViewerPawn.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void ARuntimeViewerPawn::SetupPlayerInputComponent(UInputComponent* InInputComponent)
{
	Super::SetupPlayerInputComponent(InInputComponent);
	if (!InInputComponent)
	{
		return;
	}
	// The default pawn turns on every mouse move (axes DefaultPawn_Turn / DefaultPawn_LookUp are the mouse; the *Rate
	// axes are the gamepad and stay as they are). Rebind the mouse ones so they only act while dragging.
	InInputComponent->AxisBindings.RemoveAll([](const FInputAxisBinding& B)
	{
		return B.AxisName == TEXT("DefaultPawn_Turn") || B.AxisName == TEXT("DefaultPawn_LookUp");
	});
	InInputComponent->BindAxis(TEXT("DefaultPawn_Turn"), this, &ARuntimeViewerPawn::DragTurn);
	InInputComponent->BindAxis(TEXT("DefaultPawn_LookUp"), this, &ARuntimeViewerPawn::DragLookUp);
}

bool ARuntimeViewerPawn::IsDragging() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && (PC->IsInputKeyDown(EKeys::LeftMouseButton) || PC->IsInputKeyDown(EKeys::RightMouseButton) || PC->IsInputKeyDown(EKeys::MiddleMouseButton));
}

void ARuntimeViewerPawn::DragTurn(float Value)
{
	if (Value != 0.f && IsDragging())
	{
		AddControllerYawInput(Value);
	}
}

void ARuntimeViewerPawn::DragLookUp(float Value)
{
	if (Value != 0.f && IsDragging())
	{
		AddControllerPitchInput(Value);
	}
}

ARuntimeViewerGameMode::ARuntimeViewerGameMode()
{
	DefaultPawnClass = ARuntimeViewerPawn::StaticClass();
}
