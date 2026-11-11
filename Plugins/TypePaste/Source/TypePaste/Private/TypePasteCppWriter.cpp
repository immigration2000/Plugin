// TypePaste - C++ 헤더 생성기 구현

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

		/** 빈 줄 하나 (연속 빈 줄 방지) */
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

	/** CoreMinimal.h 만으로 쓸 수 있는 타입 */
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

	/** 자주 쓰는 타입의 include 경로와 필요한 모듈 */
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

	/** UTexture2D* → true (UObject 계열 원시 포인터) */
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

	/** TArray<TArray<int32>>, TMap<FName, TArray<int32>> 처럼 컨테이너 안에 컨테이너 */
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

	/** 블루프린트가 지원하지 않는 정수 타입 */
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

	/** 원문 구문을 들여쓰기만 맞춰 붙인다 */
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
	 * UHT 는 바로 위 주석을 ToolTip 메타데이터로 쓰므로, ToolTip 지정자가 따로 있는데 주석까지 붙이면
	 * "Metadata key 'ToolTip' first seen with value A then B" 오류가 난다. ToolTip 이 있으면 주석은 쓰지 않는다.
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
				+ TEXT(" 에 ToolTip 지정자가 있어 주석은 넣지 않았습니다. (둘 다 있으면 UHT 가 ToolTip 중복 오류를 냄)"));
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
			AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, FString(TEXT("열거형 '")) + Def.Name
				+ TEXT("' 은 BlueprintType 인데 기반 타입이 uint8 이 아닙니다. UHT 에서 오류가 날 수 있습니다."));
		}
		if (!Def.bScoped)
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("열거형 '")) + Def.Name + TEXT("' 을 enum class 로 바꿔 생성합니다."));
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
			AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, FString(TEXT("구조체 '")) + Def.Name
				+ TEXT("' 의 이름이 F 로 시작하지 않습니다. USTRUCT 는 F 접두사가 필요해 UHT 오류가 납니다."));
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

			// 출력용 사본 (아래에서 타입을 다듬는다)
			FTPStructMember Member = Def.Members[Item.MemberIndex];

			// TEnumAsByte<EFoo> 에서 EFoo 가 이번에 만드는 enum class 라면 그냥 EFoo 로 쓴다
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
						+ TEXT(" 는 중첩 컨테이너라 리플렉션을 지원하지 않아 UPROPERTY 를 붙이지 않았습니다."));
				}
				else
				{
					// 블루프린트에 노출할 수 없는 타입은 BlueprintReadWrite 없이 붙인다
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

			// UHT 가 거부하는 조합 경고
			if (bHasProperty && bNestedContainer)
			{
				AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, MemberLabel
					+ TEXT(" 는 중첩 컨테이너(TArray<TArray<>> 등)라 UPROPERTY 로 쓸 수 없습니다. 안쪽을 구조체로 감싸 주세요."));
			}
			const bool bBlueprintVisible = bHasProperty
				&& (HasSpecifierFlag(Specs, TEXT("BlueprintReadWrite")) || HasSpecifierFlag(Specs, TEXT("BlueprintReadOnly")));
			if (bBlueprintVisible)
			{
				if (bUnsupportedInt)
				{
					AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, MemberLabel + TEXT(" 의 타입 '") + Member.Type.GetCleanName()
						+ TEXT("' 은 블루프린트에 노출할 수 없습니다. int32/int64/uint8 로 바꾸는 것을 권장합니다."));
				}
				if (!Member.ArrayDim.IsEmpty())
				{
					AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, MemberLabel
						+ TEXT(" 는 고정 크기 배열이라 블루프린트에 노출할 수 없습니다. TArray 를 권장합니다."));
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
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("구조체 '")) + Def.Name
				+ TEXT("' 의 TEnumAsByte<열거형> 을 열거형 타입으로 바꿨습니다. (enum class 에는 TEnumAsByte 가 필요 없음)"));
		}
		if (NumConvertedPointers > 0)
		{
			AddMessageUnique(Ctx.Messages, ETPSeverity::Info, FString(TEXT("구조체 '")) + Def.Name + TEXT("' 의 원시 포인터 ")
				+ FString::FromInt(NumConvertedPointers) + TEXT("개를 TObjectPtr<> 로 바꿨습니다. (UE5 권장 방식)"));
		}
	}

	/** 구조체들이 쓰는 외부 타입을 모아 include / 전방 선언 목록을 만든다 */
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
				+ TEXT("' 타입이 정의된 헤더를 찾지 못했습니다. 컴파일 오류가 나면 #include 를 직접 추가하세요."));
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
			AddMessageUnique(Ctx.Messages, ETPSeverity::Warning, FString(TEXT("모듈의 Build.cs 에 다음 의존 모듈이 필요할 수 있습니다: "))
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

	/** 마지막 안전장치: 생성된 헤더는 ASCII 만 (D16). 남아 있으면 오류로 알려 내보내기를 막는다 */
	static void CheckAsciiOnly(const FTPCppFile& File, TArray<FTPMessage>& Messages)
	{
		if (!IsAsciiText(File.Content))
		{
			AddMessageUnique(Messages, ETPSeverity::Error, File.FileName
				+ TEXT(" 에 영어가 아닌 문자가 남아 있어 만들 수 없습니다. 입력에서 한글 등을 지우고 다시 시도해 주세요."));
		}
	}

	static void ReportCycles(const TArray<FString>& CycleNames, TArray<FTPMessage>& Messages)
	{
		if (CycleNames.Num() > 0)
		{
			AddMessageUnique(Messages, ETPSeverity::Error, FString(TEXT("구조체가 서로(또는 자기 자신을) 값으로 포함하고 있습니다: "))
				+ FString::Join(CycleNames, TEXT(", ")) + TEXT(". 한쪽을 TArray 나 포인터로 바꿔 주세요."));
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

	// 타입 → 파일 이름 (접두사를 뺀 이름이 겹치면 C++ 이름 그대로 사용)
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
