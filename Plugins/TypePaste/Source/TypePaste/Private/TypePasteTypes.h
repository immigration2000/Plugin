// TypePaste - AI가 추천한 UENUM/USTRUCT 코드를 붙여넣어 타입을 생성하는 에디터 플러그인
// 파싱 결과를 담는 순수 데이터 모델 (에디터 API 의존 없음)

#pragma once

#include "CoreMinimal.h"

/** 메시지 심각도 */
enum class ETPSeverity : uint8
{
	Info,
	Warning,
	Error
};

/** 분석/생성 과정에서 사용자에게 보여줄 메시지 */
struct FTPMessage
{
	ETPSeverity Severity = ETPSeverity::Info;
	FString Text;

	FTPMessage() = default;
	FTPMessage(ETPSeverity InSeverity, const FString& InText)
		: Severity(InSeverity), Text(InText)
	{
	}
};

/** 파싱된 타입 참조. 예) TArray<TObjectPtr<UTexture2D>> */
struct FTPTypeRef
{
	/** 타입 이름. 예) "TArray", "int32", "EWeaponType", "UTexture2D" */
	FString Name;

	/** 템플릿 인자 */
	TArray<FTPTypeRef> Args;

	/** '*' 개수 */
	int32 PointerDepth = 0;

	bool bReference = false;
	bool bConst = false;

	bool IsValid() const { return !Name.IsEmpty(); }

	/** "EFoo::Type" 같은 구식 네임스페이스 열거형 표기를 정리한 이름 */
	FString GetCleanName() const;

	/** 사람이 읽을 수 있는 문자열. 예) "TArray<TObjectPtr<UTexture2D>>" */
	FString ToString() const;
};

/** 열거형 항목 */
struct FTPEnumEntry
{
	FString Name;
	/** "= 3" 의 "3". 없으면 빈 문자열 */
	FString ValueExpr;
	/** UMETA(...) 괄호 안 원문 */
	FString UMetaRaw;
	/** UMETA DisplayName 값 */
	FString DisplayName;
	FString Comment;
	/** UMETA(Hidden) */
	bool bHidden = false;

	/** 블루프린트에 표시할 이름: DisplayName 이 있으면 그것, 없으면 Name */
	FString GetBlueprintDisplayName() const { return DisplayName.IsEmpty() ? Name : DisplayName; }
};

/** 열거형 정의 */
struct FTPEnumDef
{
	FString Name;
	/** ": uint8" 의 "uint8". 없으면 빈 문자열 */
	FString UnderlyingType;
	/** UENUM(...) 괄호 안 원문 */
	FString MacroSpecifiers;
	bool bHadMacro = false;
	/** enum class 여부 */
	bool bScoped = false;
	FString Comment;
	TArray<FTPEnumEntry> Entries;

	/** 이름으로 항목 인덱스 찾기 (대소문자 구분) */
	int32 FindEntryIndex(const FString& EntryName) const;

	/** Hidden 이 아닌 항목들 (블루프린트 생성 대상) */
	TArray<int32> GetVisibleEntryIndices() const;
};

/** 구조체 멤버 변수 */
struct FTPStructMember
{
	FString Name;
	FTPTypeRef Type;
	/** 원문 타입 텍스트 (공백 정리됨). 예) "TArray<TObjectPtr<UTexture2D>>" */
	FString TypeText;
	/** UPROPERTY(...) 괄호 안 원문 */
	FString PropertySpecifiers;
	bool bHadUProperty = false;
	/** 원문 초기화 구문. 예) "= 10.f" 또는 "{ 10 }" (없으면 빈 문자열) */
	FString InitText;
	/** 초기값 식만. 예) "10.f" */
	FString DefaultExpr;
	/** 고정 배열 차원. 예) "[4]" */
	FString ArrayDim;
	/** 비트필드 폭 (uint8 bFlag : 1 → 1) */
	int32 BitfieldWidth = 0;
	FString Comment;
};

enum class ETPStructItemKind : uint8
{
	/** 해석된 멤버 변수 */
	Member,
	/** 해석하지 않고 원문 그대로 보존하는 구문 (함수, 생성자, static 등) */
	Raw
};

/** 구조체 본문 항목 (원래 순서 보존용) */
struct FTPStructItem
{
	ETPStructItemKind Kind = ETPStructItemKind::Member;
	int32 MemberIndex = INDEX_NONE;
	FString RawText;
	FString Comment;
};

/** 구조체 정의 */
struct FTPStructDef
{
	FString Name;
	/** 부모 타입 이름. 예) "FTableRowBase" */
	FString ParentName;
	/** 부모 선언 원문. 예) "public FTableRowBase" */
	FString ParentText;
	/** USTRUCT(...) 괄호 안 원문 */
	FString MacroSpecifiers;
	bool bHadMacro = false;
	FString Comment;
	TArray<FTPStructMember> Members;
	TArray<FTPStructItem> Items;

	int32 NumRawItems() const;
};

/** 전체 파싱 결과 */
struct FTPParseResult
{
	TArray<FTPEnumDef> Enums;
	TArray<FTPStructDef> Structs;
	TArray<FTPMessage> Messages;

	bool HasAnyType() const { return Enums.Num() > 0 || Structs.Num() > 0; }
	bool HasErrors() const;

	const FTPEnumDef* FindEnum(const FString& Name) const;
	const FTPStructDef* FindStruct(const FString& Name) const;

	void AddInfo(const FString& Text) { Messages.Add(FTPMessage(ETPSeverity::Info, Text)); }
	void AddWarning(const FString& Text) { Messages.Add(FTPMessage(ETPSeverity::Warning, Text)); }
	void AddError(const FString& Text) { Messages.Add(FTPMessage(ETPSeverity::Error, Text)); }
};

/** 블루프린트 기본값 변환용 기본 타입 분류 */
enum class ETPPrimitiveKind : uint8
{
	None,
	Bool,
	Int,
	Int64,
	Byte,
	Real,
	String,
	Name,
	Text
};

/** 블루프린트 에셋 이름 규칙 */
enum class ETPAssetNaming : uint8
{
	/** C++ 이름 그대로 (EWeaponType, FWeaponData) */
	KeepCppName,
	/** E_WeaponType, S_WeaponData */
	PrefixE_S_,
	/** E_WeaponType, F_WeaponData */
	PrefixE_F_,
	/** 접두사 제거 (WeaponType, WeaponData) */
	NoPrefix
};

/** 파서/생성기 공용 유틸리티 (순수 함수) */
namespace TypePasteUtils
{
	/** ASCII 기준 문자 분류 (로캘에 영향받지 않도록 직접 구현) */
	bool IsSpaceChar(TCHAR C);
	bool IsIdentStartChar(TCHAR C);
	bool IsIdentChar(TCHAR C);
	bool IsDigitChar(TCHAR C);

	/** 공백(줄바꿈 포함) 연속을 한 칸으로 줄이고 앞뒤 공백 제거 */
	FString CollapseWhitespace(const FString& In);

	/** 지정자 원문에서 Key = "Value" 값을 찾는다. 예) UMETA(DisplayName="근접") */
	bool FindSpecifierValue(const FString& Specifiers, const FString& Key, FString& OutValue);

	/** 지정자 원문에 단독 플래그가 있는지 (따옴표 안 제외, 대소문자 무시). 예) Hidden, Bitflags */
	bool HasSpecifierFlag(const FString& Specifiers, const FString& Flag);

	/** "EWeaponType::Melee" → "Melee" */
	FString StripScopeQualifier(const FString& In);

	/** EWeaponType → WeaponType, FItemData → ItemData (두 번째 글자가 대문자일 때만 제거) */
	FString StripUnrealPrefix(const FString& CppName);

	/** 블루프린트 에셋 이름 만들기 */
	FString MakeAssetName(const FString& CppName, bool bIsEnum, ETPAssetNaming Naming);

	/** C++ 기본 타입 분류 (블루프린트 기본값 변환용) */
	ETPPrimitiveKind GetPrimitiveKind(const FString& TypeName);

	/** C++ 초기값 식을 블루프린트 기본값 문자열로 변환. 변환 불가하면 false */
	bool ConvertDefaultValue(ETPPrimitiveKind Kind, const FString& Expr, FString& OutValue);

	/** 타입 참조에 등장하는 모든 이름 수집 (템플릿 인자 포함) */
	void CollectTypeNames(const FTPTypeRef& Type, TArray<FString>& OutNames);

	/** 값으로 포함되어 정의가 먼저 필요한 이름 / 포인터로만 쓰여 전방 선언이면 되는 이름 분리 */
	void CollectTypeUsages(const FTPTypeRef& Type, TArray<FString>& OutByValue, TArray<FString>& OutByPointer);

	/** 구조체를 의존 순서로 정렬 (값으로 포함하는 쪽이 뒤로). 순환이 있으면 OutCycleNames 에 기록 */
	void SortStructsByDependency(const FTPParseResult& Result, TArray<int32>& OutOrder, TArray<FString>& OutCycleNames);

	/** C++ 식별자로 유효한지 */
	bool IsValidIdentifier(const FString& In);

	/** 모든 문자가 ASCII(0~127) 인지 */
	bool IsAsciiText(const FString& In);

	/**
	 * 지정자 목록에서 ASCII 가 아닌 문자가 들어 있는 항목을 뺀다. meta = (...) 안쪽도 같은 규칙으로 정리하고, 비면 meta 째로 뺀다.
	 * 예) EditAnywhere, Category = "무기", meta = (ToolTip = "설명", ClampMin = "0") → EditAnywhere, meta = (ClampMin = "0")
	 * 뺀 항목의 키 이름(Category, ToolTip ...)을 OutRemovedKeys 에 추가한다. 바뀐 것이 없으면 원문 그대로 돌려준다.
	 */
	FString RemoveNonAsciiSpecifiers(const FString& Specifiers, TArray<FString>& OutRemovedKeys);

	/**
	 * 생성 결과(C++ 헤더, 블루프린트 에셋)는 영어만 쓴다 (DECISIONS D16).
	 * 파싱 결과에서 ASCII 가 아닌 주석, 지정자 값(DisplayName/ToolTip/Category ...), 기본값, 원문 구문을 빼고 안내 메시지를 남긴다.
	 */
	void EnforceEnglishOnly(FTPParseResult& Result);
}
