#include "RuntimeDebugLayer.h"
#include "SRuntimeDebugPanel.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Layout/SScrollBox.h"
#include "RuntimeSceneGraph.h"
#include "RuntimeToolsetsCore.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace RtDebug
{
	constexpr int32 MaxEntries = 400;
	constexpr int32 RowsShown = 32;
	constexpr double ThinkingFor = 120.0; // after a call, "agent thinking" this long before "idle"
	constexpr double NewBuildAfter = 60.0; // a call after this long a pause starts a new build (timer)

	FString Short(const FString& S, int32 Max)
	{
		const FString One = S.Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(""));
		return One.Len() > Max ? One.Left(Max - 1) + TEXT("…") : One;
	}

	FString Num(double N)
	{
		return FMath::IsNearlyEqual(N, FMath::RoundToDouble(N), 1e-6) ? FString::Printf(TEXT("%lld"), (long long)FMath::RoundToDouble(N))
			: FString::SanitizeFloat(N, 0).Left(8);
	}

	/** A value in a few characters: numbers, short text, small vectors (1, 2, 3); big lists as counts. */
	FString Value(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid())
		{
			return TEXT("-");
		}
		switch (V->Type)
		{
		case EJson::Number:
			return Num(V->AsNumber());
		case EJson::String:
			return Short(V->AsString(), 48);
		case EJson::Boolean:
			return V->AsBool() ? TEXT("yes") : TEXT("no");
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			bool bSmall = A.Num() <= 4;
			for (const TSharedPtr<FJsonValue>& E : A)
			{
				bSmall &= E.IsValid() && (E->Type == EJson::Number || E->Type == EJson::String);
			}
			if (bSmall && A.Num() > 0)
			{
				TArray<FString> Parts;
				for (const TSharedPtr<FJsonValue>& E : A)
				{
					Parts.Add(E->Type == EJson::Number ? Num(E->AsNumber()) : Short(E->AsString(), 16));
				}
				return FString::Join(Parts, TEXT(", "));
			}
			return FString::Printf(TEXT("%d items"), A.Num());
		}
		case EJson::Object:
			return FString::Printf(TEXT("%d fields"), V->AsObject()->Values.Num());
		default:
			return TEXT("-");
		}
	}

	/** "shape: Box    size: 300, 300, 300    parts: 312 items" */
	FString Fields(const TSharedPtr<FJsonObject>& O, int32 Max)
	{
		if (!O.IsValid())
		{
			return FString();
		}
		TArray<FString> Out;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : O->Values)
		{
			if (P.Key == TEXT("returnValue") && P.Value.IsValid() && P.Value->Type == EJson::Object)
			{
				Out.Add(Fields(P.Value->AsObject(), Max));
				continue;
			}
			Out.Add(P.Key + TEXT(": ") + Value(P.Value));
		}
		return Short(FString::Join(Out, TEXT("    ")), Max);
	}

	/** "build.create_primitive" -> "build: create primitive" */
	FString Friendly(const FString& Tool)
	{
		FString Set, Name;
		if (!Tool.Split(TEXT("."), &Set, &Name) && !Tool.Split(TEXT("_"), &Set, &Name))
		{
			return Tool;
		}
		return Set + TEXT(": ") + Name.Replace(TEXT("_"), TEXT(" "));
	}

	FString Clock(double Seconds)
	{
		if (Seconds < 1.0)
		{
			return FString::Printf(TEXT("%d ms"), FMath::RoundToInt(Seconds * 1000.0));
		}
		if (Seconds < 60.0)
		{
			return FString::Printf(TEXT("%.1f s"), Seconds);
		}
		const int32 S = FMath::FloorToInt(Seconds);
		return FString::Printf(TEXT("%dm %02ds"), S / 60, S % 60);
	}

	const FLinearColor Grey(0.62f, 0.65f, 0.7f);
	constexpr float TimeW = 64.f;
	constexpr float IconW = 18.f;
	constexpr float DurW = 72.f;
	constexpr float LabelW = 38.f;
	constexpr float StatW = 160.f;

	/** Label / value pairs in fixed columns. */
	TSharedRef<SWidget> StatRow(TArray<TPair<FString, TFunction<FString()>>> Items)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (TPair<FString, TFunction<FString()>>& It : Items)
		{
			TFunction<FString()> Get = MoveTemp(It.Value);
			Row->AddSlot().AutoWidth()
			[
				SNew(SBox).WidthOverride(StatW)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(Grey).Text(FText::FromString(It.Key))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)).Text_Lambda([Get]() { return FText::FromString(Get()); })
					]
				]
			];
		}
		return Row;
	}
	const FLinearColor Green(0.45f, 0.92f, 0.55f);
	const FLinearColor Red(1.f, 0.45f, 0.42f);
	const FLinearColor Amber(1.f, 0.8f, 0.35f);

	/** Shift+Alt+Z, wherever focus is. */
	class FToggleKey final : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Event) override
		{
			if (Event.GetKey() != EKeys::Z || Event.IsRepeat())
			{
				return false;
			}
			// Windows uses Alt+Shift to switch keyboard language and can hide Shift from the key event: ask the keyboard too.
			const FModifierKeysState Now = App.GetModifierKeys();
			bool bShift = Event.IsShiftDown() || Now.IsShiftDown();
#if PLATFORM_WINDOWS
			bShift |= (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#endif
			const bool bAlt = Event.IsAltDown() || Now.IsAltDown();
			const bool bCtrl = Event.IsControlDown() || Now.IsControlDown();
			if (bShift && bAlt && !bCtrl)
			{
				FRuntimeDebugLayer::Get().Toggle();
				return true;
			}
			return false;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("RuntimeToolsetsDebugLayer"); }
	};
}

FRuntimeDebugLayer& FRuntimeDebugLayer::Get()
{
	static FRuntimeDebugLayer Instance;
	return Instance;
}

void FRuntimeDebugLayer::Startup()
{
	TraceHandle = IRuntimeToolsetsCore::Get().OnTrace().AddRaw(this, &FRuntimeDebugLayer::OnTrace);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FRuntimeDebugLayer::Tick));
	bShown = FParse::Param(FCommandLine::Get(), TEXT("RuntimeToolsetsDebug"));
}

void FRuntimeDebugLayer::Shutdown()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (IRuntimeToolsetsCore::IsAvailable())
	{
		IRuntimeToolsetsCore::Get().OnTrace().Remove(TraceHandle);
	}
	if (Input.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Input);
	}
	Input.Reset();
	if (UGameViewportClient* VP = WidgetViewport.Get(); VP && Widget.IsValid())
	{
		VP->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	Rows.Reset();
}

void FRuntimeDebugLayer::Toggle()
{
	bShown = !bShown;
	BuiltVersion = -1;
	NextStats = 0.0;
	SetFreeCursor(bShown);
}

void FRuntimeDebugLayer::SetFreeCursor(bool bFree)
{
	UGameViewportClient* VP = GEngine ? GEngine->GameViewport : nullptr;
	UWorld* World = VP ? VP->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (bFree && !bCursorFreed && PC)
	{
		bSavedShowCursor = PC->bShowMouseCursor;
		CursorController = PC;
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ReleaseAllPointerCapture();
		}
		bCursorFreed = true;
	}
	else if (!bFree && bCursorFreed)
	{
		if (APlayerController* Old = CursorController.Get())
		{
			Old->bShowMouseCursor = bSavedShowCursor;
			Old->SetInputMode(FInputModeGameOnly());
		}
		bCursorFreed = false;
	}
}

void FRuntimeDebugLayer::OnTrace(const FRuntimeToolTrace& T)
{
	using namespace RtDebug;
	const double Now = FPlatformTime::Seconds();
	if (T.bStarted)
	{
		TSharedPtr<FEntry> E = MakeShared<FEntry>();
		E->Tool = T.Tool;
		E->Session = T.SessionId;
		E->Depth = T.Depth;
		E->bWrapper = T.Tool.EndsWith(TEXT("call_tool"));
		for (int32 i = Entries.Num() - 1; i >= 0; --i)
		{
			if (Entries[i]->bRunning && Entries[i]->Depth == T.Depth - 1)
			{
				E->Indent = Entries[i]->Indent + (Entries[i]->bWrapper ? 0 : 1);
				E->Think = Entries[i]->bWrapper ? Entries[i]->Think : -1.0;
				break;
			}
		}
		E->StartedAt = Now;
		E->Wall = FDateTime::Now();
		E->Args = Fields(T.Args, 220);
		Entries.Add(E);
		if (Entries.Num() > MaxEntries)
		{
			Entries.RemoveAt(0, Entries.Num() - MaxEntries);
		}
		FirstCall = FirstCall > 0.0 ? FirstCall : Now;
		if (T.Depth == 0)
		{
			const bool bNewBuild = RunStart <= 0.0 || (LastCallEnd > 0.0 && Now - LastCallEnd > NewBuildAfter && !AnyRunning(E));
			if (bNewBuild)
			{
				RunStart = Now;
				RunCalls = 0;
				RunErrors = 0;
				RunThink = 0.0;
				RunTools = 0.0;
			}
			// the agent's thinking before this call: since the previous call ended (none before the first of a build)
			E->Think = !bNewBuild && LastCallEnd > 0.0 && !AnyRunning(E) ? Now - LastCallEnd : 0.0;
			RunThink += E->Think;
		}
	}
	else
	{
		TSharedPtr<FEntry> E;
		for (int32 i = Entries.Num() - 1; i >= 0 && !E; --i)
		{
			if (Entries[i]->bRunning && Entries[i]->Tool == T.Tool && Entries[i]->Depth == T.Depth)
			{
				E = Entries[i];
			}
		}
		if (!E)
		{
			return;
		}
		E->bRunning = false;
		E->Seconds = T.Seconds;
		E->bOk = !T.Result || T.Result->IsOk();
		if (T.Result && !T.Result->IsOk())
		{
			E->Detail = Short(T.Result->Message + (T.Result->Hint.IsEmpty() ? FString() : TEXT("  (hint: ") + T.Result->Hint + TEXT(")")), 260);
		}
		else if (T.Result)
		{
			E->Detail = Fields(T.Result->Payload, 220);
		}
		if (T.Depth == 0)
		{
			++Calls;
			++RunCalls;
			ToolSeconds += T.Seconds;
			RunTools += T.Seconds;
		}
		const int32 NewErrors = E->bOk || E->bWrapper ? 0 : 1;
		Errors += NewErrors;
		RunErrors += NewErrors;
		LastCallEnd = Now;
	}
	++Version;
}

bool FRuntimeDebugLayer::Tick(float DeltaTime)
{
	SmoothedFrameMs = FMath::Lerp(SmoothedFrameMs, DeltaTime * 1000.f, 0.05f);
	if (!Input.IsValid() && FSlateApplication::IsInitialized())
	{
		Input = MakeShared<RtDebug::FToggleKey>(); // listens even while hidden (the default)
		FSlateApplication::Get().RegisterInputPreProcessor(Input);
	}
	if (!bShown && !Widget.IsValid())
	{
		return true;
	}
	EnsureWidget();
	if (bCursorFreed && !CursorController.IsValid())
	{
		bCursorFreed = false; // the level changed: free the new player's cursor
	}
	if (bShown && !bCursorFreed)
	{
		SetFreeCursor(true); // shown from the command line, or after a level change
	}
	if (bShown)
	{
		if (BuiltVersion != Version)
		{
			RebuildRows();
		}
		if (FPlatformTime::Seconds() >= NextStats)
		{
			RefreshStats();
		}
	}
	return true;
}

void FRuntimeDebugLayer::EnsureWidget()
{
	using namespace RtDebug;
	UGameViewportClient* VP = GEngine ? GEngine->GameViewport : nullptr;
	if (!VP || WidgetViewport.Get() == VP || !FSlateApplication::IsInitialized())
	{
		return;
	}
	WidgetViewport = VP;
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo Head = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	if (PanelPosition.X < 0.0)
	{
		FVector2D ViewSize(1280.0, 720.0);
		VP->GetViewportSize(ViewSize);
		const float Dpi = FMath::Max(VP->GetDPIScale(), 0.01f);
		PanelSize.Y = FMath::Min(PanelSize.Y, ViewSize.Y / Dpi - 24.0);
		PanelPosition = FVector2D(FMath::Max(12.0, ViewSize.X / Dpi - PanelSize.X - 12.0), 12.0);
	}
	Widget = SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding_Lambda([this]() { return FMargin(PanelPosition.X, PanelPosition.Y, 0.f, 0.f); })
		.Visibility_Lambda([this]() { return bShown ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SRuntimeDebugPanel)
			.Position(&PanelPosition)
			.Size(&PanelSize)
			.TitleHeight(44.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.84f))
				.Padding(FMargin(12.f, 8.f, 12.f, 14.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(Title).Text(FText::FromString(TEXT("Agent build debug")))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(Grey).AutoWrapText(true)
						.Text(FText::FromString(TEXT("Drag the title to move, the corner to resize. Shift+Alt+Z hides.")))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).AutoWrapText(true)
						.Text_Lambda([this]() { return StatusText(); })
						.ColorAndOpacity_Lambda([this]() { return StatusColor(); })
					]
					// the build timer: how long this build has taken (live), or how long the last one took
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
						.ColorAndOpacity_Lambda([this]() { return StatusColor(); })
						.Text_Lambda([this]() { return BuildTimerText(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						StatRow({
							{TEXT("Calls"), [this]() { return FString::FromInt(Calls); }},
							{TEXT("Errors"), [this]() { return FString::FromInt(Errors); }},
							{TEXT("Tool time"), [this]() { return Clock(ToolSeconds); }},
							{TEXT("Since start"), [this]() { return FirstCall > 0.0 ? Clock(FPlatformTime::Seconds() - FirstCall) : FString(TEXT("-")); }}})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[
						StatRow({
							{TEXT("Objects"), [this]() { return ObjectsText; }},
							{TEXT("Actors"), [this]() { return ActorsText; }},
							{TEXT("Map"), [this]() { return MapText; }},
							{TEXT("Frame"), [this]() { return PerfLine; }}})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(TimeW)[SNew(STextBlock).Font(Head).ColorAndOpacity(Grey).Text(FText::FromString(TEXT("TIME")))]]
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(IconW)]
						+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Font(Head).ColorAndOpacity(Grey).Text(FText::FromString(TEXT("TOOL")))]
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(DurW).HAlign(HAlign_Right)[SNew(STextBlock).Font(Head).ColorAndOpacity(Grey).Text(FText::FromString(TEXT("THINK")))]]
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(DurW).HAlign(HAlign_Right)[SNew(STextBlock).Font(Head).ColorAndOpacity(Grey).Text(FText::FromString(TEXT("RUN")))]]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(Rows, SVerticalBox)
						]
					]
				]
			]
		];
	VP->AddViewportWidgetContent(Widget.ToSharedRef(), 2000);
	BuiltVersion = -1;
}

bool FRuntimeDebugLayer::AnyRunning(const TSharedPtr<FEntry>& Except) const
{
	for (const TSharedPtr<FEntry>& E : Entries)
	{
		if (E->bRunning && E != Except)
		{
			return true;
		}
	}
	return false;
}

FText FRuntimeDebugLayer::BuildTimerText() const
{
	using namespace RtDebug;
	if (RunStart <= 0.0)
	{
		return FText::FromString(TEXT("Build time  -"));
	}
	const double Now = FPlatformTime::Seconds();
	const bool bActive = AnyRunning(nullptr) || (LastCallEnd > 0.0 && Now - LastCallEnd < NewBuildAfter);
	const double End = bActive ? Now : LastCallEnd;
	const FString Errs = RunErrors ? FString::Printf(TEXT("   %d error%s"), RunErrors, RunErrors == 1 ? TEXT("") : TEXT("s")) : FString();
	const double ThinkNow = RunThink + (bActive && !AnyRunning(nullptr) && LastCallEnd > 0.0 ? Now - LastCallEnd : 0.0);
	return FText::FromString(FString::Printf(TEXT("%s  %s   %d calls   thinking %s   in tools %s%s"), bActive ? TEXT("Build time") : TEXT("Last build took"),
		*Clock(FMath::Max(0.0, End - RunStart)), RunCalls, *Clock(ThinkNow), *Clock(RunTools), *Errs));
}

FText FRuntimeDebugLayer::StatusText() const
{
	using namespace RtDebug;
	const double Now = FPlatformTime::Seconds();
	// the innermost running call is what is actually happening
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		if (Entries[i]->bRunning && !Entries[i]->bWrapper && Now - Entries[i]->StartedAt < 3600.0)
		{
			return FText::FromString(FString::Printf(TEXT("Working: %s   %s"), *Friendly(Entries[i]->Tool), *Clock(Now - Entries[i]->StartedAt)));
		}
	}
	if (LastCallEnd > 0.0 && Now - LastCallEnd < ThinkingFor)
	{
		return FText::FromString(FString::Printf(TEXT("Agent is thinking about the next step   %s"), *Clock(Now - LastCallEnd)));
	}
	return FText::FromString(LastCallEnd > 0.0 ? FString::Printf(TEXT("Idle   (last call %s ago)"), *Clock(Now - LastCallEnd)) : TEXT("Idle: no agent calls yet"));
}

FSlateColor FRuntimeDebugLayer::StatusColor() const
{
	using namespace RtDebug;
	for (const TSharedPtr<FEntry>& E : Entries)
	{
		if (E->bRunning)
		{
			return FSlateColor(Amber);
		}
	}
	return FSlateColor(LastCallEnd > 0.0 && FPlatformTime::Seconds() - LastCallEnd < ThinkingFor ? FLinearColor(0.55f, 0.85f, 1.f) : Grey);
}

void FRuntimeDebugLayer::RefreshStats()
{
	NextStats = FPlatformTime::Seconds() + 0.5;
	PerfLine = FString::Printf(TEXT("%.0f fps  %.1f ms"), 1000.f / FMath::Max(SmoothedFrameMs, 0.1f), SmoothedFrameMs);
	IRuntimeToolsetsCore& Core = IRuntimeToolsetsCore::Get();
	UWorld* World = Core.FindWorld();
	if (!World)
	{
		ObjectsText = ActorsText = MapText = TEXT("-");
		return;
	}
	int32 Actors = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++Actors;
	}
	const FRuntimeSceneGraph* Graph = Core.GetSceneGraph(World);
	ObjectsText = FString::FromInt(Graph ? Graph->NumLiveNodes() : 0);
	ActorsText = FString::FromInt(Actors);
	MapText = World->GetMapName();
}

void FRuntimeDebugLayer::RebuildRows()
{
	using namespace RtDebug;
	BuiltVersion = Version;
	if (!Rows.IsValid())
	{
		return;
	}
	Rows->ClearChildren();
	const FSlateFontInfo Mono = FCoreStyle::GetDefaultFontStyle("Mono", 9);
	const FSlateFontInfo ToolFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo DetailFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FLinearColor Soft(0.68f, 0.71f, 0.76f);
	const FLinearColor Think(0.55f, 0.85f, 1.f);
	int32 Shown = 0;
	for (int32 i = Entries.Num() - 1; i >= 0 && Shown < RowsShown; --i)
	{
		const TSharedPtr<FEntry> E = Entries[i];
		if (E->bWrapper)
		{
			continue; // its inner call is shown instead
		}
		const float Indent = FMath::Min(E->Indent, 4) * 14.f;
		TSharedRef<SVerticalBox> Call = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(TimeW)[SNew(STextBlock).Font(Mono).ColorAndOpacity(Grey).Text(FText::FromString(E->Wall.ToString(TEXT("%H:%M:%S"))))]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(IconW)
					[
						SNew(STextBlock).Font(Mono)
						.ColorAndOpacity_Lambda([E]() { return FSlateColor(E->bRunning ? Amber : E->bOk ? Green : Red); })
						.Text_Lambda([E]() { return FText::FromString(E->bRunning ? TEXT("…") : E->bOk ? TEXT("✓") : TEXT("✗")); })
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(Indent, 0.f, 6.f, 0.f)
				[
					SNew(STextBlock).Font(ToolFont).Text(FText::FromString(E->Tool))
					.ColorAndOpacity_Lambda([E]() { return FSlateColor(E->bRunning ? Amber : E->bOk ? FLinearColor(0.9f, 0.92f, 0.95f) : Red); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(DurW).HAlign(HAlign_Right)
					[
						SNew(STextBlock).Font(Mono).ColorAndOpacity(Think)
						.Text(FText::FromString(E->Think > 0.05 ? Clock(E->Think) : FString()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(DurW).HAlign(HAlign_Right)
					[
						SNew(STextBlock).Font(Mono)
						.ColorAndOpacity_Lambda([E]() { return FSlateColor(E->bRunning ? Amber : Grey); })
						.Text_Lambda([E]() { return FText::FromString(Clock(E->bRunning ? FPlatformTime::Seconds() - E->StartedAt : E->Seconds)); })
					]
				]
			];
		auto DetailLine = [&](const TCHAR* Name, TFunction<FString()> Text, FLinearColor Color)
		{
			Call->AddSlot().AutoHeight().Padding(0.f, 1.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Text]() { return Text().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(TimeW + IconW + Indent)]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(LabelW)[SNew(STextBlock).Font(LabelFont).ColorAndOpacity(Color).Text(FText::FromString(Name))]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 28.f, 0.f)
				[
					SNew(STextBlock).Font(DetailFont).AutoWrapText(true).ColorAndOpacity(Color).Text_Lambda([Text]() { return FText::FromString(Text()); })
				]
			];
		};
		DetailLine(TEXT("in"), [E]() { return E->Args; }, Soft);
		DetailLine(TEXT("out"), [E]() { return E->bOk ? E->Detail : FString(); }, Soft);
		DetailLine(TEXT("error"), [E]() { return E->bOk ? FString() : E->Detail; }, Red);
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, Shown % 2 ? 0.035f : 0.075f))
			.Padding(FMargin(4.f, 4.f, 16.f, 4.f))
			[
				Call
			]
		];
		++Shown;
	}
}
