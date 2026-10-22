#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FLogicWorld;
struct FLogicUiItem;
class SConstraintCanvas;

/** Layout UI for logic.define_ui: a widget tree like HTML + flexbox (panel, row, column, grid, stack, text, button, bar). */
namespace LogicUi
{
	/** True when a top-level item uses the layout tree (a container kind, or a named anchor like "top-left"). */
	bool IsLayoutItem(const TSharedPtr<FJsonObject>& Item);
	/** Checks kinds, fields, expressions, templates and color syntax of a top-level item and its children. */
	bool Validate(const TSharedPtr<FJsonObject>& Item, FString& OutError);
	/** Checks a theme {colors:{token: color}, radius, padding, gap, screen_margin, font_size, text_shadow}. */
	bool ValidateTheme(const TSharedPtr<FJsonObject>& Theme, FString& OutError);
	/** Checks that every color token in the tree exists in the theme (defaults + custom colors). */
	bool CheckTokens(const TSharedPtr<FJsonObject>& Item, const TSharedPtr<FJsonObject>& Theme, FString& OutError);
	/** Merges a theme patch over the current theme (colors merge key by key). */
	TSharedPtr<FJsonObject> MergeTheme(const TSharedPtr<FJsonObject>& Current, const TSharedPtr<FJsonObject>& Patch);
	/** Builds the item's widgets and places them on the overlay canvas. */
	void AddRoot(FLogicWorld& World, const FLogicUiItem& Item, const TSharedRef<SConstraintCanvas>& Canvas);
}
