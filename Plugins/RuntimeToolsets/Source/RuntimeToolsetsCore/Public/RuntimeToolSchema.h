#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * One parameter of a tool's input schema. Build with the static factories, then chain modifiers:
 *   FToolParam::Integer(TEXT("Max items"), 1, 500).Default(25)
 */
struct RUNTIMETOOLSETSCORE_API FToolParam
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	bool bRequired = false;

	static FToolParam String(const FString& Description, int32 MaxLength = 1024);
	static FToolParam Integer(const FString& Description, TOptional<int64> Min = {}, TOptional<int64> Max = {});
	static FToolParam Number(const FString& Description, TOptional<double> Min = {}, TOptional<double> Max = {});
	static FToolParam Boolean(const FString& Description);
	static FToolParam Enum(const FString& Description, const TArray<FString>& Values);
	/** [x, y, z] in centimetres (or unitless for scale). */
	static FToolParam Vec3(const FString& Description);
	/** [pitch, yaw, roll] in degrees. */
	static FToolParam Rotator(const FString& Description);
	static FToolParam Array(const FString& Description, const FToolParam& Items, int32 MaxItems = 256);
	static FToolParam StringArray(const FString& Description, int32 MaxItems = 64);
	/** Free-form JSON object. */
	static FToolParam Object(const FString& Description);
	/** Any JSON value. */
	static FToolParam Any(const FString& Description);

	/** Target reference: node id (n12), path (group/child), actor:Name, object path, shortcuts (pawn, player_controller...). */
	static FToolParam Target(const FString& Description = TEXT("Target: node id (n12), path (a/b), actor:Name, or object path"));
	/** Standard list limit (default from settings). */
	static FToolParam Limit();
	/** Standard field projection for list results. */
	static FToolParam Fields(const TArray<FString>& Available);
	/** Explicit confirmation for destructive operations. */
	static FToolParam Confirm();

	FToolParam& Required();
	FToolParam& Default(bool Value);
	FToolParam& Default(int64 Value);
	FToolParam& Default(int32 Value) { return Default(static_cast<int64>(Value)); }
	FToolParam& Default(double Value);
	FToolParam& Default(const TCHAR* Value);
	FToolParam& Default(const FString& Value);
	FToolParam& Default(const TSharedPtr<FJsonValue>& Value);
};

/** Builds a "type":"object" schema with additionalProperties:false. */
class RUNTIMETOOLSETSCORE_API FToolSchema
{
public:
	FToolSchema();

	FToolSchema& Add(const FString& Name, const FToolParam& Param);
	TSharedRef<FJsonObject> Build() const;

private:
	TSharedRef<FJsonObject> Properties;
	TArray<FString> RequiredNames;
};

/** Validates arguments against a schema built by FToolSchema (JSON Schema subset) and fills defaults. */
namespace RuntimeToolSchema
{
	/**
	 * @param Schema  Object schema.
	 * @param InOutArgs  Arguments; missing optional properties with defaults are filled in.
	 * @param OutError  "$.location[2]: expected number" style message.
	 */
	RUNTIMETOOLSETSCORE_API bool ValidateAndFill(const TSharedRef<FJsonObject>& Schema, const TSharedRef<FJsonObject>& InOutArgs, FString& OutError);

	RUNTIMETOOLSETSCORE_API bool ValidateValue(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value, const FString& Path, FString& OutError);
}
