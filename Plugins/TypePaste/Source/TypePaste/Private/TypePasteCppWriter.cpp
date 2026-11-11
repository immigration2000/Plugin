// TypePaste - C++ header generator implementation

#include "TypePasteCppWriter.h"

namespace TypePasteCppWriterPrivate
{
	using namespace TypePasteUtils;

	struct FWriteContext
	{
		const FTPParseResult& Parsed;
		const FTPCppWriteOptions& Options;
		const ITPExternalTypeInfo* TypeInfo;
		TArray<FTPMessage>& Messages;
	};

	static void AddMessageUnique(TArray<FTPMessage>& Messages, ETPSeverity Severity, const FString& Text)
	{
		for (const FTPMessage& Existing : Messages)
		{
			if (Existing.Severity == Severity && Existing.Text.Equals(Text, ESearchCase::CaseSensitive))
			{
				return;
			}
		}
		Messages.Add(FTPMessage(Severity, Text));
	}

	class FLineBuilder
	{
	public:
		void Add(const FString& Line)
		{
			Lines.Add(Line);
		}

		/** One blank line (no consecutive blank lines) */
		void Blank()
		{
			if (Lines.Num() > 0 && !Lines.Last().IsEmpty())
			{
				Lines.Add(FString());
			}
		}

		void Append(const FLineBuilder& Other)
		{
			for (const FString& Line : Other.Lines)
			{
				Lines.Add(Line);
			}
		}

		FString Join(const FString& LineEnding) const
		{
			int32 Count = Lines.Num();
			while (Count > 0 && Lines[Count - 1].IsEmpty())
			{
				--Count;
			}
			FString Result;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Result += Lines[Index];
				Result += LineEnding;
			}
			return Result;
		}

		TArray<FString> Lines;
	};

	/** Types usable with CoreMinimal.h alone */
	static bool IsCoreMinimalType(const FString& Name)
	{
		static const TCHAR* Names[] =
		{
			TEXT("bool"), TEXT("int"), TEXT("int8"), TEXT("int16"), TEXT("int32"), TEXT("int64"),
			TEXT("uint8"), TEXT("uint16"), TEXT("uint32"), TEXT("uint64"), TEXT("float"), TEXT("double"),
			TEXT("FString"), TEXT("FName"), TEXT("FText"),
			TEXT("FVector"), TEXT("FVector2D"), TEXT("FVector4"), TEXT("FVector3f"), TEXT("FVector3d"),
			TEXT("FRotator"), TEXT("FQuat"), TEXT("FTransform"), TEXT("FMatrix"), TEXT("FPlane"),
			TEXT("FLinearColor"), TEXT("FColor"), TEXT("FIntPoint"), TEXT("FIntVector"), TEXT("FIntVector4"),
			TEXT("FBox"), TEXT("FBox2D"), TEXT("FBoxSphereBounds"), TEXT("FGuid"), TEXT("FDateTime"), TEXT("FTimespan"),
			TEXT("TArray"), TEXT("TSet"), TEXT("TMap"), TEXT("TOptional"), TEXT("TEnumAsByte"), TEXT("UObject"),
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

	/** Include paths and required modules of commonly used types */
	static bool FindKnownInclude(const FString& Name, FString& OutInclude, FString& OutModule)
	{
		struct FEntry
		{
			const TCHAR* Type;
			const TCHAR* Include;
			const TCHAR* Module;
		};
		static const FEntry Entries[] =
		{
			{ TEXT("TObjectPtr"), TEXT("UObject/ObjectPtr.h"), TEXT("") },
			{ TEXT("TSubclassOf"), TEXT("Templates/SubclassOf.h"), TEXT("") },
			{ TEXT("TSoftObjectPtr"), TEXT("UObject/SoftObjectPtr.h"), TEXT("") },
			{ TEXT("TSoftClassPtr"), TEXT("UObject/SoftObjectPtr.h"), TEXT("") },
			{ TEXT("TWeakObjectPtr"), TEXT("UObject/WeakObjectPtrTemplates.h"), TEXT("") },
			{ TEXT("TLazyObjectPtr"), TEXT("UObject/LazyObjectPtr.h"), TEXT("") },
			{ TEXT("TScriptInterface"), TEXT("UObject/ScriptInterface.h"), TEXT("") },
			{ TEXT("FSoftObjectPath"), TEXT("UObject/SoftObjectPath.h"), TEXT("") },
			{ TEXT("FSoftClassPath"), TEXT("UObject/SoftObjectPath.h"), TEXT("") },
			{ TEXT("FPrimaryAssetId"), TEXT("UObject/PrimaryAssetId.h"), TEXT("") },
			{ TEXT("FPrimaryAssetType"), TEXT("UObject/PrimaryAssetId.h"), TEXT("") },
			{ TEXT("FInstancedStruct"), TEXT("StructUtils/InstancedStruct.h"), TEXT("") },
			{ TEXT("FGameplayTag"), TEXT("GameplayTagContainer.h"), TEXT("GameplayTags") },
			{ TEXT("FGameplayTagContainer"), TEXT("GameplayTagContainer.h"), TEXT("GameplayTags") },
			{ TEXT("FGameplayTagQuery"), TEXT("GameplayTagContainer.h"), TEXT("GameplayTags") },
			{ TEXT("FTableRowBase"), TEXT("Engine/DataTable.h"), TEXT("") },
			{ TEXT("FDataTableRowHandle"), TEXT("Engine/DataTable.h"), TEXT("") },
			{ TEXT("FHitResult"), TEXT("Engine/HitResult.h"), TEXT("") },
		};
		for (const FEntry& Entry : Entries)
		{
			if (Name.Equals(Entry.Type, ESearchCase::CaseSensitive))
			{
				OutInclude = Entry.Include;
				OutModule = Entry.Module;
				return true;
			}
		}
		return false;
	}

	static bool IsDefaultModule(const FString& Module)
	{
		return Module.IsEmpty() || Module.Equals(TEXT("Core")) || Module.Equals(TEXT("CoreUObject")) || Module.Equals(TEXT("Engine"));
	}

	static void CollectTemplateNames(const FTPTypeRef& Type, TArray<FString>& OutNames)
	{
		if (Type.Args.Num() > 0)
		{
			OutNames.AddUnique(Type.Name);
		}
		for (const FTPTypeRef& Arg : Type.Args)
		{
			CollectTemplateNames(Arg, OutNames);
		}
	}

	/** UTexture2D* -> true (raw pointer to a UObject type) */
	static bool IsRawObjectPointer(const FTPTypeRef& Type)
	{
		if (Type.PointerDepth != 1 || Type.Args.Num() > 0 || Type.Name.Len() < 2)
		{
			return false;
		}
		const TCHAR First = Type.Name[0];
		const TCHAR Second = Type.Name[1];
		return (First == TEXT('U') || First == TEXT('A')) && Second >= TEXT('A') && Second <= TEXT('Z');
	}

	static bool IsContainerName(const FString& Name)
	{
		return Name.Equals(TEXT("TArray"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("TSet"), ESearchCase::CaseSensitive)
			|| Name.Equals(TEXT("TMap"), ESearchCase::CaseSensitive);
	}

	/** A container inside a container, such as TArray<TArray<int32>>, TMap<FName, TArray<int32>> */
	static bool IsNestedContainer(const FTPTypeRef& Type)
	{
		if (!IsContainerName(Type.Name))
		{
			return false;
		}
		for (const FTPTypeRef& Arg : Type.Args)
		{
			if (IsContainerName(Arg.Name))
			{
				return true;
			}
		}
		return false;
	}

	/** Integer types Blueprints do not support */
	static bool IsBlueprintUnsupportedInt(const FTPTypeRef& Type)
	{
		if (Type.Args.Num() > 0 || Type.PointerDepth > 0)
		{
			return false;
		}
		const FString Name = Type.GetCleanName();
		return Name.Equals(TEXT("int8"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("int16"), ESearchCase::CaseSensitive)
			|| Name.Equals(TEXT("uint16"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("uint32"), ESearchCase::CaseSensitive)
			|| Name.Equals(TEXT("uint64"), ESearchCase::CaseSensitive);
	}

	static bool WillHaveUProperty(const FTPStructMember& Member, const FTPCppWriteOptions& Options)
	{
		return Member.bHadUProperty || Options.bAddMissingUProperty;
	}

	static bool ShouldConvertPointer(const FTPStructMember& Member, const FTPCppWriteOptions& Options)
	{
		return Options.bConvertRawObjectPointers && WillHaveUProperty(Member, Options) && IsRawObjectPointer(Member.Type);
	}

	static void AddDocComment(FLineBuilder& Out, const FString& Indent, const FString& Comment)
	{
		if (Comment.IsEmpty())
		{
			return;
		}
		TArray<FString> Lines;
		Comment.Replace(TEXT("*/"), TEXT("* /")).ParseIntoArrayLines(Lines, true);
		if (Lines.Num() == 0)
		{
			return;
		}
		if (Lines.Num() == 1)
		{
			Out.Add(Indent + TEXT("/** ") + Lines[0] + TEXT(" */"));
			return;
		}
		Out.Add(Indent + TEXT("/**"));
		for (const FString& Line : Lines)
		{
			Out.Add(Indent + TEXT(" * ") + Line);
		}
		Out.Add(Indent + TEXT(" */"));
	}

	/** Appends a raw statement, only fixing indentation */
	static void AppendRaw(FLineBuilder& Out, const FString& RawText)
	{
		TArray<FString> Lines;
		RawText.ParseIntoArrayLines(Lines, false);
		if (Lines.Num() == 0)
		{
			return;
		}

		Out.Add(FString(TEXT("\t")) + Lines[0].TrimStartAndEnd());

		int32 MinIndent = MAX_int32;
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			const FString& Line = Lines[Index];
			if (Line.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			int32 Indent = 0;
			while (Indent < Line.Len() && (Line[Indent] == TEXT(' ') || Line[Indent] == TEXT('\t')))
			{
				++Indent;
			}
			MinIndent = FMath::Min(MinIndent, Indent);
		}
		if (MinIndent == MAX_int32)
		{
			MinIndent = 0;
		}

		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			const FString& Line = Lines[Index];
			if (Line.TrimStartAndEnd().IsEmpty())
			{
				Out.Add(FString());
				continue;
			}
			Out.Add(FString(TEXT("\t")) + Line.RightChop(MinIndent).TrimEnd());
		}
	}

	static FString MakeDefaultInitializer(const FTPStructMember& Member, const FTPParseResult& Parsed)
	{
		const FTPTypeRef& Type = Member.Type;
		if (Type.PointerDepth > 0)
		{
			return TEXT("nullptr");
		}
		if (Type.Args.Num() > 0)
		{
			return FString();
		}

		const FString Name = Type.GetCleanName();
		switch (GetPrimitiveKind(Name))
		{
		case ETPPrimitiveKind::Bool:
			return TEXT("false");
		case ETPPrimitiveKind::Int:
		case ETPPrimitiveKind::Int64:
		case ETPPrimitiveKind::Byte:
			return TEXT("0");
		case ETPPrimitiveKind::Real:
			return Name.Equals(TEXT("float"), ESearchCase::CaseSensitive) ? TEXT("0.f") : TEXT("0.0");
		default:
			break;
		}

		if (const FTPEnumDef* Enum = Parsed.FindEnum(Name))
		{
			const TArray<int32> Visible = Enum->GetVisibleEntryIndices();
			if (Visible.Num() > 0)
			{
				return Name + TEXT("::") + Enum->Entries[Visible[0]].Name;
			}
		}
		return FString();
	}

	/**
	 * UHT uses the comment directly above as ToolTip metadata, so a comment plus a separate ToolTip specifier
	 * fails with "Metadata key 'ToolTip' first seen with value A then B". When there is a ToolTip, the comment is not written.
	 */
	static void AddDocCommentUnlessToolTip(FLineBuilder& Out, const FString& Indent, const FString& Comment, const FString& Specifiers,
		const FString& Label, const FWriteContext& Ctx)
	{
		FString ToolTip;
		if (Comment.IsEmpty() || !FindSpecifierValue(Specifiers, TEXT("ToolTip"), ToolTip))
		{
			AddDocComment(Out, Indent, Comment);
			return;
		}
		if (!ToolTip.Equals(Comment, ESearchCase::CaseSensitive))
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, Label
				+ TEXT(" has a ToolTip specifier, so the comment was not written. (Having both makes UHT report a duplicate ToolTip)"));
		}
	}

	static void WriteEnum(FLineBuilder& Out, const FTPEnumDef& Def, const FWriteContext& Ctx)
	{
		AddDocCommentUnlessToolTip(Out, FString(), Def.Comment, Def.bHadMacro ? Def.MacroSpecifiers : FString(), Def.Name, Ctx);

		const FString Specifiers = Def.bHadMacro ? Def.MacroSpecifiers : FString(TEXT("BlueprintType"));
		Out.Add(FString(TEXT("UENUM(")) + Specifiers + TEXT(")"));

		FString Underlying = Def.UnderlyingType;
		if (Underlying.IsEmpty())
		{
			Underlying = TEXT("uint8");
		}
		else if (!Underlying.Equals(TEXT("uint8"), ESearchCase::CaseSensitive) && HasSpecifierFlag(Specifiers, TEXT("BlueprintType")))
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, FString(TEXT("Enum '")) + Def.Name
				+ TEXT("' is BlueprintType but its underlying type is not uint8. UHT may report an error."));
		}
		if (!Def.bScoped)
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("Enum '")) + Def.Name + TEXT("' is generated as an enum class."));
		}

		Out.Add(FString(TEXT("enum class ")) + Def.Name + TEXT(" : ") + Underlying);
		Out.Add(TEXT("{"));
		for (const FTPEnumEntry& Entry : Def.Entries)
		{
			AddDocCommentUnlessToolTip(Out, TEXT("\t"), Entry.Comment, Entry.UMetaRaw, Def.Name + TEXT("::") + Entry.Name, Ctx);
			FString Line = FString(TEXT("\t")) + Entry.Name;
			if (!Entry.ValueExpr.IsEmpty())
			{
				Line += TEXT(" = ");
				Line += Entry.ValueExpr;
			}
			if (!Entry.UMetaRaw.IsEmpty())
			{
				Line += TEXT(" UMETA(");
				Line += Entry.UMetaRaw;
				Line += TEXT(")");
			}
			Line += TEXT(",");
			Out.Add(Line);
		}
		Out.Add(TEXT("};"));
	}

	static void WriteStruct(FLineBuilder& Out, const FTPStructDef& Def, const FWriteContext& Ctx)
	{
		if (!Def.Name.StartsWith(TEXT("F"), ESearchCase::CaseSensitive))
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, FString(TEXT("Struct '")) + Def.Name
				+ TEXT("' does not start with F. USTRUCT names need the F prefix, otherwise UHT fails."));
		}

		const FString Specifiers = Def.bHadMacro ? Def.MacroSpecifiers : FString(TEXT("BlueprintType"));
		AddDocCommentUnlessToolTip(Out, FString(), Def.Comment, Specifiers, Def.Name, Ctx);
		Out.Add(FString(TEXT("USTRUCT(")) + Specifiers + TEXT(")"));

		FString Header = TEXT("struct ");
		if (!Ctx.Options.ApiMacro.IsEmpty())
		{
			Header += Ctx.Options.ApiMacro;
			Header += TEXT(" ");
		}
		Header += Def.Name;
		if (!Def.ParentText.IsEmpty())
		{
			Header += TEXT(" : ");
			Header += Def.ParentText;
		}
		Out.Add(Header);
		Out.Add(TEXT("{"));
		Out.Add(TEXT("\tGENERATED_BODY()"));

		const FString Category = StripUnrealPrefix(Def.Name);
		int32 NumConvertedPointers = 0;
		int32 NumUnwrappedEnums = 0;

		for (const FTPStructItem& Item : Def.Items)
		{
			Out.Blank();

			if (Item.Kind == ETPStructItemKind::Raw)
			{
				AddDocComment(Out, TEXT("\t"), Item.Comment);
				AppendRaw(Out, Item.RawText);
				continue;
			}

			// Copy for output (the type is adjusted below)
			FTPStructMember Member = Def.Members[Item.MemberIndex];

			// If EFoo in TEnumAsByte<EFoo> is an enum class created here, write just EFoo
			if (Member.Type.Name.Equals(TEXT("TEnumAsByte"), ESearchCase::CaseSensitive) && Member.Type.Args.Num() == 1
				&& Ctx.Parsed.FindEnum(Member.Type.Args[0].GetCleanName()) != nullptr)
			{
				const FTPTypeRef Inner = Member.Type.Args[0];
				Member.Type = Inner;
				Member.TypeText = Inner.GetCleanName();
				++NumUnwrappedEnums;
			}

			const FString MemberLabel = Def.Name + TEXT("::") + Member.Name;
			AddDocCommentUnlessToolTip(Out, TEXT("\t"), Member.Comment, Member.bHadUProperty ? Member.PropertySpecifiers : FString(), MemberLabel, Ctx);
			const bool bNestedContainer = IsNestedContainer(Member.Type);
			const bool bUnsupportedInt = IsBlueprintUnsupportedInt(Member.Type);

			FString Specs;
			bool bHasProperty = false;
			if (Member.bHadUProperty)
			{
				Specs = Member.PropertySpecifiers;
				bHasProperty = true;
			}
			else if (Ctx.Options.bAddMissingUProperty)
			{
				if (bNestedContainer)
				{
					AddMessageUnique(Ctx.Messages, ETPSeverity::Info, MemberLabel
						+ TEXT(" is a nested container, which reflection does not support, so no UPROPERTY was added."));
				}
				else
				{
					// Types that cannot be exposed to Blueprints get it without BlueprintReadWrite
					const bool bBlueprintCompatible = !bUnsupportedInt && Member.ArrayDim.IsEmpty();
					Specs = bBlueprintCompatible
						? FString(TEXT("EditAnywhere, BlueprintReadWrite, Category = \"")) + Category + TEXT("\"")
						: FString(TEXT("EditAnywhere, Category = \"")) + Category + TEXT("\"");
					bHasProperty = true;
				}
			}
			if (bHasProperty)
			{
				Out.Add(FString(TEXT("\tUPROPERTY(")) + Specs + TEXT(")"));
			}

			// Warn about combinations UHT rejects
			if (bHasProperty && bNestedContainer)
			{
				AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, MemberLabel
					+ TEXT(" is a nested container (TArray<TArray<>> etc.) and cannot be a UPROPERTY. Wrap the inner one in a struct."));
			}
			const bool bBlueprintVisible = bHasProperty
				&& (HasSpecifierFlag(Specs, TEXT("BlueprintReadWrite")) || HasSpecifierFlag(Specs, TEXT("BlueprintReadOnly")));
			if (bBlueprintVisible)
			{
				if (bUnsupportedInt)
				{
					AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, MemberLabel + TEXT(": type '") + Member.Type.GetCleanName()
						+ TEXT("' cannot be exposed to Blueprints. Use int32/int64/uint8 instead."));
				}
				if (!Member.ArrayDim.IsEmpty())
				{
					AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, MemberLabel
						+ TEXT(" is a fixed-size array and cannot be exposed to Blueprints. Use TArray instead."));
				}
			}

			FString TypeText = Member.TypeText;
			if (ShouldConvertPointer(Member, Ctx.Options))
			{
				TypeText = FString(TEXT("TObjectPtr<")) + (Member.Type.bConst ? TEXT("const ") : TEXT("")) + Member.Type.Name + TEXT(">");
				++NumConvertedPointers;
			}

			FString Line = FString(TEXT("\t")) + TypeText + TEXT(" ") + Member.Name + Member.ArrayDim;
			if (Member.BitfieldWidth > 0)
			{
				Line += TEXT(" : ");
				Line += FString::FromInt(Member.BitfieldWidth);
			}
			if (!Member.InitText.IsEmpty())
			{
				if (Member.InitText.StartsWith(TEXT("=")))
				{
					Line += TEXT(" ");
				}
				Line += Member.InitText;
			}
			else if (Ctx.Options.bAddDefaultInitializers && Member.BitfieldWidth == 0 && Member.ArrayDim.IsEmpty())
			{
				const FString Init = MakeDefaultInitializer(Member, Ctx.Parsed);
				if (!Init.IsEmpty())
				{
					Line += TEXT(" = ");
					Line += Init;
				}
			}
			Line += TEXT(";");
			Out.Add(Line);
		}

		Out.Add(TEXT("};"));

		if (NumUnwrappedEnums > 0)
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("Struct '")) + Def.Name
				+ TEXT("': replaced TEnumAsByte<Enum> with the enum type. (enum class does not need TEnumAsByte)"));
		}
		if (NumConvertedPointers > 0)
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("Struct '")) + Def.Name + TEXT("': converted ")
				+ FString::FromInt(NumConvertedPointers) + TEXT(" raw pointer(s) to TObjectPtr<>. (Recommended in UE5)"));
		}
	}

	/** Collects the external types the structs use and builds the include / forward declaration lists */
	static void CollectDependencies(const TArray<const FTPStructDef*>& Structs, const FString& CurrentFile,
		const TMap<FString, FString>& LocalTypeFiles, const FWriteContext& Ctx,
		TArray<FString>& OutIncludes, TArray<FString>& OutForwardDecls)
	{
		TArray<FString> ByValue;
		TArray<FString> ByPointer;
		TArray<FString> Templates;
		TArray<FString> ModuleHints;

		for (const FTPStructDef* Def : Structs)
		{
			for (const FTPStructMember& Member : Def->Members)
			{
				CollectTypeUsages(Member.Type, ByValue, ByPointer);
				CollectTemplateNames(Member.Type, Templates);
				if (ShouldConvertPointer(Member, Ctx.Options))
				{
					Templates.AddUnique(TEXT("TObjectPtr"));
				}
			}
			if (!Def->ParentName.IsEmpty())
			{
				ByValue.AddUnique(Def->ParentName);
			}
		}

		auto AddInclude = [&OutIncludes, &ModuleHints](const FString& Include, const FString& Module)
		{
			if (!Include.IsEmpty())
			{
				OutIncludes.AddUnique(Include);
			}
			if (!IsDefaultModule(Module))
			{
				ModuleHints.AddUnique(Module);
			}
		};

		for (const FString& Name : Templates)
		{
			FString Include;
			FString Module;
			if (FindKnownInclude(Name, Include, Module))
			{
				AddInclude(Include, Module);
			}
		}

		for (const FString& Name : ByValue)
		{
			if (IsCoreMinimalType(Name))
			{
				continue;
			}
			if (const FString* File = LocalTypeFiles.Find(Name))
			{
				if (!File->Equals(CurrentFile, ESearchCase::CaseSensitive))
				{
					OutIncludes.AddUnique(*File + TEXT(".h"));
				}
				continue;
			}
			FString Include;
			FString Module;
			if (FindKnownInclude(Name, Include, Module))
			{
				AddInclude(Include, Module);
				continue;
			}
			if (Ctx.TypeInfo && Ctx.TypeInfo->FindTypeHeader(Name, Include, Module))
			{
				AddInclude(Include, Module);
				continue;
			}
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("'")) + Name
				+ TEXT("': could not find the header that defines this type. Add the #include yourself if it does not compile."));
		}

		for (const FString& Name : ByPointer)
		{
			if (Name.Equals(TEXT("UObject"), ESearchCase::CaseSensitive) || LocalTypeFiles.Contains(Name) || ByValue.Contains(Name))
			{
				continue;
			}
			if (Name.Len() >= 2 && (Name[0] == TEXT('U') || Name[0] == TEXT('A') || Name[0] == TEXT('I')) && Name[1] >= TEXT('A') && Name[1] <= TEXT('Z'))
			{
				OutForwardDecls.AddUnique(Name);
			}
		}

		if (ModuleHints.Num() > 0)
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, FString(TEXT("The module's Build.cs may need these dependency modules: "))
				+ FString::Join(ModuleHints, TEXT(", ")));
		}
	}

	static FString BuildHeaderText(const FString& FileBaseName, const TArray<FString>& Includes, const TArray<FString>& ForwardDecls,
		const FLineBuilder& Body, const FTPCppWriteOptions& Options)
	{
		FLineBuilder Out;
		Out.Add(TEXT("// Generated by the TypePaste plugin."));
		Out.Add(FString());
		Out.Add(TEXT("#pragma once"));
		Out.Add(FString());
		Out.Add(TEXT("#include \"CoreMinimal.h\""));
		for (const FString& Include : Includes)
		{
			Out.Add(FString(TEXT("#include \"")) + Include + TEXT("\""));
		}
		Out.Add(FString(TEXT("#include \"")) + FileBaseName + TEXT(".generated.h\""));

		if (ForwardDecls.Num() > 0)
		{
			Out.Add(FString());
			for (const FString& Name : ForwardDecls)
			{
				Out.Add(FString(TEXT("class ")) + Name + TEXT(";"));
			}
		}
		Out.Add(FString());
		Out.Append(Body);
		return Out.Join(Options.LineEnding);
	}

	/** Last safety net: generated headers are ASCII only (D16). Anything left is reported as an error, which blocks the export */
	static void CheckAsciiOnly(const FTPCppFile& File, TArray<FTPMessage>& Messages)
	{
		if (!IsAsciiText(File.Content))
		{
			AddMessageUnique(Messages, ETPSeverity::Error, File.FileName
				+ TEXT(" still contains non-English characters and cannot be created. Remove Korean or other non-English text from the input and try again."));
		}
	}

	static void ReportCycles(const TArray<FString>& CycleNames, TArray<FTPMessage>& Messages)
	{
		if (CycleNames.Num() > 0)
		{
			AddMessageUnique(Messages, ETPSeverity::Error, FString(TEXT("Structs contain each other (or themselves) by value: "))
				+ FString::Join(CycleNames, TEXT(", ")) + TEXT(". Change one of them to a TArray or a pointer."));
		}
	}
}

FString FTypePasteCppWriter::SanitizeFileBaseName(const FString& In)
{
	FString Name = In.TrimStartAndEnd();
	if (Name.EndsWith(TEXT(".h")))
	{
		Name.LeftChopInline(2);
	}
	FString Out;
	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		if (TypePasteUtils::IsIdentChar(Name[Index]))
		{
			Out.AppendChar(Name[Index]);
		}
	}
	if (Out.IsEmpty())
	{
		Out = TEXT("TypePasteTypes");
	}
	if (TypePasteUtils::IsDigitChar(Out[0]))
	{
		Out = FString(TEXT("T")) + Out;
	}
	return Out;
}

FString FTypePasteCppWriter::SuggestFileBaseName(const FTPParseResult& Parsed)
{
	const int32 Total = Parsed.Enums.Num() + Parsed.Structs.Num();
	if (Total == 0)
	{
		return TEXT("TypePasteTypes");
	}
	const FString FirstName = Parsed.Structs.Num() > 0 ? Parsed.Structs[0].Name : Parsed.Enums[0].Name;
	const FString Base = SanitizeFileBaseName(TypePasteUtils::StripUnrealPrefix(FirstName));
	return Total == 1 ? Base : Base + TEXT("Types");
}

FTPCppFile FTypePasteCppWriter::WriteSingleFile(const FTPParseResult& Parsed, const FString& FileBaseName, const FTPCppWriteOptions& Options,
	const ITPExternalTypeInfo* TypeInfo, TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteCppWriterPrivate;

	const FString BaseName = SanitizeFileBaseName(FileBaseName.TrimStartAndEnd().IsEmpty() ? SuggestFileBaseName(Parsed) : FileBaseName);

	FWriteContext Ctx{ Parsed, Options, TypeInfo, OutMessages };
	FLineBuilder Body;

	for (const FTPEnumDef& Enum : Parsed.Enums)
	{
		WriteEnum(Body, Enum, Ctx);
		Body.Add(FString());
	}

	TArray<int32> Order;
	TArray<FString> Cycles;
	TypePasteUtils::SortStructsByDependency(Parsed, Order, Cycles);
	ReportCycles(Cycles, OutMessages);

	TArray<const FTPStructDef*> Structs;
	for (const int32 Index : Order)
	{
		WriteStruct(Body, Parsed.Structs[Index], Ctx);
		Body.Add(FString());
		Structs.Add(&Parsed.Structs[Index]);
	}

	TMap<FString, FString> LocalTypeFiles;
	for (const FTPEnumDef& Enum : Parsed.Enums)
	{
		LocalTypeFiles.Add(Enum.Name, BaseName);
	}
	for (const FTPStructDef& Struct : Parsed.Structs)
	{
		LocalTypeFiles.Add(Struct.Name, BaseName);
	}

	TArray<FString> Includes;
	TArray<FString> ForwardDecls;
	CollectDependencies(Structs, BaseName, LocalTypeFiles, Ctx, Includes, ForwardDecls);

	FTPCppFile File;
	File.FileName = BaseName + TEXT(".h");
	File.Content = BuildHeaderText(BaseName, Includes, ForwardDecls, Body, Options);
	CheckAsciiOnly(File, OutMessages);
	return File;
}

TArray<FTPCppFile> FTypePasteCppWriter::WritePerType(const FTPParseResult& Parsed, const FTPCppWriteOptions& Options,
	const ITPExternalTypeInfo* TypeInfo, TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteCppWriterPrivate;

	FWriteContext Ctx{ Parsed, Options, TypeInfo, OutMessages };

	// Type -> file name (if names without prefix collide, the C++ name is used as is)
	TMap<FString, FString> LocalTypeFiles;
	TArray<FString> UsedFileNames;
	auto AssignFile = [&LocalTypeFiles, &UsedFileNames](const FString& TypeName)
	{
		FString Base = SanitizeFileBaseName(TypePasteUtils::StripUnrealPrefix(TypeName));
		if (UsedFileNames.Contains(Base))
		{
			Base = SanitizeFileBaseName(TypeName);
		}
		UsedFileNames.Add(Base);
		LocalTypeFiles.Add(TypeName, Base);
	};
	for (const FTPEnumDef& Enum : Parsed.Enums)
	{
		AssignFile(Enum.Name);
	}
	for (const FTPStructDef& Struct : Parsed.Structs)
	{
		AssignFile(Struct.Name);
	}

	TArray<FTPCppFile> Files;

	for (const FTPEnumDef& Enum : Parsed.Enums)
	{
		const FString BaseName = LocalTypeFiles.FindRef(Enum.Name);
		FLineBuilder Body;
		WriteEnum(Body, Enum, Ctx);

		FTPCppFile File;
		File.FileName = BaseName + TEXT(".h");
		File.Content = BuildHeaderText(BaseName, TArray<FString>(), TArray<FString>(), Body, Options);
		CheckAsciiOnly(File, OutMessages);
		Files.Add(File);
	}

	TArray<int32> Order;
	TArray<FString> Cycles;
	TypePasteUtils::SortStructsByDependency(Parsed, Order, Cycles);
	ReportCycles(Cycles, OutMessages);

	for (const int32 Index : Order)
	{
		const FTPStructDef& Struct = Parsed.Structs[Index];
		const FString BaseName = LocalTypeFiles.FindRef(Struct.Name);

		FLineBuilder Body;
		WriteStruct(Body, Struct, Ctx);

		TArray<const FTPStructDef*> Structs;
		Structs.Add(&Struct);
		TArray<FString> Includes;
		TArray<FString> ForwardDecls;
		CollectDependencies(Structs, BaseName, LocalTypeFiles, Ctx, Includes, ForwardDecls);

		FTPCppFile File;
		File.FileName = BaseName + TEXT(".h");
		File.Content = BuildHeaderText(BaseName, Includes, ForwardDecls, Body, Options);
		CheckAsciiOnly(File, OutMessages);
		Files.Add(File);
	}

	return Files;
}
