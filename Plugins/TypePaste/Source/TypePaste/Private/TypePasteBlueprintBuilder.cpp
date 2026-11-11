// TypePaste - 블루프린트 열거형/구조체 에셋 생성 구현

#include "TypePasteBlueprintBuilder.h"
#include "TypePasteModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Factories/Factory.h"
#include "FileHelpers.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace TypePasteBlueprintPrivate
{
	using namespace TypePasteUtils;

	/** 타입 해석 결과 */
	struct FResolved
	{
		bool bOk = false;
		FEdGraphPinType PinType;
		/** 미리보기용 설명 */
		FString Description;
		FString Error;
		TArray<FString> Warnings;
		/** 열거형이면 그 UEnum (미리보기에서는 비어 있을 수 있음) */
		UEnum* Enum = nullptr;
		/** 이번에 새로 만드는 열거형이면 그 정의 (기본값 변환용) */
		const FTPEnumDef* NewEnumDef = nullptr;
		ETPPrimitiveKind Primitive = ETPPrimitiveKind::None;
	};

	static bool IsUpperAscii(TCHAR C)
	{
		return C >= TEXT('A') && C <= TEXT('Z');
	}

	static bool IsContainerName(const FString& Name)
	{
		return Name.Equals(TEXT("TArray"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("TSet"), ESearchCase::CaseSensitive)
			|| Name.Equals(TEXT("TMap"), ESearchCase::CaseSensitive);
	}

	static TArray<FString> GetNameVariants(const FString& CppName, bool bIsEnum)
	{
		TArray<FString> Result;
		Result.AddUnique(CppName);
		Result.AddUnique(MakeAssetName(CppName, bIsEnum, ETPAssetNaming::PrefixE_S_));
		Result.AddUnique(MakeAssetName(CppName, bIsEnum, ETPAssetNaming::PrefixE_F_));
		Result.AddUnique(MakeAssetName(CppName, bIsEnum, ETPAssetNaming::NoPrefix));
		return Result;
	}

	static FString PadRight(const FString& In, int32 Width)
	{
		FString Out = In;
		while (Out.Len() < Width)
		{
			Out.AppendChar(TEXT(' '));
		}
		return Out;
	}

	static bool PackageExists(const FString& PackageName)
	{
		return FindPackage(nullptr, *PackageName) != nullptr || FPackageName::DoesPackageExist(PackageName);
	}

	class FTypeResolver
	{
	public:
		FTypeResolver(const FTPParseResult& InParsed, const FString& InPackagePath, bool bInPreview)
			: Parsed(InParsed)
			, PackagePath(InPackagePath)
			, bPreview(bInPreview)
		{
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			CollectAssets(AssetRegistry, UUserDefinedEnum::StaticClass(), ExistingEnumAssets);
			CollectAssets(AssetRegistry, UUserDefinedStruct::StaticClass(), ExistingStructAssets);
		}

		/** 이번 작업에서 생성(또는 재사용)한 에셋 등록 */
		void RegisterLocal(const FString& CppName, UObject* Asset, bool bCreatedNow)
		{
			LocalAssets.Add(CppName, Asset);
			if (bCreatedNow)
			{
				CreatedNow.Add(CppName);
			}
		}

		FResolved Resolve(const FTPTypeRef& Type) const
		{
			FResolved Out;
			const FString Name = Type.GetCleanName();
			const bool bArray = Name.Equals(TEXT("TArray"), ESearchCase::CaseSensitive);
			const bool bSet = Name.Equals(TEXT("TSet"), ESearchCase::CaseSensitive);
			const bool bMap = Name.Equals(TEXT("TMap"), ESearchCase::CaseSensitive);

			if (!(bArray || bSet || bMap))
			{
				ResolveTerminal(Type, Out);
				return Out;
			}

			const int32 Expected = bMap ? 2 : 1;
			if (Type.Args.Num() < Expected)
			{
				Out.Error = FString(TEXT("'")) + Type.ToString() + TEXT("' 의 템플릿 인자가 부족합니다.");
				return Out;
			}
			for (int32 Index = 0; Index < Expected; ++Index)
			{
				if (IsContainerName(Type.Args[Index].Name))
				{
					Out.Error = TEXT("블루프린트 구조체는 중첩 컨테이너(TArray<TArray<>> 등)를 지원하지 않습니다. 안쪽을 구조체로 감싸 주세요.");
					return Out;
				}
			}

			FResolved Key;
			ResolveTerminal(Type.Args[0], Key);
			if (!Key.bOk)
			{
				return Key;
			}

			Out = Key;
			Out.Enum = nullptr;
			Out.NewEnumDef = nullptr;
			Out.Primitive = ETPPrimitiveKind::None;
			Out.PinType.ContainerType = bArray ? EPinContainerType::Array : (bSet ? EPinContainerType::Set : EPinContainerType::Map);

			if (bMap)
			{
				FResolved Value;
				ResolveTerminal(Type.Args[1], Value);
				if (!Value.bOk)
				{
					Out.bOk = false;
					Out.Error = Value.Error;
					return Out;
				}
				Out.PinType.PinValueType = FEdGraphTerminalType::FromPinType(Value.PinType);
				for (const FString& Warning : Value.Warnings)
				{
					Out.Warnings.Add(Warning);
				}
				Out.Description = FString(TEXT("Map<")) + Key.Description + TEXT(", ") + Value.Description + TEXT(">");
			}
			else
			{
				Out.Description = FString(bArray ? TEXT("Array<") : TEXT("Set<")) + Key.Description + TEXT(">");
			}
			return Out;
		}

		/** 이 이름의 에셋이 대상 경로에 이미 있는지 */
		bool DoesAssetExist(const FString& AssetName) const
		{
			return PackageExists(PackagePath + TEXT("/") + AssetName);
		}

	private:
		const FTPParseResult& Parsed;
		FString PackagePath;
		bool bPreview = false;
		TMap<FString, UObject*> LocalAssets;
		TArray<FString> CreatedNow;
		TMap<FString, FAssetData> ExistingEnumAssets;
		TMap<FString, FAssetData> ExistingStructAssets;

		void CollectAssets(IAssetRegistry& AssetRegistry, UClass* Class, TMap<FString, FAssetData>& OutMap) const
		{
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(Class->GetClassPathName(), Assets);
			for (const FAssetData& Data : Assets)
			{
				const FString Key = Data.AssetName.ToString();
				// 이름이 같은 에셋이 여러 개면 대상 경로에 있는 것을 우선
				if (!OutMap.Contains(Key) || Data.PackagePath.ToString().Equals(PackagePath))
				{
					OutMap.Add(Key, Data);
				}
			}
		}

		static bool Fail(FResolved& Out, const FString& Error)
		{
			Out.bOk = false;
			Out.Error = Error;
			return false;
		}

		static bool Succeed(FResolved& Out, const FString& Description)
		{
			Out.bOk = true;
			Out.Description = Description;
			return true;
		}

		UClass* FindClass(const FString& CppName) const
		{
			if (CppName.IsEmpty())
			{
				return nullptr;
			}
			if (CppName.Len() > 1 && (CppName[0] == TEXT('U') || CppName[0] == TEXT('A')) && IsUpperAscii(CppName[1]))
			{
				if (UClass* Found = FindFirstObject<UClass>(*CppName.RightChop(1), EFindFirstObjectOptions::NativeFirst))
				{
					return Found;
				}
			}
			return FindFirstObject<UClass>(*CppName, EFindFirstObjectOptions::NativeFirst);
		}

		bool FindExistingEnum(const FString& CppName, UEnum*& OutEnum, FString& OutLabel) const
		{
			OutEnum = nullptr;
			if (UEnum* Found = FindFirstObject<UEnum>(*CppName, EFindFirstObjectOptions::NativeFirst))
			{
				OutEnum = Found;
				OutLabel = Found->GetName();
				return true;
			}
			for (const FString& Candidate : GetNameVariants(CppName, true))
			{
				if (const FAssetData* Data = ExistingEnumAssets.Find(Candidate))
				{
					OutLabel = Data->AssetName.ToString();
					if (bPreview)
					{
						return true;
					}
					OutEnum = Cast<UEnum>(Data->GetAsset());
					return OutEnum != nullptr;
				}
			}
			return false;
		}

		bool FindExistingStruct(const FString& CppName, UScriptStruct*& OutStruct, FString& OutLabel) const
		{
			OutStruct = nullptr;
			if (CppName.Len() > 1 && CppName[0] == TEXT('F') && IsUpperAscii(CppName[1]))
			{
				if (UScriptStruct* Found = FindFirstObject<UScriptStruct>(*CppName.RightChop(1), EFindFirstObjectOptions::NativeFirst))
				{
					OutStruct = Found;
					OutLabel = Found->GetName();
					return true;
				}
			}
			for (const FString& Candidate : GetNameVariants(CppName, false))
			{
				if (const FAssetData* Data = ExistingStructAssets.Find(Candidate))
				{
					OutLabel = Data->AssetName.ToString();
					if (bPreview)
					{
						return true;
					}
					OutStruct = Cast<UScriptStruct>(Data->GetAsset());
					return OutStruct != nullptr;
				}
			}
			return false;
		}

		bool ResolveObjectTemplate(const FString& TemplateName, const FTPTypeRef& Type, FResolved& Out) const
		{
			if (Type.Args.Num() != 1)
			{
				return Fail(Out, FString(TEXT("'")) + Type.ToString() + TEXT("' 의 템플릿 인자를 해석하지 못했습니다."));
			}
			const FString ClassName = Type.Args[0].GetCleanName();
			UClass* Class = FindClass(ClassName);
			if (!Class)
			{
				return Fail(Out, FString(TEXT("클래스 '")) + ClassName + TEXT("' 를 찾지 못했습니다. (C++ 클래스만 지원, 해당 모듈이 로드되어 있어야 함)"));
			}
			Out.PinType.PinSubCategoryObject = Class;

			if (TemplateName.Equals(TEXT("TSubclassOf"), ESearchCase::CaseSensitive))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
				return Succeed(Out, FString(TEXT("Class Reference (")) + Class->GetName() + TEXT(")"));
			}
			if (TemplateName.Equals(TEXT("TSoftObjectPtr"), ESearchCase::CaseSensitive))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
				return Succeed(Out, FString(TEXT("Soft Object Reference (")) + Class->GetName() + TEXT(")"));
			}
			if (TemplateName.Equals(TEXT("TSoftClassPtr"), ESearchCase::CaseSensitive))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
				return Succeed(Out, FString(TEXT("Soft Class Reference (")) + Class->GetName() + TEXT(")"));
			}

			if (!TemplateName.Equals(TEXT("TObjectPtr"), ESearchCase::CaseSensitive))
			{
				Out.Warnings.Add(TemplateName + TEXT(" 는 블루프린트 구조체에 없어 일반 Object 참조로 만듭니다."));
			}
			Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			return Succeed(Out, FString(TEXT("Object Reference (")) + Class->GetName() + TEXT(")"));
		}

		bool ResolveTerminal(const FTPTypeRef& Type, FResolved& Out) const
		{
			Out = FResolved();
			const FString Name = Type.GetCleanName();

			if (Name.Equals(TEXT("TObjectPtr"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("TWeakObjectPtr"), ESearchCase::CaseSensitive)
				|| Name.Equals(TEXT("TLazyObjectPtr"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("TSubclassOf"), ESearchCase::CaseSensitive)
				|| Name.Equals(TEXT("TSoftObjectPtr"), ESearchCase::CaseSensitive) || Name.Equals(TEXT("TSoftClassPtr"), ESearchCase::CaseSensitive))
			{
				return ResolveObjectTemplate(Name, Type, Out);
			}

			if (Name.Equals(TEXT("TEnumAsByte"), ESearchCase::CaseSensitive))
			{
				if (Type.Args.Num() != 1)
				{
					return Fail(Out, TEXT("TEnumAsByte 의 인자를 해석하지 못했습니다."));
				}
				return ResolveTerminal(Type.Args[0], Out);
			}

			if (Type.Args.Num() > 0)
			{
				return Fail(Out, FString(TEXT("'")) + Type.ToString() + TEXT("' 는 블루프린트 구조체에서 지원하지 않는 템플릿 타입입니다."));
			}

			if (Type.PointerDepth > 1)
			{
				return Fail(Out, TEXT("이중 포인터는 지원하지 않습니다."));
			}
			if (Type.PointerDepth == 1)
			{
				UClass* Class = FindClass(Name);
				if (!Class)
				{
					return Fail(Out, FString(TEXT("클래스 '")) + Name + TEXT("' 를 찾지 못했습니다."));
				}
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				Out.PinType.PinSubCategoryObject = Class;
				return Succeed(Out, FString(TEXT("Object Reference (")) + Class->GetName() + TEXT(")"));
			}

			// 기본 타입
			const ETPPrimitiveKind Kind = GetPrimitiveKind(Name);
			Out.Primitive = Kind;
			switch (Kind)
			{
			case ETPPrimitiveKind::Bool:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
				return Succeed(Out, TEXT("Boolean"));
			case ETPPrimitiveKind::Int:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
				if (!Name.Equals(TEXT("int32"), ESearchCase::CaseSensitive) && !Name.Equals(TEXT("int"), ESearchCase::CaseSensitive))
				{
					Out.Warnings.Add(FString(TEXT("블루프린트에는 ")) + Name + TEXT(" 이 없어 Integer(int32)로 만듭니다."));
				}
				return Succeed(Out, TEXT("Integer"));
			case ETPPrimitiveKind::Int64:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
				if (!Name.Equals(TEXT("int64"), ESearchCase::CaseSensitive))
				{
					Out.Warnings.Add(FString(TEXT("블루프린트에는 ")) + Name + TEXT(" 이 없어 Integer64(int64)로 만듭니다."));
				}
				return Succeed(Out, TEXT("Integer64"));
			case ETPPrimitiveKind::Byte:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				return Succeed(Out, TEXT("Byte"));
			case ETPPrimitiveKind::Real:
				// UE5 블루프린트의 Float 는 double 정밀도
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
				Out.PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
				return Succeed(Out, TEXT("Float"));
			case ETPPrimitiveKind::String:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_String;
				return Succeed(Out, TEXT("String"));
			case ETPPrimitiveKind::Name:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
				return Succeed(Out, TEXT("Name"));
			case ETPPrimitiveKind::Text:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
				return Succeed(Out, TEXT("Text"));
			case ETPPrimitiveKind::None:
			default:
				break;
			}

			// 이번에 같이 붙여넣은 타입
			if (const FTPEnumDef* LocalEnum = Parsed.FindEnum(Name))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				if (UObject* const* Asset = LocalAssets.Find(Name))
				{
					Out.PinType.PinSubCategoryObject = *Asset;
					Out.Enum = Cast<UEnum>(*Asset);
					if (CreatedNow.Contains(Name))
					{
						Out.NewEnumDef = LocalEnum;
					}
					return Succeed(Out, FString(TEXT("열거형 ")) + (*Asset)->GetName());
				}
				if (!bPreview)
				{
					return Fail(Out, FString(TEXT("열거형 '")) + Name + TEXT("' 에셋을 만들지 못해 연결할 수 없습니다."));
				}
				Out.NewEnumDef = LocalEnum;
				return Succeed(Out, FString(TEXT("열거형 (함께 생성) ")) + Name);
			}
			if (Parsed.FindStruct(Name) != nullptr)
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				if (UObject* const* Asset = LocalAssets.Find(Name))
				{
					Out.PinType.PinSubCategoryObject = *Asset;
					return Succeed(Out, FString(TEXT("구조체 ")) + (*Asset)->GetName());
				}
				if (!bPreview)
				{
					return Fail(Out, FString(TEXT("구조체 '")) + Name + TEXT("' 에셋이 아직 없어 연결할 수 없습니다. (생성 실패 또는 순환 참조)"));
				}
				return Succeed(Out, FString(TEXT("구조체 (함께 생성) ")) + Name);
			}

			// 엔진/프로젝트에 이미 있는 타입
			UEnum* ExistingEnum = nullptr;
			FString Label;
			if (FindExistingEnum(Name, ExistingEnum, Label))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				Out.PinType.PinSubCategoryObject = ExistingEnum;
				Out.Enum = ExistingEnum;
				return Succeed(Out, FString(TEXT("열거형 ")) + Label);
			}
			UScriptStruct* ExistingStruct = nullptr;
			if (FindExistingStruct(Name, ExistingStruct, Label))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				Out.PinType.PinSubCategoryObject = ExistingStruct;
				return Succeed(Out, FString(TEXT("구조체 ")) + Label);
			}

			return Fail(Out, FString(TEXT("'")) + Name + TEXT("' 타입을 찾지 못했습니다. 철자를 확인하거나, 새 타입이면 정의도 함께 붙여넣어 주세요."));
		}
	};

	/** 열거형 기본값(EWeaponType::Melee) → 블루프린트 열거자 내부 이름 */
	static bool ConvertEnumDefault(const FResolved& Resolved, const FString& Expr, FString& OutValue)
	{
		UEnum* Enum = Resolved.Enum;
		if (!Enum)
		{
			return false;
		}
		const FString EntryName = StripScopeQualifier(Expr.TrimStartAndEnd());

		if (Resolved.NewEnumDef)
		{
			// 새로 만든 블루프린트 열거형: Hidden 을 뺀 순서 = 인덱스
			const TArray<int32> Visible = Resolved.NewEnumDef->GetVisibleEntryIndices();
			const int32 DefIndex = Resolved.NewEnumDef->FindEntryIndex(EntryName);
			const int32 BlueprintIndex = Visible.Find(DefIndex);
			if (DefIndex == INDEX_NONE || BlueprintIndex == INDEX_NONE)
			{
				return false;
			}
			OutValue = Enum->GetNameStringByIndex(BlueprintIndex);
			return !OutValue.IsEmpty();
		}

		// 기존 열거형: 내부 이름 또는 표시 이름으로 찾기 (_MAX 제외)
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			const FString ShortName = Enum->GetNameStringByIndex(Index);
			if (ShortName.Equals(EntryName) || Enum->GetDisplayNameTextByIndex(Index).ToString().Equals(EntryName))
			{
				OutValue = ShortName;
				return true;
			}
		}
		return false;
	}

	/** 0, false, nullptr, {} 처럼 기본값과 같아서 굳이 설정할 필요가 없는 식 */
	static bool IsTrivialDefault(const FString& Expr)
	{
		const FString E = Expr.TrimStartAndEnd();
		return E.IsEmpty() || E.Equals(TEXT("0")) || E.Equals(TEXT("0.f")) || E.Equals(TEXT("0.0f")) || E.Equals(TEXT("0.0"))
			|| E.Equals(TEXT("false")) || E.Equals(TEXT("nullptr")) || E.Equals(TEXT("NULL")) || E.Equals(TEXT("{}"))
			|| E.Equals(TEXT("NAME_None")) || E.Equals(TEXT("FText::GetEmpty()")) || E.Equals(TEXT("FString()"))
			|| E.Equals(TEXT("TEXT(\"\")")) || E.Equals(TEXT("\"\""));
	}

	/** 미리보기/생성 공용: 멤버의 블루프린트 기본값 문자열. 변환 불가/불필요하면 false */
	static bool GetBlueprintDefault(const FTPStructMember& Member, const FResolved& Resolved, FString& OutValue, FString& OutSkipReason)
	{
		OutSkipReason.Reset();
		if (IsTrivialDefault(Member.DefaultExpr))
		{
			return false;
		}
		if (Resolved.PinType.ContainerType != EPinContainerType::None)
		{
			OutSkipReason = TEXT("컨테이너 기본값은 자동 설정하지 않습니다");
			return false;
		}
		if (Resolved.Primitive != ETPPrimitiveKind::None)
		{
			if (ConvertDefaultValue(Resolved.Primitive, Member.DefaultExpr, OutValue))
			{
				return true;
			}
			OutSkipReason = TEXT("값으로 바꿀 수 없는 식입니다");
			return false;
		}
		if (Resolved.NewEnumDef || Resolved.Enum)
		{
			if (!Resolved.Enum)
			{
				// 미리보기: 아직 에셋이 없으므로 항목 이름만 보여준다
				const FString EntryName = StripScopeQualifier(Member.DefaultExpr.TrimStartAndEnd());
				if (Resolved.NewEnumDef && Resolved.NewEnumDef->FindEntryIndex(EntryName) != INDEX_NONE)
				{
					const FTPEnumEntry& Entry = Resolved.NewEnumDef->Entries[Resolved.NewEnumDef->FindEntryIndex(EntryName)];
					OutValue = Entry.GetBlueprintDisplayName();
					return true;
				}
				OutSkipReason = TEXT("열거형 항목을 찾지 못했습니다");
				return false;
			}
			if (ConvertEnumDefault(Resolved, Member.DefaultExpr, OutValue))
			{
				return true;
			}
			OutSkipReason = TEXT("열거형 항목을 찾지 못했습니다");
			return false;
		}
		OutSkipReason = TEXT("구조체/오브젝트 기본값은 자동 설정하지 않습니다");
		return false;
	}

	/** 비트필드(uint8 bFlag : 1)는 bool 로 취급 */
	static FTPTypeRef GetEffectiveType(const FTPStructMember& Member)
	{
		if (Member.BitfieldWidth == 1)
		{
			FTPTypeRef BoolType;
			BoolType.Name = TEXT("bool");
			return BoolType;
		}
		return Member.Type;
	}

	/** 에셋 클래스를 새로 만드는 팩토리 (UEnumFactory / UStructureFactory 를 직접 링크하지 않고 찾는다) */
	static UFactory* MakeFactoryFor(UClass* AssetClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UFactory::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			const UFactory* Default = Class->GetDefaultObject<UFactory>();
			if (Default && Default->GetSupportedClass() == AssetClass && Default->CanCreateNew())
			{
				return NewObject<UFactory>(GetTransientPackage(), Class);
			}
		}
		return nullptr;
	}

	template <typename TAsset>
	static TAsset* FindOrCreateAsset(IAssetTools& AssetTools, const FString& AssetName, const FString& PackagePath, UFactory* Factory,
		FTPBlueprintResult& Result, bool& bOutReused)
	{
		bOutReused = false;
		const FString PackageName = PackagePath + TEXT("/") + AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;

		if (PackageExists(PackageName))
		{
			UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath);
			if (TAsset* Typed = Cast<TAsset>(Existing))
			{
				bOutReused = true;
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("'")) + ObjectPath
					+ TEXT("' 이 이미 있어 새로 만들지 않고 기존 에셋을 참조합니다. (기존 내용은 바꾸지 않음)")));
				return Typed;
			}
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("'")) + PackageName
				+ TEXT("' 에 다른 종류의 에셋이 있어 만들 수 없습니다. 이름 규칙이나 경로를 바꿔 주세요.")));
			return nullptr;
		}

		if (!Factory)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("'")) + TAsset::StaticClass()->GetName() + TEXT("' 에셋을 만드는 팩토리를 찾지 못했습니다.")));
			return nullptr;
		}

		UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath, TAsset::StaticClass(), Factory);
		TAsset* Typed = Cast<TAsset>(Created);
		if (!Typed)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("'")) + PackageName + TEXT("' 에셋을 만들지 못했습니다. 경로를 확인해 주세요.")));
		}
		return Typed;
	}

	static void FillEnum(UUserDefinedEnum* Enum, const FTPEnumDef& Def, FTPBlueprintResult& Result)
	{
		const int32 InitialCount = FMath::Max(0, Enum->NumEnums() - 1); // _MAX 제외
		int32 NumAdded = 0;
		bool bHasCustomValues = false;

		for (const FTPEnumEntry& Entry : Def.Entries)
		{
			if (Entry.bHidden)
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Def.Name + TEXT("::") + Entry.Name
					+ TEXT(" 는 UMETA(Hidden) 이라 블루프린트 열거형에서 뺐습니다. (블루프린트 열거형은 _MAX 를 자동으로 가짐)")));
				continue;
			}

			if (!Entry.ValueExpr.IsEmpty())
			{
				const int32 ExpectedValue = NumAdded;
				if (!Entry.ValueExpr.Equals(FString::FromInt(ExpectedValue)))
				{
					bHasCustomValues = true;
				}
			}

			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
			const int32 NewIndex = Enum->NumEnums() - 2;
			const FString DisplayName = Entry.GetBlueprintDisplayName();

			if (!FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName)))
			{
				// "None" 처럼 쓸 수 없는 이름이면 뒤에 _ 를 붙여 재시도
				const FString Fallback = DisplayName + TEXT("_");
				if (FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(Fallback)))
				{
					Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Def.Name + TEXT("::") + Entry.Name + TEXT(" 의 표시 이름 '") + DisplayName
						+ TEXT("' 을 쓸 수 없어 '") + Fallback + TEXT("' 로 만들었습니다.")));
				}
				else
				{
					Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Def.Name + TEXT("::") + Entry.Name + TEXT(" 의 표시 이름 '") + DisplayName
						+ TEXT("' 을 지정하지 못했습니다. (중복된 이름일 수 있음)")));
				}
			}

			if (!Entry.Comment.IsEmpty())
			{
				Enum->SetMetaData(TEXT("ToolTip"), *Entry.Comment, NewIndex);
			}
			++NumAdded;
		}

		// 팩토리가 기본으로 넣어 둔 항목이 있으면 제거
		if (NumAdded > 0)
		{
			for (int32 Index = 0; Index < InitialCount; ++Index)
			{
				FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, 0);
			}
		}

		if (!Def.Comment.IsEmpty())
		{
			Enum->EnumDescription = FText::FromString(Def.Comment);
		}
		if (bHasCustomValues)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("열거형 '")) + Def.Name
				+ TEXT("' 에 지정한 값(= 5 등)은 무시됩니다. 블루프린트 열거형은 0부터 순서대로 번호가 매겨집니다.")));
		}
		if (HasSpecifierFlag(Def.MacroSpecifiers, TEXT("Bitflags")))
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("열거형 '")) + Def.Name
				+ TEXT("' 은 Bitflags 지만 블루프린트 열거형으로는 일반 열거형으로 만듭니다. 필요하면 에셋에서 비트마스크 옵션을 켜세요.")));
		}
		Enum->MarkPackageDirty();
	}

	static void ApplyMemberDefault(UUserDefinedStruct* Struct, const FGuid& Guid, const FString& Label, const FTPStructMember& Member,
		const FResolved& Resolved, FTPBlueprintResult& Result)
	{
		if (Member.DefaultExpr.IsEmpty())
		{
			return;
		}
		FString Value;
		FString SkipReason;
		if (!GetBlueprintDefault(Member, Resolved, Value, SkipReason))
		{
			if (!SkipReason.IsEmpty())
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(" 의 기본값 '") + Member.DefaultExpr + TEXT("' 은 적용하지 않았습니다: ")
					+ SkipReason + TEXT(". 필요하면 에셋에서 직접 설정하세요.")));
			}
			return;
		}
		if (!FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Guid, Value))
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(" 의 기본값 '") + Value + TEXT("' 을 적용하지 못했습니다. 에셋에서 직접 설정하세요.")));
		}
	}

	static void FillStruct(UUserDefinedStruct* Struct, const FTPStructDef& Def, const FTypeResolver& Resolver, FTPBlueprintResult& Result)
	{
		// 팩토리가 넣어 둔 기본 멤버(MemberVar_0)는 마지막에 지운다
		TArray<FGuid> PlaceholderGuids;
		for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
		{
			PlaceholderGuids.Add(Var.VarGuid);
		}

		if (!Def.ParentName.IsEmpty())
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("구조체 '")) + Def.Name + TEXT("' 의 부모 '") + Def.ParentName
				+ TEXT("' 는 블루프린트 구조체에 상속이 없어 무시합니다. (데이터 테이블 행 구조체로는 그대로 쓸 수 있음)")));
		}
		if (Def.NumRawItems() > 0)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("구조체 '")) + Def.Name + TEXT("' 의 함수/생성자 등 ")
				+ FString::FromInt(Def.NumRawItems()) + TEXT("개는 블루프린트 구조체에 반영되지 않습니다.")));
		}

		TArray<FString> UsedNames;
		int32 NumAdded = 0;
		for (const FTPStructMember& Member : Def.Members)
		{
			const FString Label = Def.Name + TEXT("::") + Member.Name;
			if (UsedNames.Contains(Member.Name))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" 이름이 중복되어 건너뜁니다.")));
				continue;
			}

			FResolved Resolved = Resolver.Resolve(GetEffectiveType(Member));
			if (!Resolved.bOk)
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" 건너뜀: ") + Resolved.Error));
				continue;
			}
			for (const FString& Warning : Resolved.Warnings)
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(": ") + Warning));
			}
			if (!Member.ArrayDim.IsEmpty())
			{
				if (Resolved.PinType.ContainerType != EPinContainerType::None)
				{
					Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" 건너뜀: 컨테이너의 고정 크기 배열은 지원하지 않습니다.")));
					continue;
				}
				Resolved.PinType.ContainerType = EPinContainerType::Array;
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(" 는 고정 크기 배열") + Member.ArrayDim + TEXT(" 이라 Array 로 만듭니다.")));
			}

			FString Reason;
			if (!FStructureEditorUtils::CanHaveAMemberVariableOfType(Struct, Resolved.PinType, &Reason))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" 건너뜀: ") + (Reason.IsEmpty() ? FString(TEXT("이 타입은 구조체 멤버로 쓸 수 없습니다.")) : Reason)));
				continue;
			}
			if (!FStructureEditorUtils::AddVariable(Struct, Resolved.PinType))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" 건너뜀: 멤버를 추가하지 못했습니다.")));
				continue;
			}

			const FGuid Guid = FStructureEditorUtils::GetVarDesc(Struct).Last().VarGuid;
			if (!FStructureEditorUtils::RenameVariable(Struct, Guid, Member.Name))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" 의 이름을 지정하지 못했습니다. 에셋에서 직접 바꿔 주세요.")));
			}
			if (!Member.Comment.IsEmpty())
			{
				FStructureEditorUtils::ChangeVariableTooltip(Struct, Guid, Member.Comment);
			}
			ApplyMemberDefault(Struct, Guid, Label, Member, Resolved, Result);

			UsedNames.Add(Member.Name);
			++NumAdded;
		}

		if (NumAdded > 0)
		{
			for (const FGuid& Guid : PlaceholderGuids)
			{
				FStructureEditorUtils::RemoveVariable(Struct, Guid);
			}
		}
		else
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("구조체 '")) + Def.Name
				+ TEXT("' 에 멤버를 하나도 만들지 못해 기본 멤버(MemberVar_0)만 남았습니다.")));
		}

		if (!Def.Comment.IsEmpty())
		{
			FStructureEditorUtils::ChangeTooltip(Struct, Def.Comment);
		}
		Struct->MarkPackageDirty();
	}
}

FString FTypePasteBlueprintBuilder::NormalizePackagePath(const FString& InPath)
{
	FString Path = InPath.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	if (Path.StartsWith(TEXT("/All/")))
	{
		Path.RightChopInline(4);
	}
	while (Path.EndsWith(TEXT("/")))
	{
		Path.LeftChopInline(1);
	}
	if (!Path.IsEmpty() && !Path.StartsWith(TEXT("/")))
	{
		Path = FString(TEXT("/")) + Path;
	}
	return Path;
}

FString FTypePasteBlueprintBuilder::BuildPreview(const FTPParseResult& Parsed, const FTPBlueprintOptions& Options, TArray<FTPMessage>& OutMessages)
{
	using namespace TypePasteBlueprintPrivate;

	const FString PackagePath = NormalizePackagePath(Options.PackagePath);
	FTypeResolver Resolver(Parsed, PackagePath, true);

	FString Text = FString(TEXT("블루프린트 에셋 미리보기  —  저장 위치: ")) + PackagePath + TEXT("\n");

	for (const FTPEnumDef& Def : Parsed.Enums)
	{
		const FString AssetName = MakeAssetName(Def.Name, true, Options.Naming);
		Text += FString(TEXT("\n[열거형] ")) + Def.Name + TEXT("  →  ") + AssetName;
		if (Resolver.DoesAssetExist(AssetName))
		{
			Text += TEXT("   (이미 있음: 기존 에셋 재사용)");
		}
		Text += TEXT("\n");

		int32 BlueprintIndex = 0;
		for (const FTPEnumEntry& Entry : Def.Entries)
		{
			if (Entry.bHidden)
			{
				Text += FString(TEXT("      - ")) + Entry.Name + TEXT(" (Hidden → 제외)\n");
				continue;
			}
			FString Line = FString(TEXT("   ")) + PadRight(FString::FromInt(BlueprintIndex++) + TEXT("."), 4) + Entry.GetBlueprintDisplayName();
			if (!Entry.DisplayName.IsEmpty())
			{
				Line += FString(TEXT("  (")) + Entry.Name + TEXT(")");
			}
			if (!Entry.Comment.IsEmpty())
			{
				Line += FString(TEXT("  // ")) + Entry.Comment.Replace(TEXT("\n"), TEXT(" "));
			}
			Text += Line + TEXT("\n");
		}
	}

	TArray<int32> Order;
	TArray<FString> Cycles;
	SortStructsByDependency(Parsed, Order, Cycles);
	if (Cycles.Num() > 0)
	{
		OutMessages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("구조체가 서로(또는 자기 자신을) 값으로 포함하고 있습니다: "))
			+ FString::Join(Cycles, TEXT(", ")) + TEXT(". 한쪽을 TArray 나 오브젝트 참조로 바꿔 주세요.")));
	}

	for (const int32 StructIndex : Order)
	{
		const FTPStructDef& Def = Parsed.Structs[StructIndex];
		const FString AssetName = MakeAssetName(Def.Name, false, Options.Naming);
		Text += FString(TEXT("\n[구조체] ")) + Def.Name + TEXT("  →  ") + AssetName;
		if (Resolver.DoesAssetExist(AssetName))
		{
			Text += TEXT("   (이미 있음: 기존 에셋 재사용)");
		}
		Text += TEXT("\n");

		int32 NameWidth = 4;
		for (const FTPStructMember& Member : Def.Members)
		{
			NameWidth = FMath::Max(NameWidth, Member.Name.Len());
		}

		for (const FTPStructMember& Member : Def.Members)
		{
			const FResolved Resolved = Resolver.Resolve(GetEffectiveType(Member));
			FString Line = FString(TEXT("   ")) + PadRight(Member.Name, NameWidth) + TEXT("  ");
			if (!Resolved.bOk)
			{
				Line += FString(TEXT("✗ ")) + Resolved.Error;
				OutMessages.Add(FTPMessage(ETPSeverity::Warning, Def.Name + TEXT("::") + Member.Name + TEXT(" 은 만들어지지 않습니다: ") + Resolved.Error));
			}
			else
			{
				Line += Resolved.Description;
				if (!Member.ArrayDim.IsEmpty())
				{
					Line += FString(TEXT("  → Array")) + Member.ArrayDim;
				}
				FString Value;
				FString SkipReason;
				if (!Member.DefaultExpr.IsEmpty() && GetBlueprintDefault(Member, Resolved, Value, SkipReason))
				{
					Line += FString(TEXT(" = ")) + Value;
				}
				else if (!SkipReason.IsEmpty())
				{
					Line += FString(TEXT("  (기본값 ")) + Member.DefaultExpr + TEXT(" 미적용)");
				}
				for (const FString& Warning : Resolved.Warnings)
				{
					OutMessages.Add(FTPMessage(ETPSeverity::Info, Def.Name + TEXT("::") + Member.Name + TEXT(": ") + Warning));
				}
			}
			Text += Line + TEXT("\n");
		}
		if (Def.NumRawItems() > 0)
		{
			Text += FString(TEXT("   (함수/생성자 등 ")) + FString::FromInt(Def.NumRawItems()) + TEXT("개는 블루프린트에 반영되지 않음)\n");
		}
		if (!Def.ParentName.IsEmpty())
		{
			Text += FString(TEXT("   (부모 ")) + Def.ParentName + TEXT(" 는 블루프린트 구조체에서 무시됨)\n");
		}
	}

	return Text;
}

FTPBlueprintResult FTypePasteBlueprintBuilder::Build(const FTPParseResult& Parsed, const FTPBlueprintOptions& Options)
{
	using namespace TypePasteBlueprintPrivate;

	FTPBlueprintResult Result;
	const FString PackagePath = NormalizePackagePath(Options.PackagePath);
	if (PackagePath.Len() < 2)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("저장 경로가 비어 있습니다. 예) /Game/Types")));
		return Result;
	}

	FTypeResolver Resolver(Parsed, PackagePath, false);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TArray<UPackage*> PackagesToSave;

	// 1) 열거형 먼저 (구조체 멤버가 참조할 수 있으므로)
	for (const FTPEnumDef& Def : Parsed.Enums)
	{
		const FString AssetName = MakeAssetName(Def.Name, true, Options.Naming);
		bool bReused = false;
		UUserDefinedEnum* Enum = FindOrCreateAsset<UUserDefinedEnum>(AssetTools, AssetName, PackagePath, MakeFactoryFor(UUserDefinedEnum::StaticClass()), Result, bReused);
		if (!Enum)
		{
			continue;
		}
		Resolver.RegisterLocal(Def.Name, Enum, !bReused);
		if (bReused)
		{
			++Result.NumReused;
			continue;
		}

		FillEnum(Enum, Def, Result);
		PackagesToSave.AddUnique(Enum->GetPackage());
		Result.CreatedAssets.Add(Enum);
		++Result.NumCreatedEnums;
		UE_LOG(LogTypePaste, Log, TEXT("열거형 생성: %s"), *Enum->GetPathName());
	}

	// 2) 구조체: 다른 구조체를 값으로 포함하는 것이 나중에 오도록 정렬
	TArray<int32> Order;
	TArray<FString> Cycles;
	SortStructsByDependency(Parsed, Order, Cycles);
	if (Cycles.Num() > 0)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("구조체가 서로(또는 자기 자신을) 값으로 포함하고 있어 일부 멤버를 만들 수 없습니다: "))
			+ FString::Join(Cycles, TEXT(", "))));
	}

	for (const int32 StructIndex : Order)
	{
		const FTPStructDef& Def = Parsed.Structs[StructIndex];
		const FString AssetName = MakeAssetName(Def.Name, false, Options.Naming);
		bool bReused = false;
		UUserDefinedStruct* Struct = FindOrCreateAsset<UUserDefinedStruct>(AssetTools, AssetName, PackagePath, MakeFactoryFor(UUserDefinedStruct::StaticClass()), Result, bReused);
		if (!Struct)
		{
			continue;
		}
		Resolver.RegisterLocal(Def.Name, Struct, !bReused);
		if (bReused)
		{
			++Result.NumReused;
			continue;
		}

		FillStruct(Struct, Def, Resolver, Result);
		PackagesToSave.AddUnique(Struct->GetPackage());
		Result.CreatedAssets.Add(Struct);
		++Result.NumCreatedStructs;
		UE_LOG(LogTypePaste, Log, TEXT("구조체 생성: %s"), *Struct->GetPathName());
	}

	// 3) 저장
	if (Options.bSaveAssets && PackagesToSave.Num() > 0)
	{
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Warning, TEXT("일부 에셋을 저장하지 못했습니다. 콘텐츠 브라우저에서 직접 저장해 주세요.")));
		}
	}

	return Result;
}
