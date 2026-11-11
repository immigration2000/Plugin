// TypePaste - editor plugin that creates types from pasted AI-suggested UENUM/USTRUCT code
// Pure data model for parse results (no editor API dependency)

#pragma once

#include "CoreMinimal.h"

/** Message severity */
enum class ETPSeverity : uint8
{
	Info,
	Warning,
	Error
};

/** Message shown to the user during parsing/generation */
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

/** Parsed type reference. e.g. TArray<TObjectPtr<UTexture2D>> */
struct FTPTypeRef
{
	/** Type name. e.g. "TArray", "int32", "EWeaponType", "UTexture2D" */
	FString Name;

	/** Template arguments */
	TArray<FTPTypeRef> Args;

	/** Number of '*' */
	int32 PointerDepth = 0;

	bool bReference = false;
	bool bConst = false;

	bool IsValid() const { return !Name.IsEmpty(); }

	/** Name with old-style namespaced enum notation such as "EFoo::Type" cleaned up */
	FString GetCleanName() const;

	/** Human-readable string. e.g. "TArray<TObjectPtr<UTexture2D>>" */
	FString ToString() const;
};

/** Enum entry */
struct FTPEnumEntry
{
	FString Name;
	/** "3" of "= 3". Empty if none */
	FString ValueExpr;
	/** Raw text inside UMETA(...) */
	FString UMetaRaw;
	/** UMETA DisplayName value */
	FString DisplayName;
	FString Comment;
	/** UMETA(Hidden) */
	bool bHidden = false;

	/** Name shown in Blueprints: DisplayName if present, otherwise Name */
	FString GetBlueprintDisplayName() const { return DisplayName.IsEmpty() ? Name : DisplayName; }
};

/** Enum definition */
struct FTPEnumDef
{
	FString Name;
	/** "uint8" of ": uint8". Empty if none */
	FString UnderlyingType;
	/** Raw text inside UENUM(...) */
	FString MacroSpecifiers;
	bool bHadMacro = false;
	/** Whether it is an enum class */
	bool bScoped = false;
	FString Comment;
	TArray<FTPEnumEntry> Entries;

	/** Finds an entry index by name (case-sensitive) */
	int32 FindEntryIndex(const FString& EntryName) const;

	/** Entries that are not Hidden (the ones created in Blueprints) */
	TArray<int32> GetVisibleEntryIndices() const;
};

/** Struct member variable */
struct FTPStructMember
{
	FString Name;
	FTPTypeRef Type;
	/** Raw type text (whitespace normalized). e.g. "TArray<TObjectPtr<UTexture2D>>" */
	FString TypeText;
	/** Raw text inside UPROPERTY(...) */
	FString PropertySpecifiers;
	bool bHadUProperty = false;
	/** Raw initializer. e.g. "= 10.f" or "{ 10 }" (empty if none) */
	FString InitText;
	/** Initializer expression only. e.g. "10.f" */
	FString DefaultExpr;
	/** Fixed array dimension. e.g. "[4]" */
	FString ArrayDim;
	/** Bitfield width (uint8 bFlag : 1 -> 1) */
	int32 BitfieldWidth = 0;
	FString Comment;
};

enum class ETPStructItemKind : uint8
{
	/** Parsed member variable */
	Member,
	/** Statement kept verbatim without parsing (functions, constructors, static members ...) */
	Raw
};

/** Struct body item (keeps the original order) */
struct FTPStructItem
{
	ETPStructItemKind Kind = ETPStructItemKind::Member;
	int32 MemberIndex = INDEX_NONE;
	FString RawText;
	FString Comment;
};

/** Struct definition */
struct FTPStructDef
{
	FString Name;
	/** Parent type name. e.g. "FTableRowBase" */
	FString ParentName;
	/** Raw parent declaration. e.g. "public FTableRowBase" */
	FString ParentText;
	/** Raw text inside USTRUCT(...) */
	FString MacroSpecifiers;
	bool bHadMacro = false;
	FString Comment;
	TArray<FTPStructMember> Members;
	TArray<FTPStructItem> Items;

	int32 NumRawItems() const;
};

/** Complete parse result */
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

/** Primitive type category used to convert Blueprint default values */
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

/** Blueprint asset naming rule */
enum class ETPAssetNaming : uint8
{
	/** Keep the C++ name (EWeaponType, FWeaponData) */
	KeepCppName,
	/** E_WeaponType, S_WeaponData */
	PrefixE_S_,
	/** E_WeaponType, F_WeaponData */
	PrefixE_F_,
	/** Strip the prefix (WeaponType, WeaponData) */
	NoPrefix
};

/** Utilities shared by the parser and generators (pure functions) */
namespace TypePasteUtils
{
	/** ASCII character classes (implemented by hand so the locale has no effect) */
	bool IsSpaceChar(TCHAR C);
	bool IsIdentStartChar(TCHAR C);
	bool IsIdentChar(TCHAR C);
	bool IsDigitChar(TCHAR C);

	/** Collapses whitespace runs (including newlines) to one space and trims both ends */
	FString CollapseWhitespace(const FString& In);

	/** Finds the value of Key = "Value" in raw specifiers. e.g. UMETA(DisplayName="Melee") */
	bool FindSpecifierValue(const FString& Specifiers, const FString& Key, FString& OutValue);

	/** Whether the raw specifiers contain a standalone flag (outside quotes, case-insensitive). e.g. Hidden, Bitflags */
	bool HasSpecifierFlag(const FString& Specifiers, const FString& Flag);

	/** "EWeaponType::Melee" -> "Melee" */
	FString StripScopeQualifier(const FString& In);

	/** EWeaponType -> WeaponType, FItemData -> ItemData (only stripped when the second letter is uppercase) */
	FString StripUnrealPrefix(const FString& CppName);

	/** Makes a Blueprint asset name */
	FString MakeAssetName(const FString& CppName, bool bIsEnum, ETPAssetNaming Naming);

	/** Classifies a C++ primitive type (for Blueprint default values) */
	ETPPrimitiveKind GetPrimitiveKind(const FString& TypeName);

	/** Converts a C++ initializer expression to a Blueprint default value string. Returns false if it cannot be converted */
	bool ConvertDefaultValue(ETPPrimitiveKind Kind, const FString& Expr, FString& OutValue);

	/** Collects every name used in a type reference (including template arguments) */
	void CollectTypeNames(const FTPTypeRef& Type, TArray<FString>& OutNames);

	/** Splits names used by value (definition needed first) from names used only through pointers (forward declaration is enough) */
	void CollectTypeUsages(const FTPTypeRef& Type, TArray<FString>& OutByValue, TArray<FString>& OutByPointer);

	/** Sorts structs by dependency (a struct that contains another by value comes later). Cycles are written to OutCycleNames */
	void SortStructsByDependency(const FTPParseResult& Result, TArray<int32>& OutOrder, TArray<FString>& OutCycleNames);

	/** Whether the string is a valid C++ identifier */
	bool IsValidIdentifier(const FString& In);

	/** Whether every character is ASCII (0-127) */
	bool IsAsciiText(const FString& In);

	/**
	 * Removes specifiers whose text contains non-ASCII characters. The inside of meta = (...) follows the same rule; if it ends up empty, meta is removed too.
	 * e.g. EditAnywhere, Category = "<non-ASCII>", meta = (ToolTip = "<non-ASCII>", ClampMin = "0") -> EditAnywhere, meta = (ClampMin = "0")
	 * Adds the key names of the removed specifiers (Category, ToolTip ...) to OutRemovedKeys. Returns the input unchanged if nothing was removed.
	 */
	FString RemoveNonAsciiSpecifiers(const FString& Specifiers, TArray<FString>& OutRemovedKeys);

	/**
	 * Generated output (C++ headers, Blueprint assets) is English only (DECISIONS D16).
	 * Removes non-ASCII comments, specifier values (DisplayName/ToolTip/Category ...), default values and raw statements from the parse result and adds messages.
	 */
	void EnforceEnglishOnly(FTPParseResult& Result);
}
