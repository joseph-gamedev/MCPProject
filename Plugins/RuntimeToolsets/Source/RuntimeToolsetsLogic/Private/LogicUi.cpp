#include "LogicUi.h"
#include "LogicExpr.h"
#include "LogicJsonHelpers.h"
#include "LogicRuntime.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

namespace LogicUiImpl
{
	struct FTheme
	{
		TMap<FString, FLinearColor> Colors;
		float Radius = 12.f;
		float Padding = 16.f;
		float Gap = 10.f;
		float ScreenMargin = 24.f;
		float FontSize = 14.f;
		bool bShadow = true;

		FLinearColor C(const TCHAR* Token) const
		{
			const FLinearColor* Found = Colors.Find(Token);
			return Found ? *Found : FLinearColor::White;
		}
	};

	const TArray<TPair<const TCHAR*, const TCHAR*>>& DefaultColors()
	{
		static const TArray<TPair<const TCHAR*, const TCHAR*>> List = {
			{TEXT("text"), TEXT("E8ECF4")}, {TEXT("muted"), TEXT("9AA5BC")}, {TEXT("subtle"), TEXT("5E6A82")},
			{TEXT("accent"), TEXT("5B8CFF")}, {TEXT("accent_text"), TEXT("FFFFFF")},
			{TEXT("panel"), TEXT("0E121BEB")}, {TEXT("surface"), TEXT("1A2131")}, {TEXT("surface_hover"), TEXT("263048")},
			{TEXT("border"), TEXT("FFFFFF1F")}, {TEXT("overlay"), TEXT("000000A0")},
			{TEXT("success"), TEXT("34D399")}, {TEXT("warning"), TEXT("FBBF24")}, {TEXT("danger"), TEXT("F87171")},
			{TEXT("info"), TEXT("38BDF8")}, {TEXT("gold"), TEXT("F5C542")}, {TEXT("white"), TEXT("FFFFFF")}, {TEXT("black"), TEXT("000000")},
			{TEXT("transparent"), TEXT("00000000")}};
		return List;
	}

	bool IsHex(const FString& S)
	{
		if (S.Len() != 6 && S.Len() != 8 && S.Len() != 3)
		{
			return false;
		}
		for (TCHAR Ch : S)
		{
			if (!FChar::IsHexDigit(Ch))
			{
				return false;
			}
		}
		return true;
	}

	FLinearColor FromHex(FString S)
	{
		if (S.Len() == 3)
		{
			S = FString::Printf(TEXT("%c%c%c%c%c%c"), S[0], S[0], S[1], S[1], S[2], S[2]);
		}
		return FLinearColor(FColor::FromHex(S));
	}

	/** [r,g,b,a] (linear 0-1), "#rrggbb[aa]", or a theme token ("accent", "panel/60" = 60% alpha). Theme null = syntax only. */
	bool ParseColor(const TSharedPtr<FJsonValue>& V, const FTheme* T, FLinearColor& Out, FString& Err)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (V.IsValid() && V->TryGetArray(A))
		{
			if (A->Num() < 3 || A->Num() > 4)
			{
				Err = TEXT("a color array needs 3 or 4 numbers [r,g,b,a] (0-1)");
				return false;
			}
			Out = FLinearColor((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), A->Num() > 3 ? (*A)[3]->AsNumber() : 1.0);
			return true;
		}
		FString S;
		if (!V.IsValid() || !V->TryGetString(S))
		{
			Err = TEXT("a color is [r,g,b,a], \"#rrggbb\" or a theme token like \"accent\"");
			return false;
		}
		S.TrimStartAndEndInline();
		if (S.Contains(TEXT("{")))
		{
			// Bound color, e.g. "{db[id].hex}"; evaluated while drawing.
			FString E;
			if (!LogicExpr::CheckTemplate(S, E))
			{
				Err = FString::Printf(TEXT("'%s': %s"), *S, *E);
				return false;
			}
			Out = FLinearColor::White;
			return true;
		}
		float Alpha = -1.f;
		FString Base = S, Pct;
		if (S.Split(TEXT("/"), &Base, &Pct))
		{
			if (!Pct.IsNumeric())
			{
				Err = FString::Printf(TEXT("'%s': the part after / is an opacity percent, e.g. \"panel/60\""), *S);
				return false;
			}
			Alpha = FMath::Clamp(FCString::Atof(*Pct) / 100.f, 0.f, 1.f);
		}
		if (Base.StartsWith(TEXT("#")))
		{
			const FString Hex = Base.Mid(1);
			if (!IsHex(Hex))
			{
				Err = FString::Printf(TEXT("'%s' is not a hex color (#rgb, #rrggbb or #rrggbbaa)"), *S);
				return false;
			}
			Out = FromHex(Hex);
		}
		else if (T)
		{
			const FLinearColor* Found = T->Colors.Find(Base.ToLower());
			if (!Found)
			{
				TArray<FString> Keys;
				T->Colors.GetKeys(Keys);
				Err = FString::Printf(TEXT("unknown color token '%s' (tokens: %s; or use \"#rrggbb\" / [r,g,b,a])"), *Base, *FString::Join(Keys, TEXT(", ")));
				return false;
			}
			Out = *Found;
		}
		else
		{
			for (TCHAR Ch : Base)
			{
				if (!FChar::IsAlnum(Ch) && Ch != '_')
				{
					Err = FString::Printf(TEXT("'%s' is not a color (use a token like \"accent\", \"#rrggbb\" or [r,g,b,a])"), *S);
					return false;
				}
			}
			Out = FLinearColor::White;
		}
		if (Alpha >= 0.f)
		{
			Out.A *= Alpha;
		}
		return true;
	}

	FTheme MakeTheme(const TSharedPtr<FJsonObject>& J)
	{
		FTheme T;
		for (const TPair<const TCHAR*, const TCHAR*>& P : DefaultColors())
		{
			T.Colors.Add(P.Key, FromHex(P.Value));
		}
		if (!J.IsValid())
		{
			return T;
		}
		const TSharedPtr<FJsonObject>* Colors = nullptr;
		if (J->TryGetObjectField(TEXT("colors"), Colors))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Colors)->Values)
			{
				FLinearColor C;
				FString Err;
				if (ParseColor(P.Value, nullptr, C, Err))
				{
					T.Colors.Add(P.Key.ToLower(), C);
				}
			}
		}
		T.Radius = Num(J, TEXT("radius"), T.Radius);
		T.Padding = Num(J, TEXT("padding"), T.Padding);
		T.Gap = Num(J, TEXT("gap"), T.Gap);
		T.ScreenMargin = Num(J, TEXT("screen_margin"), T.ScreenMargin);
		T.FontSize = Num(J, TEXT("font_size"), T.FontSize);
		J->TryGetBoolField(TEXT("text_shadow"), T.bShadow);
		return T;
	}

	// ---- schema ----------------------------------------------------------------------------------------------------

	const TArray<FString>& Kinds()
	{
		static const TArray<FString> K = {TEXT("panel"), TEXT("column"), TEXT("row"), TEXT("grid"), TEXT("stack"), TEXT("text"), TEXT("button"), TEXT("bar"),
			TEXT("spacer"), TEXT("divider")};
		return K;
	}

	const TArray<FString>& CommonFields()
	{
		static const TArray<FString> F = {TEXT("name"), TEXT("kind"), TEXT("visible_if"), TEXT("tooltip"), TEXT("width"), TEXT("height"), TEXT("min_width"),
			TEXT("min_height"), TEXT("max_width"), TEXT("max_height"), TEXT("grow"), TEXT("margin"), TEXT("padding"), TEXT("background"), TEXT("radius"),
			TEXT("border_color"), TEXT("border_width"), TEXT("opacity"), TEXT("style_if"), TEXT("h_align"), TEXT("v_align"), TEXT("repeat"), TEXT("as"),
			TEXT("anchor"), TEXT("offset"), TEXT("rotate")};
		return F;
	}

	const TArray<FString>& KindFields(const FString& Kind)
	{
		static const TMap<FString, TArray<FString>> M = {
			{TEXT("panel"), {TEXT("children"), TEXT("gap"), TEXT("align"), TEXT("justify"), TEXT("scroll")}},
			{TEXT("column"), {TEXT("children"), TEXT("gap"), TEXT("align"), TEXT("justify"), TEXT("scroll")}},
			{TEXT("row"), {TEXT("children"), TEXT("gap"), TEXT("align"), TEXT("justify")}},
			{TEXT("grid"), {TEXT("children"), TEXT("gap"), TEXT("columns"), TEXT("cell")}},
			{TEXT("stack"), {TEXT("children")}},
			{TEXT("text"), {TEXT("text"), TEXT("size"), TEXT("weight"), TEXT("color"), TEXT("align"), TEXT("wrap"), TEXT("shadow")}},
			{TEXT("button"), {TEXT("text"), TEXT("size"), TEXT("weight"), TEXT("color"), TEXT("variant"), TEXT("disabled_if"), TEXT("children"), TEXT("gap"),
				TEXT("align")}},
			{TEXT("bar"), {TEXT("value"), TEXT("max"), TEXT("fill"), TEXT("ghost"), TEXT("text"), TEXT("size"), TEXT("weight"), TEXT("color")}},
			{TEXT("spacer"), {}},
			{TEXT("divider"), {TEXT("color"), TEXT("thickness")}}};
		static const TArray<FString> None;
		const TArray<FString>* F = M.Find(Kind);
		return F ? *F : None;
	}

	bool IsContainer(const FString& Kind)
	{
		return Kind == TEXT("panel") || Kind == TEXT("column") || Kind == TEXT("row") || Kind == TEXT("grid") || Kind == TEXT("stack");
	}

	FString KindOf(const TSharedPtr<FJsonObject>& J)
	{
		FString K = Str(J, TEXT("kind")).ToLower();
		if (K.IsEmpty())
		{
			K = J->HasField(TEXT("children")) ? TEXT("column") : TEXT("text");
		}
		return K;
	}

	const TMap<FString, FVector2D>& AnchorNames()
	{
		static const TMap<FString, FVector2D> M = {{TEXT("top-left"), {0, 0}}, {TEXT("top"), {0.5, 0}}, {TEXT("top-right"), {1, 0}},
			{TEXT("left"), {0, 0.5}}, {TEXT("center"), {0.5, 0.5}}, {TEXT("right"), {1, 0.5}},
			{TEXT("bottom-left"), {0, 1}}, {TEXT("bottom"), {0.5, 1}}, {TEXT("bottom-right"), {1, 1}}};
		return M;
	}

	const TMap<FString, float>& SizeNames()
	{
		static const TMap<FString, float> M = {{TEXT("xs"), 10.f}, {TEXT("sm"), 12.f}, {TEXT("md"), 0.f}, {TEXT("lg"), 17.f}, {TEXT("xl"), 21.f},
			{TEXT("2xl"), 27.f}, {TEXT("3xl"), 34.f}};
		return M;
	}

	bool CheckEnum(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, std::initializer_list<const TCHAR*> Allowed, const FString& Path, FString& Err)
	{
		FString V;
		if (!J->TryGetStringField(Field, V))
		{
			if (J->HasField(Field))
			{
				Err = FString::Printf(TEXT("%s: %s must be a string"), *Path, Field);
				return false;
			}
			return true;
		}
		TArray<FString> Names;
		for (const TCHAR* A : Allowed)
		{
			if (V.Equals(A, ESearchCase::IgnoreCase))
			{
				return true;
			}
			Names.Add(A);
		}
		Err = FString::Printf(TEXT("%s: %s '%s' must be one of %s"), *Path, Field, *V, *FString::Join(Names, TEXT(", ")));
		return false;
	}

	bool CheckExprField(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Path, FString& Err)
	{
		FString V;
		if (!J->TryGetStringField(Field, V))
		{
			return true;
		}
		FString E;
		if (!LogicExpr::Parse(V, E))
		{
			Err = FString::Printf(TEXT("%s: %s '%s': %s"), *Path, Field, *V, *E);
			return false;
		}
		return true;
	}

	bool CheckTemplateField(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Path, FString& Err)
	{
		FString V;
		if (!J->TryGetStringField(Field, V))
		{
			return true;
		}
		FString E;
		if (!LogicExpr::CheckTemplate(V, E))
		{
			Err = FString::Printf(TEXT("%s: %s: %s"), *Path, Field, *E);
			return false;
		}
		return true;
	}

	bool CheckBox(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FString& Path, FString& Err)
	{
		const TSharedPtr<FJsonValue> V = J->TryGetField(Field);
		if (!V.IsValid())
		{
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (V->Type == EJson::Number || (V->TryGetArray(A) && (A->Num() == 2 || A->Num() == 4)))
		{
			return true;
		}
		Err = FString::Printf(TEXT("%s: %s is a number, [vertical, horizontal] or [top, right, bottom, left] (CSS order)"), *Path, Field);
		return false;
	}

	/** Walks every color field of a node (and its style_if entries). */
	bool ForEachColor(const TSharedPtr<FJsonObject>& J, const FString& Path, TFunctionRef<bool(const TSharedPtr<FJsonValue>&, const FString&)> Fn)
	{
		static const TCHAR* Fields[] = {TEXT("background"), TEXT("border_color"), TEXT("color"), TEXT("fill")};
		for (const TCHAR* F : Fields)
		{
			if (const TSharedPtr<FJsonValue> V = J->TryGetField(F))
			{
				if (!Fn(V, Path + TEXT(" ") + F))
				{
					return false;
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Styles = nullptr;
		if (J->TryGetArrayField(TEXT("style_if"), Styles))
		{
			for (int32 i = 0; i < Styles->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* S = nullptr;
				if ((*Styles)[i]->TryGetObject(S))
				{
					for (const TCHAR* F : Fields)
					{
						if (const TSharedPtr<FJsonValue> V = (*S)->TryGetField(F))
						{
							if (!Fn(V, FString::Printf(TEXT("%s style_if[%d] %s"), *Path, i, F)))
							{
								return false;
							}
						}
					}
				}
			}
		}
		return true;
	}

	FString ChildPath(const FString& Path, const TSharedPtr<FJsonObject>& Child, int32 Index)
	{
		const FString Name = Str(Child, TEXT("name"));
		return Name.IsEmpty() ? FString::Printf(TEXT("%s.children[%d]"), *Path, Index) : FString::Printf(TEXT("%s/%s"), *Path, *Name);
	}

	bool ValidateNode(const TSharedPtr<FJsonObject>& J, const FString& Path, bool bRoot, FString& Err)
	{
		const FString Kind = KindOf(J);
		if (!Kinds().Contains(Kind))
		{
			Err = FString::Printf(TEXT("%s: unknown kind '%s' (kinds: %s)"), *Path, *Kind, *FString::Join(Kinds(), TEXT(", ")));
			return false;
		}
		const TArray<FString>& Own = KindFields(Kind);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : J->Values)
		{
			if (!CommonFields().Contains(P.Key) && !Own.Contains(P.Key))
			{
				Err = FString::Printf(TEXT("%s: unknown field '%s' for kind %s (its fields: %s; any node: %s)"), *Path, *P.Key, *Kind,
					Own.Num() ? *FString::Join(Own, TEXT(", ")) : TEXT("none"), *FString::Join(CommonFields(), TEXT(", ")));
				return false;
			}
		}
		if (!bRoot && (J->HasField(TEXT("anchor")) || J->HasField(TEXT("offset"))))
		{
			Err = FString::Printf(TEXT("%s: anchor and offset only work on top-level items; use the parent's align/justify or margin"), *Path);
			return false;
		}
		if (bRoot && J->HasField(TEXT("repeat")))
		{
			Err = FString::Printf(TEXT("%s: repeat only works on children (wrap the item in a row, column or grid)"), *Path);
			return false;
		}
		if (const TSharedPtr<FJsonValue> A = J->TryGetField(TEXT("anchor")))
		{
			FString S;
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (A->TryGetString(S) ? !AnchorNames().Contains(S.ToLower()) : !(A->TryGetArray(Arr) && Arr->Num() == 2))
			{
				TArray<FString> Names;
				AnchorNames().GetKeys(Names);
				Err = FString::Printf(TEXT("%s: anchor is one of %s, or [x,y] 0-1"), *Path, *FString::Join(Names, TEXT(", ")));
				return false;
			}
		}
		if (const TSharedPtr<FJsonValue> R = J->TryGetField(TEXT("repeat")))
		{
			if (R->Type != EJson::Number && R->Type != EJson::String)
			{
				Err = FString::Printf(TEXT("%s: repeat is a count or an expression, e.g. 12 or \"len(bag)\""), *Path);
				return false;
			}
			if (!CheckExprField(J, TEXT("repeat"), Path, Err))
			{
				return false;
			}
		}
		if (const TSharedPtr<FJsonValue> S = J->TryGetField(TEXT("size")); S.IsValid() && S->Type == EJson::String && !SizeNames().Contains(S->AsString()))
		{
			Err = FString::Printf(TEXT("%s: size is a number or one of xs, sm, md, lg, xl, 2xl, 3xl"), *Path);
			return false;
		}
		if (!CheckExprField(J, TEXT("visible_if"), Path, Err) || !CheckExprField(J, TEXT("disabled_if"), Path, Err)
			|| !CheckExprField(J, TEXT("value"), Path, Err) || !CheckExprField(J, TEXT("max"), Path, Err)
			|| !CheckTemplateField(J, TEXT("text"), Path, Err) || !CheckTemplateField(J, TEXT("tooltip"), Path, Err)
			|| !CheckBox(J, TEXT("padding"), Path, Err) || !CheckBox(J, TEXT("margin"), Path, Err))
		{
			return false;
		}
		const bool bAlignText = Kind == TEXT("text");
		if (!CheckEnum(J, TEXT("h_align"), {TEXT("start"), TEXT("center"), TEXT("end"), TEXT("stretch")}, Path, Err)
			|| !CheckEnum(J, TEXT("v_align"), {TEXT("start"), TEXT("center"), TEXT("end"), TEXT("stretch")}, Path, Err)
			|| !CheckEnum(J, TEXT("justify"), {TEXT("start"), TEXT("center"), TEXT("end"), TEXT("between")}, Path, Err)
			|| !CheckEnum(J, TEXT("weight"), {TEXT("light"), TEXT("regular"), TEXT("medium"), TEXT("bold"), TEXT("black")}, Path, Err)
			|| !CheckEnum(J, TEXT("variant"), {TEXT("primary"), TEXT("secondary"), TEXT("ghost"), TEXT("danger"), TEXT("success")}, Path, Err)
			|| (bAlignText ? !CheckEnum(J, TEXT("align"), {TEXT("left"), TEXT("center"), TEXT("right")}, Path, Err)
				: !CheckEnum(J, TEXT("align"), {TEXT("start"), TEXT("center"), TEXT("end"), TEXT("stretch")}, Path, Err)))
		{
			return false;
		}
		if (Kind == TEXT("bar") && (!J->HasField(TEXT("value")) || !J->HasField(TEXT("max"))))
		{
			Err = FString::Printf(TEXT("%s: a bar needs value and max expressions, e.g. \"health.player\" and \"max_hp\""), *Path);
			return false;
		}
		if (Kind == TEXT("button") && Str(J, TEXT("name")).IsEmpty())
		{
			Err = FString::Printf(TEXT("%s: a button needs a name (rules react with {\"on\":\"button\",\"filter\":\"<name>\"})"), *Path);
			return false;
		}
		if (!ForEachColor(J, Path, [&Err](const TSharedPtr<FJsonValue>& V, const FString& Where)
		{
			FLinearColor C;
			FString E;
			if (!ParseColor(V, nullptr, C, E))
			{
				Err = Where + TEXT(": ") + E;
				return false;
			}
			return true;
		}))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Styles = nullptr;
		if (J->HasField(TEXT("style_if")))
		{
			if (!J->TryGetArrayField(TEXT("style_if"), Styles))
			{
				Err = FString::Printf(TEXT("%s: style_if is a list of {if, background, border_color, color, opacity}"), *Path);
				return false;
			}
			for (int32 i = 0; i < Styles->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* S = nullptr;
				const FString At = FString::Printf(TEXT("%s style_if[%d]"), *Path, i);
				if (!(*Styles)[i]->TryGetObject(S) || !(*S)->HasField(TEXT("if")))
				{
					Err = At + TEXT(": needs {if: \"<condition>\", background | border_color | color | opacity}");
					return false;
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*S)->Values)
				{
					if (P.Key != TEXT("if") && P.Key != TEXT("background") && P.Key != TEXT("border_color") && P.Key != TEXT("color") && P.Key != TEXT("opacity"))
					{
						Err = FString::Printf(TEXT("%s: unknown field '%s' (fields: if, background, border_color, color, opacity)"), *At, *P.Key);
						return false;
					}
				}
				if (!CheckExprField(*S, TEXT("if"), At, Err))
				{
					return false;
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (J->HasField(TEXT("children")))
		{
			if (!J->TryGetArrayField(TEXT("children"), Children))
			{
				Err = FString::Printf(TEXT("%s: children is a list of nodes"), *Path);
				return false;
			}
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* C = nullptr;
				if (!(*Children)[i]->TryGetObject(C))
				{
					Err = FString::Printf(TEXT("%s.children[%d] must be an object {kind, ...}"), *Path, i);
					return false;
				}
				if (!ValidateNode(*C, ChildPath(Path, *C, i), false, Err))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CheckTokensIn(const TSharedPtr<FJsonObject>& J, const FString& Path, const FTheme& T, FString& Err)
	{
		if (!ForEachColor(J, Path, [&](const TSharedPtr<FJsonValue>& V, const FString& Where)
		{
			FLinearColor C;
			FString E;
			if (!ParseColor(V, &T, C, E))
			{
				Err = Where + TEXT(": ") + E;
				return false;
			}
			return true;
		}))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (J->TryGetArrayField(TEXT("children"), Children))
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* C = nullptr;
				if ((*Children)[i]->TryGetObject(C) && !CheckTokensIn(*C, ChildPath(Path, *C, i), T, Err))
				{
					return false;
				}
			}
		}
		return true;
	}

	// ---- widgets ---------------------------------------------------------------------------------------------------

	const FButtonStyle& FlatButtonStyle()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateNoResource())
			.SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource())
			.SetDisabled(FSlateNoResource())
			.SetNormalPadding(FMargin(0.f))
			.SetPressedPadding(FMargin(0.f));
		return Style;
	}

	FSlateFontInfo Font(const FString& Weight, float Size)
	{
		static const FString Dir = FPaths::EngineContentDir() / TEXT("Slate/Fonts");
		const TCHAR* File = Weight == TEXT("light") ? TEXT("Roboto-Light.ttf")
			: Weight == TEXT("medium") ? TEXT("Roboto-Medium.ttf")
			: Weight == TEXT("bold") ? TEXT("Roboto-Bold.ttf")
			: Weight == TEXT("black") ? TEXT("Roboto-Black.ttf")
			: TEXT("Roboto-Regular.ttf");
		return FSlateFontInfo(Dir / File, FMath::Clamp(Size, 6.f, 120.f));
	}

	FMargin Box(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FMargin& Default)
	{
		const TSharedPtr<FJsonValue> V = J->TryGetField(Field);
		if (!V.IsValid())
		{
			return Default;
		}
		if (V->Type == EJson::Number)
		{
			return FMargin(V->AsNumber());
		}
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		if (A.Num() == 2)
		{
			return FMargin(A[1]->AsNumber(), A[0]->AsNumber());
		}
		if (A.Num() == 4)
		{
			return FMargin(A[3]->AsNumber(), A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
		}
		return Default;
	}

	EHorizontalAlignment HAlignOf(const FString& S, EHorizontalAlignment Default)
	{
		return S == TEXT("start") ? HAlign_Left : S == TEXT("center") ? HAlign_Center : S == TEXT("end") ? HAlign_Right : S == TEXT("stretch") ? HAlign_Fill : Default;
	}

	EVerticalAlignment VAlignOf(const FString& S, EVerticalAlignment Default)
	{
		return S == TEXT("start") ? VAlign_Top : S == TEXT("center") ? VAlign_Center : S == TEXT("end") ? VAlign_Bottom : S == TEXT("stretch") ? VAlign_Fill : Default;
	}

	/** Bar painted with rounded track, delayed "ghost" and fill; sized by its parent. */
	class SLogicUiBar : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SLogicUiBar) : _Track(nullptr), _Fill(nullptr), _Ghost(nullptr), _Desired(160.0, 10.0) {}
			SLATE_ATTRIBUTE(float, Fraction)
			SLATE_ARGUMENT(const FSlateBrush*, Track)
			SLATE_ARGUMENT(const FSlateBrush*, Fill)
			SLATE_ARGUMENT(const FSlateBrush*, Ghost)
			SLATE_ARGUMENT(FVector2D, Desired)
		SLATE_END_ARGS()

		void Construct(const FArguments& A)
		{
			Fraction = A._Fraction;
			Track = A._Track;
			FillBrush = A._Fill;
			GhostBrush = A._Ghost;
			Desired = A._Desired;
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return Desired; }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const FVector2D Sz = Geo.GetLocalSize();
			const float F = FMath::Clamp(Fraction.Get(), 0.f, 1.f);
			GhostValue = (GhostValue < 0.f || F >= GhostValue) ? F : FMath::FInterpConstantTo(GhostValue, F, FApp::GetDeltaTime(), 0.6f);
			const FLinearColor Tint = Style.GetColorAndOpacityTint();
			FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), Track, ESlateDrawEffect::None, Tint * Track->GetTint(Style));
			if (GhostBrush && GhostValue > F + 0.002f)
			{
				FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(FVector2D(Sz.X * GhostValue, Sz.Y), FSlateLayoutTransform()), GhostBrush, ESlateDrawEffect::None, Tint * GhostBrush->GetTint(Style));
			}
			if (F > 0.f)
			{
				FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2D(FMath::Max(Sz.X * F, FMath::Min(Sz.Y, Sz.X)), Sz.Y), FSlateLayoutTransform()), FillBrush, ESlateDrawEffect::None, Tint * FillBrush->GetTint(Style));
			}
			return Layer + 2;
		}

	private:
		TAttribute<float> Fraction;
		const FSlateBrush* Track = nullptr;
		const FSlateBrush* FillBrush = nullptr;
		const FSlateBrush* GhostBrush = nullptr;
		FVector2D Desired = FVector2D(160, 10);
		mutable float GhostValue = -1.f;
	};
	/** Builds Slate widgets from layout nodes; a friend of FLogicWorld so bindings can evaluate expressions. */
	struct FBuilder
	{
	using FLocals = TMap<FString, FLogicValue>;

	FLogicWorld& W;
	TWeakPtr<FLogicWorld> Weak;
	FTheme T;

	explicit FBuilder(FLogicWorld& InWorld) : W(InWorld), Weak(InWorld.AsShared()), T(MakeTheme(InWorld.UiTheme)) {}

	static FLogicValue Eval(const TWeakPtr<FLogicWorld>& Weak, const FLocals& Locals, const FString& Expr)
	{
		TSharedPtr<FLogicWorld> S = Weak.Pin();
		if (!S)
		{
			return FLogicValue();
		}
		FLogicWorld::FFrame Frame;
		Frame.Locals = Locals;
		FLogicScope Scope = S->MakeScope(Frame);
		return LogicExpr::Evaluate(Expr, Scope);
	}

	static FString Interp(const TWeakPtr<FLogicWorld>& Weak, const FLocals& Locals, const FString& Text)
	{
		TSharedPtr<FLogicWorld> S = Weak.Pin();
		if (!S)
		{
			return FString();
		}
		FLogicWorld::FFrame Frame;
		Frame.Locals = Locals;
		FLogicScope Scope = S->MakeScope(Frame);
		return LogicExpr::Interpolate(Text, Scope);
	}

	const FSlateBrush* Brush(const FLinearColor& Fill, float Radius, const FLinearColor& Border, float BorderWidth)
	{
		return BrushIn(W, Fill, Radius, Border, BorderWidth);
	}

	static const FSlateBrush* BrushIn(FLogicWorld& W, const FLinearColor& Fill, float Radius, const FLinearColor& Border, float BorderWidth)
	{
		const FString Key = FString::Printf(TEXT("%s|%.1f|%s|%.1f"), *Fill.ToString(), Radius, *Border.ToString(), BorderWidth);
		if (const TSharedPtr<FSlateBrush>* Found = W.UiBrushes.Find(Key))
		{
			return Found->Get();
		}
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateRoundedBoxBrush>(Fill, Radius, Border, BorderWidth);
		W.UiBrushes.Add(Key, B);
		return B.Get();
	}

	static FString ColorTemplate(const TSharedPtr<FJsonObject>& J, const TCHAR* Field)
	{
		FString S;
		return J->TryGetStringField(Field, S) && S.Contains(TEXT("{")) ? S : FString();
	}

	/** Evaluates a bound color template ("{db[id].hex}") to a color; transparent when it is empty or not a color. */
	static FLinearColor ResolveColor(const TWeakPtr<FLogicWorld>& Weak, const FLocals& Locals, const FString& Template, const FTheme& Theme)
	{
		const FString S = Interp(Weak, Locals, Template).TrimStartAndEnd();
		FLinearColor C = FLinearColor::Transparent;
		FString Err;
		if (!S.IsEmpty() && !S.Contains(TEXT("{")) && !ParseColor(MakeShared<FJsonValueString>(S), &Theme, C, Err))
		{
			C = FLinearColor::Transparent;
		}
		return C;
	}

	FLinearColor Color(const TSharedPtr<FJsonObject>& J, const TCHAR* Field, const FLinearColor& Default) const
	{
		if (!ColorTemplate(J, Field).IsEmpty())
		{
			return Default;
		}
		FLinearColor C = Default;
		FString Err;
		if (const TSharedPtr<FJsonValue> V = J->TryGetField(Field); V.IsValid() && !ParseColor(V, &T, C, Err))
		{
			C = FLinearColor(1.f, 0.f, 1.f, 1.f); // unknown token: magenta so it is easy to spot
		}
		return C;
	}

	float FontSize(const TSharedPtr<FJsonObject>& J, float Default) const
	{
		const TSharedPtr<FJsonValue> V = J->TryGetField(TEXT("size"));
		if (!V.IsValid())
		{
			return Default;
		}
		if (V->Type == EJson::Number)
		{
			return V->AsNumber();
		}
		const float* Named = SizeNames().Find(V->AsString());
		return Named && *Named > 0.f ? *Named : T.FontSize;
	}

	struct FLook
	{
		FLinearColor Bg = FLinearColor::Transparent;
		FLinearColor Border = FLinearColor::Transparent;
		FLinearColor Text = FLinearColor::White;
		float Opacity = 1.f;
	};

	/** Base look plus one look per style_if entry; Pick() returns the index of the first matching entry + 1, else 0. */
	struct FLooks
	{
		TArray<FLook> List;
		TSharedPtr<TArray<FString>> Conditions;
	};

	FLooks ReadLooks(const TSharedPtr<FJsonObject>& J, const FLook& Base)
	{
		FLooks L;
		L.List.Add(Base);
		L.Conditions = MakeShared<TArray<FString>>();
		const TArray<TSharedPtr<FJsonValue>>* Styles = nullptr;
		if (J->TryGetArrayField(TEXT("style_if"), Styles))
		{
			for (const TSharedPtr<FJsonValue>& V : *Styles)
			{
				const TSharedPtr<FJsonObject> S = V->AsObject();
				FLook K = Base;
				K.Bg = Color(S, TEXT("background"), Base.Bg);
				K.Border = Color(S, TEXT("border_color"), Base.Border);
				K.Text = Color(S, TEXT("color"), Base.Text);
				K.Opacity = Num(S, TEXT("opacity"), Base.Opacity);
				L.List.Add(K);
				L.Conditions->Add(Str(S, TEXT("if")));
			}
		}
		return L;
	}

	TFunction<int32()> Picker(const FLooks& L, const FLocals& Locals) const
	{
		if (L.Conditions->Num() == 0)
		{
			return []() { return 0; };
		}
		return [Weak = Weak, Locals, Conds = L.Conditions]()
		{
			for (int32 i = 0; i < Conds->Num(); ++i)
			{
				if (Eval(Weak, Locals, (*Conds)[i]).Truthy())
				{
					return i + 1;
				}
			}
			return 0;
		};
	}

	TAttribute<FText> TextAttr(const FString& Text, const FString& Name, const FLocals& Locals) const
	{
		if (!Text.Contains(TEXT("{")) && Name.IsEmpty())
		{
			return FText::FromString(Text);
		}
		return TAttribute<FText>::CreateLambda([Weak = Weak, Text, Name, Locals]()
		{
			TSharedPtr<FLogicWorld> S = Weak.Pin();
			if (!S)
			{
				return FText();
			}
			const FString* Ov = Name.IsEmpty() ? nullptr : S->TextOverrides.Find(Name);
			return FText::FromString(Interp(Weak, Locals, Ov ? *Ov : Text));
		});
	}

	TSharedRef<STextBlock> Label(const TSharedPtr<FJsonObject>& J, const FString& Name, const FLocals& Locals, const FLooks& Looks,
		const TFunction<int32()>& Pick, float DefaultSize, const FString& DefaultWeight, ETextJustify::Type DefaultJustify,
		const TFunction<bool()>& IsDisabled = nullptr)
	{
		const FString Align = Str(J, TEXT("align"));
		const ETextJustify::Type Justify = Align == TEXT("left") ? ETextJustify::Left : Align == TEXT("center") ? ETextJustify::Center
			: Align == TEXT("right") ? ETextJustify::Right : DefaultJustify;
		bool bShadow = T.bShadow;
		J->TryGetBoolField(TEXT("shadow"), bShadow);
		bool bWrap = false;
		J->TryGetBoolField(TEXT("wrap"), bWrap);
		TArray<FLinearColor> Colors;
		for (const FLook& K : Looks.List)
		{
			Colors.Add(K.Text);
		}
		const FLinearColor Dim = T.C(TEXT("muted"));
		const FString ColorTpl = ColorTemplate(J, TEXT("color"));
		TAttribute<FSlateColor> ColorAttr = !ColorTpl.IsEmpty()
			? TAttribute<FSlateColor>::CreateLambda([Weak = Weak, Locals, ColorTpl, Theme = T]() { return FSlateColor(ResolveColor(Weak, Locals, ColorTpl, Theme)); })
			: (Colors.Num() == 1 && !IsDisabled) ? TAttribute<FSlateColor>(FSlateColor(Colors[0]))
			: TAttribute<FSlateColor>::CreateLambda([Colors, Pick, IsDisabled, Dim]() { return FSlateColor(IsDisabled && IsDisabled() ? Dim : Colors[Pick()]); });
		return SNew(STextBlock)
			.Font(Font(Str(J, TEXT("weight"), DefaultWeight).ToLower(), FontSize(J, DefaultSize)))
			.ColorAndOpacity(ColorAttr)
			.ShadowOffset(bShadow ? FVector2D(0.0, 1.0) : FVector2D::ZeroVector)
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, bShadow ? 0.45f : 0.f))
			.Justification(Justify)
			.AutoWrapText(bWrap)
			.Text(TextAttr(Str(J, TEXT("text")), Name, Locals));
	}

	/** Children with repeat expanded: each copy gets its index as a local (default name "i"). */
	TArray<TPair<TSharedPtr<FJsonObject>, FLocals>> Expand(const TSharedPtr<FJsonObject>& J, const FLocals& Locals)
	{
		TArray<TPair<TSharedPtr<FJsonObject>, FLocals>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (!J->TryGetArrayField(TEXT("children"), Children))
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& V : *Children)
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			const TSharedPtr<FJsonValue> R = C->TryGetField(TEXT("repeat"));
			if (!R.IsValid())
			{
				Out.Emplace(C, Locals);
				continue;
			}
			int32 Count = 0;
			if (R->Type == EJson::Number)
			{
				Count = static_cast<int32>(R->AsNumber());
			}
			else
			{
				Count = static_cast<int32>(Eval(Weak, Locals, R->AsString()).N);
				FLogicUiWatch Watch;
				Watch.Expr = R->AsString();
				Watch.Locals = Locals;
				Watch.Count = Count;
				W.UiWatch.Add(MoveTemp(Watch));
			}
			const FString As = Str(C, TEXT("as"), TEXT("i"));
			for (int32 k = 0; k < FMath::Clamp(Count, 0, 500); ++k)
			{
				FLocals L = Locals;
				L.Add(As, FLogicValue::MakeNumber(k));
				Out.Emplace(C, MoveTemp(L));
			}
		}
		return Out;
	}

	TSharedRef<SWidget> Container(const FString& Kind, const TSharedPtr<FJsonObject>& J, const FLocals& Locals)
	{
		const TArray<TPair<TSharedPtr<FJsonObject>, FLocals>> Kids = Expand(J, Locals);
		const float Gap = Num(J, TEXT("gap"), T.Gap);
		const FString Justify = Str(J, TEXT("justify"), TEXT("start")).ToLower();
		if (Kind == TEXT("stack"))
		{
			TSharedRef<SOverlay> O = SNew(SOverlay);
			for (const TPair<TSharedPtr<FJsonObject>, FLocals>& K : Kids)
			{
				O->AddSlot()
					.HAlign(HAlignOf(Str(K.Key, TEXT("h_align")), HAlign_Fill))
					.VAlign(VAlignOf(Str(K.Key, TEXT("v_align")), VAlign_Fill))
					.Padding(Box(K.Key, TEXT("margin"), FMargin(0.f)))
					[Build(K.Key, K.Value, false)];
			}
			return O;
		}
		if (Kind == TEXT("grid"))
		{
			const int32 Cols = FMath::Max(1, static_cast<int32>(Num(J, TEXT("columns"), 4)));
			FVector2D Cell = FVector2D::ZeroVector;
			const TArray<TSharedPtr<FJsonValue>>* CellA = nullptr;
			if (J->TryGetArrayField(TEXT("cell"), CellA) && CellA->Num() == 2)
			{
				Cell = FVector2D((*CellA)[0]->AsNumber(), (*CellA)[1]->AsNumber());
			}
			TSharedRef<SGridPanel> G = SNew(SGridPanel);
			for (int32 i = 0; i < Kids.Num(); ++i)
			{
				const int32 Col = i % Cols, Row = i / Cols;
				TSharedRef<SWidget> Child = Build(Kids[i].Key, Kids[i].Value, false);
				if (!Cell.IsZero())
				{
					Child = SNew(SBox).WidthOverride(Cell.X).HeightOverride(Cell.Y)[Child];
				}
				G->AddSlot(Col, Row).Padding(FMargin(Col > 0 ? Gap : 0.f, Row > 0 ? Gap : 0.f, 0.f, 0.f)).HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Child];
			}
			return G;
		}
		const bool bRow = Kind == TEXT("row");
		const FString Align = Str(J, TEXT("align"), bRow ? TEXT("center") : TEXT("stretch")).ToLower();
		bool bScroll = false;
		J->TryGetBoolField(TEXT("scroll"), bScroll);
		if (bScroll)
		{
			TSharedRef<SScrollBox> S = SNew(SScrollBox);
			for (int32 i = 0; i < Kids.Num(); ++i)
			{
				FMargin Pad = Box(Kids[i].Key, TEXT("margin"), FMargin(0.f));
				Pad.Top += i > 0 ? Gap : 0.f;
				S->AddSlot().Padding(Pad).HAlign(HAlignOf(Str(Kids[i].Key, TEXT("h_align"), Align), HAlign_Fill))[Build(Kids[i].Key, Kids[i].Value, false)];
			}
			return S;
		}
		if (bRow)
		{
			TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
			auto Spacer = [&H]() { H->AddSlot().FillWidth(1.f)[SNew(SSpacer)]; };
			if (Justify == TEXT("end") || Justify == TEXT("center"))
			{
				Spacer();
			}
			for (int32 i = 0; i < Kids.Num(); ++i)
			{
				const TSharedPtr<FJsonObject>& K = Kids[i].Key;
				if (i > 0 && Justify == TEXT("between"))
				{
					Spacer();
				}
				FMargin Pad = Box(K, TEXT("margin"), FMargin(0.f));
				Pad.Left += i > 0 ? Gap : 0.f;
				const double Grow = Num(K, TEXT("grow"), 0.0);
				SHorizontalBox::FScopedWidgetSlotArguments Slot = H->AddSlot();
				if (Grow > 0.0)
				{
					Slot.FillWidth(static_cast<float>(Grow));
				}
				else
				{
					Slot.AutoWidth();
				}
				Slot.HAlign(HAlignOf(Str(K, TEXT("h_align")), HAlign_Fill));
				Slot.VAlign(VAlignOf(Str(K, TEXT("v_align"), Align), VAlign_Center));
				Slot.Padding(Pad);
				Slot[Build(K, Kids[i].Value, true)];
			}
			if (Justify == TEXT("center"))
			{
				Spacer();
			}
			return H;
		}
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		auto Spacer = [&V]() { V->AddSlot().FillHeight(1.f)[SNew(SSpacer)]; };
		if (Justify == TEXT("end") || Justify == TEXT("center"))
		{
			Spacer();
		}
		for (int32 i = 0; i < Kids.Num(); ++i)
		{
			const TSharedPtr<FJsonObject>& K = Kids[i].Key;
			if (i > 0 && Justify == TEXT("between"))
			{
				Spacer();
			}
			FMargin Pad = Box(K, TEXT("margin"), FMargin(0.f));
			Pad.Top += i > 0 ? Gap : 0.f;
			const double Grow = Num(K, TEXT("grow"), 0.0);
			SVerticalBox::FScopedWidgetSlotArguments Slot = V->AddSlot();
			if (Grow > 0.0)
			{
				Slot.FillHeight(static_cast<float>(Grow));
			}
			else
			{
				Slot.AutoHeight();
			}
			Slot.HAlign(HAlignOf(Str(K, TEXT("h_align"), Align), HAlign_Fill));
			Slot.VAlign(VAlignOf(Str(K, TEXT("v_align")), VAlign_Fill));
			Slot.Padding(Pad);
			Slot[Build(K, Kids[i].Value, false)];
		}
		if (Justify == TEXT("center"))
		{
			Spacer();
		}
		return V;
	}

	TSharedRef<SWidget> Build(const TSharedPtr<FJsonObject>& J, const FLocals& Locals, bool bInRow)
	{
		const FString Kind = KindOf(J);
		const FString Name = Str(J, TEXT("name"));
		const bool bPanel = Kind == TEXT("panel");
		const bool bButton = Kind == TEXT("button");
		const bool bBar = Kind == TEXT("bar");
		const FString Variant = Str(J, TEXT("variant"), TEXT("secondary")).ToLower();

		// Defaults per kind, then the node's own style.
		FLook Base;
		Base.Text = T.C(TEXT("text"));
		float DefBorderWidth = 0.f;
		float DefRadius = 0.f;
		FMargin DefPadding(0.f);
		if (bPanel)
		{
			Base.Bg = T.C(TEXT("panel"));
			Base.Border = T.C(TEXT("border"));
			DefBorderWidth = 1.f;
			DefRadius = T.Radius;
			DefPadding = FMargin(T.Padding);
		}
		else if (bButton)
		{
			DefRadius = T.Radius * 0.7f;
			DefPadding = FMargin(16.f, 9.f);
			if (Variant == TEXT("primary"))
			{
				Base.Bg = T.C(TEXT("accent"));
				Base.Text = T.C(TEXT("accent_text"));
			}
			else if (Variant == TEXT("danger") || Variant == TEXT("success"))
			{
				Base.Bg = T.C(*Variant);
				Base.Text = Variant == TEXT("danger") ? T.C(TEXT("white")) : T.C(TEXT("black"));
			}
			else if (Variant == TEXT("secondary"))
			{
				Base.Bg = T.C(TEXT("surface"));
				Base.Border = T.C(TEXT("border"));
				DefBorderWidth = 1.f;
			}
		}
		else if (bBar)
		{
			Base.Bg = T.C(TEXT("black")).CopyWithNewOpacity(0.45f);
		}
		Base.Bg = Color(J, TEXT("background"), Base.Bg);
		Base.Border = Color(J, TEXT("border_color"), Base.Border);
		Base.Text = Color(J, TEXT("color"), Base.Text);
		Base.Opacity = Num(J, TEXT("opacity"), 1.0);
		const float BorderWidth = Num(J, TEXT("border_width"), J->HasField(TEXT("border_color")) ? FMath::Max(DefBorderWidth, 1.f) : DefBorderWidth);
		if (!bPanel && !bButton && !bBar && J->HasField(TEXT("background")))
		{
			DefRadius = T.Radius * 0.7f;
		}
		const float Radius = Num(J, TEXT("radius"), DefRadius);
		const FMargin Padding = Box(J, TEXT("padding"), DefPadding);
		const FLooks Looks = ReadLooks(J, Base);
		const TFunction<int32()> Pick = Picker(Looks, Locals);
		const bool bDynamic = Looks.List.Num() > 1;

		TSharedPtr<SWidget> Content;
		bool bDecorated = false;
		if (IsContainer(Kind))
		{
			Content = Container(Kind == TEXT("panel") ? TEXT("column") : Kind, J, Locals);
		}
		else if (Kind == TEXT("text"))
		{
			Content = Label(J, Name, Locals, Looks, Pick, T.FontSize, TEXT("regular"), ETextJustify::Left);
		}
		else if (Kind == TEXT("spacer"))
		{
			Content = SNew(SSpacer).Size(FVector2D(Num(J, TEXT("width"), 0.0), Num(J, TEXT("height"), 0.0)));
		}
		else if (Kind == TEXT("divider"))
		{
			const float Thick = Num(J, TEXT("thickness"), 1.0);
			const FSlateBrush* Line = Brush(Color(J, TEXT("color"), T.C(TEXT("border"))), 0.f, FLinearColor::Transparent, 0.f);
			Content = bInRow ? SNew(SBox).WidthOverride(Thick)[SNew(SBorder).BorderImage(Line)] : SNew(SBox).HeightOverride(Thick)[SNew(SBorder).BorderImage(Line)];
		}
		else if (bBar)
		{
			const FString Value = Str(J, TEXT("value")), MaxValue = Str(J, TEXT("max"));
			const float Height = Num(J, TEXT("height"), J->HasField(TEXT("text")) ? 22.0 : 10.0);
			const float BarRadius = Num(J, TEXT("radius"), Height * 0.5);
			bool bGhost = true;
			J->TryGetBoolField(TEXT("ghost"), bGhost);
			TSharedRef<SOverlay> O = SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SLogicUiBar)
					.Fraction(TAttribute<float>::CreateLambda([Weak = Weak, Locals, Value, MaxValue]()
					{
						const double M = Eval(Weak, Locals, MaxValue).N;
						return M > 0.0 ? static_cast<float>(Eval(Weak, Locals, Value).N / M) : 0.f;
					}))
					.Track(Brush(Base.Bg, BarRadius, Base.Border, BorderWidth))
					.Fill(Brush(Color(J, TEXT("fill"), T.C(TEXT("accent"))), BarRadius, FLinearColor::Transparent, 0.f))
					.Ghost(bGhost ? Brush(FLinearColor(1.f, 0.9f, 0.7f, 0.75f), BarRadius, FLinearColor::Transparent, 0.f) : nullptr)
					.Desired(FVector2D(Num(J, TEXT("min_width"), 160.0), Height))
				];
			if (J->HasField(TEXT("text")))
			{
				O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[Label(J, Name, Locals, Looks, Pick, SizeNames()[TEXT("sm")], TEXT("medium"), ETextJustify::Center)];
			}
			Content = O;
			bDecorated = true;
		}
		else if (bButton)
		{
			const bool bCustom = J->HasField(TEXT("children"));
			const FString DisabledIf = Str(J, TEXT("disabled_if"));
			TFunction<bool()> IsDisabled;
			if (!DisabledIf.IsEmpty())
			{
				IsDisabled = [Weak = Weak, Locals, DisabledIf]() { return Eval(Weak, Locals, DisabledIf).Truthy(); };
			}
			TSharedRef<SWidget> Inner = bCustom
				? Container(TEXT("column"), J, Locals)
				: StaticCastSharedRef<SWidget>(Label(J, Name, Locals, Looks, Pick, T.FontSize, TEXT("medium"), ETextJustify::Center, IsDisabled));
			// Brushes per look: normal, hovered, pressed.
			TArray<const FSlateBrush*> Brushes;
			const FLinearColor Hover0 = T.C(TEXT("surface_hover"));
			for (const FLook& K : Looks.List)
			{
				const bool bClear = K.Bg.A < 0.05f;
				const FLinearColor Hover = bClear ? Hover0 : FLinearColor::LerpUsingHSV(K.Bg, FLinearColor::White, 0.12f).CopyWithNewOpacity(K.Bg.A);
				const FLinearColor Press = bClear ? T.C(TEXT("surface")) : FLinearColor::LerpUsingHSV(K.Bg, FLinearColor::Black, 0.15f).CopyWithNewOpacity(K.Bg.A);
				Brushes.Add(Brush(K.Bg, Radius, K.Border, BorderWidth));
				Brushes.Add(Brush(Hover, Radius, K.Border.A > 0.01f ? K.Border : FLinearColor::Transparent, BorderWidth));
				Brushes.Add(Brush(Press, Radius, K.Border, BorderWidth));
			}
			const FSlateBrush* DisabledBrush = Brush(T.C(TEXT("surface")), Radius, T.C(TEXT("border")), FMath::Max(BorderWidth, 1.f));
			TSharedRef<TWeakPtr<SButton>> Holder = MakeShared<TWeakPtr<SButton>>();
			TArray<float> Opacities;
			for (const FLook& K : Looks.List)
			{
				Opacities.Add(K.Opacity);
			}
			TSharedPtr<SButton> Btn;
			SAssignNew(Btn, SButton)
				.ButtonStyle(&FlatButtonStyle())
				.ContentPadding(FMargin(0.f))
				.IsFocusable(false)
				.Cursor(EMouseCursor::Hand)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.IsEnabled(!IsDisabled ? TAttribute<bool>(true) : TAttribute<bool>::CreateLambda([IsDisabled]() { return !IsDisabled(); }))
				.OnClicked_Lambda([Weak = Weak, Name, Locals]()
				{
					if (TSharedPtr<FLogicWorld> S = Weak.Pin(); S && S->IsRunning())
					{
						FLogicEvent E;
						E.On = TEXT("button");
						E.Name = Name;
						for (const TPair<FString, FLogicValue>& P : Locals)
						{
							E.Data.M->Add(P.Key, P.Value);
						}
						S->Fire(MoveTemp(E));
					}
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage_Lambda([Holder, Brushes, Pick, DisabledBrush]()
					{
						TSharedPtr<SButton> B = Holder->Pin();
						if (B.IsValid() && !B->IsEnabled())
						{
							return DisabledBrush;
						}
						const int32 State = !B.IsValid() ? 0 : B->IsPressed() ? 2 : B->IsHovered() ? 1 : 0;
						return Brushes[Pick() * 3 + State];
					})
					.ColorAndOpacity_Lambda([Holder, Opacities, Pick]()
					{
						TSharedPtr<SButton> B = Holder->Pin();
						return FLinearColor(1.f, 1.f, 1.f, Opacities[Pick()] * (B.IsValid() && !B->IsEnabled() ? 0.75f : 1.f));
					})
					.BorderBackgroundColor_Lambda([Holder, Opacities, Pick]()
					{
						TSharedPtr<SButton> B = Holder->Pin();
						return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Opacities[Pick()] * (B.IsValid() && !B->IsEnabled() ? 0.75f : 1.f)));
					})
					.Padding(Padding)
					.HAlign(bCustom ? HAlign_Fill : HAlign_Center)
					.VAlign(bCustom ? VAlign_Fill : VAlign_Center)
					[Inner]
				];
			*Holder = Btn;
			Content = Btn;
			bDecorated = true;
		}

		TSharedRef<SWidget> Out = Content.ToSharedRef();
		bool bBlocksClicks = bButton;
		const FString BgTpl = ColorTemplate(J, TEXT("background"));
		const bool bNeedsBox = Base.Bg.A > 0.f || Base.Border.A > 0.f || bDynamic || Padding != FMargin(0.f) || Base.Opacity < 1.f || !BgTpl.IsEmpty();
		if (!bDecorated && bNeedsBox)
		{
			TArray<const FSlateBrush*> Brushes;
			TArray<float> Opacities;
			bool bAnyBg = false;
			for (const FLook& K : Looks.List)
			{
				Brushes.Add(Brush(K.Bg, Radius, K.Border, BorderWidth));
				Opacities.Add(K.Opacity);
				bAnyBg |= K.Bg.A > 0.f;
			}
			bAnyBg |= !BgTpl.IsEmpty();
			bBlocksClicks = bAnyBg;
			TAttribute<const FSlateBrush*> Image = bDynamic ? TAttribute<const FSlateBrush*>::CreateLambda([Brushes, Pick]() { return Brushes[Pick()]; })
				: TAttribute<const FSlateBrush*>(Brushes[0]);
			if (!BgTpl.IsEmpty())
			{
				const FLinearColor BorderC = Base.Border;
				Image = TAttribute<const FSlateBrush*>::CreateLambda([Weak = Weak, Locals, BgTpl, Theme = T, Radius, BorderC, BorderWidth, Fallback = Brushes[0]]()
				{
					TSharedPtr<FLogicWorld> S = Weak.Pin();
					return S ? BrushIn(*S, ResolveColor(Weak, Locals, BgTpl, Theme), Radius, BorderC, BorderWidth) : Fallback;
				});
			}
			TSharedRef<SBorder> B = SNew(SBorder)
				.Padding(Padding)
				.BorderImage(Image)
				[Out];
			if (bDynamic || Base.Opacity < 1.f)
			{
				B->SetColorAndOpacity(TAttribute<FLinearColor>::CreateLambda([Opacities, Pick]() { return FLinearColor(1.f, 1.f, 1.f, Opacities[Pick()]); }));
				B->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([Opacities, Pick]() { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Opacities[Pick()])); }));
			}
			if (bAnyBg)
			{
				// Panels swallow clicks so clicking the UI does not shoot or move in the game behind it.
				B->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }));
			}
			Out = B;
		}
		if (Kind != TEXT("spacer") && Kind != TEXT("divider")
			&& (J->HasField(TEXT("width")) || J->HasField(TEXT("height")) || J->HasField(TEXT("min_width")) || J->HasField(TEXT("min_height"))
				|| J->HasField(TEXT("max_width")) || J->HasField(TEXT("max_height"))))
		{
			auto Opt = [&J](const TCHAR* F) { double V = 0.0; return J->TryGetNumberField(F, V) ? FOptionalSize(V) : FOptionalSize(); };
			Out = SNew(SBox)
				.WidthOverride(Opt(TEXT("width")))
				.HeightOverride(Opt(TEXT("height")))
				.MinDesiredWidth(Opt(TEXT("min_width")))
				.MinDesiredHeight(Opt(TEXT("min_height")))
				.MaxDesiredWidth(Opt(TEXT("max_width")))
				.MaxDesiredHeight(Opt(TEXT("max_height")))
				[Out];
		}
		double Rotate = 0.0;
		if (J->TryGetNumberField(TEXT("rotate"), Rotate) && Rotate != 0.0)
		{
			Out->SetRenderTransformPivot(FVector2D(0.5, 0.5));
			Out->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(static_cast<float>(Rotate)))));
		}
		const FString Tooltip = Str(J, TEXT("tooltip"));
		if (!Tooltip.IsEmpty())
		{
			bBlocksClicks = true;
			TSharedRef<SToolTip> Tip = SNew(SToolTip)
				.BorderImage(Brush(T.C(TEXT("surface")).CopyWithNewOpacity(0.97f), 8.f, T.C(TEXT("border")), 1.f))
				.TextMargin(FMargin(12.f, 8.f))
				[
					SNew(STextBlock)
					.Font(Font(TEXT("regular"), 12.f))
					.ColorAndOpacity(FSlateColor(T.C(TEXT("text"))))
					.Text(TAttribute<FText>::CreateLambda([Weak = Weak, Locals, Tooltip]() { return FText::FromString(Interp(Weak, Locals, Tooltip)); }))
				];
			// No tooltip while its text is empty (e.g. an empty inventory slot).
			Out->SetToolTip(TAttribute<TSharedPtr<IToolTip>>::CreateLambda([Tip, Weak = Weak, Locals, Tooltip]() -> TSharedPtr<IToolTip>
			{
				return Interp(Weak, Locals, Tooltip).TrimStartAndEnd().IsEmpty() ? nullptr : TSharedPtr<IToolTip>(Tip);
			}));
		}
		const EVisibility Shown = bBlocksClicks ? EVisibility::Visible : (IsContainer(Kind) ? EVisibility::SelfHitTestInvisible : EVisibility::HitTestInvisible);
		const FString VisibleIf = Str(J, TEXT("visible_if"));
		if (VisibleIf.IsEmpty() && Name.IsEmpty())
		{
			Out->SetVisibility(Shown);
		}
		else
		{
			Out->SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak = Weak, Locals, VisibleIf, Name, Shown]()
			{
				TSharedPtr<FLogicWorld> S = Weak.Pin();
				if (!S)
				{
					return EVisibility::Collapsed;
				}
				if (const bool* Ov = Name.IsEmpty() ? nullptr : S->VisibleOverrides.Find(Name))
				{
					return *Ov ? Shown : EVisibility::Collapsed;
				}
				return VisibleIf.IsEmpty() || Eval(Weak, Locals, VisibleIf).Truthy() ? Shown : EVisibility::Collapsed;
			}));
		}
		return Out;
	}
};
}

// ---- public ---------------------------------------------------------------------------------------------------------

bool LogicUi::IsLayoutItem(const TSharedPtr<FJsonObject>& Item)
{
	using namespace LogicUiImpl;
	if (!Item.IsValid())
	{
		return false;
	}
	FString Anchor;
	const FString Kind = Str(Item, TEXT("kind")).ToLower();
	return IsContainer(Kind) || Kind == TEXT("spacer") || Kind == TEXT("divider") || Item->HasField(TEXT("children")) || Item->TryGetStringField(TEXT("anchor"), Anchor);
}

bool LogicUi::Validate(const TSharedPtr<FJsonObject>& Item, FString& OutError)
{
	using namespace LogicUiImpl;
	return ValidateNode(Item, FString::Printf(TEXT("ui '%s'"), *Str(Item, TEXT("name"))), true, OutError);
}

bool LogicUi::ValidateTheme(const TSharedPtr<FJsonObject>& Theme, FString& OutError)
{
	using namespace LogicUiImpl;
	static const TArray<FString> Fields = {TEXT("colors"), TEXT("radius"), TEXT("padding"), TEXT("gap"), TEXT("screen_margin"), TEXT("font_size"), TEXT("text_shadow")};
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Theme->Values)
	{
		if (!Fields.Contains(P.Key))
		{
			OutError = FString::Printf(TEXT("theme: unknown field '%s' (fields: %s)"), *P.Key, *FString::Join(Fields, TEXT(", ")));
			return false;
		}
	}
	const TSharedPtr<FJsonObject>* Colors = nullptr;
	if (Theme->HasField(TEXT("colors")))
	{
		if (!Theme->TryGetObjectField(TEXT("colors"), Colors))
		{
			OutError = TEXT("theme.colors is an object {token: color}, e.g. {\"accent\": \"#f5c542\"}");
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Colors)->Values)
		{
			FLinearColor C;
			FString Err;
			FString S;
			if (!ParseColor(P.Value, nullptr, C, Err) || (P.Value->TryGetString(S) && !S.StartsWith(TEXT("#"))))
			{
				OutError = FString::Printf(TEXT("theme.colors.%s: %s"), *P.Key, Err.IsEmpty() ? TEXT("use \"#rrggbb[aa]\" or [r,g,b,a]") : *Err);
				return false;
			}
		}
	}
	return true;
}

bool LogicUi::CheckTokens(const TSharedPtr<FJsonObject>& Item, const TSharedPtr<FJsonObject>& Theme, FString& OutError)
{
	using namespace LogicUiImpl;
	return CheckTokensIn(Item, FString::Printf(TEXT("ui '%s'"), *Str(Item, TEXT("name"))), MakeTheme(Theme), OutError);
}

TSharedPtr<FJsonObject> LogicUi::MergeTheme(const TSharedPtr<FJsonObject>& Current, const TSharedPtr<FJsonObject>& Patch)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (Current.IsValid())
	{
		Out->Values = Current->Values;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Patch->Values)
	{
		const TSharedPtr<FJsonObject>* NewColors = nullptr;
		const TSharedPtr<FJsonObject>* OldColors = nullptr;
		if (P.Key == TEXT("colors") && P.Value->TryGetObject(NewColors) && Out->TryGetObjectField(TEXT("colors"), OldColors))
		{
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->Values = (*OldColors)->Values;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& K : (*NewColors)->Values)
			{
				C->SetField(K.Key, K.Value);
			}
			Out->SetObjectField(TEXT("colors"), C);
		}
		else
		{
			Out->SetField(P.Key, P.Value);
		}
	}
	return Out;
}

void LogicUi::AddRoot(FLogicWorld& World, const FLogicUiItem& Item, const TSharedRef<SConstraintCanvas>& Canvas)
{
	using namespace LogicUiImpl;
	FBuilder B(World);
	TSharedRef<SWidget> Widget = B.Build(Item.Tree, {}, false);
	FVector2D Anchor(0.5, 0.5);
	const TSharedPtr<FJsonValue> A = Item.Tree->TryGetField(TEXT("anchor"));
	FString Name;
	if (A.IsValid() && A->TryGetString(Name))
	{
		Anchor = AnchorNames().FindRef(Name.ToLower());
	}
	else if (A.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = A->AsArray();
		Anchor = FVector2D(Arr[0]->AsNumber(), Arr[1]->AsNumber());
	}
	const float M = B.T.ScreenMargin;
	FVector2D Offset(Anchor.X <= 0.01 ? M : Anchor.X >= 0.99 ? -M : 0.f, Anchor.Y <= 0.01 ? M : Anchor.Y >= 0.99 ? -M : 0.f);
	const TArray<TSharedPtr<FJsonValue>>* O = nullptr;
	if (Item.Tree->TryGetArrayField(TEXT("offset"), O) && O->Num() == 2)
	{
		Offset += FVector2D((*O)[0]->AsNumber(), (*O)[1]->AsNumber());
	}
	Canvas->AddSlot()
		.Anchors(FAnchors(Anchor.X, Anchor.Y))
		.Alignment(Anchor)
		.Offset(FMargin(Offset.X, Offset.Y, 0.f, 0.f))
		.AutoSize(true)
		[Widget];
}

void FLogicWorld::TickUi()
{
	for (const FLogicUiWatch& Watch : UiWatch)
	{
		if (static_cast<int32>(LogicUiImpl::FBuilder::Eval(AsShared(), Watch.Locals, Watch.Expr).N) != Watch.Count)
		{
			RefreshUi();
			return;
		}
	}
}
