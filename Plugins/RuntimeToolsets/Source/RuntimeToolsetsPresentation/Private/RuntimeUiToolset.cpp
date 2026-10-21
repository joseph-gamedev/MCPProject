#include "RuntimeUiToolset.h"
#include "PresentationToolCall.h"
#include "RuntimeCommand.h"
#include "RuntimeCommonCommands.h"
#include "RuntimeJson.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolHelpers.h"
#include "RuntimeToolsetsActivity.h"
#include "RuntimeToolsetsHudWidget.h"
#include "RuntimeToolsetsLabelActor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace RtUi
{
	using namespace PresentationTool;

	/** Top-level user widgets of this world that are on screen, sorted by name. */
	TArray<UUserWidget*> ViewportWidgets(UWorld* World)
	{
		TArray<UUserWidget*> Out;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* W = *It;
			if (W && W->GetWorld() == World && W->IsInViewport() && !W->HasAnyFlags(RF_ClassDefaultObject))
			{
				Out.Add(W);
			}
		}
		Out.Sort([](const UUserWidget& A, const UUserWidget& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	FString WidgetText(const UWidget* W)
	{
		if (const UTextBlock* T = Cast<UTextBlock>(W)) return T->GetText().ToString();
		if (const URichTextBlock* R = Cast<URichTextBlock>(W)) return R->GetText().ToString();
		if (const UEditableTextBox* E = Cast<UEditableTextBox>(W)) return E->GetText().ToString();
		if (const UEditableText* E2 = Cast<UEditableText>(W)) return E2->GetText().ToString();
		return FString();
	}

	bool HoldsText(const UWidget* W)
	{
		return W && (W->IsA<UTextBlock>() || W->IsA<URichTextBlock>() || W->IsA<UEditableTextBox>() || W->IsA<UEditableText>());
	}

	bool SetWidgetText(UWidget* W, const FText& Text)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W)) { T->SetText(Text); return true; }
		if (URichTextBlock* R = Cast<URichTextBlock>(W)) { R->SetText(Text); return true; }
		if (UEditableTextBox* E = Cast<UEditableTextBox>(W)) { E->SetText(Text); return true; }
		if (UEditableText* E2 = Cast<UEditableText>(W)) { E2->SetText(Text); return true; }
		return false;
	}

	/** Target string for a widget: "widget:Name" for top-level user widgets on screen, else the object path. */
	FString WidgetTarget(const UWidget* W)
	{
		if (!W)
		{
			return FString();
		}
		if (const UUserWidget* U = Cast<UUserWidget>(W); U && U->IsInViewport())
		{
			return TEXT("widget:") + U->GetName();
		}
		return W->GetPathName();
	}

	FRuntimeWidgetInfo MakeInfo(const UWidget* W, int32 Depth = 0)
	{
		FRuntimeWidgetInfo I;
		if (!W)
		{
			return I;
		}
		I.Target = WidgetTarget(W);
		I.Name = W->GetName();
		I.Class = W->GetClass()->GetName();
		I.Path = W->GetPathName();
		if (UPanelWidget* P = W->GetParent())
		{
			I.Parent = WidgetTarget(P);
			I.ChildIndex = P->GetChildIndex(W);
		}
		I.Depth = Depth;
		I.bPanel = W->IsA<UPanelWidget>();
		I.bVisible = W->IsVisible();
		const FString Text = WidgetText(W);
		I.Text = Text.Len() > 200 ? Text.Left(200) + TEXT("...") : Text;
		return I;
	}

	/** Resolves "widget:Name" (searching on-screen widgets and their trees) or any widget object path. Fails the call on error. */
	UWidget* ResolveWidget(const FString& Target, const TCHAR* Arg = TEXT("target"))
	{
		if (Target.IsEmpty())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is required"), Arg), TEXT("Use \"widget:Name\" or a target from ui.list_widgets"));
			return nullptr;
		}
		if (Target.StartsWith(TEXT("widget:")))
		{
			const FString Name = Target.Mid(7);
			TArray<UWidget*> Matches;
			TArray<FString> TopNames;
			for (UUserWidget* Root : ViewportWidgets(RuntimeToolCall::GetWorld()))
			{
				TopNames.Add(Root->GetName());
				if (Root->GetName().Equals(Name, ESearchCase::IgnoreCase))
				{
					Matches.Add(Root);
				}
				if (Root->WidgetTree)
				{
					Root->WidgetTree->ForEachWidget([&](UWidget* W)
					{
						if (W->GetName().Equals(Name, ESearchCase::IgnoreCase))
						{
							Matches.Add(W);
						}
					});
				}
			}
			if (Matches.Num() == 1)
			{
				return Matches[0];
			}
			if (Matches.Num() == 0)
			{
				RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no widget named '%s' on screen (top-level: %s)"), *Name, *JoinOrNone(TopNames)),
					TEXT("Use ui.find_widget or ui.get_widget_description"));
			}
			else
			{
				TArray<FString> Paths;
				for (const UWidget* M : Matches)
				{
					Paths.Add(M->GetPathName());
				}
				RuntimeToolCall::Fail(ERuntimeToolError::AmbiguousTarget, FString::Printf(TEXT("%d widgets named '%s': %s"), Matches.Num(), *Name, *FString::Join(Paths, TEXT(", "))),
					TEXT("Pass one of the paths"));
			}
			return nullptr;
		}
		UObject* O = Target.StartsWith(TEXT("/")) ? StaticFindObject(UWidget::StaticClass(), nullptr, *Target) : nullptr;
		if (!O)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("no widget '%s'"), *Target), TEXT("Use \"widget:Name\" or a target from ui.find_widget"));
			return nullptr;
		}
		return Cast<UWidget>(O);
	}

	template <typename T>
	T* ResolveWidgetAs(const FString& Target, const TCHAR* Arg = TEXT("target"))
	{
		UWidget* W = ResolveWidget(Target, Arg);
		if (!W)
		{
			return nullptr;
		}
		T* Typed = Cast<T>(W);
		if (!Typed)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is a %s, not a %s"), *W->GetName(), *W->GetClass()->GetName(), *T::StaticClass()->GetName()));
		}
		return Typed;
	}

	APlayerController* RequireWidgetOwner()
	{
		APlayerController* PC = RuntimeToolCall::GetWorld()->GetFirstPlayerController();
		if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("no local player to own widgets"), TEXT("UI tools need a running game or PIE session with a local player"));
			return nullptr;
		}
		return PC;
	}

	/** Children of a widget for tree walks: panel children, or a user widget's root. */
	TArray<UWidget*> ChildrenOf(UWidget* W)
	{
		TArray<UWidget*> Out;
		if (UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i)
			{
				if (UWidget* C = P->GetChildAt(i))
				{
					Out.Add(C);
				}
			}
		}
		else if (UUserWidget* U = Cast<UUserWidget>(W); U && U->WidgetTree && U->WidgetTree->RootWidget)
		{
			Out.Add(U->WidgetTree->RootWidget);
		}
		return Out;
	}

	void Walk(UWidget* W, int32 Depth, int32 MaxDepth, int32 Limit, TArray<FRuntimeWidgetInfo>& Out, bool& bTruncated)
	{
		if (Out.Num() >= Limit)
		{
			bTruncated = true;
			return;
		}
		Out.Add(MakeInfo(W, Depth));
		const TArray<UWidget*> Children = ChildrenOf(W);
		if (Children.Num() == 0)
		{
			return;
		}
		if (MaxDepth >= 0 && Depth >= MaxDepth)
		{
			bTruncated = true;
			return;
		}
		for (UWidget* C : Children)
		{
			Walk(C, Depth + 1, MaxDepth, Limit, Out, bTruncated);
		}
	}

	UWidget* TreeRoot(UWidget* W)
	{
		if (UUserWidget* User = Cast<UUserWidget>(W); User && User->WidgetTree && User->WidgetTree->RootWidget)
		{
			return User->WidgetTree->RootWidget;
		}
		return W;
	}

	/**
	 * Inserts a widget into a live panel at an index (-1 = end). Later siblings are removed and re-added with their own
	 * slots as templates, so the live Slate widget updates and slot layout is kept. Template: slot settings for the widget
	 * (only valid for the same panel type).
	 */
	bool InsertAt(UPanelWidget* Panel, UWidget* W, int32 Index, UPanelSlot* Template)
	{
		if (!Panel || !W)
		{
			return false;
		}
		const int32 Count = Panel->GetChildrenCount();
		if (Index < 0 || Index >= Count)
		{
			return (Template ? Panel->AddChild(W, Template) : Panel->AddChild(W)) != nullptr;
		}
		TArray<TPair<UWidget*, TStrongObjectPtr<UPanelSlot>>> Tail;
		for (int32 i = Index; i < Count; ++i)
		{
			UWidget* C = Panel->GetChildAt(i);
			Tail.Emplace(C, TStrongObjectPtr<UPanelSlot>(C ? C->Slot.Get() : nullptr));
		}
		for (int32 i = Tail.Num() - 1; i >= 0; --i)
		{
			Panel->RemoveChild(Tail[i].Key);
		}
		const bool bOk = (Template ? Panel->AddChild(W, Template) : Panel->AddChild(W)) != nullptr;
		for (TPair<UWidget*, TStrongObjectPtr<UPanelSlot>>& T : Tail)
		{
			if (T.Key)
			{
				T.Value.IsValid() ? Panel->AddChild(T.Key, T.Value.Get()) : Panel->AddChild(T.Key);
			}
		}
		return bOk;
	}

	/** Where a widget sits, to put it back. */
	struct FWidgetPlace
	{
		TWeakObjectPtr<UPanelWidget> Parent;
		int32 Index = -1;
		TStrongObjectPtr<UPanelSlot> Slot;

		static FWidgetPlace Of(UWidget* W)
		{
			FWidgetPlace P;
			if (UPanelWidget* Parent = W->GetParent())
			{
				P.Parent = Parent;
				P.Index = Parent->GetChildIndex(W);
				P.Slot.Reset(W->Slot.Get());
			}
			return P;
		}

		bool Restore(UWidget* W) const
		{
			if (!Parent.IsValid())
			{
				return false;
			}
			if (W->GetParent())
			{
				W->RemoveFromParent();
			}
			return InsertAt(Parent.Get(), W, Index, Slot.Get());
		}
	};

	bool CheckPanelAccepts(UPanelWidget* Panel)
	{
		if (!Panel->CanAddMoreChildren())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s (%s) holds a single child and already has one"), *Panel->GetName(), *Panel->GetClass()->GetName()),
				TEXT("Use a multi-child panel (CanvasPanel, VerticalBox, HorizontalBox, Overlay) or ui.wrap_widget"));
			return false;
		}
		return true;
	}

	/** Adds a widget to the viewport. Undo removes it, redo re-adds it; the widget is kept alive meanwhile. */
	class FViewportWidgetCommand final : public IRuntimeCommand
	{
	public:
		FViewportWidgetCommand(UUserWidget* InWidget, int32 InZOrder, bool bInAdd) : Widget(InWidget), ZOrder(InZOrder), bAdd(bInAdd) {}

		virtual FString GetLabel() const override { return FString::Printf(TEXT("%s widget %s"), bAdd ? TEXT("add") : TEXT("remove"), *Widget->GetName()); }
		virtual bool Do(FRuntimeCommandContext& Context, FString& OutError) override { return Set(bAdd); }
		virtual bool Undo(FRuntimeCommandContext& Context) override { return Set(!bAdd); }
		virtual bool Redo(FRuntimeCommandContext& Context) override { return Set(bAdd); }

		virtual TSharedPtr<FJsonObject> GetDelta(bool bForUndo) const override
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField((bAdd != bForUndo) ? TEXT("shown") : TEXT("removed"), Widget->GetPathName());
			return D;
		}

	private:
		bool Set(bool bInViewport)
		{
			UUserWidget* W = Widget.Get();
			if (!W)
			{
				return false;
			}
			if (bInViewport && !W->IsInViewport())
			{
				W->AddToViewport(ZOrder);
			}
			else if (!bInViewport && W->IsInViewport())
			{
				W->RemoveFromParent();
			}
			return true;
		}

		TStrongObjectPtr<UUserWidget> Widget;
		int32 ZOrder;
		bool bAdd;
	};

	/** Lambda command keeping widgets alive while the history holds it. */
	TUniquePtr<IRuntimeCommand> WidgetCommand(const FString& Label, TArray<UWidget*> Keep, TFunction<bool()> Do, TFunction<bool()> Undo)
	{
		TSharedRef<TArray<TStrongObjectPtr<UWidget>>> Alive = MakeShared<TArray<TStrongObjectPtr<UWidget>>>();
		for (UWidget* W : Keep)
		{
			Alive->Emplace(W);
		}
		return MakeUnique<FLambdaCommand>(Label,
			[Do, Alive](FRuntimeCommandContext&, FString& Err)
			{
				if (!Do())
				{
					Err = TEXT("the widget tree changed");
					return false;
				}
				return true;
			},
			[Undo, Alive](FRuntimeCommandContext&) { return Undo(); });
	}

	/** Creates a widget in the tree that owns Parent. */
	UWidget* ConstructIn(UWidget* Parent, TSubclassOf<UWidget> Class, const FString& Name)
	{
		UWidgetTree* Tree = Parent->GetTypedOuter<UWidgetTree>();
		if (!Tree)
		{
			if (UUserWidget* User = Cast<UUserWidget>(Parent))
			{
				Tree = User->WidgetTree;
			}
		}
		if (!Tree)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("%s is not part of a widget tree"), *Parent->GetName()));
			return nullptr;
		}
		if (!Name.IsEmpty() && StaticFindObjectFast(UObject::StaticClass(), Tree, FName(*Name)))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("a widget named '%s' already exists in this tree"), *Name), TEXT("Pick another name"));
			return nullptr;
		}
		return Tree->ConstructWidget<UWidget>(Class, Name.IsEmpty() ? NAME_None : FName(*Name));
	}

	FRuntimeWidgetChange Change(UWidget* W, const TCHAR* What)
	{
		FRuntimeWidgetChange C;
		C.Widget = MakeInfo(W);
		C.Change = What;
		return C;
	}

	// ---- world labels ----

	bool ApplyLabelSpec(ARuntimeToolsetsLabelActor* Label, const TSharedPtr<FJsonObject>& Spec)
	{
		FString Text;
		Spec->TryGetStringField(TEXT("text"), Text);
		double Size = 28.0;
		Spec->TryGetNumberField(TEXT("size"), Size);
		FVector C(1.0);
		RuntimeJson::ToVector(Spec->TryGetField(TEXT("color")), C);
		bool bBackground = true;
		Spec->TryGetBoolField(TEXT("background"), bBackground);
		return Label->SetLabel(Text, static_cast<float>(Size), FLinearColor(C.X, C.Y, C.Z), bBackground);
	}

	TSharedRef<FJsonValue> ColorValue(const FLinearColor& C)
	{
		return RuntimeJson::Vec(FVector(C.R, C.G, C.B));
	}

	FRuntimeLabelInfo LabelInfo(const FString& NodeId)
	{
		FRuntimeLabelInfo Out;
		FRuntimeSceneGraph* Graph = RuntimeToolCall::GetGraph();
		const FRuntimeSceneNode* Node = Graph ? Graph->Find(NodeId) : nullptr;
		if (!Node)
		{
			return Out;
		}
		Out.Id = NodeId;
		Out.Name = Node->Name;
		Out.Path = Graph->GetPath(NodeId);
		if (const TSharedPtr<FJsonObject> Spec = RuntimeJson::Parse(Node->Source.Mid(6)))
		{
			Spec->TryGetStringField(TEXT("text"), Out.Text);
		}
		if (Node->Actor.IsValid())
		{
			Out.Location = Node->Actor->GetActorLocation();
			Out.bVisibleOnScreen = RuntimeToolCall::GetWorld()->GetGameInstance() != nullptr;
		}
		return Out;
	}
}

FRuntimeWidgetList URuntimeUiToolset::ListWidgets()
{
	using namespace RtUi;
	FRuntimeWidgetList Out;
	for (UUserWidget* W : ViewportWidgets(RuntimeToolCall::GetWorld()))
	{
		Out.Widgets.Add(MakeInfo(W));
	}
	Out.Total = Out.Widgets.Num();
	return Out;
}

FRuntimeWidgetTree URuntimeUiToolset::ListWidgetTree(const FString& Target, int32 Depth, int32 Limit)
{
	using namespace RtUi;
	FRuntimeWidgetTree Out;
	UWidget* W = ResolveWidget(Target);
	if (!W)
	{
		return Out;
	}
	UWidget* Root = TreeRoot(W);
	Walk(Root, 0, FMath::Max(0, Depth), URuntimeToolsetsSettings::Get().ClampLimit(Limit), Out.Widgets, Out.bTruncated);
	Out.Root = MakeInfo(Root);
	return Out;
}

FRuntimeWidgetDescription URuntimeUiToolset::GetWidgetDescription(const FString& Target, int32 MaxDepth)
{
	using namespace RtUi;
	FRuntimeWidgetDescription Out;
	UWidget* W = ResolveWidget(Target);
	if (!W)
	{
		return Out;
	}
	bool bTruncated = false;
	Walk(TreeRoot(W), 0, MaxDepth, URuntimeToolsetsSettings::Get().ClampLimit(1000), Out.Widgets, bTruncated);
	TArray<FString> Lines;
	for (int32 i = 0; i < Out.Widgets.Num(); ++i)
	{
		const FRuntimeWidgetInfo& I = Out.Widgets[i];
		FString Line = FString::Printf(TEXT("%s[%d] %s %s"), *FString::ChrN(I.Depth * 2, TEXT(' ')), i, *I.Class, *I.Name);
		if (!I.Text.IsEmpty())
		{
			Line += FString::Printf(TEXT(" text=\"%s\""), *I.Text.Replace(TEXT("\n"), TEXT("\\n")));
		}
		if (!I.bVisible)
		{
			Line += TEXT(" (hidden)");
		}
		Lines.Add(Line);
	}
	if (bTruncated)
	{
		Lines.Add(TEXT("... (cut by max_depth or size)"));
	}
	Out.Description = FString::Join(Lines, TEXT("\n"));
	return Out;
}

FRuntimeWidgetList URuntimeUiToolset::FindWidget(const FString& Name, const FString& Class, const FString& Text, int32 Limit)
{
	using namespace RtUi;
	FRuntimeWidgetList Out;
	const FString Pattern = Name.IsEmpty() ? FString(TEXT("*")) : Name;
	const int32 Max = URuntimeToolsetsSettings::Get().ClampLimit(Limit);
	for (UUserWidget* Root : ViewportWidgets(RuntimeToolCall::GetWorld()))
	{
		TArray<UWidget*> All = {Root};
		if (Root->WidgetTree)
		{
			Root->WidgetTree->GetAllWidgets(All);
		}
		for (UWidget* W : All)
		{
			if (!W->GetName().MatchesWildcard(Pattern)
				|| (!Class.IsEmpty() && !W->GetClass()->GetName().MatchesWildcard(Class))
				|| (!Text.IsEmpty() && !WidgetText(W).MatchesWildcard(Text)))
			{
				continue;
			}
			++Out.Total;
			if (Out.Widgets.Num() < Max)
			{
				Out.Widgets.Add(MakeInfo(W));
			}
		}
	}
	return Out;
}

FRuntimeWidgetInfo URuntimeUiToolset::CreateWidget(const FString& Class, int32 ZOrder)
{
	using namespace RtUi;
	APlayerController* PC = RequireWidgetOwner();
	if (!PC)
	{
		return {};
	}
	FString Path = Class;
	if (Path.StartsWith(TEXT("/Game/")) && !Path.EndsWith(TEXT("_C")))
	{
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPaths::GetBaseFilename(Path);
		}
		Path += TEXT("_C");
	}
	UClass* WidgetClass = FSoftClassPath(Path).TryLoadClass<UUserWidget>();
	if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()) || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("widget class '%s' not found or abstract (is it cooked?)"), *Path), TEXT("Use /Game/UI/WBP_Name"));
		return {};
	}
	UUserWidget* W = ::CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!W)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, TEXT("CreateWidget failed"));
		return {};
	}
	if (!RuntimeToolCall::RunCommand(MakeUnique<FViewportWidgetCommand>(W, ZOrder, true)))
	{
		return {};
	}
	return MakeInfo(W);
}

FRuntimeWidgetInfo URuntimeUiToolset::CreateHudText(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Size, const FString& Name, int32 ZOrder)
{
	using namespace RtUi;
	const FVector2D Pos = Passed(TEXT("position")) ? Position : FVector2D(0.5, 0.1);
	if (Pos.X < 0.0 || Pos.X > 1.0 || Pos.Y < 0.0 || Pos.Y > 1.0)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("position [%g, %g] is outside 0-1 screen space"), Pos.X, Pos.Y));
		return {};
	}
	if (Size < 4.f || Size > 400.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size %g is outside 4-400"), Size));
		return {};
	}
	APlayerController* PC = RequireWidgetOwner();
	if (!PC)
	{
		return {};
	}
	URuntimeToolsetsHudWidget* W = ::CreateWidget<URuntimeToolsetsHudWidget>(PC, URuntimeToolsetsHudWidget::StaticClass(), Name.IsEmpty() ? NAME_None : FName(*Name));
	if (!W || !W->WidgetTree)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::Internal, TEXT("could not create the HUD widget"));
		return {};
	}
	UCanvasPanel* Canvas = W->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	UTextBlock* TextBlock = W->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text"));
	W->WidgetTree->RootWidget = Canvas;
	TextBlock->SetText(FText::FromString(Text));
	TextBlock->SetFontSize(Size);
	if (Passed(TEXT("color")))
	{
		TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(Color.R, Color.G, Color.B)));
	}
	// Position is normalised screen space: [0,0] top-left, [1,1] bottom-right; the text is centred on it.
	UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(TextBlock);
	CanvasSlot->SetAnchors(FAnchors(Pos.X, Pos.Y));
	CanvasSlot->SetAlignment(FVector2D(0.5, 0.5));
	CanvasSlot->SetPosition(FVector2D::ZeroVector);
	CanvasSlot->SetAutoSize(true);
	if (!RuntimeToolCall::RunCommand(MakeUnique<FViewportWidgetCommand>(W, ZOrder, true)))
	{
		return {};
	}
	return MakeInfo(W);
}

FRuntimeWidgetInfo URuntimeUiToolset::AddWidget(const FString& Parent, TSubclassOf<UWidget> WidgetClass, const FString& Name, const FString& Text, int32 ChildIndex)
{
	using namespace RtUi;
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract) || WidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("'%s' is not a concrete native widget class"), WidgetClass ? *WidgetClass->GetName() : TEXT("(none)")),
			TEXT("e.g. TextBlock, Button, Image, Border, VerticalBox, HorizontalBox, CanvasPanel, Overlay, Spacer; project widgets: ui.create_widget"));
		return {};
	}
	UPanelWidget* Panel = ResolveWidgetAs<UPanelWidget>(Parent, TEXT("parent"));
	if (!Panel || !CheckPanelAccepts(Panel))
	{
		return {};
	}
	UWidget* W = ConstructIn(Panel, WidgetClass, Name);
	if (!W)
	{
		return {};
	}
	if (!Text.IsEmpty())
	{
		if (!HoldsText(W))
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s does not hold text"), *WidgetClass->GetName()), TEXT("Add a TextBlock inside it instead"));
			return {};
		}
		SetWidgetText(W, FText::FromString(Text));
	}
	TWeakObjectPtr<UPanelWidget> WeakPanel = Panel;
	TWeakObjectPtr<UWidget> WeakW = W;
	if (!RuntimeToolCall::RunCommand(WidgetCommand(FString::Printf(TEXT("add widget %s"), *W->GetName()), {W},
		[WeakPanel, WeakW, ChildIndex]() { return WeakPanel.IsValid() && WeakW.IsValid() && InsertAt(WeakPanel.Get(), WeakW.Get(), ChildIndex, nullptr); },
		[WeakW]() { if (WeakW.IsValid()) { WeakW->RemoveFromParent(); } return WeakW.IsValid(); })))
	{
		return {};
	}
	return MakeInfo(W);
}

FRuntimeWidgetInfo URuntimeUiToolset::MoveWidget(const FString& Widget, const FString& NewParent, int32 ChildIndex)
{
	using namespace RtUi;
	UWidget* W = ResolveWidget(Widget, TEXT("widget"));
	if (!W)
	{
		return {};
	}
	UPanelWidget* Panel = ResolveWidgetAs<UPanelWidget>(NewParent, TEXT("new_parent"));
	if (!Panel)
	{
		return {};
	}
	if (!W->GetParent())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is a root widget and cannot move"), *W->GetName()));
		return {};
	}
	for (UWidget* P = Panel; P; P = P->GetParent())
	{
		if (P == W)
		{
			RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("new_parent is inside the widget"));
			return {};
		}
	}
	if (Panel != W->GetParent() && !CheckPanelAccepts(Panel))
	{
		return {};
	}
	const TSharedRef<FWidgetPlace> Old = MakeShared<FWidgetPlace>(FWidgetPlace::Of(W));
	const bool bSamePanel = Old->Parent.Get() == Panel;
	TWeakObjectPtr<UPanelWidget> WeakPanel = Panel;
	TWeakObjectPtr<UWidget> WeakW = W;
	if (!RuntimeToolCall::RunCommand(WidgetCommand(FString::Printf(TEXT("move widget %s"), *W->GetName()), {W},
		[WeakPanel, WeakW, ChildIndex, Old, bSamePanel]()
		{
			if (!WeakPanel.IsValid() || !WeakW.IsValid())
			{
				return false;
			}
			WeakW->RemoveFromParent();
			return InsertAt(WeakPanel.Get(), WeakW.Get(), ChildIndex, bSamePanel ? Old->Slot.Get() : nullptr);
		},
		[WeakW, Old]() { return WeakW.IsValid() && Old->Restore(WeakW.Get()); })))
	{
		return {};
	}
	return MakeInfo(W);
}

FRuntimeWidgetInfo URuntimeUiToolset::WrapWidget(const FString& Widget, TSubclassOf<UPanelWidget> WrapperClass, const FString& Name)
{
	using namespace RtUi;
	UWidget* W = ResolveWidget(Widget, TEXT("widget"));
	if (!W)
	{
		return {};
	}
	if (!WrapperClass || WrapperClass->HasAnyClassFlags(CLASS_Abstract))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("wrapper_class must be a concrete panel class"), TEXT("e.g. Border, SizeBox, VerticalBox, Overlay"));
		return {};
	}
	if (!W->GetParent())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s has no parent panel to wrap it in"), *W->GetName()),
			TEXT("Root widgets cannot be wrapped at runtime; wrap its children instead"));
		return {};
	}
	UPanelWidget* Wrapper = Cast<UPanelWidget>(ConstructIn(W->GetParent(), WrapperClass, Name));
	if (!Wrapper)
	{
		return {};
	}
	const TSharedRef<FWidgetPlace> Old = MakeShared<FWidgetPlace>(FWidgetPlace::Of(W));
	TWeakObjectPtr<UWidget> WeakW = W;
	TWeakObjectPtr<UPanelWidget> WeakWrapper = Wrapper;
	if (!RuntimeToolCall::RunCommand(WidgetCommand(FString::Printf(TEXT("wrap widget %s"), *W->GetName()), {W, Wrapper},
		[WeakW, WeakWrapper, Old]()
		{
			if (!WeakW.IsValid() || !WeakWrapper.IsValid() || !Old->Parent.IsValid())
			{
				return false;
			}
			WeakW->RemoveFromParent();
			InsertAt(Old->Parent.Get(), WeakWrapper.Get(), Old->Index, Old->Slot.Get());
			return WeakWrapper->AddChild(WeakW.Get()) != nullptr;
		},
		[WeakW, WeakWrapper, Old]()
		{
			if (!WeakW.IsValid() || !WeakWrapper.IsValid())
			{
				return false;
			}
			WeakW->RemoveFromParent();
			WeakWrapper->RemoveFromParent();
			return Old->Restore(WeakW.Get());
		})))
	{
		return {};
	}
	return MakeInfo(Wrapper);
}

FRuntimeWidgetChange URuntimeUiToolset::RemoveWidget(const FString& Target)
{
	using namespace RtUi;
	UWidget* W = ResolveWidget(Target);
	if (!W)
	{
		return {};
	}
	const FRuntimeWidgetChange Before = Change(W, TEXT("removed"));
	if (UUserWidget* User = Cast<UUserWidget>(W); User && !User->GetParent())
	{
		if (!User->IsInViewport())
		{
			RuntimeToolCall::Fail(ERuntimeToolError::WrongState, TEXT("widget is not on screen"));
			return {};
		}
		if (!RuntimeToolCall::RunCommand(MakeUnique<FViewportWidgetCommand>(User, 0, false)))
		{
			return {};
		}
		return Before;
	}
	if (!W->GetParent())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is the root of its tree"), *W->GetName()), TEXT("Remove the user widget that owns it"));
		return {};
	}
	const TSharedRef<FWidgetPlace> Old = MakeShared<FWidgetPlace>(FWidgetPlace::Of(W));
	TWeakObjectPtr<UWidget> WeakW = W;
	if (!RuntimeToolCall::RunCommand(WidgetCommand(FString::Printf(TEXT("remove widget %s"), *W->GetName()), {W},
		[WeakW]() { if (WeakW.IsValid()) { WeakW->RemoveFromParent(); } return WeakW.IsValid(); },
		[WeakW, Old]() { return WeakW.IsValid() && Old->Restore(WeakW.Get()); })))
	{
		return {};
	}
	return Before;
}

FRuntimeWidgetChange URuntimeUiToolset::SetText(const FString& Target, const FString& Text)
{
	using namespace RtUi;
	UWidget* W = ResolveWidget(Target);
	if (!W)
	{
		return {};
	}
	if (UUserWidget* User = Cast<UUserWidget>(W); User && User->WidgetTree)
	{
		// Convenience: a HUD widget's (first) text.
		UWidget* Found = nullptr;
		User->WidgetTree->ForEachWidget([&Found](UWidget* Child) { if (!Found && HoldsText(Child)) { Found = Child; } });
		W = Found ? Found : W;
	}
	if (!HoldsText(W))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is a %s, which does not hold text"), *W->GetName(), *W->GetClass()->GetName()),
			TEXT("Target a TextBlock, RichTextBlock or EditableText (ui.get_widget_description lists them)"));
		return {};
	}
	const FString Old = WidgetText(W);
	TWeakObjectPtr<UWidget> Weak = W;
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("widget"), W->GetName());
	Delta->SetStringField(TEXT("text"), Text);
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("set text"),
		[Weak, Text](FRuntimeCommandContext&, FString&) { return Weak.IsValid() && SetWidgetText(Weak.Get(), FText::FromString(Text)); },
		[Weak, Old](FRuntimeCommandContext&) { return Weak.IsValid() && SetWidgetText(Weak.Get(), FText::FromString(Old)); }, Delta)))
	{
		return {};
	}
	return Change(W, TEXT("text"));
}

FRuntimeWidgetChange URuntimeUiToolset::SetWidgetVisibility(const FString& Target, ERuntimeWidgetVisibility Visibility)
{
	using namespace RtUi;
	UWidget* W = ResolveWidget(Target);
	if (!W)
	{
		return {};
	}
	ESlateVisibility New = ESlateVisibility::Visible;
	switch (Visibility)
	{
	case ERuntimeWidgetVisibility::Visible: New = ESlateVisibility::Visible; break;
	case ERuntimeWidgetVisibility::Hidden: New = ESlateVisibility::Hidden; break;
	case ERuntimeWidgetVisibility::Collapsed: New = ESlateVisibility::Collapsed; break;
	case ERuntimeWidgetVisibility::HitTestInvisible: New = ESlateVisibility::HitTestInvisible; break;
	case ERuntimeWidgetVisibility::SelfHitTestInvisible: New = ESlateVisibility::SelfHitTestInvisible; break;
	}
	const ESlateVisibility Old = W->GetVisibility();
	TWeakObjectPtr<UWidget> Weak = W;
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("widget visibility"),
		[Weak, New](FRuntimeCommandContext&, FString&) { if (Weak.IsValid()) { Weak->SetVisibility(New); } return Weak.IsValid(); },
		[Weak, Old](FRuntimeCommandContext&) { if (Weak.IsValid()) { Weak->SetVisibility(Old); } return Weak.IsValid(); })))
	{
		return {};
	}
	return Change(W, TEXT("visibility"));
}

FRuntimeWidgetChange URuntimeUiToolset::SetBrush(const FString& Target, const FLinearColor& Tint, const FString& Texture)
{
	using namespace RtUi;
	UImage* Image = ResolveWidgetAs<UImage>(Target);
	if (!Image)
	{
		return {};
	}
	UTexture2D* NewTexture = nullptr;
	if (!Texture.IsEmpty())
	{
		NewTexture = LoadAllowed<UTexture2D>(Texture, TEXT("texture"), TEXT("/Game/UI/T_Icon.T_Icon"));
		if (!NewTexture)
		{
			return {};
		}
	}
	FLinearColor Color = Image->GetColorAndOpacity();
	if (Passed(TEXT("tint")))
	{
		Color = FLinearColor(Tint.R, Tint.G, Tint.B, Color.A);
	}
	const FSlateBrush OldBrush = Image->GetBrush();
	const FLinearColor OldColor = Image->GetColorAndOpacity();
	TWeakObjectPtr<UImage> Weak = Image;
	TSharedPtr<TStrongObjectPtr<UTexture2D>> KeepAlive = MakeShared<TStrongObjectPtr<UTexture2D>>(NewTexture);
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("set brush"),
		[Weak, KeepAlive, Color](FRuntimeCommandContext&, FString&)
		{
			if (!Weak.IsValid())
			{
				return false;
			}
			if (KeepAlive->Get())
			{
				Weak->SetBrushFromTexture(KeepAlive->Get(), false);
			}
			Weak->SetColorAndOpacity(Color);
			return true;
		},
		[Weak, OldBrush, OldColor](FRuntimeCommandContext&)
		{
			if (!Weak.IsValid())
			{
				return false;
			}
			Weak->SetBrush(OldBrush);
			Weak->SetColorAndOpacity(OldColor);
			return true;
		})))
	{
		return {};
	}
	return Change(Image, TEXT("brush"));
}

FRuntimeActivityInfo URuntimeUiToolset::ShowActivity(const bool& bEnabled, const FString& Status)
{
	using namespace RtUi;
	FRuntimeToolsetsActivity& Activity = FRuntimeToolsetsActivity::Get();
	if (Passed(TEXT("enabled")))
	{
		Activity.SetPanelEnabled(bEnabled);
	}
	if (Passed(TEXT("status")))
	{
		Activity.SetStatus(Status);
	}
	FRuntimeActivityInfo Out;
	Out.bEnabled = Activity.IsPanelEnabled();
	return Out;
}

AActor* URuntimeUiToolset::SpawnLabelFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
{
	const TSharedPtr<FJsonObject> Spec = RuntimeJson::Parse(Source.Mid(6));
	if (!Spec.IsValid())
	{
		OutError = TEXT("bad label: spec");
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARuntimeToolsetsLabelActor* Label = World->SpawnActor<ARuntimeToolsetsLabelActor>(ARuntimeToolsetsLabelActor::StaticClass(), Transform, Params);
	if (!Label)
	{
		OutError = TEXT("could not spawn the label");
		return nullptr;
	}
	RtUi::ApplyLabelSpec(Label, Spec); // no widget without a game instance (isolated worlds); the node still exists
	return Label;
}

FRuntimeLabelInfo URuntimeUiToolset::CreateLabel(const FString& Text, const FVector& Location, const FVector& Offset, const FLinearColor& Color, AActor* Target, float Size,
	bool bBackground, const FString& Name)
{
	using namespace RtUi;
	if (Size < 6.f || Size > 200.f)
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size %g is outside 6-200"), Size));
		return {};
	}
	TSharedRef<FJsonObject> Spec = MakeShared<FJsonObject>();
	Spec->SetStringField(TEXT("text"), Text);
	Spec->SetNumberField(TEXT("size"), Size);
	if (Passed(TEXT("color")))
	{
		Spec->SetField(TEXT("color"), ColorValue(Color));
	}
	Spec->SetBoolField(TEXT("background"), bBackground);

	const FVector Off = Passed(TEXT("offset")) ? Offset : FVector(0, 0, 100);
	FString ParentId;
	FTransform Relative = FTransform::Identity;
	if (Target)
	{
		// Anchor above the target's top centre; as a child node the label follows the target.
		const FBox Box = RuntimeTools::ActorBox(Target);
		const FVector Anchor = FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z) + Off;
		if (const FRuntimeSceneNode* Node = RuntimeToolCall::NodeOf(Target))
		{
			ParentId = Node->Id;
			Relative.SetLocation(Target->GetActorTransform().InverseTransformPosition(Anchor));
			Relative.SetScale3D(FVector(1.0) / Target->GetActorScale3D().ComponentMax(FVector(0.0001)));
		}
		else
		{
			Relative.SetLocation(Anchor);
		}
	}
	else if (Passed(TEXT("location")))
	{
		Relative.SetLocation(Location + Off);
	}
	else
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, TEXT("give a target to label, or a world location"), TEXT("e.g. {\"text\":\"Tower\",\"target\":\"n3\"}"));
		return {};
	}
	const FString Source = TEXT("label:") + RuntimeJson::Write(Spec);
	const FString NodeName = Name.IsEmpty() ? FString(TEXT("label")) : Name;
	auto Spawn = [Source, Relative, NodeName](FRuntimeCommandContext& Ctx, FString& Err) { return URuntimeUiToolset::SpawnLabelFromSource(Ctx.World, Source, Relative, NodeName, Err); };
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("label %s"), *Text), Spawn, NodeName, ParentId,
		ERuntimeNodeKind::Actor, Source, TArray<FName>());
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!RuntimeToolCall::RunCommand(MoveTemp(Cmd)))
	{
		return {};
	}
	return LabelInfo(Raw->GetNodeId());
}

FRuntimeLabelInfo URuntimeUiToolset::SetLabel(AActor* Target, const FString& Text, const float& Size, const FLinearColor& Color, const bool& bBackground)
{
	using namespace RtUi;
	const FRuntimeSceneNode* Node = Target ? RuntimeToolCall::NodeOf(Target) : nullptr;
	if (!Cast<ARuntimeToolsetsLabelActor>(Target) || !Node || !Node->Source.StartsWith(TEXT("label:")))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s is not a label"), *Ref(Target)), TEXT("Create one with ui.create_label"));
		return {};
	}
	if (Passed(TEXT("size")) && (Size < 6.f || Size > 200.f))
	{
		RuntimeToolCall::Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("size %g is outside 6-200"), Size));
		return {};
	}
	TSharedPtr<FJsonObject> Parsed = RuntimeJson::Parse(Node->Source.Mid(6));
	TSharedRef<FJsonObject> Spec = Parsed.IsValid() ? Parsed.ToSharedRef() : MakeShared<FJsonObject>();
	if (Passed(TEXT("text"))) { Spec->SetStringField(TEXT("text"), Text); }
	if (Passed(TEXT("size"))) { Spec->SetNumberField(TEXT("size"), Size); }
	if (Passed(TEXT("color"))) { Spec->SetField(TEXT("color"), ColorValue(Color)); }
	if (Passed(TEXT("background"))) { Spec->SetBoolField(TEXT("background"), bBackground); }
	const FString OldSource = Node->Source;
	const FString NewSource = TEXT("label:") + RuntimeJson::Write(Spec);
	const FString Id = Node->Id;
	auto Apply = [Id](FRuntimeCommandContext& Ctx, const FString& Source)
	{
		FRuntimeSceneNode* N = Ctx.Graph ? Ctx.Graph->Find(Id) : nullptr;
		ARuntimeToolsetsLabelActor* L = N ? Cast<ARuntimeToolsetsLabelActor>(N->Actor.Get()) : nullptr;
		if (!L)
		{
			return false;
		}
		N->Source = Source;
		ApplyLabelSpec(L, RuntimeJson::Parse(Source.Mid(6)));
		return true;
	};
	TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
	Delta->SetStringField(TEXT("label"), Id);
	if (!RuntimeToolCall::RunCommand(MakeUnique<FLambdaCommand>(TEXT("set label"),
		[Apply, NewSource](FRuntimeCommandContext& Ctx, FString&) { return Apply(Ctx, NewSource); },
		[Apply, OldSource](FRuntimeCommandContext& Ctx) { return Apply(Ctx, OldSource); }, Delta)))
	{
		return {};
	}
	return LabelInfo(Id);
}

FRuntimeWidgetInfo URuntimeUiToolset::ClickButton(const FString& Target)
{
	using namespace RtUi;
	UButton* Button = ResolveWidgetAs<UButton>(Target);
	if (!Button)
	{
		return {};
	}
	if (!Button->GetIsEnabled())
	{
		RuntimeToolCall::Fail(ERuntimeToolError::WrongState, FString::Printf(TEXT("button %s is disabled"), *Button->GetName()));
		return {};
	}
	Inv().RecordIrreversible(FString::Printf(TEXT("click %s"), *Button->GetName()));
	Button->OnPressed.Broadcast();
	Button->OnReleased.Broadcast();
	Button->OnClicked.Broadcast();
	return MakeInfo(Button);
}
