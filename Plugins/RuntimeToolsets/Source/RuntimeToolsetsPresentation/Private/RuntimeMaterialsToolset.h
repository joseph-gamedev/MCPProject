#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolCall.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeMaterialsToolset.generated.h"

class UPrimitiveComponent;

/** Type of a material parameter that can change at runtime (static switches are baked into shaders and cannot). */
UENUM()
enum class ERuntimeMaterialParamType : uint8
{
	Scalar,
	Vector,
	Texture,
};

/** Material slots of a mesh and the parameters (with current values) of one slot. */
USTRUCT()
struct FRuntimeMaterialParameters
{
	GENERATED_BODY()

	/** The mesh component (pass it back as `target`). */
	UPROPERTY()
	FString Component;

	/** Material path per slot, index-aligned with slot numbers. */
	UPROPERTY()
	TArray<FString> Slots;

	/** The slot the parameters below belong to. */
	UPROPERTY()
	int32 Slot = 0;

	/** Material of that slot. */
	UPROPERTY()
	FString Material;

	/** Whether the slot already holds a dynamic instance (set_* tools create one on first change). */
	UPROPERTY()
	bool bDynamic = false;

	/** Scalar parameters: name -> value. Change with set_scalar. */
	UPROPERTY()
	TMap<FString, float> Scalars;

	/** Vector (colour) parameters: name -> [r,g,b,a]. Change with set_vector. */
	UPROPERTY()
	TMap<FString, FLinearColor> Vectors;

	/** Texture parameters: name -> texture path (empty = none). Change with set_texture. */
	UPROPERTY()
	TMap<FString, FString> Textures;
};

/** One material parameter and its current value. */
USTRUCT()
struct FRuntimeMaterialParameter
{
	GENERATED_BODY()

	/** The mesh component the material is on. */
	UPROPERTY()
	FString Target;

	UPROPERTY()
	int32 Slot = 0;

	/** Material of the slot (a dynamic instance after a change). */
	UPROPERTY()
	FString Material;

	/** Parameter name, exact case. */
	UPROPERTY()
	FString Name;

	UPROPERTY()
	ERuntimeMaterialParamType Type = ERuntimeMaterialParamType::Scalar;

	/** Value when type is Scalar. */
	UPROPERTY()
	float Scalar = 0.f;

	/** Value when type is Vector. */
	UPROPERTY()
	FLinearColor Vector = FLinearColor::Black;

	/** Value when type is Texture: texture path, empty = none. */
	UPROPERTY()
	FString Texture;
};

/** Result of set_material. */
USTRUCT()
struct FRuntimeMaterialAssignment
{
	GENERATED_BODY()

	UPROPERTY()
	FString Target;

	/** Assigned material path. */
	UPROPERTY()
	FString Material;

	/** Slots that changed. */
	UPROPERTY()
	TArray<int32> Slots;
};

/** Parameters of a material parameter collection (values for the current world). */
USTRUCT()
struct FRuntimeCollectionParameters
{
	GENERATED_BODY()

	UPROPERTY()
	FString Collection;

	/** Scalar parameters: name -> value. */
	UPROPERTY()
	TMap<FString, float> Scalars;

	/** Vector parameters: name -> [r,g,b,a]. */
	UPROPERTY()
	TMap<FString, FLinearColor> Vectors;
};

/** One collection parameter after a change. */
USTRUCT()
struct FRuntimeCollectionParameter
{
	GENERATED_BODY()

	UPROPERTY()
	FString Collection;

	UPROPERTY()
	FString Name;

	/** Scalar or Vector. */
	UPROPERTY()
	ERuntimeMaterialParamType Type = ERuntimeMaterialParamType::Scalar;

	UPROPERTY()
	float Scalar = 0.f;

	UPROPERTY()
	FLinearColor Vector = FLinearColor::Black;

	/** Value before the change. */
	UPROPERTY()
	FRuntimeToolJson Previous;
};

/**
 * Materials at runtime: material slots and parameters on live meshes, and material parameter collections (world-wide values).
 * Workflow: list_parameters (slots, parameter names, types and values) -> get_parameter -> set_scalar / set_vector /
 * set_texture (the first change puts a dynamic instance on the slot; undo restores the original material) or set_material
 * to swap the whole material. Collections: list_collection_parameters -> set_collection_param.
 * Parameter names are matched case-insensitively; a name of another type fails with the tool to use instead.
 */
UCLASS()
class URuntimeMaterialsToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Lists a mesh's material slots and the scalar, vector and texture parameters (with current values) of one slot.
	 * Call this first: it gives exact parameter names and types.
	 *
	 * @param Target - Mesh actor or component (node id like "n12", actor:Name, or actor#Component to pick a component).
	 * @param Slot - Material slot index (0 = first).
	 * @param Limit - Maximum parameters returned per type.
	 * @return Slots (material path per slot), the slot's material, whether it is dynamic, and name -> value maps per type (sorted by name).
	 * Fails if the target has no mesh with material slots or the slot is out of range.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Materials")
	static FRuntimeMaterialParameters ListParameters(UPrimitiveComponent* Target, int32 Slot = 0, int32 Limit = 100);

	/**
	 * Reads one material parameter (any type) on a mesh slot.
	 *
	 * @param Target - Mesh actor or component, e.g. "n12".
	 * @param Name - Parameter name (case-insensitive), e.g. "Color".
	 * @param Slot - Material slot index.
	 * @return The parameter: name (exact case), type and value.
	 * Fails if no parameter of that name exists (the error lists the available ones).
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Materials")
	static FRuntimeMaterialParameter GetParameter(UPrimitiveComponent* Target, const FString& Name, int32 Slot = 0);

	/**
	 * Assigns a material asset to a slot, or to every slot. Undoable.
	 *
	 * @param Target - Mesh actor or component, e.g. "n12".
	 * @param Material - Material or material instance path, e.g. "/Game/Materials/M_Stone.M_Stone".
	 * @param Slot - Material slot index.
	 * @param bAllSlots - Apply to every slot instead of `slot`.
	 * @return The target, the material path and the slots that changed.
	 * Fails if the material is not found or outside AllowedObjectRoots, or the slot is out of range.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Materials")
	static FRuntimeMaterialAssignment SetMaterial(UPrimitiveComponent* Target, const FString& Material, int32 Slot = 0, bool bAllSlots = false);

	/**
	 * Sets a scalar material parameter (creates a dynamic instance on the slot on first change). Undoable.
	 *
	 * @param Target - Mesh actor or component, e.g. "n12".
	 * @param Name - Scalar parameter name, e.g. "Roughness".
	 * @param Value - New value, e.g. 0.4.
	 * @param Slot - Material slot index.
	 * @return The parameter with its new value.
	 * Fails if the parameter does not exist, or is not a scalar (the error names the right tool).
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Materials")
	static FRuntimeMaterialParameter SetScalar(UPrimitiveComponent* Target, const FString& Name, float Value, int32 Slot = 0);

	/**
	 * Sets a vector (colour) material parameter (creates a dynamic instance on first change). Undoable.
	 *
	 * @param Target - Mesh actor or component, e.g. "n12".
	 * @param Name - Vector parameter name, e.g. "Color".
	 * @param Value - [r,g,b] or [r,g,b,a], linear 0-1 (above 1 for emissive), e.g. [1,0.2,0.1].
	 * @param Slot - Material slot index.
	 * @return The parameter with its new value.
	 * Fails if the parameter does not exist or is not a vector.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Materials")
	static FRuntimeMaterialParameter SetVector(UPrimitiveComponent* Target, const FString& Name, const FLinearColor& Value, int32 Slot = 0);

	/**
	 * Sets a texture material parameter (creates a dynamic instance on first change). Undoable.
	 *
	 * @param Target - Mesh actor or component, e.g. "n12".
	 * @param Name - Texture parameter name, e.g. "BaseTexture".
	 * @param Texture - Texture path, e.g. "/Game/Textures/T_Brick.T_Brick"; empty clears the parameter.
	 * @param Slot - Material slot index.
	 * @return The parameter with its new value.
	 * Fails if the parameter does not exist or is not a texture, or the texture is not found.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Materials")
	static FRuntimeMaterialParameter SetTexture(UPrimitiveComponent* Target, const FString& Name, const FString& Texture = TEXT(""), int32 Slot = 0);

	/**
	 * Lists the parameters of a material parameter collection with their values in the current world.
	 *
	 * @param Collection - Collection path, e.g. "/Game/Materials/MPC_Global.MPC_Global".
	 * @return Scalars and vectors as name -> value maps.
	 * Fails if the collection is not found.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "Materials|Collections")
	static FRuntimeCollectionParameters ListCollectionParameters(const FString& Collection);

	/**
	 * Sets a material parameter collection value for the whole world: a number for scalars, [r,g,b(,a)] for vectors. Undoable.
	 *
	 * @param Collection - Collection path, e.g. "/Game/Materials/MPC_Global.MPC_Global".
	 * @param Name - Parameter name, e.g. "WindStrength".
	 * @param Value - A number (scalar parameter) or [r,g,b] / [r,g,b,a] (vector parameter), e.g. 0.5.
	 * @return The parameter, its type, the new value and the previous one.
	 * Fails if the collection or parameter is not found, or the value does not match the parameter type.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "Materials|Collections")
	static FRuntimeCollectionParameter SetCollectionParam(const FString& Collection, const FString& Name, const FRuntimeToolJson& Value);
};
