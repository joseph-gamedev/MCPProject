#include "UiToolset.h"
#include "RuntimeToolHelpers.h"
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
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

using namespace RuntimeTools;

namespace
{
	/** Top-level user widgets of this world that are on screen. */
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

	bool SetWidgetText(UWidget* W, const FText& Text)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W)) { T->SetText(Text); return true; }
		if (URichTextBlock* R = Cast<URichTextBlock>(W)) { R->SetText(Text); return true; }
		if (UEditableTextBox* E = Cast<UEditableTextBox>(W)) { E->SetText(Text); return true; }
		if (UEditableText* E2 = Cast<UEditableText>(W)) { E2->SetText(Text); return true; }
		return false;
	}

	TSharedRef<FJsonObject> WidgetJson(const UWidget* W)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("name"), W->GetName());
		J->SetStringField(TEXT("class"), W->GetClass()->GetName());
		J->SetStringField(TEXT("path"), W->GetPathName());
		if (!W->IsVisible())
		{
			J->SetBoolField(TEXT("visible"), false);
		}
		const FString Text = WidgetText(W);
		if (!Text.IsEmpty())
		{
			J->SetStringField(TEXT("text"), Text.Len() > 200 ? Text.Left(200) + TEXT("...") : Text);
		}
		return J;
	}

	/** Resolves "widget:Name" (searching viewport widgets and their trees) or any object path to a UWidget. */
	UWidget* ResolveWidget(FToolInvocation& Inv, const TCHAR* Arg = TEXT("target"))
	{
		const FString Target = Inv.GetString(Arg);
		if (Target.StartsWith(TEXT("widget:")))
		{
			const FString Name = Target.Mid(7);
			TArray<UWidget*> Matches;
			for (UUserWidget* Root : ViewportWidgets(Inv.GetWorld()))
			{
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
			Inv.Fail(Matches.Num() ? ERuntimeToolError::AmbiguousTarget : ERuntimeToolError::NotFound,
				FString::Printf(TEXT("%d widgets named '%s' on screen"), Matches.Num(), *Name), TEXT("Use ui.find_widget and pass the path"));
			return nullptr;
		}
		FResolvedTarget R;
		if (!Inv.ResolveTarget(Arg, R))
		{
			return nullptr;
		}
		UWidget* W = Cast<UWidget>(R.Object);
		if (!W)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a widget"));
		}
		return W;
	}

	APlayerController* RequireWidgetOwner(FToolInvocation& Inv)
	{
		APlayerController* PC = Inv.GetWorld()->GetFirstPlayerController();
		if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer())
		{
			Inv.Fail(ERuntimeToolError::WrongState, TEXT("no local player to own widgets"));
			return nullptr;
		}
		return PC;
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

	void SucceedWithWidget(FToolInvocation& Inv, UUserWidget* W)
	{
		TSharedRef<FJsonObject> Out = WidgetJson(W);
		Out->SetStringField(TEXT("target"), FString::Printf(TEXT("widget:%s"), *W->GetName()));
		Inv.Succeed(Out);
	}
}

static void ListWidgets(FToolInvocation& Inv)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	for (UUserWidget* W : ViewportWidgets(Inv.GetWorld()))
	{
		Items.Add(MakeShared<FJsonValueObject>(WidgetJson(W)));
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetArrayField(TEXT("widgets"), Items);
	Inv.Succeed(Out);
}

static TSharedRef<FJsonObject> TreeJson(UWidget* W, int32 Depth, int32& Budget)
{
	--Budget;
	TSharedRef<FJsonObject> J = WidgetJson(W);
	J->RemoveField(TEXT("path"));
	UPanelWidget* Panel = Cast<UPanelWidget>(W);
	if (!Panel || Panel->GetChildrenCount() == 0)
	{
		return J;
	}
	if (Depth <= 0 || Budget <= 0)
	{
		J->SetNumberField(TEXT("child_count"), Panel->GetChildrenCount());
		return J;
	}
	TArray<TSharedPtr<FJsonValue>> Children;
	for (int32 i = 0; i < Panel->GetChildrenCount() && Budget > 0; ++i)
	{
		if (UWidget* Child = Panel->GetChildAt(i))
		{
			Children.Add(MakeShared<FJsonValueObject>(TreeJson(Child, Depth - 1, Budget)));
		}
	}
	J->SetArrayField(TEXT("children"), Children);
	return J;
}

static void ListWidgetTree(FToolInvocation& Inv)
{
	UWidget* W = ResolveWidget(Inv);
	if (!W)
	{
		return;
	}
	UWidget* Root = W;
	if (UUserWidget* User = Cast<UUserWidget>(W); User && User->WidgetTree && User->WidgetTree->RootWidget)
	{
		Root = User->WidgetTree->RootWidget;
	}
	int32 Budget = Inv.GetLimit();
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetObjectField(TEXT("root"), TreeJson(Root, static_cast<int32>(Inv.GetInt(TEXT("depth"), 4)), Budget));
	Inv.Succeed(Out);
}

static void FindWidget(FToolInvocation& Inv)
{
	const FString Pattern = Inv.GetString(TEXT("name"), TEXT("*"));
	const FString ClassPattern = Inv.GetString(TEXT("class"));
	const FString TextPattern = Inv.GetString(TEXT("text"));
	const int32 Limit = Inv.GetLimit();
	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Total = 0;
	for (UUserWidget* Root : ViewportWidgets(Inv.GetWorld()))
	{
		TArray<UWidget*> All = {Root};
		if (Root->WidgetTree)
		{
			Root->WidgetTree->GetAllWidgets(All);
		}
		for (UWidget* W : All)
		{
			if (!W->GetName().MatchesWildcard(Pattern))
			{
				continue;
			}
			if (!ClassPattern.IsEmpty() && !W->GetClass()->GetName().MatchesWildcard(ClassPattern))
			{
				continue;
			}
			if (!TextPattern.IsEmpty() && !WidgetText(W).MatchesWildcard(TextPattern))
			{
				continue;
			}
			++Total;
			if (Items.Num() < Limit)
			{
				Items.Add(MakeShared<FJsonValueObject>(WidgetJson(W)));
			}
		}
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetNumberField(TEXT("total"), Total);
	Out->SetArrayField(TEXT("widgets"), Items);
	Inv.Succeed(Out);
}

static void CreateProjectWidget(FToolInvocation& Inv)
{
	APlayerController* PC = RequireWidgetOwner(Inv);
	if (!PC)
	{
		return;
	}
	FString Path = Inv.GetString(TEXT("class"));
	if (Path.StartsWith(TEXT("/Game/")) && !Path.EndsWith(TEXT("_C")))
	{
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPaths::GetBaseFilename(Path);
		}
		Path += TEXT("_C");
	}
	UClass* Class = FSoftClassPath(Path).TryLoadClass<UUserWidget>();
	if (!Class || !Class->IsChildOf(UUserWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
	{
		Inv.Fail(ERuntimeToolError::NotFound, FString::Printf(TEXT("widget class '%s' not found or abstract (is it cooked?)"), *Path), TEXT("Use /Game/UI/WBP_Name"));
		return;
	}
	UUserWidget* W = ::CreateWidget<UUserWidget>(PC, Class);
	if (!W)
	{
		Inv.Fail(ERuntimeToolError::Internal, TEXT("CreateWidget failed"));
		return;
	}
	if (Inv.RunCommand(MakeUnique<FViewportWidgetCommand>(W, static_cast<int32>(Inv.GetInt(TEXT("z_order"), 0)), true)))
	{
		SucceedWithWidget(Inv, W);
	}
}

static void CreateHudText(FToolInvocation& Inv)
{
	APlayerController* PC = RequireWidgetOwner(Inv);
	if (!PC)
	{
		return;
	}
	const FString Name = Inv.GetString(TEXT("name"));
	URuntimeToolsetsHudWidget* W = ::CreateWidget<URuntimeToolsetsHudWidget>(PC, URuntimeToolsetsHudWidget::StaticClass(), Name.IsEmpty() ? NAME_None : FName(*Name));
	if (!W || !W->WidgetTree)
	{
		Inv.Fail(ERuntimeToolError::Internal, TEXT("could not create the HUD widget"));
		return;
	}
	UCanvasPanel* Canvas = W->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	UTextBlock* Text = W->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text"));
	W->WidgetTree->RootWidget = Canvas;
	Text->SetText(FText::FromString(Inv.GetString(TEXT("text"))));
	Text->SetFontSize(static_cast<float>(Inv.GetNumber(TEXT("size"), 24.0)));
	FVector C;
	if (Inv.GetVector(TEXT("color"), C))
	{
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(C.X, C.Y, C.Z)));
	}
	// Position is normalised screen space: [0,0] top-left, [1,1] bottom-right; the text is centred on it.
	FVector Pos(0.5, 0.1, 0.0);
	if (const TSharedPtr<FJsonValue> P = Inv.GetValue(TEXT("position")); P.IsValid() && P->Type == EJson::Array && P->AsArray().Num() == 2)
	{
		Pos = FVector(P->AsArray()[0]->AsNumber(), P->AsArray()[1]->AsNumber(), 0.0);
	}
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Text);
	Slot->SetAnchors(FAnchors(Pos.X, Pos.Y));
	Slot->SetAlignment(FVector2D(0.5, 0.5));
	Slot->SetPosition(FVector2D::ZeroVector);
	Slot->SetAutoSize(true);
	if (Inv.RunCommand(MakeUnique<FViewportWidgetCommand>(W, static_cast<int32>(Inv.GetInt(TEXT("z_order"), 10)), true)))
	{
		SucceedWithWidget(Inv, W);
	}
}

static void RemoveWidget(FToolInvocation& Inv)
{
	UUserWidget* W = Cast<UUserWidget>(ResolveWidget(Inv));
	if (!W)
	{
		if (!Inv.IsComplete())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a top-level user widget"));
		}
		return;
	}
	if (!W->IsInViewport())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("widget is not on screen"));
		return;
	}
	TSharedPtr<FJsonObject> Delta;
	if (Inv.RunCommand(MakeUnique<FViewportWidgetCommand>(W, 0, false), &Delta))
	{
		Inv.Succeed(Delta);
	}
}

static void SetText(FToolInvocation& Inv)
{
	UWidget* W = ResolveWidget(Inv);
	if (!W)
	{
		return;
	}
	if (W->IsA<UUserWidget>())
	{
		// Convenience: a HUD widget's single text block.
		if (UUserWidget* User = Cast<UUserWidget>(W); User->WidgetTree)
		{
			UWidget* Found = nullptr;
			User->WidgetTree->ForEachWidget([&Found](UWidget* Child) { if (!Found && !WidgetText(Child).IsEmpty()) { Found = Child; } });
			W = Found ? Found : W;
		}
	}
	const FString Old = WidgetText(W);
	const FString New = Inv.GetString(TEXT("text"));
	if (!SetWidgetText(W, FText::FromString(Old)))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, FString::Printf(TEXT("%s does not hold text"), *W->GetClass()->GetName()));
		return;
	}
	TWeakObjectPtr<UWidget> Weak = W;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("widget"), W->GetName());
	Delta->SetStringField(TEXT("text"), New);
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("set text"),
		[Weak, New](FRuntimeCommandContext&, FString& Error) { return Weak.IsValid() && SetWidgetText(Weak.Get(), FText::FromString(New)); },
		[Weak, Old](FRuntimeCommandContext&) { return Weak.IsValid() && SetWidgetText(Weak.Get(), FText::FromString(Old)); }, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetWidgetVisibility(FToolInvocation& Inv)
{
	UWidget* W = ResolveWidget(Inv);
	if (!W)
	{
		return;
	}
	const FString Mode = Inv.GetString(TEXT("visibility"));
	const ESlateVisibility New = Mode == TEXT("hidden") ? ESlateVisibility::Hidden
		: Mode == TEXT("collapsed") ? ESlateVisibility::Collapsed
		: Mode == TEXT("hit_test_invisible") ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible;
	const ESlateVisibility Old = W->GetVisibility();
	TWeakObjectPtr<UWidget> Weak = W;
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("widget"), W->GetName());
	Delta->SetStringField(TEXT("visibility"), Mode);
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("widget visibility"),
		[Weak, New](FRuntimeCommandContext&, FString&) { if (Weak.IsValid()) { Weak->SetVisibility(New); } return Weak.IsValid(); },
		[Weak, Old](FRuntimeCommandContext&) { if (Weak.IsValid()) { Weak->SetVisibility(Old); } return Weak.IsValid(); }, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void SetBrush(FToolInvocation& Inv)
{
	UImage* Image = Cast<UImage>(ResolveWidget(Inv));
	if (!Image)
	{
		if (!Inv.IsComplete())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not an Image widget"));
		}
		return;
	}
	UTexture2D* Texture = nullptr;
	if (Inv.Has(TEXT("texture")))
	{
		Texture = LoadObject<UTexture2D>(nullptr, *Inv.GetString(TEXT("texture")));
		if (!Texture)
		{
			Inv.Fail(ERuntimeToolError::NotFound, TEXT("texture not found (is it cooked?)"));
			return;
		}
	}
	FLinearColor Color = Image->GetColorAndOpacity();
	FVector C;
	const bool bColor = Inv.GetVector(TEXT("tint"), C);
	if (bColor)
	{
		Color = FLinearColor(C.X, C.Y, C.Z, Color.A);
	}
	const FSlateBrush OldBrush = Image->GetBrush();
	const FLinearColor OldColor = Image->GetColorAndOpacity();
	TWeakObjectPtr<UImage> Weak = Image;
	TSharedPtr<TStrongObjectPtr<UTexture2D>> KeepAlive = MakeShared<TStrongObjectPtr<UTexture2D>>(Texture);
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("widget"), Image->GetName());
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("set brush"),
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
		}, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

static void ClickButton(FToolInvocation& Inv)
{
	UButton* Button = Cast<UButton>(ResolveWidget(Inv));
	if (!Button)
	{
		if (!Inv.IsComplete())
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a Button widget"));
		}
		return;
	}
	if (!Button->GetIsEnabled())
	{
		Inv.Fail(ERuntimeToolError::WrongState, TEXT("button is disabled"));
		return;
	}
	Inv.RecordIrreversible(FString::Printf(TEXT("click %s"), *Button->GetName()));
	Button->OnPressed.Broadcast();
	Button->OnReleased.Broadcast();
	Button->OnClicked.Broadcast();
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("clicked"), Button->GetName());
	Out->SetBoolField(TEXT("undoable"), false);
	Inv.Succeed(Out);
}

// ---- world labels ---------------------------------------------------------------------------------------------------

static bool ApplyLabelSpec(ARuntimeToolsetsLabelActor* Label, const TSharedPtr<FJsonObject>& Spec)
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

AActor* FUiToolset::SpawnLabelFromSource(UWorld* World, const FString& Source, const FTransform& Transform, const FString& Name, FString& OutError)
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
	ApplyLabelSpec(Label, Spec); // no widget without a game instance (isolated worlds); the node still exists
	return Label;
}

static void CreateLabel(FToolInvocation& Inv)
{
	TSharedRef<FJsonObject> Spec = Obj();
	Spec->SetStringField(TEXT("text"), Inv.GetString(TEXT("text")));
	Spec->SetNumberField(TEXT("size"), Inv.GetNumber(TEXT("size"), 28.0));
	if (Inv.Has(TEXT("color")))
	{
		Spec->SetField(TEXT("color"), Inv.GetValue(TEXT("color")));
	}
	Spec->SetBoolField(TEXT("background"), Inv.GetBool(TEXT("background"), true));

	FVector Offset(0, 0, 100);
	Inv.GetVector(TEXT("offset"), Offset);
	FString ParentId;
	FTransform Relative = FTransform::Identity;
	if (!Inv.GetString(TEXT("target")).IsEmpty())
	{
		// Anchor above the target's top centre; as a child node the label follows the target.
		FResolvedTarget Target;
		if (!Inv.ResolveTarget(TEXT("target"), Target))
		{
			return;
		}
		if (!Target.Actor)
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target must be an actor or node"));
			return;
		}
		const FBox Box = ActorBox(Target.Actor);
		const FVector Top(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z);
		const FVector Anchor = Top + Offset;
		if (Target.Node)
		{
			ParentId = Target.Node->Id;
			Relative.SetLocation(Target.Actor->GetActorTransform().InverseTransformPosition(Anchor));
			Relative.SetScale3D(FVector(1.0) / Target.Actor->GetActorScale3D().ComponentMax(FVector(0.0001)));
		}
		else
		{
			Relative.SetLocation(Anchor);
		}
	}
	else
	{
		FVector Location;
		if (!Inv.GetVector(TEXT("location"), Location))
		{
			Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("give a target to label, or a world location"));
			return;
		}
		Relative.SetLocation(Location + Offset);
	}
	const FString Source = TEXT("label:") + RuntimeJson::Write(Spec);
	const FString Name = Inv.GetString(TEXT("name"), TEXT("label"));
	auto Spawn = [Source, Relative, Name](FRuntimeCommandContext& Ctx, FString& Err) { return FUiToolset::SpawnLabelFromSource(Ctx.World, Source, Relative, Name, Err); };
	TUniquePtr<FCreateNodeCommand> Cmd = MakeUnique<FCreateNodeCommand>(FString::Printf(TEXT("label %s"), *Inv.GetString(TEXT("text"))), Spawn, Name, ParentId,
		ERuntimeNodeKind::Actor, Source, TArray<FName>());
	FCreateNodeCommand* Raw = Cmd.Get();
	if (!Inv.RunCommand(MoveTemp(Cmd)))
	{
		return;
	}
	TSharedRef<FJsonObject> Out = Obj();
	Out->SetStringField(TEXT("id"), Raw->GetNodeId());
	Out->SetStringField(TEXT("path"), Inv.GetGraph()->GetPath(Raw->GetNodeId()));
	if (const FRuntimeSceneNode* Node = Inv.GetGraph()->Find(Raw->GetNodeId()); Node && Node->Actor.IsValid())
	{
		Out->SetField(TEXT("location"), RuntimeJson::Vec(Node->Actor->GetActorLocation()));
		Out->SetBoolField(TEXT("visible_on_screen"), Inv.GetWorld()->GetGameInstance() != nullptr);
	}
	Inv.Succeed(Out);
}

static void SetLabel(FToolInvocation& Inv)
{
	FResolvedTarget Target;
	if (!Inv.ResolveTarget(TEXT("target"), Target))
	{
		return;
	}
	ARuntimeToolsetsLabelActor* Label = Cast<ARuntimeToolsetsLabelActor>(Target.Actor);
	if (!Label || !Target.Node || !Target.Node->Source.StartsWith(TEXT("label:")))
	{
		Inv.Fail(ERuntimeToolError::InvalidArguments, TEXT("target is not a label"), TEXT("Create one with ui.create_label"));
		return;
	}
	TSharedRef<FJsonObject> Spec = RuntimeJson::Parse(Target.Node->Source.Mid(6)).ToSharedRef();
	for (const TCHAR* Field : {TEXT("text"), TEXT("size"), TEXT("color"), TEXT("background")})
	{
		if (Inv.Has(Field))
		{
			Spec->SetField(Field, Inv.GetValue(Field));
		}
	}
	const FString OldSource = Target.Node->Source;
	const FString NewSource = TEXT("label:") + RuntimeJson::Write(Spec);
	const FString Id = Target.Node->Id;
	auto Apply = [Id](FRuntimeCommandContext& Ctx, const FString& Source)
	{
		FRuntimeSceneNode* Node = Ctx.Graph ? Ctx.Graph->Find(Id) : nullptr;
		ARuntimeToolsetsLabelActor* L = Node ? Cast<ARuntimeToolsetsLabelActor>(Node->Actor.Get()) : nullptr;
		if (!L)
		{
			return false;
		}
		Node->Source = Source;
		ApplyLabelSpec(L, RuntimeJson::Parse(Source.Mid(6)));
		return true;
	};
	TSharedRef<FJsonObject> Delta = Obj();
	Delta->SetStringField(TEXT("label"), Id);
	Delta->SetStringField(TEXT("text"), Spec->GetStringField(TEXT("text")));
	if (Inv.RunCommand(MakeUnique<FLambdaCommand>(TEXT("set label"),
		[Apply, NewSource](FRuntimeCommandContext& Ctx, FString& Error) { return Apply(Ctx, NewSource); },
		[Apply, OldSource](FRuntimeCommandContext& Ctx) { return Apply(Ctx, OldSource); }, Delta)))
	{
		Inv.Succeed(Delta);
	}
}

void FUiToolset::RegisterTools(FRuntimeToolsetBuilder& B)
{
	const auto Widget = []() { return FToolParam::String(TEXT("widget:Name or a widget path from find_widget"), 1024).Required(); };

	B.Tool(TEXT("list_widgets"), TEXT("Top-level widgets currently on screen."))
		.RequiresWorld()
		.Handle(&ListWidgets);

	B.Tool(TEXT("list_widget_tree"), TEXT("Child hierarchy of a widget with classes and texts."))
		.Args(FToolSchema()
			.Add(TEXT("target"), Widget())
			.Add(TEXT("depth"), FToolParam::Integer(TEXT("Levels"), 1, 32).Default(4))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&ListWidgetTree);

	B.Tool(TEXT("find_widget"), TEXT("Find on-screen widgets by name, class or text wildcard."))
		.Args(FToolSchema()
			.Add(TEXT("name"), FToolParam::String(TEXT("Name wildcard"), 128))
			.Add(TEXT("class"), FToolParam::String(TEXT("Class wildcard, e.g. Button"), 128))
			.Add(TEXT("text"), FToolParam::String(TEXT("Text wildcard, e.g. *Start*"), 256))
			.Add(TEXT("limit"), FToolParam::Limit()))
		.RequiresWorld()
		.Handle(&FindWidget);

	B.Tool(TEXT("create_widget"), TEXT("Create a project widget (WBP) and add it to the screen."))
		.Args(FToolSchema()
			.Add(TEXT("class"), FToolParam::String(TEXT("Widget class, e.g. /Game/UI/WBP_Menu"), 512).Required())
			.Add(TEXT("z_order"), FToolParam::Integer(TEXT("Z order"), -100, 1000).Default(0)))
		.Undoable().RequiresWorld()
		.Handle(&CreateProjectWidget);

	B.Tool(TEXT("create_hud_text"), TEXT("Show text on screen without any widget asset."))
		.Args(FToolSchema()
			.Add(TEXT("text"), FToolParam::String(TEXT("Text"), 2000).Required())
			.Add(TEXT("position"), FToolParam::Array(TEXT("[x,y] 0-1 screen space, centre of the text"), FToolParam::Number(FString(), 0.0, 1.0), 2))
			.Add(TEXT("size"), FToolParam::Number(TEXT("Font size"), 4.0, 400.0).Default(24.0))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("[r,g,b] 0-1")))
			.Add(TEXT("name"), FToolParam::String(TEXT("Widget name"), 128))
			.Add(TEXT("z_order"), FToolParam::Integer(TEXT("Z order"), -100, 1000).Default(10)))
		.Undoable().RequiresWorld()
		.Handle(&CreateHudText);

	B.Tool(TEXT("remove_widget"), TEXT("Take a top-level widget off the screen (undo puts it back)."))
		.Args(FToolSchema().Add(TEXT("target"), Widget()))
		.Undoable().RequiresWorld()
		.Handle(&RemoveWidget);

	B.Tool(TEXT("set_text"), TEXT("Change the text of a text block, rich text or editable text."))
		.Args(FToolSchema().Add(TEXT("target"), Widget()).Add(TEXT("text"), FToolParam::String(TEXT("New text"), 4000).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetText);

	B.Tool(TEXT("set_widget_visibility"), TEXT("Show, hide or collapse a widget."))
		.Args(FToolSchema()
			.Add(TEXT("target"), Widget())
			.Add(TEXT("visibility"), FToolParam::Enum(TEXT("Visibility"), {TEXT("visible"), TEXT("hidden"), TEXT("collapsed"), TEXT("hit_test_invisible")}).Required()))
		.Undoable().RequiresWorld()
		.Handle(&SetWidgetVisibility);

	B.Tool(TEXT("set_brush"), TEXT("Set an Image widget's texture and/or tint."))
		.Args(FToolSchema()
			.Add(TEXT("target"), Widget())
			.Add(TEXT("texture"), FToolParam::String(TEXT("Texture2D path"), 512))
			.Add(TEXT("tint"), FToolParam::Vec3(TEXT("[r,g,b] 0-1"))))
		.Undoable().RequiresWorld()
		.Handle(&SetBrush);

	B.Tool(TEXT("create_label"), TEXT("Name tag in the world: text drawn above a target (it follows the target) or at a location."))
		.Args(FToolSchema()
			.Add(TEXT("text"), FToolParam::String(TEXT("Label text"), 500).Required())
			.Add(TEXT("target"), FToolParam::Target(TEXT("Label this node/actor (placed above its top centre)")))
			.Add(TEXT("location"), FToolParam::Vec3(TEXT("World location when there is no target")))
			.Add(TEXT("offset"), FToolParam::Vec3(TEXT("World offset from the anchor, default [0,0,100]")))
			.Add(TEXT("size"), FToolParam::Number(TEXT("Font size"), 6.0, 200.0).Default(28.0))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("Text color [r,g,b] 0-1")))
			.Add(TEXT("background"), FToolParam::Boolean(TEXT("Dark backing box")).Default(true))
			.Add(TEXT("name"), FToolParam::String(TEXT("Node name"), 128)))
		.Undoable().RequiresWorld()
		.Handle(&CreateLabel);

	B.Tool(TEXT("set_label"), TEXT("Change a label's text, size, color or background."))
		.Args(FToolSchema()
			.Add(TEXT("target"), FToolParam::Target(TEXT("Label node")).Required())
			.Add(TEXT("text"), FToolParam::String(TEXT("Text"), 500))
			.Add(TEXT("size"), FToolParam::Number(TEXT("Font size"), 6.0, 200.0))
			.Add(TEXT("color"), FToolParam::Vec3(TEXT("[r,g,b] 0-1")))
			.Add(TEXT("background"), FToolParam::Boolean(TEXT("Dark backing box"))))
		.Undoable().RequiresWorld()
		.Handle(&SetLabel);

	B.Tool(TEXT("click_button"), TEXT("Fire a Button's pressed/released/clicked events. Not undoable."))
		.Args(FToolSchema().Add(TEXT("target"), Widget()))
		.Irreversible().RequiresWorld()
		.Handle(&ClickButton);
}
