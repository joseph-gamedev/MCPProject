#pragma once

#include "CoreMinimal.h"
#include "RuntimeToolsetDefinition.h"
#include "RuntimeUiToolset.generated.h"

class AActor;
class UPanelWidget;
class UWidget;
class UWorld;

/** One on-screen widget. */
USTRUCT()
struct FRuntimeWidgetInfo
{
	GENERATED_BODY()

	/** Pass this directly as `target`, `parent` or `widget` to other ui tools: "widget:Name" for top-level widgets, the object path otherwise. */
	UPROPERTY()
	FString Target;

	/** Widget name. */
	UPROPERTY()
	FString Name;

	/** Widget class, e.g. TextBlock, Button, CanvasPanel. */
	UPROPERTY()
	FString Class;

	/** Full object path (always accepted as a target). */
	UPROPERTY()
	FString Path;

	/** Parent panel's target, empty for a root widget. */
	UPROPERTY()
	FString Parent;

	/** Index in the parent's children. */
	UPROPERTY()
	int32 ChildIndex = 0;

	/** Depth below the listed root (0 = root). */
	UPROPERTY()
	int32 Depth = 0;

	/** Whether the widget is a panel (can take children: add_widget, move_widget). */
	UPROPERTY()
	bool bPanel = false;

	UPROPERTY()
	bool bVisible = true;

	/** Text for text widgets (first 200 characters). */
	UPROPERTY()
	FString Text;
};

/** A list of widgets. */
USTRUCT()
struct FRuntimeWidgetList
{
	GENERATED_BODY()

	/** All matches (the list is cut at `limit`). */
	UPROPERTY()
	int32 Total = 0;

	UPROPERTY()
	TArray<FRuntimeWidgetInfo> Widgets;
};

/** A widget's tree. */
USTRUCT()
struct FRuntimeWidgetTree
{
	GENERATED_BODY()

	/** The root of the listed tree (a user widget's root panel). */
	UPROPERTY()
	FRuntimeWidgetInfo Root;

	/** Widgets in depth-first order (children in slot order); Parent and Depth give the hierarchy. */
	UPROPERTY()
	TArray<FRuntimeWidgetInfo> Widgets;

	/** True when the tree was cut by depth or limit. */
	UPROPERTY()
	bool bTruncated = false;
};

/** Human-readable tree with index-aligned widget refs (Epic: GetWidgetDescription). */
USTRUCT()
struct FRuntimeWidgetDescription
{
	GENERATED_BODY()

	/** One line per widget: "[N] Class Name text=... (hidden)", indented by depth. N indexes Widgets. */
	UPROPERTY()
	FString Description;

	/** Widgets[N] is line [N] of Description: use its target without parsing text. */
	UPROPERTY()
	TArray<FRuntimeWidgetInfo> Widgets;
};

UENUM()
enum class ERuntimeWidgetVisibility : uint8
{
	Visible,
	/** Invisible but still takes space. */
	Hidden,
	/** Invisible and takes no space. */
	Collapsed,
	/** Visible, ignores clicks (it and its children). */
	HitTestInvisible,
	/** Visible, ignores clicks itself; children still get them. */
	SelfHitTestInvisible,
};

/** A widget change. */
USTRUCT()
struct FRuntimeWidgetChange
{
	GENERATED_BODY()

	/** The changed widget. */
	UPROPERTY()
	FRuntimeWidgetInfo Widget;

	/** What changed, e.g. "text", "visibility", "removed". */
	UPROPERTY()
	FString Change;
};

/** A world label node. */
USTRUCT()
struct FRuntimeLabelInfo
{
	GENERATED_BODY()

	/** Node id, e.g. "n12" (pass it to set_label). */
	UPROPERTY()
	FString Id;

	UPROPERTY()
	FString Name;

	/** Scene path. */
	UPROPERTY()
	FString Path;

	UPROPERTY()
	FString Text;

	/** World location of the label. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** False in worlds without a game instance (no widget can be drawn; the node still exists). */
	UPROPERTY()
	bool bVisibleOnScreen = false;
};

/** Activity panel state. */
USTRUCT()
struct FRuntimeActivityInfo
{
	GENERATED_BODY()

	UPROPERTY()
	bool bEnabled = false;
};

/**
 * Live UMG at runtime: inspect, create and change on-screen widgets, HUD text without assets, world labels, and the
 * activity panel. Widgets are addressed by "widget:Name" (top-level or unique names) or by the `target` that every
 * result returns (pass it directly).
 * Workflow: list_widgets -> get_widget_description (one line per widget, index-aligned refs) -> set_text /
 * set_widget_visibility / set_brush, or edit the tree (add_widget, move_widget, wrap_widget, remove_widget).
 * create_hud_text shows text without a widget asset; create_label tags things in the world. All edits are undoable
 * except click_button.
 */
UCLASS()
class URuntimeUiToolset : public URuntimeToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override { return TEXT("2.0.0"); }

	/**
	 * Lists the top-level widgets currently on screen.
	 *
	 * @return Total and the widgets, sorted by name.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetList ListWidgets();

	/**
	 * Lists a widget's hierarchy in depth-first order with classes, texts and parents.
	 *
	 * @param Target - "widget:Name" or a widget target from another result, e.g. "widget:MainMenu".
	 * @param Depth - Levels below the root to include.
	 * @param Limit - Maximum widgets returned.
	 * @return The root and the widgets (children in slot order; parent and depth give the hierarchy).
	 * Fails if the widget is not on screen (NotFound) or the name is ambiguous.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetTree ListWidgetTree(const FString& Target, int32 Depth = 4, int32 Limit = 100);

	/**
	 * Describes a widget tree, one line per widget ("[N] Class Name text=... (hidden)", indented by depth), with an
	 * index-aligned widget list: Widgets[N] is line [N]. The quickest way to understand a screen.
	 *
	 * @param Target - "widget:Name" or a widget target, e.g. "widget:HUD".
	 * @param MaxDepth - Levels to include (-1 = all).
	 * @return The description and the index-aligned widgets.
	 * Fails if the widget is not on screen.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetDescription GetWidgetDescription(const FString& Target, int32 MaxDepth = -1);

	/**
	 * Finds on-screen widgets by name, class or text wildcards (all given filters must match).
	 *
	 * @param Name - Name wildcard, e.g. "*Button*".
	 * @param Class - Class wildcard, e.g. "Button".
	 * @param Text - Text wildcard, e.g. "*Start*".
	 * @param Limit - Maximum widgets returned.
	 * @return Total matches and the widgets in screen-tree order.
	 */
	UFUNCTION(meta = (AICallable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetList FindWidget(const FString& Name = TEXT("*"), const FString& Class = TEXT(""), const FString& Text = TEXT(""), int32 Limit = 100);

	/**
	 * Creates a project widget (Widget Blueprint) and adds it to the screen. Undoable.
	 *
	 * @param Class - Widget class, e.g. "/Game/UI/WBP_Menu" (the _C suffix is added).
	 * @param ZOrder - Higher draws on top.
	 * @return The widget.
	 * Fails if the class is not found or abstract, or there is no local player.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetInfo CreateWidget(const FString& Class, int32 ZOrder = 0);

	/**
	 * Shows text on screen without any widget asset (a canvas with one text block). Undoable.
	 *
	 * @param Text - The text, e.g. "Level 2".
	 * @param Position - [x, y] in 0-1 screen space, the centre of the text; default [0.5, 0.1].
	 * @param Color - Text colour [r, g, b], 0-1; default white.
	 * @param Size - Font size (4-400).
	 * @param Name - Widget name (then "widget:Name" addresses it).
	 * @param ZOrder - Higher draws on top.
	 * @return The widget (target "widget:Name").
	 * Fails if there is no local player (WrongState) or position is outside 0-1.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Position,Color"), Category = "UI")
	static FRuntimeWidgetInfo CreateHudText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Size = 24.f, const FString& Name = TEXT(""),
		int32 ZOrder = 10);

	/**
	 * Adds a new widget as a child of a panel in a live widget tree. Undoable.
	 *
	 * @param Parent - Panel widget target, e.g. a CanvasPanel or VerticalBox from get_widget_description.
	 * @param WidgetClass - Widget class, e.g. "TextBlock", "Button", "Image", "VerticalBox".
	 * @param Name - New widget name.
	 * @param Text - Initial text for text widgets.
	 * @param ChildIndex - Position among the parent's children (-1 = last).
	 * @return The new widget.
	 * Fails if the parent is not a panel or is full, or the class is not a widget class.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI|Tree")
	static FRuntimeWidgetInfo AddWidget(const FString& Parent, TSubclassOf<UWidget> WidgetClass, const FString& Name = TEXT(""), const FString& Text = TEXT(""),
		int32 ChildIndex = -1);

	/**
	 * Moves a widget to another panel (or another position in the same one). Undoable.
	 *
	 * @param Widget - The widget target.
	 * @param NewParent - Destination panel target.
	 * @param ChildIndex - Position among the new parent's children (-1 = last).
	 * @return The widget in its new place.
	 * Fails if the widget is a root, the destination is not a panel, is full, or is inside the widget.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI|Tree")
	static FRuntimeWidgetInfo MoveWidget(const FString& Widget, const FString& NewParent, int32 ChildIndex = -1);

	/**
	 * Wraps a widget in a new panel that takes its place (e.g. a Border for a background, a SizeBox for a fixed size). Undoable.
	 *
	 * @param Widget - The widget target.
	 * @param WrapperClass - Panel class, e.g. "Border", "SizeBox", "VerticalBox", "Overlay".
	 * @param Name - Wrapper name.
	 * @return The new wrapper (the widget is its only child).
	 * Fails if the widget has no parent panel or the class is not a panel.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI|Tree")
	static FRuntimeWidgetInfo WrapWidget(const FString& Widget, TSubclassOf<UPanelWidget> WrapperClass, const FString& Name = TEXT(""));

	/**
	 * Removes a widget: takes a top-level widget off the screen, or removes a child from its panel. Undo puts it back.
	 *
	 * @param Target - The widget target.
	 * @return The removed widget.
	 * Fails if the widget is not found or not on screen.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetChange RemoveWidget(const FString& Target);

	/**
	 * Changes the text of a text block, rich text or editable text (for a user widget: its first text). Undoable.
	 *
	 * @param Target - The widget target, e.g. "widget:Score".
	 * @param Text - New text.
	 * @return The widget after the change.
	 * Fails if the widget is not found or holds no text.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetChange SetText(const FString& Target, const FString& Text);

	/**
	 * Shows, hides or collapses a widget. Undoable.
	 *
	 * @param Target - The widget target.
	 * @param Visibility - Visible, Hidden (keeps its space), Collapsed (no space), HitTestInvisible, SelfHitTestInvisible.
	 * @return The widget after the change.
	 * Fails if the widget is not found.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld), Category = "UI")
	static FRuntimeWidgetChange SetWidgetVisibility(const FString& Target, ERuntimeWidgetVisibility Visibility);

	/**
	 * Sets an Image widget's texture and/or tint. Undoable.
	 *
	 * @param Target - The Image widget target.
	 * @param Tint - Colour [r, g, b], 0-1 (default: keep).
	 * @param Texture - Texture2D path, e.g. "/Game/UI/T_Icon.T_Icon" (default: keep).
	 * @return The widget after the change.
	 * Fails if the target is not an Image or the texture is not found.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Tint"), Category = "UI")
	static FRuntimeWidgetChange SetBrush(const FString& Target, const FLinearColor& Tint, const FString& Texture = TEXT(""));

	/**
	 * Optional on-screen activity panel (current step, count, time, recent actions). Use only when the user asks to see progress.
	 *
	 * @param bEnabled - Show or hide the panel (default: keep).
	 * @param Status - What you are doing, e.g. "Building the Eiffel Tower legs"; empty clears (default: keep).
	 * @return Whether the panel is shown.
	 */
	UFUNCTION(meta = (AICallable, AutoCreateRefTerm = "bEnabled,Status"), Category = "UI")
	static FRuntimeActivityInfo ShowActivity(const bool& bEnabled, const FString& Status);

	/**
	 * Puts a name tag in the world: text drawn above a target (as its child node, so it follows it) or at a location. Undoable.
	 *
	 * @param Text - Label text, e.g. "Town Hall".
	 * @param Location - World location [x, y, z] when there is no target.
	 * @param Offset - World offset from the anchor, default [0, 0, 100].
	 * @param Color - Text colour [r, g, b], 0-1; default white.
	 * @param Target - Node or actor to label (placed above its top centre), e.g. "n12".
	 * @param Size - Font size (6-200).
	 * @param bBackground - Dark backing box.
	 * @param Name - Node name.
	 * @return The label node.
	 * Fails if neither target nor location is given.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Location,Offset,Color"), Category = "UI|Labels")
	static FRuntimeLabelInfo CreateLabel(const FString& Text, const FVector& Location, const FVector& Offset, const FLinearColor& Color, AActor* Target = nullptr,
		float Size = 28.f, bool bBackground = true, const FString& Name = TEXT("label"));

	/**
	 * Changes a label's text, size, colour or background; arguments left out keep their value. Undoable.
	 *
	 * @param Target - The label node, e.g. "n12".
	 * @param Text - New text.
	 * @param Size - Font size (6-200).
	 * @param Color - Text colour [r, g, b].
	 * @param bBackground - Dark backing box.
	 * @return The label after the change.
	 * Fails if the target is not a label made by create_label.
	 */
	UFUNCTION(meta = (AICallable, Undoable, RequiresWorld, AutoCreateRefTerm = "Text,Size,Color,bBackground"), Category = "UI|Labels")
	static FRuntimeLabelInfo SetLabel(AActor* Target, const FString& Text, const float& Size, const FLinearColor& Color, const bool& bBackground);

	/**
	 * Fires a Button's pressed, released and clicked events, as if the player clicked it.
	 *
	 * @param Target - The Button widget target, e.g. "widget:StartButton".
	 * @return The button.
	 * Fails if the target is not a Button or is disabled.
	 */
	UFUNCTION(meta = (AICallable, Irreversible, RequiresWorld), Category = "UI")
	static FRuntimeWidgetInfo ClickButton(const FString& Target);

	/** Source spawner for "label:{text,size,color,background}" nodes. */
	static AActor* SpawnLabelFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError);
};
