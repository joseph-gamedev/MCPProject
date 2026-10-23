#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/GameModeBase.h"
#include "RuntimeViewerPawn.generated.h"

/**
 * The free camera players and stream viewers fly around a built scene with: WASD / QE / gamepad as usual, but the mouse
 * only turns the view while a mouse button is held (drag to look). Over Pixel Streaming the cursor hovers over the
 * stream all the time; without this every mouse move spun the camera away from what the agent framed.
 */
UCLASS()
class ARuntimeViewerPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;

private:
	bool IsDragging() const;
	void DragTurn(float Value);
	void DragLookUp(float Value);
};

/** Default game mode: the drag-to-look viewer pawn. Toolsets that start a game (characters, vehicles) still swap pawns. */
UCLASS()
class ARuntimeViewerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARuntimeViewerGameMode();
};
