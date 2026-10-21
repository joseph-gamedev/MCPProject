#pragma once

#include "CoreMinimal.h"

class AActor;

/** Actor helpers shared by toolsets. */
namespace RuntimeActorUtils
{
	/**
	 * Copies material assignments from one actor to another (components matched by name, slots by index).
	 * Dynamic material instances are duplicated so the copies can be changed independently.
	 */
	RUNTIMETOOLSETSCORE_API void CopyMaterials(const AActor* From, AActor* To);
}
