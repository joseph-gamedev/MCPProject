#include "LogicExpr.h"
#include "Dom/JsonObject.h"

// ------------------------------------------------------------------ values

FLogicValue FLogicValue::MakeBool(bool V)
{
	FLogicValue R;
	R.Type = EType::Bool;
	R.B = V;
	return R;
}

FLogicValue FLogicValue::MakeNumber(double V)
{
	FLogicValue R;
	R.Type = EType::Number;
	R.N = V;
	return R;
}

FLogicValue FLogicValue::MakeString(const FString& V)
{
	FLogicValue R;
	R.Type = EType::String;
	R.S = V;
	return R;
}

FLogicValue FLogicValue::MakeList(TArray<FLogicValue> V)
{
	FLogicValue R;
	R.Type = EType::List;
	R.L = MakeShared<TArray<FLogicValue>>(MoveTemp(V));
	return R;
}

FLogicValue FLogicValue::MakeMap()
{
	FLogicValue R;
	R.Type = EType::Map;
	R.M = MakeShared<TMap<FString, FLogicValue>>();
	return R;
}

bool FLogicValue::Truthy() const
{
	switch (Type)
	{
	case EType::Bool: return B;
	case EType::Number: return N != 0.0;
	case EType::String: return !S.IsEmpty();
	case EType::List: return L->Num() > 0;
	case EType::Map: return M->Num() > 0;
	default: return false;
	}
}

FString FLogicValue::ToText() const
{
	switch (Type)
	{
	case EType::Bool: return B ? TEXT("true") : TEXT("false");
	case EType::Number:
		return FMath::IsNearlyEqual(N, FMath::RoundToDouble(N)) && FMath::Abs(N) < 1e15 ? FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(N)))
			: FString::SanitizeFloat(N);
	case EType::String: return S;
	case EType::List:
	{
		TArray<FString> Parts;
		for (const FLogicValue& V : *L)
		{
			Parts.Add(V.ToText());
		}
		return TEXT("[") + FString::Join(Parts, TEXT(", ")) + TEXT("]");
	}
	case EType::Map:
	{
		TArray<FString> Parts;
		for (const TPair<FString, FLogicValue>& P : *M)
		{
			Parts.Add(P.Key + TEXT(": ") + P.Value.ToText());
		}
		return TEXT("{") + FString::Join(Parts, TEXT(", ")) + TEXT("}");
	}
	default: return FString();
	}
}

bool FLogicValue::Equals(const FLogicValue& O) const
{
	if (Type != O.Type)
	{
		return false;
	}
	switch (Type)
	{
	case EType::Null: return true;
	case EType::Bool: return B == O.B;
	case EType::Number: return FMath::IsNearlyEqual(N, O.N, 1e-9);
	case EType::String: return S.Equals(O.S, ESearchCase::CaseSensitive);
	case EType::List:
		if (L->Num() != O.L->Num())
		{
			return false;
		}
		for (int32 i = 0; i < L->Num(); ++i)
		{
			if (!(*L)[i].Equals((*O.L)[i]))
			{
				return false;
			}
		}
		return true;
	case EType::Map:
		if (M->Num() != O.M->Num())
		{
			return false;
		}
		for (const TPair<FString, FLogicValue>& P : *M)
		{
			const FLogicValue* Other = O.M->Find(P.Key);
			if (!Other || !P.Value.Equals(*Other))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

FLogicValue FLogicValue::DeepCopy() const
{
	if (Type == EType::List)
	{
		TArray<FLogicValue> Copy;
		for (const FLogicValue& V : *L)
		{
			Copy.Add(V.DeepCopy());
		}
		return MakeList(MoveTemp(Copy));
	}
	if (Type == EType::Map)
	{
		FLogicValue R = MakeMap();
		for (const TPair<FString, FLogicValue>& P : *M)
		{
			R.M->Add(P.Key, P.Value.DeepCopy());
		}
		return R;
	}
	return *this;
}

const TCHAR* FLogicValue::TypeName() const
{
	static const TCHAR* Names[] = {TEXT("null"), TEXT("bool"), TEXT("number"), TEXT("string"), TEXT("list"), TEXT("map")};
	return Names[static_cast<int32>(Type)];
}

TSharedPtr<FJsonValue> FLogicValue::ToJson() const
{
	switch (Type)
	{
	case EType::Bool: return MakeShared<FJsonValueBoolean>(B);
	case EType::Number: return MakeShared<FJsonValueNumber>(N);
	case EType::String: return MakeShared<FJsonValueString>(S);
	case EType::List:
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FLogicValue& V : *L)
		{
			Items.Add(V.ToJson());
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	case EType::Map:
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<FString, FLogicValue>& P : *M)
		{
			O->SetField(P.Key, P.Value.ToJson());
		}
		return MakeShared<FJsonValueObject>(O);
	}
	default: return MakeShared<FJsonValueNull>();
	}
}

FLogicValue FLogicValue::FromJson(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		return FLogicValue();
	}
	switch (Json->Type)
	{
	case EJson::Boolean: return MakeBool(Json->AsBool());
	case EJson::Number: return MakeNumber(Json->AsNumber());
	case EJson::String: return MakeString(Json->AsString());
	case EJson::Array:
	{
		TArray<FLogicValue> Items;
		for (const TSharedPtr<FJsonValue>& V : Json->AsArray())
		{
			Items.Add(FromJson(V));
		}
		return MakeList(MoveTemp(Items));
	}
	case EJson::Object:
	{
		FLogicValue R = MakeMap();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Json->AsObject()->Values)
		{
			R.M->Add(P.Key, FromJson(P.Value));
		}
		return R;
	}
	default: return FLogicValue();
	}
}

// ------------------------------------------------------------------ syntax tree

enum class ELogicNode : uint8
{
	Literal,
	Ident,
	ListLit,
	Unary,
	Binary,
	And,
	Or,
	Ternary,
	Index,
	Member,
	Call
};

struct FLogicNode
{
	ELogicNode Kind = ELogicNode::Literal;
	FLogicValue Value;
	FString Name; // identifier, member, function, or operator
	TArray<TSharedPtr<FLogicNode>> Kids;
};

namespace
{
	struct FToken
	{
		enum EKind
		{
			End,
			Num,
			Str,
			Ident,
			Op
		} Kind = End;
		FString Text;
		double Number = 0.0;
	};

	bool Tokenize(const FString& In, TArray<FToken>& Out, FString& Err)
	{
		int32 i = 0;
		const int32 Len = In.Len();
		while (i < Len)
		{
			const TCHAR C = In[i];
			if (FChar::IsWhitespace(C))
			{
				++i;
				continue;
			}
			FToken T;
			if (FChar::IsDigit(C) || (C == TEXT('.') && i + 1 < Len && FChar::IsDigit(In[i + 1])))
			{
				int32 j = i;
				while (j < Len && (FChar::IsDigit(In[j]) || In[j] == TEXT('.')))
				{
					++j;
				}
				T.Kind = FToken::Num;
				T.Number = FCString::Atod(*In.Mid(i, j - i));
				i = j;
			}
			else if (C == TEXT('"') || C == TEXT('\''))
			{
				int32 j = i + 1;
				FString S;
				while (j < Len && In[j] != C)
				{
					if (In[j] == TEXT('\\') && j + 1 < Len)
					{
						++j;
						S.AppendChar(In[j] == TEXT('n') ? TEXT('\n') : In[j]);
					}
					else
					{
						S.AppendChar(In[j]);
					}
					++j;
				}
				if (j >= Len)
				{
					Err = TEXT("unterminated string");
					return false;
				}
				T.Kind = FToken::Str;
				T.Text = S;
				i = j + 1;
			}
			else if (FChar::IsAlpha(C) || C == TEXT('_'))
			{
				int32 j = i;
				while (j < Len && (FChar::IsAlnum(In[j]) || In[j] == TEXT('_')))
				{
					++j;
				}
				T.Kind = FToken::Ident;
				T.Text = In.Mid(i, j - i);
				i = j;
				// word operators
				if (T.Text == TEXT("and")) { T.Kind = FToken::Op; T.Text = TEXT("&&"); }
				else if (T.Text == TEXT("or")) { T.Kind = FToken::Op; T.Text = TEXT("||"); }
				else if (T.Text == TEXT("not")) { T.Kind = FToken::Op; T.Text = TEXT("!"); }
			}
			else
			{
				static const TCHAR* Two[] = {TEXT("=="), TEXT("!="), TEXT("<="), TEXT(">="), TEXT("&&"), TEXT("||")};
				bool bFound = false;
				if (i + 1 < Len)
				{
					const FString Pair = In.Mid(i, 2);
					for (const TCHAR* Op : Two)
					{
						if (Pair == Op)
						{
							T.Kind = FToken::Op;
							T.Text = Pair;
							i += 2;
							bFound = true;
							break;
						}
					}
				}
				if (!bFound)
				{
					if (FCString::Strchr(TEXT("+-*/%()[],.?:<>!"), C) == nullptr)
					{
						Err = FString::Printf(TEXT("unexpected character '%c' (use == to compare)"), C);
						return false;
					}
					T.Kind = FToken::Op;
					T.Text = FString(1, &C);
					++i;
				}
			}
			Out.Add(T);
		}
		Out.Add(FToken());
		return true;
	}

	class FParser
	{
	public:
		FParser(TArray<FToken>&& InTokens) : Tokens(MoveTemp(InTokens)) {}
		FString Err;

		TSharedPtr<FLogicNode> ParseAll()
		{
			TSharedPtr<FLogicNode> N = Expr();
			if (N && Peek().Kind != FToken::End)
			{
				Fail(FString::Printf(TEXT("unexpected '%s'"), *Describe(Peek())));
			}
			return Err.IsEmpty() ? N : nullptr;
		}

	private:
		TArray<FToken> Tokens;
		int32 Pos = 0;

		const FToken& Peek() const { return Tokens[Pos]; }
		bool IsOp(const TCHAR* Op) const { return Peek().Kind == FToken::Op && Peek().Text == Op; }
		bool Accept(const TCHAR* Op)
		{
			if (IsOp(Op))
			{
				++Pos;
				return true;
			}
			return false;
		}
		static FString Describe(const FToken& T) { return T.Kind == FToken::End ? TEXT("end") : T.Kind == FToken::Num ? FString::SanitizeFloat(T.Number) : T.Text; }
		TSharedPtr<FLogicNode> Fail(const FString& Message)
		{
			if (Err.IsEmpty())
			{
				Err = Message;
			}
			return nullptr;
		}
		static TSharedPtr<FLogicNode> Make(ELogicNode Kind, const FString& Name, TArray<TSharedPtr<FLogicNode>> Kids)
		{
			TSharedPtr<FLogicNode> N = MakeShared<FLogicNode>();
			N->Kind = Kind;
			N->Name = Name;
			N->Kids = MoveTemp(Kids);
			return N;
		}

		TSharedPtr<FLogicNode> Expr()
		{
			TSharedPtr<FLogicNode> C = OrExpr();
			if (C && Accept(TEXT("?")))
			{
				TSharedPtr<FLogicNode> A = Expr();
				if (!Accept(TEXT(":")))
				{
					return Fail(TEXT("expected ':' in a ? b : c"));
				}
				TSharedPtr<FLogicNode> B = Expr();
				return A && B ? Make(ELogicNode::Ternary, FString(), {C, A, B}) : nullptr;
			}
			return C;
		}

		TSharedPtr<FLogicNode> OrExpr()
		{
			TSharedPtr<FLogicNode> L = AndExpr();
			while (L && Accept(TEXT("||")))
			{
				TSharedPtr<FLogicNode> R = AndExpr();
				L = R ? Make(ELogicNode::Or, FString(), {L, R}) : nullptr;
			}
			return L;
		}

		TSharedPtr<FLogicNode> AndExpr()
		{
			TSharedPtr<FLogicNode> L = Equality();
			while (L && Accept(TEXT("&&")))
			{
				TSharedPtr<FLogicNode> R = Equality();
				L = R ? Make(ELogicNode::And, FString(), {L, R}) : nullptr;
			}
			return L;
		}

		TSharedPtr<FLogicNode> Binary(TFunction<TSharedPtr<FLogicNode>()> Next, std::initializer_list<const TCHAR*> Ops)
		{
			TSharedPtr<FLogicNode> L = Next();
			while (L)
			{
				const TCHAR* Found = nullptr;
				for (const TCHAR* Op : Ops)
				{
					if (IsOp(Op))
					{
						Found = Op;
						break;
					}
				}
				if (!Found)
				{
					break;
				}
				++Pos;
				TSharedPtr<FLogicNode> R = Next();
				L = R ? Make(ELogicNode::Binary, Found, {L, R}) : nullptr;
			}
			return L;
		}

		TSharedPtr<FLogicNode> Equality() { return Binary([this]() { return Compare(); }, {TEXT("=="), TEXT("!=")}); }
		TSharedPtr<FLogicNode> Compare() { return Binary([this]() { return Additive(); }, {TEXT("<="), TEXT(">="), TEXT("<"), TEXT(">")}); }
		TSharedPtr<FLogicNode> Additive() { return Binary([this]() { return Multiplicative(); }, {TEXT("+"), TEXT("-")}); }
		TSharedPtr<FLogicNode> Multiplicative() { return Binary([this]() { return Unary(); }, {TEXT("*"), TEXT("/"), TEXT("%")}); }

		TSharedPtr<FLogicNode> Unary()
		{
			if (Accept(TEXT("!")))
			{
				TSharedPtr<FLogicNode> X = Unary();
				return X ? Make(ELogicNode::Unary, TEXT("!"), {X}) : nullptr;
			}
			if (Accept(TEXT("-")))
			{
				TSharedPtr<FLogicNode> X = Unary();
				return X ? Make(ELogicNode::Unary, TEXT("-"), {X}) : nullptr;
			}
			return Postfix();
		}

		TSharedPtr<FLogicNode> Postfix()
		{
			TSharedPtr<FLogicNode> N = Primary();
			while (N)
			{
				if (Accept(TEXT("[")))
				{
					TSharedPtr<FLogicNode> I = Expr();
					if (!Accept(TEXT("]")))
					{
						return Fail(TEXT("expected ']'"));
					}
					N = I ? Make(ELogicNode::Index, FString(), {N, I}) : nullptr;
				}
				else if (Accept(TEXT(".")))
				{
					if (Peek().Kind != FToken::Ident)
					{
						return Fail(TEXT("expected a field name after '.'"));
					}
					N = Make(ELogicNode::Member, Tokens[Pos++].Text, {N});
				}
				else
				{
					break;
				}
			}
			return N;
		}

		TSharedPtr<FLogicNode> Primary()
		{
			const FToken T = Peek();
			if (T.Kind == FToken::Num)
			{
				++Pos;
				TSharedPtr<FLogicNode> N = Make(ELogicNode::Literal, FString(), {});
				N->Value = FLogicValue::MakeNumber(T.Number);
				return N;
			}
			if (T.Kind == FToken::Str)
			{
				++Pos;
				TSharedPtr<FLogicNode> N = Make(ELogicNode::Literal, FString(), {});
				N->Value = FLogicValue::MakeString(T.Text);
				return N;
			}
			if (T.Kind == FToken::Ident)
			{
				++Pos;
				if (T.Text == TEXT("true") || T.Text == TEXT("false") || T.Text == TEXT("null"))
				{
					TSharedPtr<FLogicNode> N = Make(ELogicNode::Literal, FString(), {});
					N->Value = T.Text == TEXT("null") ? FLogicValue() : FLogicValue::MakeBool(T.Text == TEXT("true"));
					return N;
				}
				if (Accept(TEXT("(")))
				{
					TArray<TSharedPtr<FLogicNode>> Args;
					if (!Accept(TEXT(")")))
					{
						do
						{
							TSharedPtr<FLogicNode> A = Expr();
							if (!A)
							{
								return nullptr;
							}
							Args.Add(A);
						} while (Accept(TEXT(",")));
						if (!Accept(TEXT(")")))
						{
							return Fail(FString::Printf(TEXT("expected ')' after arguments of %s"), *T.Text));
						}
					}
					if (!LogicExpr::FunctionNames().Contains(T.Text))
					{
						return Fail(FString::Printf(TEXT("unknown function '%s'"), *T.Text));
					}
					return Make(ELogicNode::Call, T.Text, MoveTemp(Args));
				}
				return Make(ELogicNode::Ident, T.Text, {});
			}
			if (Accept(TEXT("(")))
			{
				TSharedPtr<FLogicNode> N = Expr();
				if (!Accept(TEXT(")")))
				{
					return Fail(TEXT("expected ')'"));
				}
				return N;
			}
			if (Accept(TEXT("[")))
			{
				TArray<TSharedPtr<FLogicNode>> Items;
				if (!Accept(TEXT("]")))
				{
					do
					{
						TSharedPtr<FLogicNode> A = Expr();
						if (!A)
						{
							return nullptr;
						}
						Items.Add(A);
					} while (Accept(TEXT(",")));
					if (!Accept(TEXT("]")))
					{
						return Fail(TEXT("expected ']' to close the list"));
					}
				}
				return Make(ELogicNode::ListLit, FString(), MoveTemp(Items));
			}
			return Fail(FString::Printf(TEXT("unexpected '%s'"), *Describe(T)));
		}
	};

	FLogicValue FailExpr(FLogicScope& Scope, const FString& Message)
	{
		if (Scope.Error.IsEmpty())
		{
			Scope.Error = Message;
		}
		return FLogicValue();
	}

	bool IsEmptyCell(const FLogicValue& V, const FLogicValue& Empty)
	{
		return V.IsNull() || V.Equals(Empty);
	}

	FLogicValue CallFunction(const FString& Name, TArray<FLogicValue>& A, FLogicScope& Scope)
	{
		auto Need = [&](int32 Min, int32 Max) -> bool
		{
			if (A.Num() < Min || A.Num() > Max)
			{
				FailExpr(Scope, FString::Printf(TEXT("%s takes %d-%d arguments, got %d"), *Name, Min, Max, A.Num()));
				return false;
			}
			return true;
		};
		auto NumArg = [&](int32 i, double& Out) -> bool
		{
			if (A[i].Type != FLogicValue::EType::Number)
			{
				FailExpr(Scope, FString::Printf(TEXT("%s: argument %d must be a number, got %s"), *Name, i + 1, A[i].TypeName()));
				return false;
			}
			Out = A[i].N;
			return true;
		};
		auto ListArg = [&](int32 i) -> const TArray<FLogicValue>*
		{
			if (A[i].Type != FLogicValue::EType::List)
			{
				FailExpr(Scope, FString::Printf(TEXT("%s: argument %d must be a list, got %s"), *Name, i + 1, A[i].TypeName()));
				return nullptr;
			}
			return A[i].L.Get();
		};
		FRandomStream Fallback(FPlatformTime::Cycles());
		FRandomStream& Rand = Scope.Random ? *Scope.Random : Fallback;

		if (Name == TEXT("len"))
		{
			if (!Need(1, 1)) return {};
			switch (A[0].Type)
			{
			case FLogicValue::EType::List: return FLogicValue::MakeNumber(A[0].L->Num());
			case FLogicValue::EType::Map: return FLogicValue::MakeNumber(A[0].M->Num());
			case FLogicValue::EType::String: return FLogicValue::MakeNumber(A[0].S.Len());
			default: return FLogicValue::MakeNumber(0);
			}
		}
		if (Name == TEXT("abs") || Name == TEXT("floor") || Name == TEXT("ceil") || Name == TEXT("round") || Name == TEXT("sqrt"))
		{
			double X;
			if (!Need(1, 1) || !NumArg(0, X)) return {};
			const double R = Name == TEXT("abs") ? FMath::Abs(X) : Name == TEXT("floor") ? FMath::FloorToDouble(X) : Name == TEXT("ceil") ? FMath::CeilToDouble(X)
				: Name == TEXT("round") ? FMath::RoundToDouble(X) : FMath::Sqrt(FMath::Max(0.0, X));
			return FLogicValue::MakeNumber(R);
		}
		if (Name == TEXT("min") || Name == TEXT("max"))
		{
			TArray<FLogicValue> Items = A;
			if (A.Num() == 1 && A[0].Type == FLogicValue::EType::List)
			{
				Items = *A[0].L;
			}
			if (Items.Num() == 0) return {};
			double Best = 0;
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				if (Items[i].Type != FLogicValue::EType::Number)
				{
					return FailExpr(Scope, Name + TEXT(": numbers only"));
				}
				Best = i == 0 ? Items[i].N : (Name == TEXT("min") ? FMath::Min(Best, Items[i].N) : FMath::Max(Best, Items[i].N));
			}
			return FLogicValue::MakeNumber(Best);
		}
		if (Name == TEXT("rand"))
		{
			if (!Need(0, 0)) return {};
			return FLogicValue::MakeNumber(Rand.FRand());
		}
		if (Name == TEXT("rand_int"))
		{
			double Lo, Hi;
			if (!Need(2, 2) || !NumArg(0, Lo) || !NumArg(1, Hi)) return {};
			return FLogicValue::MakeNumber(Rand.RandRange(static_cast<int32>(Lo), static_cast<int32>(Hi)));
		}
		if (Name == TEXT("str"))
		{
			if (!Need(1, 1)) return {};
			return FLogicValue::MakeString(A[0].ToText());
		}
		if (Name == TEXT("num"))
		{
			if (!Need(1, 1)) return {};
			if (A[0].Type == FLogicValue::EType::Number) return A[0];
			if (A[0].Type == FLogicValue::EType::Bool) return FLogicValue::MakeNumber(A[0].B ? 1 : 0);
			return FLogicValue::MakeNumber(FCString::Atod(*A[0].ToText()));
		}
		if (Name == TEXT("list"))
		{
			double Count;
			if (!Need(1, 2) || !NumArg(0, Count)) return {};
			TArray<FLogicValue> Items;
			for (int32 i = 0; i < FMath::Clamp(static_cast<int32>(Count), 0, 100000); ++i)
			{
				Items.Add(A.Num() > 1 ? A[1].DeepCopy() : FLogicValue());
			}
			return FLogicValue::MakeList(MoveTemp(Items));
		}
		if (Name == TEXT("range"))
		{
			double Count;
			if (!Need(1, 1) || !NumArg(0, Count)) return {};
			TArray<FLogicValue> Items;
			for (int32 i = 0; i < FMath::Clamp(static_cast<int32>(Count), 0, 100000); ++i)
			{
				Items.Add(FLogicValue::MakeNumber(i));
			}
			return FLogicValue::MakeList(MoveTemp(Items));
		}
		if (Name == TEXT("contains") || Name == TEXT("index_of") || Name == TEXT("count") || Name == TEXT("indices"))
		{
			if (!Need(2, 2)) return {};
			if (A[0].Type == FLogicValue::EType::String && Name == TEXT("contains"))
			{
				return FLogicValue::MakeBool(A[0].S.Contains(A[1].ToText()));
			}
			if (A[0].Type == FLogicValue::EType::Map && Name == TEXT("contains"))
			{
				return FLogicValue::MakeBool(A[0].M->Contains(A[1].ToText()));
			}
			const TArray<FLogicValue>* Items = ListArg(0);
			if (!Items) return {};
			int32 Count = 0, First = -1;
			TArray<FLogicValue> Found;
			for (int32 i = 0; i < Items->Num(); ++i)
			{
				if ((*Items)[i].Equals(A[1]))
				{
					++Count;
					First = First < 0 ? i : First;
					Found.Add(FLogicValue::MakeNumber(i));
				}
			}
			if (Name == TEXT("contains")) return FLogicValue::MakeBool(Count > 0);
			if (Name == TEXT("index_of")) return FLogicValue::MakeNumber(First);
			if (Name == TEXT("count")) return FLogicValue::MakeNumber(Count);
			return FLogicValue::MakeList(MoveTemp(Found));
		}
		if (Name == TEXT("pick"))
		{
			if (!Need(1, 1)) return {};
			const TArray<FLogicValue>* Items = ListArg(0);
			if (!Items || Items->Num() == 0) return {};
			return (*Items)[Rand.RandRange(0, Items->Num() - 1)];
		}
		if (Name == TEXT("sum"))
		{
			if (!Need(1, 1)) return {};
			const TArray<FLogicValue>* Items = ListArg(0);
			if (!Items) return {};
			double S = 0;
			for (const FLogicValue& V : *Items)
			{
				S += V.Type == FLogicValue::EType::Number ? V.N : 0.0;
			}
			return FLogicValue::MakeNumber(S);
		}
		if (Name == TEXT("join"))
		{
			if (!Need(1, 2)) return {};
			const TArray<FLogicValue>* Items = ListArg(0);
			if (!Items) return {};
			TArray<FString> Parts;
			for (const FLogicValue& V : *Items)
			{
				Parts.Add(V.ToText());
			}
			return FLogicValue::MakeString(FString::Join(Parts, A.Num() > 1 ? *A[1].ToText() : TEXT("")));
		}
		if (Name == TEXT("set"))
		{
			// set(list, index, value) / set(map, key, value): a modified copy.
			if (!Need(3, 3)) return {};
			FLogicValue Copy = A[0].DeepCopy();
			if (Copy.Type == FLogicValue::EType::List)
			{
				double I;
				if (!NumArg(1, I)) return {};
				const int32 Index = static_cast<int32>(I);
				if (!Copy.L->IsValidIndex(Index)) return FailExpr(Scope, FString::Printf(TEXT("set: index %d out of range"), Index));
				(*Copy.L)[Index] = A[2].DeepCopy();
				return Copy;
			}
			if (Copy.Type == FLogicValue::EType::Map)
			{
				Copy.M->Add(A[1].ToText(), A[2].DeepCopy());
				return Copy;
			}
			return FailExpr(Scope, TEXT("set: first argument must be a list or map"));
		}
		if (Name == TEXT("keys"))
		{
			if (!Need(1, 1)) return {};
			if (A[0].Type != FLogicValue::EType::Map) return FailExpr(Scope, TEXT("keys: needs a map"));
			TArray<FLogicValue> Items;
			for (const TPair<FString, FLogicValue>& P : *A[0].M)
			{
				Items.Add(FLogicValue::MakeString(P.Key));
			}
			return FLogicValue::MakeList(MoveTemp(Items));
		}
		if (Name == TEXT("upper") || Name == TEXT("lower"))
		{
			if (!Need(1, 1)) return {};
			return FLogicValue::MakeString(Name == TEXT("upper") ? A[0].ToText().ToUpper() : A[0].ToText().ToLower());
		}
		if (Name == TEXT("grid_lines"))
		{
			// Every straight run of k cells (rows, columns, both diagonals) on a rows x cols board indexed row*cols+col.
			double Rows, Cols, K;
			if (!Need(3, 3) || !NumArg(0, Rows) || !NumArg(1, Cols) || !NumArg(2, K)) return {};
			const int32 R = static_cast<int32>(Rows), C = static_cast<int32>(Cols), Kk = static_cast<int32>(K);
			TArray<FLogicValue> Lines;
			const int32 Dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
			for (int32 r = 0; r < R; ++r)
			{
				for (int32 c = 0; c < C; ++c)
				{
					for (const auto& D : Dirs)
					{
						const int32 Er = r + D[0] * (Kk - 1), Ec = c + D[1] * (Kk - 1);
						if (Er < 0 || Er >= R || Ec < 0 || Ec >= C)
						{
							continue;
						}
						TArray<FLogicValue> Line;
						for (int32 s = 0; s < Kk; ++s)
						{
							Line.Add(FLogicValue::MakeNumber((r + D[0] * s) * C + (c + D[1] * s)));
						}
						Lines.Add(FLogicValue::MakeList(MoveTemp(Line)));
					}
				}
			}
			return FLogicValue::MakeList(MoveTemp(Lines));
		}
		if (Name == TEXT("line_winner") || Name == TEXT("winning_line") || Name == TEXT("winning_move"))
		{
			// line_winner(board, lines, empty?) -> owner of a complete line or null
			// winning_line(board, lines, empty?) -> the indices of that line or []
			// winning_move(board, lines, player, empty?) -> empty index completing a line for player, or -1
			const bool bMove = Name == TEXT("winning_move");
			if (!Need(bMove ? 3 : 2, bMove ? 4 : 3)) return {};
			const TArray<FLogicValue>* Board = ListArg(0);
			const TArray<FLogicValue>* Lines = ListArg(1);
			if (!Board || !Lines) return {};
			const FLogicValue Empty = A.Num() > (bMove ? 3 : 2) ? A[bMove ? 3 : 2] : FLogicValue::MakeString(FString());
			for (const FLogicValue& Line : *Lines)
			{
				if (Line.Type != FLogicValue::EType::List || Line.L->Num() == 0)
				{
					continue;
				}
				TArray<int32> Idx;
				for (const FLogicValue& I : *Line.L)
				{
					const int32 Index = static_cast<int32>(I.N);
					if (!Board->IsValidIndex(Index))
					{
						return FailExpr(Scope, FString::Printf(TEXT("%s: line index %d outside the board"), *Name, Index));
					}
					Idx.Add(Index);
				}
				if (bMove)
				{
					int32 Mine = 0, EmptyAt = -1, Empties = 0;
					for (const int32 I : Idx)
					{
						if ((*Board)[I].Equals(A[2])) ++Mine;
						else if (IsEmptyCell((*Board)[I], Empty)) { ++Empties; EmptyAt = I; }
					}
					if (Mine == Idx.Num() - 1 && Empties == 1)
					{
						return FLogicValue::MakeNumber(EmptyAt);
					}
					continue;
				}
				const FLogicValue& First = (*Board)[Idx[0]];
				if (IsEmptyCell(First, Empty))
				{
					continue;
				}
				bool bAll = true;
				for (const int32 I : Idx)
				{
					bAll &= (*Board)[I].Equals(First);
				}
				if (bAll)
				{
					return Name == TEXT("line_winner") ? First : Line.DeepCopy();
				}
			}
			return bMove ? FLogicValue::MakeNumber(-1) : (Name == TEXT("line_winner") ? FLogicValue() : FLogicValue::MakeList());
		}
		return FailExpr(Scope, FString::Printf(TEXT("unknown function '%s'"), *Name));
	}

	FLogicValue Arith(const FString& Op, const FLogicValue& L, const FLogicValue& R, FLogicScope& Scope)
	{
		if (Op == TEXT("=="))
		{
			return FLogicValue::MakeBool(L.Equals(R));
		}
		if (Op == TEXT("!="))
		{
			return FLogicValue::MakeBool(!L.Equals(R));
		}
		if (Op == TEXT("+"))
		{
			if (L.Type == FLogicValue::EType::String || R.Type == FLogicValue::EType::String)
			{
				return FLogicValue::MakeString(L.ToText() + R.ToText());
			}
			if (L.Type == FLogicValue::EType::List && R.Type == FLogicValue::EType::List)
			{
				TArray<FLogicValue> Items = *L.DeepCopy().L;
				for (const FLogicValue& V : *R.L)
				{
					Items.Add(V.DeepCopy());
				}
				return FLogicValue::MakeList(MoveTemp(Items));
			}
		}
		if (L.Type == FLogicValue::EType::String && R.Type == FLogicValue::EType::String)
		{
			const int32 C = L.S.Compare(R.S);
			if (Op == TEXT("<")) return FLogicValue::MakeBool(C < 0);
			if (Op == TEXT("<=")) return FLogicValue::MakeBool(C <= 0);
			if (Op == TEXT(">")) return FLogicValue::MakeBool(C > 0);
			if (Op == TEXT(">=")) return FLogicValue::MakeBool(C >= 0);
		}
		if (L.Type != FLogicValue::EType::Number || R.Type != FLogicValue::EType::Number)
		{
			return FailExpr(Scope, FString::Printf(TEXT("cannot apply '%s' to %s and %s"), *Op, L.TypeName(), R.TypeName()));
		}
		const double A = L.N, B = R.N;
		if (Op == TEXT("+")) return FLogicValue::MakeNumber(A + B);
		if (Op == TEXT("-")) return FLogicValue::MakeNumber(A - B);
		if (Op == TEXT("*")) return FLogicValue::MakeNumber(A * B);
		if (Op == TEXT("/") || Op == TEXT("%"))
		{
			if (B == 0.0)
			{
				return FailExpr(Scope, TEXT("division by zero"));
			}
			return FLogicValue::MakeNumber(Op == TEXT("/") ? A / B : FMath::Fmod(A, B));
		}
		if (Op == TEXT("<")) return FLogicValue::MakeBool(A < B);
		if (Op == TEXT("<=")) return FLogicValue::MakeBool(A <= B);
		if (Op == TEXT(">")) return FLogicValue::MakeBool(A > B);
		if (Op == TEXT(">=")) return FLogicValue::MakeBool(A >= B);
		return FailExpr(Scope, TEXT("unknown operator ") + Op);
	}

	struct FCached
	{
		TSharedPtr<const FLogicNode> Node;
		FString Error;
	};

	TMap<FString, FCached>& Cache()
	{
		static TMap<FString, FCached> Map;
		return Map;
	}
}

namespace LogicExpr
{
	const TArray<FString>& FunctionNames()
	{
		static const TArray<FString> Names = {TEXT("len"), TEXT("abs"), TEXT("floor"), TEXT("ceil"), TEXT("round"), TEXT("sqrt"), TEXT("min"), TEXT("max"),
			TEXT("rand"), TEXT("rand_int"), TEXT("str"), TEXT("num"), TEXT("list"), TEXT("range"), TEXT("contains"), TEXT("index_of"), TEXT("count"),
			TEXT("indices"), TEXT("pick"), TEXT("sum"), TEXT("join"), TEXT("set"), TEXT("keys"), TEXT("upper"), TEXT("lower"), TEXT("grid_lines"),
			TEXT("line_winner"), TEXT("winning_line"), TEXT("winning_move")};
		return Names;
	}

	TSharedPtr<const FLogicNode> Parse(const FString& Text, FString& OutError)
	{
		check(IsInGameThread());
		if (const FCached* Hit = Cache().Find(Text))
		{
			OutError = Hit->Error;
			return Hit->Node;
		}
		TArray<FToken> Tokens;
		FCached Entry;
		if (Tokenize(Text, Tokens, Entry.Error))
		{
			if (Tokens.Num() == 1)
			{
				Entry.Error = TEXT("empty expression");
			}
			else
			{
				FParser P(MoveTemp(Tokens));
				Entry.Node = P.ParseAll();
				Entry.Error = P.Err;
			}
		}
		if (Cache().Num() > 4096)
		{
			Cache().Reset();
		}
		Cache().Add(Text, Entry);
		OutError = Entry.Error;
		return Entry.Node;
	}

	FLogicValue Eval(const FLogicNode& N, FLogicScope& Scope)
	{
		if (!Scope.Error.IsEmpty())
		{
			return {};
		}
		switch (N.Kind)
		{
		case ELogicNode::Literal: return N.Value;
		case ELogicNode::Ident:
		{
			const FLogicValue* V = Scope.Lookup ? Scope.Lookup(N.Name) : nullptr;
			return V ? *V : FailExpr(Scope, FString::Printf(TEXT("unknown variable '%s'"), *N.Name));
		}
		case ELogicNode::ListLit:
		{
			TArray<FLogicValue> Items;
			for (const TSharedPtr<FLogicNode>& K : N.Kids)
			{
				Items.Add(Eval(*K, Scope));
			}
			return FLogicValue::MakeList(MoveTemp(Items));
		}
		case ELogicNode::Unary:
		{
			const FLogicValue X = Eval(*N.Kids[0], Scope);
			if (N.Name == TEXT("!"))
			{
				return FLogicValue::MakeBool(!X.Truthy());
			}
			if (X.Type != FLogicValue::EType::Number)
			{
				return FailExpr(Scope, FString::Printf(TEXT("cannot negate a %s"), X.TypeName()));
			}
			return FLogicValue::MakeNumber(-X.N);
		}
		case ELogicNode::And:
		{
			const FLogicValue L = Eval(*N.Kids[0], Scope);
			return L.Truthy() ? FLogicValue::MakeBool(Eval(*N.Kids[1], Scope).Truthy()) : FLogicValue::MakeBool(false);
		}
		case ELogicNode::Or:
		{
			const FLogicValue L = Eval(*N.Kids[0], Scope);
			return L.Truthy() ? FLogicValue::MakeBool(true) : FLogicValue::MakeBool(Eval(*N.Kids[1], Scope).Truthy());
		}
		case ELogicNode::Ternary: return Eval(*N.Kids[0], Scope).Truthy() ? Eval(*N.Kids[1], Scope) : Eval(*N.Kids[2], Scope);
		case ELogicNode::Binary:
		{
			const FLogicValue L = Eval(*N.Kids[0], Scope);
			const FLogicValue R = Eval(*N.Kids[1], Scope);
			return Scope.Error.IsEmpty() ? Arith(N.Name, L, R, Scope) : FLogicValue();
		}
		case ELogicNode::Index:
		{
			const FLogicValue Base = Eval(*N.Kids[0], Scope);
			const FLogicValue I = Eval(*N.Kids[1], Scope);
			if (Base.Type == FLogicValue::EType::List)
			{
				if (I.Type != FLogicValue::EType::Number)
				{
					return FailExpr(Scope, FString::Printf(TEXT("list index must be a number, got %s"), I.TypeName()));
				}
				int32 Index = static_cast<int32>(I.N);
				Index = Index < 0 ? Base.L->Num() + Index : Index;
				return Base.L->IsValidIndex(Index) ? (*Base.L)[Index] : FLogicValue();
			}
			if (Base.Type == FLogicValue::EType::Map)
			{
				const FLogicValue* V = Base.M->Find(I.ToText());
				return V ? *V : FLogicValue();
			}
			if (Base.Type == FLogicValue::EType::String && I.Type == FLogicValue::EType::Number)
			{
				const int32 Index = static_cast<int32>(I.N);
				return Base.S.IsValidIndex(Index) ? FLogicValue::MakeString(Base.S.Mid(Index, 1)) : FLogicValue();
			}
			return FailExpr(Scope, FString::Printf(TEXT("cannot index a %s"), Base.TypeName()));
		}
		case ELogicNode::Member:
		{
			const FLogicValue Base = Eval(*N.Kids[0], Scope);
			if (Base.Type != FLogicValue::EType::Map)
			{
				return FailExpr(Scope, FString::Printf(TEXT("'.%s' needs a map, got %s"), *N.Name, Base.TypeName()));
			}
			const FLogicValue* V = Base.M->Find(N.Name);
			return V ? *V : FLogicValue();
		}
		case ELogicNode::Call:
		{
			TArray<FLogicValue> Args;
			for (const TSharedPtr<FLogicNode>& K : N.Kids)
			{
				Args.Add(Eval(*K, Scope));
			}
			return Scope.Error.IsEmpty() ? CallFunction(N.Name, Args, Scope) : FLogicValue();
		}
		}
		return {};
	}

	FLogicValue Evaluate(const FString& Text, FLogicScope& Scope)
	{
		FString Err;
		TSharedPtr<const FLogicNode> Node = Parse(Text, Err);
		if (!Node)
		{
			return FailExpr(Scope, FString::Printf(TEXT("syntax error in '%s': %s"), *Text, *Err));
		}
		return Eval(*Node, Scope);
	}

	static bool ForEachSegment(const FString& Text, TFunctionRef<void(const FString&)> OnText, TFunctionRef<bool(const FString&)> OnExpr, FString& OutError)
	{
		int32 i = 0;
		FString Plain;
		while (i < Text.Len())
		{
			const TCHAR C = Text[i];
			if (C == TEXT('{') && i + 1 < Text.Len() && Text[i + 1] == TEXT('{'))
			{
				Plain.AppendChar(TEXT('{'));
				i += 2;
				continue;
			}
			if (C == TEXT('}') && i + 1 < Text.Len() && Text[i + 1] == TEXT('}'))
			{
				Plain.AppendChar(TEXT('}'));
				i += 2;
				continue;
			}
			if (C == TEXT('{'))
			{
				// find the matching brace, skipping strings
				int32 Depth = 1, j = i + 1;
				TCHAR Quote = 0;
				while (j < Text.Len() && Depth > 0)
				{
					const TCHAR D = Text[j];
					if (Quote)
					{
						Quote = D == Quote ? 0 : Quote;
					}
					else if (D == TEXT('"') || D == TEXT('\''))
					{
						Quote = D;
					}
					else if (D == TEXT('{'))
					{
						++Depth;
					}
					else if (D == TEXT('}'))
					{
						--Depth;
					}
					++j;
				}
				if (Depth != 0)
				{
					OutError = TEXT("unclosed '{' in text (use {{ for a literal brace)");
					return false;
				}
				OnText(Plain);
				Plain.Reset();
				if (!OnExpr(Text.Mid(i + 1, j - i - 2)))
				{
					return false;
				}
				i = j;
				continue;
			}
			Plain.AppendChar(C);
			++i;
		}
		OnText(Plain);
		return true;
	}

	FString Interpolate(const FString& Text, FLogicScope& Scope)
	{
		if (!Text.Contains(TEXT("{")) && !Text.Contains(TEXT("}")))
		{
			return Text;
		}
		FString Out, Err;
		ForEachSegment(Text, [&Out](const FString& S) { Out += S; },
			[&Out, &Scope](const FString& Expr)
			{
				Out += Evaluate(Expr, Scope).ToText();
				return true;
			},
			Err);
		if (!Err.IsEmpty() && Scope.Error.IsEmpty())
		{
			Scope.Error = Err;
		}
		return Out;
	}

	bool CheckTemplate(const FString& Text, FString& OutError)
	{
		return ForEachSegment(Text, [](const FString&) {},
			[&OutError](const FString& Expr)
			{
				FString Err;
				if (!Parse(Expr, Err))
				{
					OutError = FString::Printf(TEXT("'{%s}': %s"), *Expr, *Err);
					return false;
				}
				return true;
			},
			OutError);
	}
}
