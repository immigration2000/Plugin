// TypePaste - parser implementation

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
		/** Start offset in the source text */
		int32 Start = 0;
		/** End offset in the source text (exclusive) */
		int32 End = 0;
		int32 Line = 0;
		/** Comments on the lines before this token */
		FString LeadingComment;
		/** Comment after this token on the same line */
		FString TrailingComment;
	};

	/** Whether it is a decorative comment such as "// ========" */
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

	/** Strips the comment markers and keeps only the text */
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
			// Doxygen trailing comment markers (//!< , /**< )
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

			// Markdown code fence (```cpp) or inline code marker: ignore until the end of the line
			if (C == TEXT('`'))
			{
				while (Pos < Len && Source[Pos] != TEXT('\n'))
				{
					++Pos;
				}
				continue;
			}

			// Preprocessor directives (#include, #pragma) and markdown headings (#)
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

			// Line comment
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

			// Block comment
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
				// Only 'a' or '\n' forms count as character literals (apostrophes in prose are ignored)
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
		// Token helpers
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

		/** Index of the matching bracket for an opening ( { [. INDEX_NONE if none */
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

		/** Source text of the token range [First, Last] (including comments/whitespace in between) */
		FString SourceSpan(int32 First, int32 Last) const
		{
			if (First > Last || !Tokens.IsValidIndex(First) || !Tokens.IsValidIndex(Last))
			{
				return FString();
			}
			return Source.Mid(Tokens[First].Start, Tokens[Last].End - Tokens[First].Start);
		}

		/**
		 * Description comment attached to the range.
		 * If bIncludeInner is true, comments on tokens inside the range are searched too (when the source is rebuilt, as for members).
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
			return FString(TEXT("[line ")) + FString::FromInt(Tokens[TokenIndex].Line) + TEXT("] ");
		}

		bool IsNameTaken(const FString& Name) const
		{
			return Result.FindEnum(Name) != nullptr || Result.FindStruct(Name) != nullptr;
		}

		// ------------------------------------------------------------------
		// Top-level declarations
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
					Result.AddError(LineLabel(MacroIndex) + MacroName + TEXT(": the parentheses are not closed."));
					Pos = MacroIndex + 1;
					return;
				}
				Specifiers = CollapseWhitespace(SourceSpan(Cursor + 1, Close - 1));
				Cursor = Close + 1;
			}

			const TCHAR* Keyword = bIsEnum ? TEXT("enum") : TEXT("struct");
			if (!IsWordAt(Cursor, Keyword))
			{
				Result.AddWarning(LineLabel(MacroIndex) + MacroName + TEXT(" is not followed by a ") + Keyword + TEXT(" declaration, skipped."));
				Pos = Cursor;
				return;
			}

			const bool bParsed = bIsEnum
				? ParseEnum(Cursor, Specifiers, true, Comment)
				: ParseStruct(Cursor, Specifiers, true, Comment);
			if (!bParsed)
			{
				Result.AddError(LineLabel(Cursor) + MacroName + TEXT(": could not parse the declaration. Please check the syntax."));
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

			Result.AddWarning(LineLabel(MacroIndex) + Tokens[MacroIndex].Text + TEXT(" classes are not supported, skipped. (Only enums/structs are generated)"));
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
		// Enums
		// ------------------------------------------------------------------

		/** Start is the 'enum' token. Returns false if it is not an enum declaration (Pos unchanged) */
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
				// Forward declaration -> ignore
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
				Result.AddError(LineLabel(Start) + TEXT("Enum '") + Def.Name + TEXT("': the braces are not closed."));
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
					Result.AddWarning(LineLabel(Q) + TEXT("Enum '") + Def.Name + TEXT("': skipped '") + Tokens[Q].Text + TEXT("', which could not be parsed."));
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
					Result.AddWarning(LineLabel(EntryFirst) + TEXT("Enum '") + Def.Name + TEXT("': entry '") + Entry.Name + TEXT("' is duplicated, only one is used."));
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
				Result.AddWarning(LineLabel(Start) + TEXT("'") + Def.Name + TEXT("' is defined twice, only the first one is used."));
				return true;
			}
			if (Def.Entries.Num() == 0)
			{
				Result.AddWarning(LineLabel(Start) + TEXT("Enum '") + Def.Name + TEXT("' has no entries."));
			}
			Result.Enums.Add(MoveTemp(Def));
			return true;
		}

		// ------------------------------------------------------------------
		// Structs
		// ------------------------------------------------------------------

		/** Start is the 'struct' token. Returns false if it is not a struct declaration (Pos unchanged) */
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
				// Forward declaration -> ignore
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
				Result.AddError(LineLabel(Start) + TEXT("Struct '") + Def.Name + TEXT("': the braces are not closed."));
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
				Result.AddWarning(LineLabel(Start) + TEXT("'") + Def.Name + TEXT("' is defined twice, only the first one is used."));
				return true;
			}
			if (Def.Members.Num() == 0 && Def.NumRawItems() == 0)
			{
				Result.AddWarning(LineLabel(Start) + TEXT("Struct '") + Def.Name + TEXT("' has no members."));
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
							Result.AddError(LineLabel(StatementStart) + TEXT("Struct '") + Def.Name + TEXT("': the UPROPERTY parentheses are not closed."));
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

					const TCHAR* What = (bForceRaw || bIsFunction) ? TEXT("function/constructor") : TEXT("statement");
					Result.AddInfo(LineLabel(StatementStart) + TEXT("Struct '") + Def.Name + TEXT("': keeping a ") + What
						+ TEXT(" verbatim. (Kept in C++ output only, not reflected in Blueprint structs)"));
				}

				Q = StatementLast + 1;
			}
		}

		/** Finds the last token index of a statement (';' or the '}' of a function body) */
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
					// Operator overloads: operator==(...), operator()(...), operator bool() ...
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
						// '(' right after the name -> function/constructor declaration
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

		/** Skips from after the parameter list to the end of the function */
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
					// Constructor initializer list: A(1), B{2} { ... }
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

		/** Parses the range [First, Last] (Last is usually ';') as a member variable declaration. Returns false on failure */
		bool ParseMemberDeclaration(int32 First, int32 Last, const FString& Specifiers, bool bHadUProperty, const FString& Comment, FTPStructDef& Def)
		{
			const int32 DeclEnd = IsSymbolAt(Last, TEXT(";")) ? Last : Last + 1; // exclusive
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

			// Split point after the name: '=', '{', ':' (bitfield), '[' or ',' of a multiple declaration
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
			int32 TypeEnd = INDEX_NONE; // exclusive
			if (Commas.Num() > 0)
			{
				if (Split != DeclEnd)
				{
					// Multiple declarations mixed with initializers are kept verbatim
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

		/** Parses Index up to End (exclusive) as a type */
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
				// Template argument that is not a type (TInlineAllocator<4> etc.)
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

			// Multi-word primitive types such as unsigned int, long long
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

	// Generated output is English only (D16): non-ASCII text is removed here so both Blueprint and C++ output get it
	TypePasteUtils::EnforceEnglishOnly(Result);

	if (!Result.HasAnyType() && !Result.HasErrors() && !Source.TrimStartAndEnd().IsEmpty())
	{
		Result.AddWarning(TEXT("No enum or struct declaration was found. Make sure the code part of the AI answer is included."));
	}
	return Result;
}
