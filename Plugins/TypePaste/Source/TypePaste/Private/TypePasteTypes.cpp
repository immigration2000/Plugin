// TypePaste - data model and shared utility implementation

#include "TypePasteTypes.h"

// ---------------------------------------------------------------------------
// Model member functions
// ---------------------------------------------------------------------------

FString FTPTypeRef::GetCleanName() const
{
	FString Result = Name;
	if (Result.EndsWith(TEXT("::Type"), ESearchCase::CaseSensitive))
	{
		Result.LeftChopInline(6);
	}
	return Result;
}

FString FTPTypeRef::ToString() const
{
	FString Result = Name;
	if (Args.Num() > 0)
	{
		Result += TEXT("<");
		for (int32 Index = 0; Index < Args.Num(); ++Index)
		{
			if (Index > 0)
			{
				Result += TEXT(", ");
			}
			Result += Args[Index].ToString();
		}
		Result += TEXT(">");
	}
	for (int32 Index = 0; Index < PointerDepth; ++Index)
	{
		Result += TEXT("*");
	}
	if (bReference)
	{
		Result += TEXT("&");
	}
	return Result;
}

int32 FTPEnumDef::FindEntryIndex(const FString& EntryName) const
{
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].Name.Equals(EntryName, ESearchCase::CaseSensitive))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

TArray<int32> FTPEnumDef::GetVisibleEntryIndices() const
{
	TArray<int32> Result;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (!Entries[Index].bHidden)
		{
			Result.Add(Index);
		}
	}
	return Result;
}

int32 FTPStructDef::NumRawItems() const
{
	int32 Count = 0;
	for (const FTPStructItem& Item : Items)
	{
		if (Item.Kind == ETPStructItemKind::Raw)
		{
			++Count;
		}
	}
	return Count;
}

bool FTPParseResult::HasErrors() const
{
	for (const FTPMessage& Message : Messages)
	{
		if (Message.Severity == ETPSeverity::Error)
		{
			return true;
		}
	}
	return false;
}

const FTPEnumDef* FTPParseResult::FindEnum(const FString& Name) const
{
	for (const FTPEnumDef& Def : Enums)
	{
		if (Def.Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			return &Def;
		}
	}
	return nullptr;
}

const FTPStructDef* FTPParseResult::FindStruct(const FString& Name) const
{
	for (const FTPStructDef& Def : Structs)
	{
		if (Def.Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			return &Def;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

namespace TypePasteUtilsPrivate
{
	/** When Spec[Index] is '"', returns the index after the closing quote */
	static int32 SkipQuoted(const FString& Spec, int32 Index)
	{
		const int32 Len = Spec.Len();
		++Index;
		while (Index < Len && Spec[Index] != TEXT('"'))
		{
			Index += (Spec[Index] == TEXT('\\')) ? 2 : 1;
		}
		return FMath::Min(Index + 1, Len);
	}

	/** When Spec[Index] is '"', returns the unescaped text inside the quotes */
	static FString ReadQuoted(const FString& Spec, int32 Index)
	{
		const int32 Len = Spec.Len();
		FString Out;
		++Index;
		while (Index < Len && Spec[Index] != TEXT('"'))
		{
			const TCHAR C = Spec[Index];
			if (C == TEXT('\\') && Index + 1 < Len)
			{
				const TCHAR N = Spec[Index + 1];
				switch (N)
				{
				case TEXT('n'): Out.AppendChar(TEXT('\n')); break;
				case TEXT('t'): Out.AppendChar(TEXT('\t')); break;
				case TEXT('r'): break;
				default: Out.AppendChar(N); break;
				}
				Index += 2;
				continue;
			}
			Out.AppendChar(C);
			++Index;
		}
		return Out;
	}

	static FString Int64ToString(int64 Value)
	{
		if (Value == 0)
		{
			return TEXT("0");
		}
		const bool bNegative = Value < 0;
		uint64 Magnitude = bNegative ? (uint64)(-(Value + 1)) + 1 : (uint64)Value;
		FString Digits;
		while (Magnitude > 0)
		{
			Digits.AppendChar((TCHAR)(TEXT('0') + (int32)(Magnitude % 10)));
			Magnitude /= 10;
		}
		FString Result = bNegative ? FString(TEXT("-")) : FString();
		for (int32 Index = Digits.Len() - 1; Index >= 0; --Index)
		{
			Result.AppendChar(Digits[Index]);
		}
		return Result;
	}

	static int32 HexDigitValue(TCHAR C)
	{
		if (C >= TEXT('0') && C <= TEXT('9')) return C - TEXT('0');
		if (C >= TEXT('a') && C <= TEXT('f')) return 10 + (C - TEXT('a'));
		if (C >= TEXT('A') && C <= TEXT('F')) return 10 + (C - TEXT('A'));
		return -1;
	}

	/** Integer literal -> decimal string. Returns false on failure */
	static bool ParseIntegerLiteral(const FString& InExpr, int64& OutValue)
	{
		FString Expr = InExpr.TrimStartAndEnd();
		// Strip suffixes (u, U, l, L, ll ...)
		while (Expr.Len() > 0)
		{
			const TCHAR Last = Expr[Expr.Len() - 1];
			if (Last == TEXT('u') || Last == TEXT('U') || Last == TEXT('l') || Last == TEXT('L'))
			{
				Expr.LeftChopInline(1);
				continue;
			}
			break;
		}
		Expr = Expr.Replace(TEXT("'"), TEXT(""));

		bool bNegative = false;
		if (Expr.StartsWith(TEXT("-")))
		{
			bNegative = true;
			Expr.RightChopInline(1);
		}
		else if (Expr.StartsWith(TEXT("+")))
		{
			Expr.RightChopInline(1);
		}
		Expr = Expr.TrimStartAndEnd();
		if (Expr.IsEmpty())
		{
			return false;
		}

		uint64 Value = 0;
		if (Expr.Len() > 2 && Expr[0] == TEXT('0') && (Expr[1] == TEXT('x') || Expr[1] == TEXT('X')))
		{
			for (int32 Index = 2; Index < Expr.Len(); ++Index)
			{
				const int32 Digit = HexDigitValue(Expr[Index]);
				if (Digit < 0)
				{
					return false;
				}
				Value = Value * 16 + (uint64)Digit;
			}
		}
		else
		{
			for (int32 Index = 0; Index < Expr.Len(); ++Index)
			{
				if (!TypePasteUtils::IsDigitChar(Expr[Index]))
				{
					return false;
				}
				Value = Value * 10 + (uint64)(Expr[Index] - TEXT('0'));
			}
		}
		OutValue = bNegative ? -(int64)Value : (int64)Value;
		return true;
	}

	/** Normalizes a floating-point literal. Returns false on failure */
	static bool ParseRealLiteral(const FString& InExpr, FString& OutValue)
	{
		FString Expr = InExpr.TrimStartAndEnd();
		if (Expr.EndsWith(TEXT("f")) || Expr.EndsWith(TEXT("F")))
		{
			// Only when it is not hexadecimal (assumes floating-point literals are never hex)
			Expr.LeftChopInline(1);
		}
		Expr = Expr.Replace(TEXT("'"), TEXT(""));

		bool bNegative = false;
		if (Expr.StartsWith(TEXT("-")))
		{
			bNegative = true;
			Expr.RightChopInline(1);
		}
		else if (Expr.StartsWith(TEXT("+")))
		{
			Expr.RightChopInline(1);
		}
		Expr = Expr.TrimStartAndEnd();
		if (Expr.IsEmpty())
		{
			return false;
		}

		bool bHasDigit = false;
		for (int32 Index = 0; Index < Expr.Len(); ++Index)
		{
			const TCHAR C = Expr[Index];
			if (TypePasteUtils::IsDigitChar(C))
			{
				bHasDigit = true;
				continue;
			}
			if (C == TEXT('.') || C == TEXT('e') || C == TEXT('E'))
			{
				continue;
			}
			if ((C == TEXT('+') || C == TEXT('-')) && Index > 0 && (Expr[Index - 1] == TEXT('e') || Expr[Index - 1] == TEXT('E')))
			{
				continue;
			}
			return false;
		}
		if (!bHasDigit)
		{
			return false;
		}

		if (Expr.StartsWith(TEXT(".")))
		{
			Expr = FString(TEXT("0")) + Expr;
		}
		if (Expr.EndsWith(TEXT(".")))
		{
			Expr += TEXT("0");
		}
		OutValue = bNegative ? FString(TEXT("-")) + Expr : Expr;
		return true;
	}

	/** Returns the content of the last string literal in the expression */
	static bool FindLastStringLiteral(const FString& Expr, FString& OutValue)
	{
		bool bFound = false;
		int32 Index = 0;
		const int32 Len = Expr.Len();
		while (Index < Len)
		{
			if (Expr[Index] == TEXT('"'))
			{
				OutValue = ReadQuoted(Expr, Index);
				Index = SkipQuoted(Expr, Index);
				bFound = true;
				continue;
			}
			++Index;
		}
		return bFound;
	}

	static bool IsPointerLikeTemplate(const FString& Name)
	{
		static const TCHAR* Names[] =
		{
			TEXT("TObjectPtr"), TEXT("TSubclassOf"), TEXT("TSoftObjectPtr"), TEXT("TSoftClassPtr"),
			TEXT("TWeakObjectPtr"), TEXT("TLazyObjectPtr"), TEXT("TScriptInterface"),
			TEXT("TWeakInterfacePtr"), TEXT("TStrongObjectPtr")
		};
		for (const TCHAR* Candidate : Names)
		{
			if (Name.Equals(Candidate, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}
}

bool TypePasteUtils::IsSpaceChar(TCHAR C)
{
	return C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\r') || C == TEXT('\n') || C == TEXT('\f') || C == TEXT('\v')
		|| C == (TCHAR)0x00A0 || C == (TCHAR)0x3000 || C == (TCHAR)0xFEFF;
}

bool TypePasteUtils::IsIdentStartChar(TCHAR C)
{
	return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || C == TEXT('_');
}

bool TypePasteUtils::IsIdentChar(TCHAR C)
{
	return IsIdentStartChar(C) || IsDigitChar(C);
}

bool TypePasteUtils::IsDigitChar(TCHAR C)
{
	return C >= TEXT('0') && C <= TEXT('9');
}

FString TypePasteUtils::CollapseWhitespace(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	bool bPendingSpace = false;
	for (int32 Index = 0; Index < In.Len(); ++Index)
	{
		const TCHAR C = In[Index];
		if (IsSpaceChar(C))
		{
			bPendingSpace = !Out.IsEmpty();
			continue;
		}
		if (bPendingSpace)
		{
			Out.AppendChar(TEXT(' '));
			bPendingSpace = false;
		}
		Out.AppendChar(C);
	}
	return Out;
}

bool TypePasteUtils::FindSpecifierValue(const FString& Specifiers, const FString& Key, FString& OutValue)
{
	using namespace TypePasteUtilsPrivate;

	const int32 Len = Specifiers.Len();
	int32 Index = 0;
	while (Index < Len)
	{
		const TCHAR C = Specifiers[Index];
		if (C == TEXT('"'))
		{
			Index = SkipQuoted(Specifiers, Index);
			continue;
		}
		if (!IsIdentStartChar(C))
		{
			++Index;
			continue;
		}

		const int32 WordStart = Index;
		while (Index < Len && IsIdentChar(Specifiers[Index]))
		{
			++Index;
		}
		if (!Specifiers.Mid(WordStart, Index - WordStart).Equals(Key, ESearchCase::IgnoreCase))
		{
			continue;
		}

		int32 Cursor = Index;
		while (Cursor < Len && IsSpaceChar(Specifiers[Cursor]))
		{
			++Cursor;
		}
		if (Cursor >= Len || Specifiers[Cursor] != TEXT('='))
		{
			continue;
		}
		++Cursor;
		while (Cursor < Len && IsSpaceChar(Specifiers[Cursor]))
		{
			++Cursor;
		}
		if (Cursor < Len && Specifiers[Cursor] == TEXT('"'))
		{
			OutValue = ReadQuoted(Specifiers, Cursor);
			return true;
		}

		const int32 ValueStart = Cursor;
		int32 Depth = 0;
		while (Cursor < Len)
		{
			const TCHAR D = Specifiers[Cursor];
			if (D == TEXT('('))
			{
				++Depth;
			}
			else if (D == TEXT(')'))
			{
				if (Depth == 0)
				{
					break;
				}
				--Depth;
			}
			else if (D == TEXT(',') && Depth == 0)
			{
				break;
			}
			++Cursor;
		}
		OutValue = Specifiers.Mid(ValueStart, Cursor - ValueStart).TrimStartAndEnd();
		return true;
	}
	return false;
}

bool TypePasteUtils::HasSpecifierFlag(const FString& Specifiers, const FString& Flag)
{
	using namespace TypePasteUtilsPrivate;

	const int32 Len = Specifiers.Len();
	int32 Index = 0;
	while (Index < Len)
	{
		const TCHAR C = Specifiers[Index];
		if (C == TEXT('"'))
		{
			Index = SkipQuoted(Specifiers, Index);
			continue;
		}
		if (!IsIdentStartChar(C))
		{
			++Index;
			continue;
		}
		const int32 WordStart = Index;
		while (Index < Len && IsIdentChar(Specifiers[Index]))
		{
			++Index;
		}
		if (Specifiers.Mid(WordStart, Index - WordStart).Equals(Flag, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString TypePasteUtils::StripScopeQualifier(const FString& In)
{
	const int32 Found = In.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	return Found == INDEX_NONE ? In : In.RightChop(Found + 2);
}

FString TypePasteUtils::StripUnrealPrefix(const FString& CppName)
{
	if (CppName.Len() >= 2)
	{
		const TCHAR First = CppName[0];
		const TCHAR Second = CppName[1];
		const bool bKnownPrefix = First == TEXT('E') || First == TEXT('F') || First == TEXT('U') || First == TEXT('A');
		if (bKnownPrefix && Second >= TEXT('A') && Second <= TEXT('Z'))
		{
			return CppName.RightChop(1);
		}
	}
	return CppName;
}

FString TypePasteUtils::MakeAssetName(const FString& CppName, bool bIsEnum, ETPAssetNaming Naming)
{
	switch (Naming)
	{
	case ETPAssetNaming::PrefixE_S_:
		return (bIsEnum ? FString(TEXT("E_")) : FString(TEXT("S_"))) + StripUnrealPrefix(CppName);
	case ETPAssetNaming::PrefixE_F_:
		return (bIsEnum ? FString(TEXT("E_")) : FString(TEXT("F_"))) + StripUnrealPrefix(CppName);
	case ETPAssetNaming::NoPrefix:
		return StripUnrealPrefix(CppName);
	case ETPAssetNaming::KeepCppName:
	default:
		return CppName;
	}
}

ETPPrimitiveKind TypePasteUtils::GetPrimitiveKind(const FString& TypeName)
{
	struct FEntry
	{
		const TCHAR* Name;
		ETPPrimitiveKind Kind;
	};
	static const FEntry Entries[] =
	{
		{ TEXT("bool"), ETPPrimitiveKind::Bool },
		{ TEXT("int32"), ETPPrimitiveKind::Int },
		{ TEXT("int"), ETPPrimitiveKind::Int },
		{ TEXT("int8"), ETPPrimitiveKind::Int },
		{ TEXT("int16"), ETPPrimitiveKind::Int },
		{ TEXT("uint16"), ETPPrimitiveKind::Int },
		{ TEXT("int64"), ETPPrimitiveKind::Int64 },
		{ TEXT("uint32"), ETPPrimitiveKind::Int64 },
		{ TEXT("uint64"), ETPPrimitiveKind::Int64 },
		{ TEXT("uint8"), ETPPrimitiveKind::Byte },
		{ TEXT("float"), ETPPrimitiveKind::Real },
		{ TEXT("double"), ETPPrimitiveKind::Real },
		{ TEXT("FString"), ETPPrimitiveKind::String },
		{ TEXT("FName"), ETPPrimitiveKind::Name },
		{ TEXT("FText"), ETPPrimitiveKind::Text },
	};
	for (const FEntry& Entry : Entries)
	{
		if (TypeName.Equals(Entry.Name, ESearchCase::CaseSensitive))
		{
			return Entry.Kind;
		}
	}
	return ETPPrimitiveKind::None;
}

bool TypePasteUtils::ConvertDefaultValue(ETPPrimitiveKind Kind, const FString& InExpr, FString& OutValue)
{
	using namespace TypePasteUtilsPrivate;

	const FString Expr = InExpr.TrimStartAndEnd();
	if (Expr.IsEmpty())
	{
		return false;
	}

	switch (Kind)
	{
	case ETPPrimitiveKind::Bool:
		if (Expr.Equals(TEXT("true"), ESearchCase::CaseSensitive))
		{
			OutValue = TEXT("true");
			return true;
		}
		if (Expr.Equals(TEXT("false"), ESearchCase::CaseSensitive) || Expr.Equals(TEXT("0")))
		{
			OutValue = TEXT("false");
			return true;
		}
		if (Expr.Equals(TEXT("1")))
		{
			OutValue = TEXT("true");
			return true;
		}
		return false;

	case ETPPrimitiveKind::Int:
	case ETPPrimitiveKind::Int64:
	case ETPPrimitiveKind::Byte:
	{
		int64 Value = 0;
		if (!ParseIntegerLiteral(Expr, Value))
		{
			return false;
		}
		if (Kind == ETPPrimitiveKind::Byte && (Value < 0 || Value > 255))
		{
			return false;
		}
		if (Kind == ETPPrimitiveKind::Int && (Value < (int64)MIN_int32 || Value > (int64)MAX_int32))
		{
			return false;
		}
		OutValue = Int64ToString(Value);
		return true;
	}

	case ETPPrimitiveKind::Real:
		return ParseRealLiteral(Expr, OutValue);

	case ETPPrimitiveKind::Name:
		if (Expr.Equals(TEXT("NAME_None"), ESearchCase::CaseSensitive))
		{
			return false;
		}
		[[fallthrough]];
	case ETPPrimitiveKind::String:
	case ETPPrimitiveKind::Text:
	{
		FString Literal;
		if (!FindLastStringLiteral(Expr, Literal) || Literal.IsEmpty())
		{
			return false;
		}
		OutValue = Literal;
		return true;
	}

	case ETPPrimitiveKind::None:
	default:
		return false;
	}
}

void TypePasteUtils::CollectTypeNames(const FTPTypeRef& Type, TArray<FString>& OutNames)
{
	if (!Type.IsValid())
	{
		return;
	}
	if (!IsDigitChar(Type.Name[0]))
	{
		OutNames.AddUnique(Type.GetCleanName());
	}
	for (const FTPTypeRef& Arg : Type.Args)
	{
		CollectTypeNames(Arg, OutNames);
	}
}

void TypePasteUtils::CollectTypeUsages(const FTPTypeRef& Type, TArray<FString>& OutByValue, TArray<FString>& OutByPointer)
{
	using namespace TypePasteUtilsPrivate;

	if (!Type.IsValid() || IsDigitChar(Type.Name[0]))
	{
		return;
	}

	if (Type.PointerDepth > 0)
	{
		OutByPointer.AddUnique(Type.GetCleanName());
		return;
	}

	if (IsPointerLikeTemplate(Type.Name))
	{
		for (const FTPTypeRef& Arg : Type.Args)
		{
			if (Arg.Args.Num() == 0)
			{
				if (Arg.IsValid() && !IsDigitChar(Arg.Name[0]))
				{
					OutByPointer.AddUnique(Arg.GetCleanName());
				}
			}
			else
			{
				CollectTypeUsages(Arg, OutByValue, OutByPointer);
			}
		}
		return;
	}

	if (Type.Args.Num() > 0)
	{
		// TArray, TSet, TMap, TEnumAsByte, TOptional ... -> arguments are used by value
		for (const FTPTypeRef& Arg : Type.Args)
		{
			CollectTypeUsages(Arg, OutByValue, OutByPointer);
		}
		return;
	}

	OutByValue.AddUnique(Type.GetCleanName());
}

void TypePasteUtils::SortStructsByDependency(const FTPParseResult& Result, TArray<int32>& OutOrder, TArray<FString>& OutCycleNames)
{
	OutOrder.Reset();
	OutCycleNames.Reset();

	const int32 Num = Result.Structs.Num();
	TArray<TArray<int32>> Dependencies;
	Dependencies.SetNum(Num);

	auto FindLocalStruct = [&Result](const FString& Name) -> int32
	{
		for (int32 Index = 0; Index < Result.Structs.Num(); ++Index)
		{
			if (Result.Structs[Index].Name.Equals(Name, ESearchCase::CaseSensitive))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	};

	for (int32 StructIndex = 0; StructIndex < Num; ++StructIndex)
	{
		const FTPStructDef& Def = Result.Structs[StructIndex];
		TArray<FString> ByValue;
		TArray<FString> ByPointer;
		for (const FTPStructMember& Member : Def.Members)
		{
			CollectTypeUsages(Member.Type, ByValue, ByPointer);
		}
		if (!Def.ParentName.IsEmpty())
		{
			ByValue.AddUnique(Def.ParentName);
		}
		for (const FString& Name : ByValue)
		{
			const int32 DepIndex = FindLocalStruct(Name);
			if (DepIndex == StructIndex)
			{
				OutCycleNames.AddUnique(Def.Name);
			}
			else if (DepIndex != INDEX_NONE)
			{
				Dependencies[StructIndex].AddUnique(DepIndex);
			}
		}
	}

	TArray<bool> Placed;
	Placed.Init(false, Num);
	int32 NumPlaced = 0;
	while (NumPlaced < Num)
	{
		bool bProgress = false;
		for (int32 StructIndex = 0; StructIndex < Num; ++StructIndex)
		{
			if (Placed[StructIndex])
			{
				continue;
			}
			bool bReady = true;
			for (const int32 DepIndex : Dependencies[StructIndex])
			{
				if (!Placed[DepIndex])
				{
					bReady = false;
					break;
				}
			}
			if (bReady)
			{
				Placed[StructIndex] = true;
				OutOrder.Add(StructIndex);
				++NumPlaced;
				bProgress = true;
				break; // start over to keep the original order as much as possible
			}
		}

		if (!bProgress)
		{
			// Dependency cycle: append the rest in their original order
			for (int32 StructIndex = 0; StructIndex < Num; ++StructIndex)
			{
				if (!Placed[StructIndex])
				{
					Placed[StructIndex] = true;
					OutOrder.Add(StructIndex);
					OutCycleNames.AddUnique(Result.Structs[StructIndex].Name);
					++NumPlaced;
				}
			}
		}
	}
}

bool TypePasteUtils::IsValidIdentifier(const FString& In)
{
	if (In.IsEmpty() || !IsIdentStartChar(In[0]))
	{
		return false;
	}
	for (int32 Index = 1; Index < In.Len(); ++Index)
	{
		if (!IsIdentChar(In[Index]))
		{
			return false;
		}
	}
	return true;
}

bool TypePasteUtils::IsAsciiText(const FString& In)
{
	for (int32 Index = 0; Index < In.Len(); ++Index)
	{
		if ((uint32)In[Index] > 127u)
		{
			return false;
		}
	}
	return true;
}

namespace TypePasteUtilsPrivate
{
	/** Splits a specifier list at commas outside quotes/parentheses (each part trimmed) */
	static TArray<FString> SplitTopLevelSpecifiers(const FString& Specifiers)
	{
		TArray<FString> Parts;
		const int32 Len = Specifiers.Len();
		int32 Depth = 0;
		int32 Start = 0;
		int32 Index = 0;
		while (Index < Len)
		{
			const TCHAR C = Specifiers[Index];
			if (C == TEXT('"'))
			{
				Index = SkipQuoted(Specifiers, Index);
				continue;
			}
			if (C == TEXT('('))
			{
				++Depth;
			}
			else if (C == TEXT(')'))
			{
				Depth = FMath::Max(0, Depth - 1);
			}
			else if (C == TEXT(',') && Depth == 0)
			{
				Parts.Add(Specifiers.Mid(Start, Index - Start).TrimStartAndEnd());
				Start = Index + 1;
			}
			++Index;
		}
		Parts.Add(Specifiers.Mid(Start).TrimStartAndEnd());

		TArray<FString> NonEmpty;
		for (const FString& Part : Parts)
		{
			if (!Part.IsEmpty())
			{
				NonEmpty.Add(Part);
			}
		}
		return NonEmpty;
	}

	/** Leading identifier of a specifier part. e.g. Category = "X" -> Category */
	static FString GetSpecifierKey(const FString& Part)
	{
		int32 Index = 0;
		while (Index < Part.Len() && TypePasteUtils::IsIdentChar(Part[Index]))
		{
			++Index;
		}
		return Part.Left(Index);
	}

	/** For the form Key = ( ... ), returns the range inside the parentheses */
	static bool FindNestedList(const FString& Part, int32& OutInnerStart, int32& OutInnerEnd)
	{
		int32 Index = GetSpecifierKey(Part).Len();
		if (Index == 0)
		{
			return false;
		}
		while (Index < Part.Len() && TypePasteUtils::IsSpaceChar(Part[Index]))
		{
			++Index;
		}
		if (Index >= Part.Len() || Part[Index] != TEXT('='))
		{
			return false;
		}
		++Index;
		while (Index < Part.Len() && TypePasteUtils::IsSpaceChar(Part[Index]))
		{
			++Index;
		}
		if (Index >= Part.Len() || Part[Index] != TEXT('(') || Part[Part.Len() - 1] != TEXT(')'))
		{
			return false;
		}
		OutInnerStart = Index + 1;
		OutInnerEnd = Part.Len() - 1;
		return true;
	}

	static void AddCount(TArray<FString>& Keys, TArray<int32>& Counts, const FString& Key)
	{
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			if (Keys[Index].Equals(Key, ESearchCase::IgnoreCase))
			{
				++Counts[Index];
				return;
			}
		}
		Keys.Add(Key);
		Counts.Add(1);
	}
}

FString TypePasteUtils::RemoveNonAsciiSpecifiers(const FString& Specifiers, TArray<FString>& OutRemovedKeys)
{
	using namespace TypePasteUtilsPrivate;

	if (IsAsciiText(Specifiers))
	{
		return Specifiers;
	}

	TArray<FString> Kept;
	for (const FString& Part : SplitTopLevelSpecifiers(Specifiers))
	{
		if (IsAsciiText(Part))
		{
			Kept.Add(Part);
			continue;
		}

		int32 InnerStart = 0;
		int32 InnerEnd = 0;
		if (FindNestedList(Part, InnerStart, InnerEnd))
		{
			const FString Inner = RemoveNonAsciiSpecifiers(Part.Mid(InnerStart, InnerEnd - InnerStart), OutRemovedKeys);
			if (!Inner.TrimStartAndEnd().IsEmpty())
			{
				Kept.Add(GetSpecifierKey(Part) + TEXT(" = (") + Inner + TEXT(")"));
			}
			continue;
		}

		const FString Key = GetSpecifierKey(Part);
		OutRemovedKeys.Add(Key.IsEmpty() ? FString(TEXT("?")) : Key);
	}
	return FString::Join(Kept, TEXT(", "));
}

void TypePasteUtils::EnforceEnglishOnly(FTPParseResult& Result)
{
	using namespace TypePasteUtilsPrivate;

	int32 NumComments = 0;
	TArray<FString> RemovedKeys;
	TArray<int32> RemovedKeyCounts;
	TArray<FTPMessage> Details;

	auto CleanComment = [&NumComments](FString& Comment)
	{
		if (!Comment.IsEmpty() && !IsAsciiText(Comment))
		{
			Comment.Reset();
			++NumComments;
		}
	};
	auto CleanSpecifiers = [&RemovedKeys, &RemovedKeyCounts](FString& Specifiers) -> TArray<FString>
	{
		TArray<FString> Keys;
		const FString Cleaned = RemoveNonAsciiSpecifiers(Specifiers, Keys);
		if (Keys.Num() > 0)
		{
			Specifiers = Cleaned;
			for (const FString& Key : Keys)
			{
				AddCount(RemovedKeys, RemovedKeyCounts, Key);
			}
		}
		return Keys;
	};

	for (FTPEnumDef& Enum : Result.Enums)
	{
		CleanComment(Enum.Comment);
		CleanSpecifiers(Enum.MacroSpecifiers);
		for (FTPEnumEntry& Entry : Enum.Entries)
		{
			CleanComment(Entry.Comment);
			CleanSpecifiers(Entry.UMetaRaw);
			if (!IsAsciiText(Entry.DisplayName))
			{
				Entry.DisplayName.Reset();
			}
		}
	}

	for (FTPStructDef& Struct : Result.Structs)
	{
		CleanComment(Struct.Comment);
		CleanSpecifiers(Struct.MacroSpecifiers);

		for (FTPStructMember& Member : Struct.Members)
		{
			CleanComment(Member.Comment);

			const TArray<FString> Keys = CleanSpecifiers(Member.PropertySpecifiers);
			if (Keys.Contains(FString(TEXT("Category"))))
			{
				// If Category was removed, use the C++ generator's default category (the struct name)
				const FString Category = FString(TEXT("Category = \"")) + StripUnrealPrefix(Struct.Name) + TEXT("\"");
				Member.PropertySpecifiers = Member.PropertySpecifiers.TrimStartAndEnd().IsEmpty()
					? Category
					: Member.PropertySpecifiers + TEXT(", ") + Category;
			}

			if (!IsAsciiText(Member.InitText) || !IsAsciiText(Member.DefaultExpr))
			{
				Details.Add(FTPMessage(ETPSeverity::Warning, Struct.Name + TEXT("::") + Member.Name
					+ TEXT(": the default value contains non-English text, so it was removed.")));
				Member.InitText.Reset();
				Member.DefaultExpr.Reset();
			}
		}

		TArray<FTPStructItem> KeptItems;
		for (FTPStructItem& Item : Struct.Items)
		{
			CleanComment(Item.Comment);
			if (Item.Kind == ETPStructItemKind::Raw && !IsAsciiText(Item.RawText))
			{
				Details.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("Struct '")) + Struct.Name
					+ TEXT("': removed an unparsed statement that contains non-English text: ") + CollapseWhitespace(Item.RawText).Left(60)));
				continue;
			}
			KeptItems.Add(Item);
		}
		Struct.Items = KeptItems;
	}

	if (NumComments == 0 && RemovedKeys.Num() == 0 && Details.Num() == 0)
	{
		return;
	}

	TArray<FString> Parts;
	if (NumComments > 0)
	{
		Parts.Add(FString::FromInt(NumComments) + TEXT(" comment(s)"));
	}
	for (int32 Index = 0; Index < RemovedKeys.Num(); ++Index)
	{
		Parts.Add(FString::FromInt(RemovedKeyCounts[Index]) + TEXT(" ") + RemovedKeys[Index]);
	}
	FString Summary = TEXT("Generated output is English only. Removed non-English text");
	if (Parts.Num() > 0)
	{
		Summary += TEXT(": ");
		Summary += FString::Join(Parts, TEXT(", "));
	}
	Summary += TEXT(". Ask the AI to answer in English (use [Copy AI Prompt]).");
	Result.AddWarning(Summary);
	for (const FTPMessage& Detail : Details)
	{
		Result.Messages.Add(Detail);
	}
}
