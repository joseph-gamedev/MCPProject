#include "RuntimeActorUtils.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace RuntimeActorUtils
{
	void CopyMaterials(const AActor* From, AActor* To)
	{
		if (!From || !To)
		{
			return;
		}
		TInlineComponentArray<UPrimitiveComponent*> Targets(To);
		TInlineComponentArray<UPrimitiveComponent*> Sources(const_cast<AActor*>(From));
		for (UPrimitiveComponent* Dst : Targets)
		{
			const UPrimitiveComponent* const* SrcPtr = Sources.FindByPredicate([Dst](const UPrimitiveComponent* C) { return C->GetFName() == Dst->GetFName(); });
			const UPrimitiveComponent* Src = SrcPtr ? *SrcPtr : (Sources.Num() == 1 && Targets.Num() == 1 ? Sources[0] : nullptr);
			if (!Src)
			{
				continue;
			}
			const int32 Slots = FMath::Min(Src->GetNumMaterials(), Dst->GetNumMaterials());
			for (int32 i = 0; i < Slots; ++i)
			{
				UMaterialInterface* M = Src->GetMaterial(i);
				if (const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(M))
				{
					UMaterialInstanceDynamic* Copy = UMaterialInstanceDynamic::Create(MID->Parent, Dst);
					Copy->CopyParameterOverrides(const_cast<UMaterialInstanceDynamic*>(MID));
					Dst->SetMaterial(i, Copy);
				}
				else
				{
					Dst->SetMaterial(i, M);
				}
			}
		}
	}
}
