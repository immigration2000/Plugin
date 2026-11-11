// TypePaste - 파서 구현

#include "TypePasteParser.h"

namespace TypePasteParserPrivate
{
	using namespace TypePasteUtils;

	enum class ETokenType : uint8
	{
		Identifier,
		Number,
		String,
		CharLiteral,
		Symbol,
		Other
	};

	struct FToken
	{
		ETokenType Type = ETokenType::Other;
		FString Text;
		/** 원문 시작 오프셋 */
		int32 Start = 0;
		/** 원문 끝 오프셋 (미포함) */
		int32 End = 0;
		int32 Line = 0;
		/** 이 토큰 앞 줄들에 있던 주석 */
		FString LeadingComment;
		/** 이 토큰과 같은 줄 뒤에 있던 주석 */
		FString TrailingComment;
	};

	/** "// ========" 같은 장식용 주석인지 */
	static bool IsDecorativeComment(const FString& Text)
	{
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR C = Text[Index];
			if (IsIdentChar(C) || C > 127)
			{
				return false;
			}
		}
		return true;
	}

	/** 주석 기호를 걷어내고 내용만 남긴다 */
	static FString CleanCommentText(const FString& Raw)
	{
		FString Body = Raw;
		if (Body.StartsWith(TEXT("//")))
		{
			Body.RightChopInline(2);
			while (Body.StartsWith(TEXT("/")) || Body.StartsWith(TEXT("!")))
			{
				Body.RightChopInline(1);
			}
		}
		else if (Body.StartsWith(TEXT("/*")))
		{
			Body.RightChopInline(2);
			if (Body.EndsWith(TEXT("*/")))
			{
				Body.LeftChopInline(2);
			}
			while (Body.StartsWith(TEXT("*")) || Body.StartsWith(TEXT("!")))
			{
				Body.RightChopInline(1);
			}
		}
		if (Body.StartsWith(TEXT("<")))
		{
			// Doxygen 의 뒤쪽 주석 표기 (//!< , /**< )
			Body.RightChopInline(1);
		}

		TArray<FString> Lines;
		Body.ParseIntoArrayLines(Lines, false);

		TArray<FString> Cleaned;
		for (FString Line : Lines)
		{
			Line = Line.TrimStartAndEnd();
			while (Line.StartsWith(TEXT("*")))
			{
				Line.RightChopInline(1);
			}
			Line = Line.TrimStartAndEnd();
			if (!Line.IsEmpty() && !IsDecorativeComment(Line))
			{
				Cleaned.Add(Line);
			}
		}
		return FString::Join(Cleaned, TEXT("\n"));
	}

	static bool IsSymbolChar(TCHAR C)
	{
		switch (C)
		{
		case TEXT('('): case TEXT(')'): case TEXT('{'): case TEXT('}'): case TEXT('['): case TEXT(']'):
		case TEXT('<'): case TEXT('>'): case TEXT(';'): case TEXT(','): case TEXT('='): case TEXT('*'):
		case TEXT('&'): case TEXT(':'): case TEXT('.'): case TEXT('-'): case TEXT('+'): case TEXT('!'):
		case TEXT('~'): case TEXT('|'): case TEXT('^'): case TEXT('%'): case TEXT('?'): case TEXT('/'):
		case TEXT('\\'): case TEXT('@'): case TEXT('$'):
			return true;
		default:
			return false;
		}
	}

	static TArray<FToken> Tokenize(const FString& Source)
	{
		TArray<FToken> Tokens;
		const int32 Len = Source.Len();
		int32 Pos = 0;
		int32 Line = 1;
		bool bAtLineStart = true;
		FString PendingComment;

		auto AddComment = [&Tokens, &PendingComment](const FString& Text, int32 CommentLine)
		{
			if (Text.IsEmpty())
			{
				return;
			}
			if (Tokens.Num() > 0 && Tokens.Last().Line == CommentLine && PendingComment.IsEmpty())
			{
				FString& Trailing = Tokens.Last().TrailingComment;
				Trailing = Trailing.IsEmpty() ? Text : Trailing + TEXT("\n") + Text;
			}
			else
			{
				PendingComment = PendingComment.IsEmpty() ? Text : PendingComment + TEXT("\n") + Text;
			}
		};

		while (Pos < Len)
		{
			const TCHAR C = Source[Pos];

			if (C == TEXT('\n'))
			{
				++Line;
				++Pos;
				bAtLineStart = true;
				continue;
			}
			if (IsSpaceChar(C))
			{
				++Pos;
				continue;
			}

			// 마크다운 코드 펜스(```cpp) 나 인라인 코드 표시: 줄 끝까지 무시
			if (C == TEXT('`'))
			{
				while (Pos < Len && Source[Pos] != TEXT('\n'))
				{
					++Pos;
				}
				continue;
			}

			// 전처리기 지시문 (#include, #pragma) 및 마크다운 제목(#)
			if (C == TEXT('#') && bAtLineStart)
			{
				while (Pos < Len && Source[Pos] != TEXT('\n'))
				{
					if (Source[Pos] == TEXT('\\') && Pos + 1 < Len && Source[Pos + 1] == TEXT('\n'))
					{
						Pos += 2;
						++Line;
						continue;
					}
					++Pos;
				}
				continue;
			}

			// 한 줄 주석
			if (C == TEXT('/') && Pos + 1 < Len && Source[Pos + 1] == TEXT('/'))
			{
				const int32 Start = Pos;
				while (Pos < Len && Source[Pos] != TEXT('\n'))
				{
					++Pos;
				}
				AddComment(CleanCommentText(Source.Mid(Start, Pos - Start)), Line);
				continue;
			}

			// 블록 주석
			if (C == TEXT('/') && Pos + 1 < Len && Source[Pos + 1] == TEXT('*'))
			{
				const int32 Start = Pos;
				const int32 StartLine = Line;
				Pos += 2;
				while (Pos < Len && !(Source[Pos] == TEXT('*') && Pos + 1 < Len && Source[Pos + 1] == TEXT('/')))
				{
					if (Source[Pos] == TEXT('\n'))
					{
						++Line;
					}
					++Pos;
				}
				Pos = FMath::Min(Pos + 2, Len);
				AddComment(CleanCommentText(Source.Mid(Start, Pos - Start)), StartLine);
				continue;
			}

			bAtLineStart = false;

			FToken Token;
			Token.Start = Pos;
			Token.Line = Line;

			if (IsIdentStartChar(C))
			{
				Token.Type = ETokenType::Identifier;
				while (Pos < Len && IsIdentChar(Source[Pos]))
				{
					++Pos;
				}
			}
			else if (IsDigitChar(C) || (C == TEXT('.') && Pos + 1 < Len && IsDigitChar(Source[Pos + 1])))
			{
				Token.Type = ETokenType::Number;
				const bool bHex = C == TEXT('0') && Pos + 1 < Len && (Source[Pos + 1] == TEXT('x') || Source[Pos + 1] == TEXT('X'));
				++Pos;
				while (Pos < Len)
				{
					const TCHAR D = Source[Pos];
					if (!bHex && (D == TEXT('+') || D == TEXT('-')) && (Source[Pos - 1] == TEXT('e') || Source[Pos - 1] == TEXT('E')))
					{
						++Pos;
						continue;
					}
					if (IsIdentChar(D) || D == TEXT('.') || D == TEXT('\''))
					{
						++Pos;
						continue;
					}
					break;
				}
			}
			else if (C == TEXT('"'))
			{
				Token.Type = ETokenType::String;
				++Pos;
				while (Pos < Len && Source[Pos] != TEXT('"') && Source[Pos] != TEXT('\n'))
				{
					if (Source[Pos] == TEXT('\\') && Pos + 1 < Len && Source[Pos + 1] != TEXT('\n'))
					{
						Pos += 2;
						continue;
					}
					++Pos;
				}
				if (Pos < Len && Source[Pos] == TEXT('"'))
				{
					++Pos;
				}
			}
			else if (C == TEXT('\''))
			{
				// 'a' 또는 '\n' 형태만 문자 리터럴로 본다 (설명 문장의 아포스트로피는 무시)
				int32 Close = INDEX_NONE;
				if (Pos + 2 < Len && Source[Pos + 1] != TEXT('\\') && Source[Pos + 2] == TEXT('\''))
				{
					Close = Pos + 2;
				}
				else if (Pos + 3 < Len && Source[Pos + 1] == TEXT('\\') && Source[Pos + 3] == TEXT('\''))
				{
					Close = Pos + 3;
				}
				if (Close != INDEX_NONE)
				{
					Token.Type = ETokenType::CharLiteral;
					Pos = Close + 1;
				}
				else
				{
					Token.Type = ETokenType::Other;
					++Pos;
				}
			}
			else if (C == TEXT(':') && Pos + 1 < Len && Source[Pos + 1] == TEXT(':'))
			{
				Token.Type = ETokenType::Symbol;
				Pos += 2;
			}
			else if (IsSymbolChar(C))
			{
				Token.Type = ETokenType::Symbol;
				++Pos;
			}
			else
			{
				Token.Type = ETokenType::Other;
				++Pos;
			}

			Token.End = Pos;
			Token.Text = Source.Mid(Token.Start, Token.End - Token.Start);
			Token.LeadingComment = PendingComment;
			PendingComment.Reset();
			Tokens.Add(MoveTemp(Token));
		}

		return Tokens;
	}

	class FParser
	{
	public:
		FParser(const FString& InSource, TArray<FToken>&& InTokens, FTPParseResult& InResult)
			: Source(InSource)
			, Tokens(MoveTemp(InTokens))
			, Result(InResult)
		{
		}

		void Run()
		{
			while (Pos < Tokens.Num())
			{
				if (Tokens[Pos].Type != ETokenType::Identifier)
				{
					++Pos;
					continue;
				}

				if (IsWordAt(Pos, TEXT("UENUM")))
				{
					ParseMacroDeclaration(true);
					continue;
				}
				if (IsWordAt(Pos, TEXT("USTRUCT")))
				{
					ParseMacroDeclaration(false);
					continue;
				}
				if (IsWordAt(Pos, TEXT("UCLASS")) || IsWordAt(Pos, TEXT("UINTERFACE")))
				{
					SkipClassDeclaration();
					continue;
				}
				if (IsWordAt(Pos, TEXT("enum")))
				{
					const int32 Start = Pos;
					if (!ParseEnum(Start, FString(), false, Tokens[Start].LeadingComment))
					{
						Pos = Start + 1;
					}
					continue;
				}
				if (IsWordAt(Pos, TEXT("struct")))
				{
					const int32 Start = Pos;
					if (!ParseStruct(Start, FString(), false, Tokens[Start].LeadingComment))
					{
						Pos = Start + 1;
					}
					continue;
				}
				++Pos;
			}
		}

	private:
		const FString& Source;
		TArray<FToken> Tokens;
		FTPParseResult& Result;
		int32 Pos = 0;

		// ------------------------------------------------------------------
		// 토큰 헬퍼
		// ------------------------------------------------------------------

		bool IsIdentAt(int32 Index) const
		{
			return Tokens.IsValidIndex(Index) && Tokens[Index].Type == ETokenType::Identifier;
		}

		bool IsWordAt(int32 Index, const TCHAR* Word) const
		{
			return IsIdentAt(Index) && Tokens[Index].Text.Equals(Word, ESearchCase::CaseSensitive);
		}

		bool IsSymbolAt(int32 Index, const TCHAR* Symbol) const
		{
			return Tokens.IsValidIndex(Index) && Tokens[Index].Type == ETokenType::Symbol && Tokens[Index].Text.Equals(Symbol, ESearchCase::CaseSensitive);
		}

		bool IsOpenBracketAt(int32 Index) const
		{
			return IsSymbolAt(Index, TEXT("(")) || IsSymbolAt(Index, TEXT("{")) || IsSymbolAt(Index, TEXT("["));
		}

		bool IsCloseBracketAt(int32 Index) const
		{
			return IsSymbolAt(Index, TEXT(")")) || IsSymbolAt(Index, TEXT("}")) || IsSymbolAt(Index, TEXT("]"));
		}

		/** 여는 괄호 ( { [ 의 짝 인덱스. 없으면 INDEX_NONE */
		int32 FindMatching(int32 OpenIndex) const
		{
			if (!Tokens.IsValidIndex(OpenIndex) || Tokens[OpenIndex].Type != ETokenType::Symbol)
			{
				return INDEX_NONE;
			}
			const FString& Open = Tokens[OpenIndex].Text;
			const TCHAR* Close = nullptr;
			if (Open.Equals(TEXT("(")))
			{
				Close = TEXT(")");
			}
			else if (Open.Equals(TEXT("{")))
			{
				Close = TEXT("}");
			}
			else if (Open.Equals(TEXT("[")))
			{
				Close = TEXT("]");
			}
			else
			{
				return INDEX_NONE;
			}

			int32 Depth = 0;
			for (int32 Index = OpenIndex; Index < Tokens.Num(); ++Index)
			{
				if (Tokens[Index].Type != ETokenType::Symbol)
				{
					continue;
				}
				if (Tokens[Index].Text.Equals(Open))
				{
					++Depth;
				}
				else if (Tokens[Index].Text.Equals(Close))
				{
					--Depth;
					if (Depth == 0)
					{
						return Index;
					}
				}
			}
			return INDEX_NONE;
		}

		/** [First, Last] 토큰 범위의 원문 (사이의 주석/공백 포함) */
		FString SourceSpan(int32 First, int32 Last) const
		{
			if (First > Last || !Tokens.IsValidIndex(First) || !Tokens.IsValidIndex(Last))
			{
				return FString();
			}
			return Source.Mid(Tokens[First].Start, Tokens[Last].End - Tokens[First].Start);
		}

		/**
		 * 범위에 붙은 설명 주석.
		 * bIncludeInner 가 true 면 범위 안쪽 토큰의 주석도 찾는다 (멤버처럼 원문을 재구성하는 경우).
		 */
		FString CommentFor(int32 First, int32 Last, bool bIncludeInner) const
		{
			if (!Tokens.IsValidIndex(First))
			{
				return FString();
			}
			if (!Tokens[First].LeadingComment.IsEmpty())
			{
				return Tokens[First].LeadingComment;
			}
			const int32 SafeLast = FMath::Min(Last, Tokens.Num() - 1);
			if (bIncludeInner)
			{
				for (int32 Index = First + 1; Index <= SafeLast; ++Index)
				{
					if (!Tokens[Index].LeadingComment.IsEmpty())
					{
						return Tokens[Index].LeadingComment;
					}
				}
				for (int32 Index = First; Index <= SafeLast; ++Index)
				{
					if (!Tokens[Index].TrailingComment.IsEmpty())
					{
						return Tokens[Index].TrailingComment;
					}
				}
				return FString();
			}
			return Tokens.IsValidIndex(SafeLast) ? Tokens[SafeLast].TrailingComment : FString();
		}

		FString LineLabel(int32 TokenIndex) const
		{
			if (!Tokens.IsValidIndex(TokenIndex))
			{
				return FString();
			}
			return FString(TEXT("[")) + FString::FromInt(Tokens[TokenIndex].Line) + TEXT("줄] ");
		}

		bool IsNameTaken(const FString& Name) const
		{
			return Result.FindEnum(Name) != nullptr || Result.FindStruct(Name) != nullptr;
		}

		// ------------------------------------------------------------------
		// 최상위 선언
		// ------------------------------------------------------------------

		void ParseMacroDeclaration(bool bIsEnum)
		{
			const TCHAR* MacroName = bIsEnum ? TEXT("UENUM") : TEXT("USTRUCT");
			const int32 MacroIndex = Pos;
			const FString Comment = Tokens[MacroIndex].LeadingComment;

			FString Specifiers;
			int32 Cursor = MacroIndex + 1;
			if (IsSymbolAt(Cursor, TEXT("(")))
			{
				const int32 Close = FindMatching(Cursor);
				if (Close == INDEX_NONE)
				{
					Result.AddError(LineLabel(MacroIndex) + MacroName + TEXT(" 의 괄호가 닫히지 않았습니다."));
					Pos = MacroIndex + 1;
					return;
				}
				Specifiers = CollapseWhitespace(SourceSpan(Cursor + 1, Close - 1));
				Cursor = Close + 1;
			}

			const TCHAR* Keyword = bIsEnum ? TEXT("enum") : TEXT("struct");
			if (!IsWordAt(Cursor, Keyword))
			{
				Result.AddWarning(LineLabel(MacroIndex) + MacroName + TEXT(" 다음에 ") + Keyword + TEXT(" 선언이 없어 건너뜁니다."));
				Pos = Cursor;
				return;
			}

			const bool bParsed = bIsEnum
				? ParseEnum(Cursor, Specifiers, true, Comment)
				: ParseStruct(Cursor, Specifiers, true, Comment);
			if (!bParsed)
			{
				Result.AddError(LineLabel(Cursor) + MacroName + TEXT(" 선언을 해석하지 못했습니다. 문법을 확인해 주세요."));
				Pos = Cursor + 1;
			}
		}

		void SkipClassDeclaration()
		{
			const int32 MacroIndex = Pos;
			int32 Cursor = MacroIndex + 1;
			if (IsSymbolAt(Cursor, TEXT("(")))
			{
				const int32 Close = FindMatching(Cursor);
				Cursor = (Close == INDEX_NONE) ? Cursor + 1 : Close + 1;
			}
			if (!IsWordAt(Cursor, TEXT("class")))
			{
				Pos = Cursor;
				return;
			}

			Result.AddWarning(LineLabel(MacroIndex) + Tokens[MacroIndex].Text + TEXT(" 클래스는 지원하지 않아 건너뜁니다. (열거형/구조체만 생성합니다)"));
			while (Cursor < Tokens.Num() && !IsSymbolAt(Cursor, TEXT("{")) && !IsSymbolAt(Cursor, TEXT(";")))
			{
				++Cursor;
			}
			if (IsSymbolAt(Cursor, TEXT("{")))
			{
				const int32 Close = FindMatching(Cursor);
				Cursor = (Close == INDEX_NONE) ? Tokens.Num() : Close + 1;
			}
			if (IsSymbolAt(Cursor, TEXT(";")))
			{
				++Cursor;
			}
			Pos = Cursor;
		}

		// ------------------------------------------------------------------
		// 열거형
		// ------------------------------------------------------------------

		/** Start 는 'enum' 토큰. 열거형 선언이 아니면 false (Pos 변경 없음) */
		bool ParseEnum(int32 Start, const FString& Specifiers, bool bHadMacro, const FString& Comment)
		{
			FTPEnumDef Def;
			Def.MacroSpecifiers = Specifiers;
			Def.bHadMacro = bHadMacro;
			Def.Comment = Comment;

			int32 Cursor = Start + 1;
			if (IsWordAt(Cursor, TEXT("class")) || IsWordAt(Cursor, TEXT("struct")))
			{
				Def.bScoped = true;
				++Cursor;
			}
			if (!IsIdentAt(Cursor))
			{
				return false;
			}
			Def.Name = Tokens[Cursor].Text;
			++Cursor;

			if (IsSymbolAt(Cursor, TEXT(":")))
			{
				++Cursor;
				const int32 TypeStart = Cursor;
				while (IsIdentAt(Cursor))
				{
					++Cursor;
				}
				if (Cursor == TypeStart)
				{
					return false;
				}
				Def.UnderlyingType = CollapseWhitespace(SourceSpan(TypeStart, Cursor - 1));
			}

			if (IsSymbolAt(Cursor, TEXT(";")))
			{
				// 전방 선언 → 무시
				Pos = Cursor + 1;
				return true;
			}
			if (!IsSymbolAt(Cursor, TEXT("{")))
			{
				return false;
			}

			const int32 Close = FindMatching(Cursor);
			if (Close == INDEX_NONE)
			{
				Result.AddError(LineLabel(Start) + TEXT("열거형 '") + Def.Name + TEXT("' 의 중괄호가 닫히지 않았습니다."));
				Pos = Cursor + 1;
				return true;
			}

			int32 Q = Cursor + 1;
			while (Q < Close)
			{
				if (IsSymbolAt(Q, TEXT(",")))
				{
					++Q;
					continue;
				}
				if (!IsIdentAt(Q))
				{
					Result.AddWarning(LineLabel(Q) + TEXT("열거형 '") + Def.Name + TEXT("' 에서 해석할 수 없는 '") + Tokens[Q].Text + TEXT("' 부분을 건너뜁니다."));
					int32 Depth = 0;
					while (Q < Close)
					{
						if (Depth == 0 && IsSymbolAt(Q, TEXT(",")))
						{
							break;
						}
						if (IsOpenBracketAt(Q))
						{
							++Depth;
						}
						else if (IsCloseBracketAt(Q))
						{
							--Depth;
						}
						++Q;
					}
					continue;
				}

				FTPEnumEntry Entry;
				const int32 EntryFirst = Q;
				Entry.Name = Tokens[Q].Text;
				++Q;

				int32 ValueStart = INDEX_NONE;
				int32 ValueEnd = INDEX_NONE;
				int32 Depth = 0;
				while (Q < Close)
				{
					if (Depth == 0 && IsSymbolAt(Q, TEXT(",")))
					{
						break;
					}
					if (Depth == 0 && IsWordAt(Q, TEXT("UMETA")) && IsSymbolAt(Q + 1, TEXT("(")))
					{
						const int32 MetaClose = FindMatching(Q + 1);
						if (MetaClose == INDEX_NONE || MetaClose >= Close)
						{
							Q = Close;
							break;
						}
						if (ValueStart != INDEX_NONE && ValueEnd == INDEX_NONE)
						{
							ValueEnd = Q - 1;
						}
						Entry.UMetaRaw = CollapseWhitespace(SourceSpan(Q + 2, MetaClose - 1));
						Q = MetaClose + 1;
						continue;
					}
					if (Depth == 0 && ValueStart == INDEX_NONE && IsSymbolAt(Q, TEXT("=")))
					{
						ValueStart = Q + 1;
						++Q;
						continue;
					}
					if (IsOpenBracketAt(Q))
					{
						++Depth;
					}
					else if (IsCloseBracketAt(Q))
					{
						--Depth;
					}
					++Q;
				}

				if (ValueStart != INDEX_NONE)
				{
					if (ValueEnd == INDEX_NONE)
					{
						ValueEnd = Q - 1;
					}
					Entry.ValueExpr = CollapseWhitespace(SourceSpan(ValueStart, ValueEnd));
				}

				const int32 EntryLast = (Q < Close) ? Q : Close - 1;
				Entry.Comment = CommentFor(EntryFirst, EntryLast, true);

				FString MetaValue;
				if (FindSpecifierValue(Entry.UMetaRaw, TEXT("DisplayName"), MetaValue))
				{
					Entry.DisplayName = MetaValue;
				}
				if (Entry.Comment.IsEmpty() && FindSpecifierValue(Entry.UMetaRaw, TEXT("ToolTip"), MetaValue))
				{
					Entry.Comment = MetaValue;
				}
				Entry.bHidden = HasSpecifierFlag(Entry.UMetaRaw, TEXT("Hidden"));

				if (Def.FindEntryIndex(Entry.Name) != INDEX_NONE)
				{
					Result.AddWarning(LineLabel(EntryFirst) + TEXT("열거형 '") + Def.Name + TEXT("' 에 항목 '") + Entry.Name + TEXT("' 이 중복되어 하나만 사용합니다."));
					continue;
				}
				Def.Entries.Add(MoveTemp(Entry));
			}

			Pos = Close + 1;
			if (IsSymbolAt(Pos, TEXT(";")))
			{
				++Pos;
			}

			if (IsNameTaken(Def.Name))
			{
				Result.AddWarning(LineLabel(Start) + TEXT("'") + Def.Name + TEXT("' 이 두 번 정의되어 처음 것만 사용합니다."));
				return true;
			}
			if (Def.Entries.Num() == 0)
			{
				Result.AddWarning(LineLabel(Start) + TEXT("열거형 '") + Def.Name + TEXT("' 에 항목이 없습니다."));
			}
			Result.Enums.Add(MoveTemp(Def));
			return true;
		}

		// ------------------------------------------------------------------
		// 구조체
		// ------------------------------------------------------------------

		/** Start 는 'struct' 토큰. 구조체 선언이 아니면 false (Pos 변경 없음) */
		bool ParseStruct(int32 Start, const FString& Specifiers, bool bHadMacro, const FString& Comment)
		{
			FTPStructDef Def;
			Def.MacroSpecifiers = Specifiers;
			Def.bHadMacro = bHadMacro;
			Def.Comment = Comment;

			int32 Cursor = Start + 1;
			while (IsIdentAt(Cursor))
			{
				const FString& Word = Tokens[Cursor].Text;
				const bool bIsApiMacro = Word.EndsWith(TEXT("_API"), ESearchCase::CaseSensitive);
				const bool bIsKeyword = Word.Equals(TEXT("final"), ESearchCase::CaseSensitive);
				if (!bIsApiMacro && !bIsKeyword)
				{
					Def.Name = Word;
				}
				++Cursor;
			}
			if (Def.Name.IsEmpty())
			{
				return false;
			}

			if (IsSymbolAt(Cursor, TEXT(";")))
			{
				// 전방 선언 → 무시
				Pos = Cursor + 1;
				return true;
			}

			if (IsSymbolAt(Cursor, TEXT(":")))
			{
				++Cursor;
				const int32 ParentStart = Cursor;
				int32 Steps = 0;
				while (Cursor < Tokens.Num() && !IsSymbolAt(Cursor, TEXT("{")))
				{
					const bool bAllowed = IsIdentAt(Cursor) || IsSymbolAt(Cursor, TEXT("::")) || IsSymbolAt(Cursor, TEXT(","))
						|| IsSymbolAt(Cursor, TEXT("<")) || IsSymbolAt(Cursor, TEXT(">"));
					if (!bAllowed || ++Steps > 16)
					{
						return false;
					}
					++Cursor;
				}
				if (!IsSymbolAt(Cursor, TEXT("{")) || Cursor == ParentStart)
				{
					return false;
				}
				Def.ParentText = CollapseWhitespace(SourceSpan(ParentStart, Cursor - 1));
				for (int32 Index = ParentStart; Index < Cursor; ++Index)
				{
					if (IsSymbolAt(Index, TEXT(",")) || IsSymbolAt(Index, TEXT("<")))
					{
						break;
					}
					const bool bAccess = IsWordAt(Index, TEXT("public")) || IsWordAt(Index, TEXT("protected"))
						|| IsWordAt(Index, TEXT("private")) || IsWordAt(Index, TEXT("virtual"));
					if (IsIdentAt(Index) && !bAccess)
					{
						Def.ParentName = Tokens[Index].Text;
					}
				}
			}

			if (!IsSymbolAt(Cursor, TEXT("{")))
			{
				return false;
			}

			const int32 Close = FindMatching(Cursor);
			if (Close == INDEX_NONE)
			{
				Result.AddError(LineLabel(Start) + TEXT("구조체 '") + Def.Name + TEXT("' 의 중괄호가 닫히지 않았습니다."));
				Pos = Cursor + 1;
				return true;
			}

			ParseStructBody(Cursor + 1, Close, Def);

			Pos = Close + 1;
			if (IsSymbolAt(Pos, TEXT(";")))
			{
				++Pos;
			}

			if (IsNameTaken(Def.Name))
			{
				Result.AddWarning(LineLabel(Start) + TEXT("'") + Def.Name + TEXT("' 이 두 번 정의되어 처음 것만 사용합니다."));
				return true;
			}
			if (Def.Members.Num() == 0 && Def.NumRawItems() == 0)
			{
				Result.AddWarning(LineLabel(Start) + TEXT("구조체 '") + Def.Name + TEXT("' 에 멤버가 없습니다."));
			}
			Result.Structs.Add(MoveTemp(Def));
			return true;
		}

		void ParseStructBody(int32 Begin, int32 End, FTPStructDef& Def)
		{
			int32 Q = Begin;
			while (Q < End)
			{
				if (IsSymbolAt(Q, TEXT(";")))
				{
					++Q;
					continue;
				}

				if (IsWordAt(Q, TEXT("GENERATED_BODY")) || IsWordAt(Q, TEXT("GENERATED_USTRUCT_BODY")) || IsWordAt(Q, TEXT("GENERATED_UCLASS_BODY")))
				{
					++Q;
					if (IsSymbolAt(Q, TEXT("(")))
					{
						const int32 Close = FindMatching(Q);
						Q = (Close == INDEX_NONE || Close >= End) ? Q + 1 : Close + 1;
					}
					continue;
				}

				if ((IsWordAt(Q, TEXT("public")) || IsWordAt(Q, TEXT("protected")) || IsWordAt(Q, TEXT("private"))) && IsSymbolAt(Q + 1, TEXT(":")))
				{
					Q += 2;
					continue;
				}

				const int32 StatementStart = Q;
				FString Specifiers;
				bool bHadUProperty = false;
				bool bForceRaw = false;

				if (IsWordAt(Q, TEXT("UPROPERTY")))
				{
					bHadUProperty = true;
					++Q;
					if (IsSymbolAt(Q, TEXT("(")))
					{
						const int32 Close = FindMatching(Q);
						if (Close == INDEX_NONE || Close >= End)
						{
							Result.AddError(LineLabel(StatementStart) + TEXT("구조체 '") + Def.Name + TEXT("' 의 UPROPERTY 괄호가 닫히지 않았습니다."));
							return;
						}
						Specifiers = CollapseWhitespace(SourceSpan(Q + 1, Close - 1));
						Q = Close + 1;
					}
				}
				else if (IsWordAt(Q, TEXT("UFUNCTION")))
				{
					bForceRaw = true;
					++Q;
					if (IsSymbolAt(Q, TEXT("(")))
					{
						const int32 Close = FindMatching(Q);
						Q = (Close == INDEX_NONE || Close >= End) ? Q + 1 : Close + 1;
					}
				}

				if (Q >= End)
				{
					break;
				}

				bool bIsFunction = false;
				const int32 StatementLast = FindStatementEnd(Q, End, bIsFunction);

				bool bParsed = false;
				if (!bForceRaw && !bIsFunction)
				{
					bParsed = ParseMemberDeclaration(Q, StatementLast, Specifiers, bHadUProperty, CommentFor(StatementStart, StatementLast, true), Def);
				}

				if (!bParsed)
				{
					FTPStructItem Item;
					Item.Kind = ETPStructItemKind::Raw;
					Item.RawText = SourceSpan(StatementStart, StatementLast);
					Item.Comment = CommentFor(StatementStart, StatementLast, false);
					Def.Items.Add(Item);

					const TCHAR* What = (bForceRaw || bIsFunction) ? TEXT("함수/생성자") : TEXT("구문");
					Result.AddInfo(LineLabel(StatementStart) + TEXT("구조체 '") + Def.Name + TEXT("' 의 ") + What
						+ TEXT(" 를 원문 그대로 보존합니다. (C++ 생성 시에만 유지, 블루프린트 구조체에는 반영되지 않음)"));
				}

				Q = StatementLast + 1;
			}
		}

		/** 구문의 마지막 토큰 인덱스(';' 또는 함수 본문의 '}')를 찾는다 */
		int32 FindStatementEnd(int32 Start, int32 Limit, bool& bOutIsFunction) const
		{
			bOutIsFunction = false;
			bool bSawAssign = false;
			int32 Q = Start;
			while (Q < Limit)
			{
				if (IsSymbolAt(Q, TEXT(";")))
				{
					return Q;
				}
				if (IsWordAt(Q, TEXT("operator")))
				{
					// 연산자 오버로드: operator==(...), operator()(...), operator bool() ...
					bOutIsFunction = true;
					int32 ParamOpen = Q + 1;
					if (IsSymbolAt(ParamOpen, TEXT("(")) && IsSymbolAt(ParamOpen + 1, TEXT(")")))
					{
						ParamOpen += 2;
					}
					while (ParamOpen < Limit && !IsSymbolAt(ParamOpen, TEXT("(")))
					{
						++ParamOpen;
					}
					const int32 ParamClose = (ParamOpen < Limit) ? FindMatching(ParamOpen) : INDEX_NONE;
					if (ParamClose == INDEX_NONE || ParamClose >= Limit)
					{
						return Limit - 1;
					}
					return SkipFunctionTail(ParamClose + 1, Limit);
				}
				if (IsSymbolAt(Q, TEXT("=")))
				{
					bSawAssign = true;
					++Q;
					continue;
				}
				if (IsSymbolAt(Q, TEXT("(")))
				{
					const int32 Close = FindMatching(Q);
					if (Close == INDEX_NONE || Close >= Limit)
					{
						return Limit - 1;
					}
					if (!bSawAssign && Q > Start && IsIdentAt(Q - 1))
					{
						// 이름 바로 뒤의 '(' → 함수/생성자 선언
						bOutIsFunction = true;
						return SkipFunctionTail(Close + 1, Limit);
					}
					Q = Close + 1;
					continue;
				}
				if (IsSymbolAt(Q, TEXT("{")) || IsSymbolAt(Q, TEXT("[")))
				{
					const int32 Close = FindMatching(Q);
					if (Close == INDEX_NONE || Close >= Limit)
					{
						return Limit - 1;
					}
					Q = Close + 1;
					continue;
				}
				++Q;
			}
			return Limit - 1;
		}

		/** 함수 매개변수 목록 다음부터 함수 끝까지 건너뛴다 */
		int32 SkipFunctionTail(int32 Index, int32 Limit) const
		{
			int32 Q = Index;
			while (Q < Limit)
			{
				if (IsSymbolAt(Q, TEXT(";")))
				{
					return Q;
				}
				if (IsSymbolAt(Q, TEXT("{")))
				{
					const int32 Close = FindMatching(Q);
					if (Close == INDEX_NONE || Close >= Limit)
					{
						return Limit - 1;
					}
					return (Close + 1 < Limit && IsSymbolAt(Close + 1, TEXT(";"))) ? Close + 1 : Close;
				}
				if (IsSymbolAt(Q, TEXT(":")))
				{
					// 생성자 초기화 목록: A(1), B{2} { ... }
					++Q;
					while (Q < Limit)
					{
						if (IsSymbolAt(Q, TEXT(";")))
						{
							return Q;
						}
						if (IsSymbolAt(Q, TEXT("(")) || IsSymbolAt(Q, TEXT("{")))
						{
							const bool bIsBody = IsSymbolAt(Q, TEXT("{")) && !(IsIdentAt(Q - 1) || IsSymbolAt(Q - 1, TEXT(">")));
							const int32 Close = FindMatching(Q);
							if (Close == INDEX_NONE || Close >= Limit)
							{
								return Limit - 1;
							}
							if (bIsBody)
							{
								return (Close + 1 < Limit && IsSymbolAt(Close + 1, TEXT(";"))) ? Close + 1 : Close;
							}
							Q = Close + 1;
							continue;
						}
						++Q;
					}
					return Limit - 1;
				}
				++Q;
			}
			return Limit - 1;
		}

		/** [First, Last] 구간(마지막은 보통 ';')을 멤버 변수 선언으로 해석. 실패하면 false */
		bool ParseMemberDeclaration(int32 First, int32 Last, const FString& Specifiers, bool bHadUProperty, const FString& Comment, FTPStructDef& Def)
		{
			const int32 DeclEnd = IsSymbolAt(Last, TEXT(";")) ? Last : Last + 1; // 미포함
			int32 Cursor = First;

			while (Cursor < DeclEnd && IsIdentAt(Cursor))
			{
				if (IsWordAt(Cursor, TEXT("static")) || IsWordAt(Cursor, TEXT("constexpr")) || IsWordAt(Cursor, TEXT("typedef"))
					|| IsWordAt(Cursor, TEXT("using")) || IsWordAt(Cursor, TEXT("friend")) || IsWordAt(Cursor, TEXT("template")))
				{
					return false;
				}
				if (IsWordAt(Cursor, TEXT("mutable")) || IsWordAt(Cursor, TEXT("inline")) || IsWordAt(Cursor, TEXT("volatile")))
				{
					++Cursor;
					continue;
				}
				break;
			}
			if (Cursor >= DeclEnd)
			{
				return false;
			}

			// 이름 뒤 분할점: '=', '{', ':'(비트필드), '[' 또는 다중 선언의 ','
			int32 Angle = 0;
			int32 Paren = 0;
			int32 Split = DeclEnd;
			TArray<int32> Commas;
			for (int32 K = Cursor; K < DeclEnd; ++K)
			{
				if (Tokens[K].Type != ETokenType::Symbol)
				{
					continue;
				}
				if (IsSymbolAt(K, TEXT("<")))
				{
					++Angle;
				}
				else if (IsSymbolAt(K, TEXT(">")))
				{
					Angle = FMath::Max(0, Angle - 1);
				}
				else if (IsSymbolAt(K, TEXT("(")))
				{
					++Paren;
				}
				else if (IsSymbolAt(K, TEXT(")")))
				{
					Paren = FMath::Max(0, Paren - 1);
				}
				else if (Angle == 0 && Paren == 0)
				{
					if (IsSymbolAt(K, TEXT("=")) || IsSymbolAt(K, TEXT("{")) || IsSymbolAt(K, TEXT(":")) || IsSymbolAt(K, TEXT("[")))
					{
						Split = K;
						break;
					}
					if (IsSymbolAt(K, TEXT(",")))
					{
						Commas.Add(K);
					}
				}
			}

			TArray<int32> NameIndices;
			int32 TypeEnd = INDEX_NONE; // 미포함
			if (Commas.Num() > 0)
			{
				if (Split != DeclEnd)
				{
					// 초기화가 섞인 다중 선언은 원문 보존
					return false;
				}
				const int32 FirstName = Commas[0] - 1;
				if (FirstName <= Cursor || !IsIdentAt(FirstName))
				{
					return false;
				}
				NameIndices.Add(FirstName);
				TypeEnd = FirstName;
				for (int32 CommaIndex = 0; CommaIndex < Commas.Num(); ++CommaIndex)
				{
					const int32 SegStart = Commas[CommaIndex] + 1;
					const int32 SegEnd = (CommaIndex + 1 < Commas.Num()) ? Commas[CommaIndex + 1] : DeclEnd;
					if (SegEnd - SegStart != 1 || !IsIdentAt(SegStart))
					{
						return false;
					}
					NameIndices.Add(SegStart);
				}
			}
			else
			{
				const int32 NameIndex = Split - 1;
				if (NameIndex <= Cursor || !IsIdentAt(NameIndex))
				{
					return false;
				}
				NameIndices.Add(NameIndex);
				TypeEnd = NameIndex;
			}

			FTPTypeRef Type;
			int32 TypeCursor = Cursor;
			if (!ParseType(TypeCursor, TypeEnd, Type) || TypeCursor != TypeEnd)
			{
				return false;
			}
			const FString TypeText = CollapseWhitespace(SourceSpan(Cursor, TypeEnd - 1));

			FString InitText;
			FString DefaultExpr;
			FString ArrayDim;
			int32 BitfieldWidth = 0;
			int32 K = Split;
			while (K < DeclEnd)
			{
				if (IsSymbolAt(K, TEXT("[")))
				{
					const int32 Close = FindMatching(K);
					if (Close == INDEX_NONE || Close >= DeclEnd)
					{
						return false;
					}
					ArrayDim = CollapseWhitespace(SourceSpan(K, Close));
					K = Close + 1;
					continue;
				}
				if (IsSymbolAt(K, TEXT(":")))
				{
					if (K + 1 < DeclEnd && Tokens[K + 1].Type == ETokenType::Number)
					{
						BitfieldWidth = FCString::Atoi(*Tokens[K + 1].Text);
						K += 2;
						continue;
					}
					return false;
				}
				if (IsSymbolAt(K, TEXT("=")))
				{
					if (K + 1 >= DeclEnd)
					{
						return false;
					}
					DefaultExpr = CollapseWhitespace(SourceSpan(K + 1, DeclEnd - 1));
					InitText = FString(TEXT("= ")) + DefaultExpr;
					break;
				}
				if (IsSymbolAt(K, TEXT("{")))
				{
					const int32 Close = FindMatching(K);
					if (Close == INDEX_NONE || Close != DeclEnd - 1)
					{
						return false;
					}
					InitText = CollapseWhitespace(SourceSpan(K, Close));
					DefaultExpr = CollapseWhitespace(SourceSpan(K + 1, Close - 1));
					break;
				}
				return false;
			}

			for (const int32 NameIndex : NameIndices)
			{
				FTPStructMember Member;
				Member.Name = Tokens[NameIndex].Text;
				Member.Type = Type;
				Member.TypeText = TypeText;
				Member.PropertySpecifiers = Specifiers;
				Member.bHadUProperty = bHadUProperty;
				Member.InitText = InitText;
				Member.DefaultExpr = DefaultExpr;
				Member.ArrayDim = ArrayDim;
				Member.BitfieldWidth = BitfieldWidth;
				Member.Comment = Comment;
				if (Member.Comment.IsEmpty())
				{
					FString ToolTip;
					if (FindSpecifierValue(Specifiers, TEXT("ToolTip"), ToolTip))
					{
						Member.Comment = ToolTip;
					}
				}

				FTPStructItem Item;
				Item.Kind = ETPStructItemKind::Member;
				Item.MemberIndex = Def.Members.Num();
				Def.Members.Add(MoveTemp(Member));
				Def.Items.Add(Item);
			}
			return true;
		}

		/** Index 부터 End(미포함)까지를 타입으로 해석 */
		bool ParseType(int32& Index, int32 End, FTPTypeRef& OutType) const
		{
			while (Index < End && IsIdentAt(Index))
			{
				if (IsWordAt(Index, TEXT("const")))
				{
					OutType.bConst = true;
					++Index;
					continue;
				}
				if (IsWordAt(Index, TEXT("class")) || IsWordAt(Index, TEXT("struct")) || IsWordAt(Index, TEXT("enum"))
					|| IsWordAt(Index, TEXT("typename")) || IsWordAt(Index, TEXT("volatile")))
				{
					++Index;
					continue;
				}
				break;
			}
			if (Index >= End)
			{
				return false;
			}

			if (Tokens[Index].Type == ETokenType::Number)
			{
				// 타입이 아닌 템플릿 인자 (TInlineAllocator<4> 등)
				OutType.Name = Tokens[Index].Text;
				++Index;
				return true;
			}

			if (IsSymbolAt(Index, TEXT("::")))
			{
				++Index;
			}
			if (Index >= End || !IsIdentAt(Index))
			{
				return false;
			}

			OutType.Name = Tokens[Index].Text;
			++Index;

			// unsigned int, long long 같은 여러 단어 기본 타입
			if (OutType.Name.Equals(TEXT("unsigned"), ESearchCase::CaseSensitive) || OutType.Name.Equals(TEXT("signed"), ESearchCase::CaseSensitive)
				|| OutType.Name.Equals(TEXT("long"), ESearchCase::CaseSensitive) || OutType.Name.Equals(TEXT("short"), ESearchCase::CaseSensitive))
			{
				while (Index < End && (IsWordAt(Index, TEXT("int")) || IsWordAt(Index, TEXT("char")) || IsWordAt(Index, TEXT("long")) || IsWordAt(Index, TEXT("short"))))
				{
					OutType.Name += TEXT(" ");
					OutType.Name += Tokens[Index].Text;
					++Index;
				}
			}

			while (Index + 1 < End && IsSymbolAt(Index, TEXT("::")) && IsIdentAt(Index + 1))
			{
				OutType.Name += TEXT("::");
				OutType.Name += Tokens[Index + 1].Text;
				Index += 2;
			}

			if (Index < End && IsSymbolAt(Index, TEXT("<")))
			{
				++Index;
				while (Index < End && !IsSymbolAt(Index, TEXT(">")))
				{
					FTPTypeRef Arg;
					if (!ParseType(Index, End, Arg))
					{
						return false;
					}
					OutType.Args.Add(MoveTemp(Arg));
					if (Index < End && IsSymbolAt(Index, TEXT(",")))
					{
						++Index;
					}
					else if (!(Index < End && IsSymbolAt(Index, TEXT(">"))))
					{
						return false;
					}
				}
				if (Index >= End)
				{
					return false;
				}
				++Index; // '>'
			}

			while (Index < End)
			{
				if (IsSymbolAt(Index, TEXT("*")))
				{
					++OutType.PointerDepth;
					++Index;
				}
				else if (IsSymbolAt(Index, TEXT("&")))
				{
					OutType.bReference = true;
					++Index;
				}
				else if (IsWordAt(Index, TEXT("const")))
				{
					++Index;
				}
				else
				{
					break;
				}
			}
			return true;
		}
	};
}

FTPParseResult FTypePasteParser::Parse(const FString& Source)
{
	using namespace TypePasteParserPrivate;

	FTPParseResult Result;
	TArray<FToken> Tokens = Tokenize(Source);
	FParser Parser(Source, MoveTemp(Tokens), Result);
	Parser.Run();

	if (!Result.HasAnyType() && !Result.HasErrors() && !Source.TrimStartAndEnd().IsEmpty())
	{
		Result.AddWarning(TEXT("열거형(enum)이나 구조체(struct) 선언을 찾지 못했습니다. AI 답변의 코드 부분이 포함되었는지 확인해 주세요."));
	}
	return Result;
}
