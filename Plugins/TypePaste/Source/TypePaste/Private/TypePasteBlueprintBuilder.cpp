// TypePaste - Blueprint enum/struct asset creation

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

	/** Type resolution result */
	struct FResolved
	{
		bool bOk = false;
		FEdGraphPinType PinType;
		/** Description for the preview */
		FString Description;
		FString Error;
		TArray<FString> Warnings;
		/** The UEnum if it is an enum (may be empty in the preview) */
		UEnum* Enum = nullptr;
		/** The definition if it is an enum created in this run (for default values) */
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

		/** Registers an asset created (or reused) in this run */
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
				Out.Error = FString(TEXT("'")) + Type.ToString() + TEXT("': not enough template arguments.");
				return Out;
			}
			for (int32 Index = 0; Index < Expected; ++Index)
			{
				if (IsContainerName(Type.Args[Index].Name))
				{
					Out.Error = TEXT("Blueprint structs do not support nested containers (TArray<TArray<>> etc.). Wrap the inner one in a struct.");
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

		/** Whether an asset with this name already exists in the target path */
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
				// If several assets share the name, prefer the one in the target path
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
				return Fail(Out, FString(TEXT("'")) + Type.ToString() + TEXT("': could not resolve the template argument."));
			}
			const FString ClassName = Type.Args[0].GetCleanName();
			UClass* Class = FindClass(ClassName);
			if (!Class)
			{
				return Fail(Out, FString(TEXT("Class '")) + ClassName + TEXT("' was not found. (Only C++ classes are supported, and their module must be loaded)"));
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
				Out.Warnings.Add(TemplateName + TEXT(" does not exist in Blueprint structs, created as a plain Object reference."));
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
					return Fail(Out, TEXT("Could not resolve the TEnumAsByte argument."));
				}
				return ResolveTerminal(Type.Args[0], Out);
			}

			if (Type.Args.Num() > 0)
			{
				return Fail(Out, FString(TEXT("'")) + Type.ToString() + TEXT("' is a template type Blueprint structs do not support."));
			}

			if (Type.PointerDepth > 1)
			{
				return Fail(Out, TEXT("Double pointers are not supported."));
			}
			if (Type.PointerDepth == 1)
			{
				UClass* Class = FindClass(Name);
				if (!Class)
				{
					return Fail(Out, FString(TEXT("Class '")) + Name + TEXT("' was not found."));
				}
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				Out.PinType.PinSubCategoryObject = Class;
				return Succeed(Out, FString(TEXT("Object Reference (")) + Class->GetName() + TEXT(")"));
			}

			// Primitive types
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
					Out.Warnings.Add(FString(TEXT("Blueprints have no ")) + Name + TEXT(", created as Integer (int32)."));
				}
				return Succeed(Out, TEXT("Integer"));
			case ETPPrimitiveKind::Int64:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
				if (!Name.Equals(TEXT("int64"), ESearchCase::CaseSensitive))
				{
					Out.Warnings.Add(FString(TEXT("Blueprints have no ")) + Name + TEXT(", created as Integer64 (int64)."));
				}
				return Succeed(Out, TEXT("Integer64"));
			case ETPPrimitiveKind::Byte:
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				return Succeed(Out, TEXT("Byte"));
			case ETPPrimitiveKind::Real:
				// Float in UE5 Blueprints has double precision
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

			// Types pasted together in this run
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
					return Succeed(Out, FString(TEXT("Enum ")) + (*Asset)->GetName());
				}
				if (!bPreview)
				{
					return Fail(Out, FString(TEXT("Enum '")) + Name + TEXT("' asset could not be created, so it cannot be linked."));
				}
				Out.NewEnumDef = LocalEnum;
				return Succeed(Out, FString(TEXT("Enum (created together) ")) + Name);
			}
			if (Parsed.FindStruct(Name) != nullptr)
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				if (UObject* const* Asset = LocalAssets.Find(Name))
				{
					Out.PinType.PinSubCategoryObject = *Asset;
					return Succeed(Out, FString(TEXT("Struct ")) + (*Asset)->GetName());
				}
				if (!bPreview)
				{
					return Fail(Out, FString(TEXT("Struct '")) + Name + TEXT("' asset does not exist yet, so it cannot be linked. (Creation failed or dependency cycle)"));
				}
				return Succeed(Out, FString(TEXT("Struct (created together) ")) + Name);
			}

			// Types that already exist in the engine/project
			UEnum* ExistingEnum = nullptr;
			FString Label;
			if (FindExistingEnum(Name, ExistingEnum, Label))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				Out.PinType.PinSubCategoryObject = ExistingEnum;
				Out.Enum = ExistingEnum;
				return Succeed(Out, FString(TEXT("Enum ")) + Label);
			}
			UScriptStruct* ExistingStruct = nullptr;
			if (FindExistingStruct(Name, ExistingStruct, Label))
			{
				Out.PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				Out.PinType.PinSubCategoryObject = ExistingStruct;
				return Succeed(Out, FString(TEXT("Struct ")) + Label);
			}

			return Fail(Out, FString(TEXT("Type '")) + Name + TEXT("' was not found. Check the spelling, or paste its definition too if it is a new type."));
		}
	};

	/** Enum default value (EWeaponType::Melee) -> internal name of the Blueprint enumerator */
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
			// Newly created Blueprint enum: order without Hidden entries = index
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

		// Existing enum: find by internal name or display name (excluding _MAX)
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

	/** Expressions such as 0, false, nullptr, {} that equal the default and need not be set */
	static bool IsTrivialDefault(const FString& Expr)
	{
		const FString E = Expr.TrimStartAndEnd();
		return E.IsEmpty() || E.Equals(TEXT("0")) || E.Equals(TEXT("0.f")) || E.Equals(TEXT("0.0f")) || E.Equals(TEXT("0.0"))
			|| E.Equals(TEXT("false")) || E.Equals(TEXT("nullptr")) || E.Equals(TEXT("NULL")) || E.Equals(TEXT("{}"))
			|| E.Equals(TEXT("NAME_None")) || E.Equals(TEXT("FText::GetEmpty()")) || E.Equals(TEXT("FString()"))
			|| E.Equals(TEXT("TEXT(\"\")")) || E.Equals(TEXT("\"\""));
	}

	/** Shared by preview/creation: Blueprint default value string of a member. Returns false if it cannot be converted or is not needed */
	static bool GetBlueprintDefault(const FTPStructMember& Member, const FResolved& Resolved, FString& OutValue, FString& OutSkipReason)
	{
		OutSkipReason.Reset();
		if (IsTrivialDefault(Member.DefaultExpr))
		{
			return false;
		}
		if (Resolved.PinType.ContainerType != EPinContainerType::None)
		{
			OutSkipReason = TEXT("container default values are not set automatically");
			return false;
		}
		if (Resolved.Primitive != ETPPrimitiveKind::None)
		{
			if (ConvertDefaultValue(Resolved.Primitive, Member.DefaultExpr, OutValue))
			{
				return true;
			}
			OutSkipReason = TEXT("the expression cannot be turned into a value");
			return false;
		}
		if (Resolved.NewEnumDef || Resolved.Enum)
		{
			if (!Resolved.Enum)
			{
				// Preview: the asset does not exist yet, so only show the entry name
				const FString EntryName = StripScopeQualifier(Member.DefaultExpr.TrimStartAndEnd());
				if (Resolved.NewEnumDef && Resolved.NewEnumDef->FindEntryIndex(EntryName) != INDEX_NONE)
				{
					const FTPEnumEntry& Entry = Resolved.NewEnumDef->Entries[Resolved.NewEnumDef->FindEntryIndex(EntryName)];
					OutValue = Entry.GetBlueprintDisplayName();
					return true;
				}
				OutSkipReason = TEXT("the enum entry was not found");
				return false;
			}
			if (ConvertEnumDefault(Resolved, Member.DefaultExpr, OutValue))
			{
				return true;
			}
			OutSkipReason = TEXT("the enum entry was not found");
			return false;
		}
		OutSkipReason = TEXT("struct/object default values are not set automatically");
		return false;
	}

	/** Bitfields (uint8 bFlag : 1) are treated as bool */
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

	/** Factory that creates a new asset of the class (found without linking UEnumFactory / UStructureFactory directly) */
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
					+ TEXT("' already exists, so the existing asset is referenced instead of creating a new one. (Its content is not changed)")));
				return Typed;
			}
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("'")) + PackageName
				+ TEXT("' holds an asset of a different kind and cannot be created. Change the naming rule or the path.")));
			return nullptr;
		}

		if (!Factory)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Could not find a factory that creates '")) + TAsset::StaticClass()->GetName() + TEXT("' assets.")));
			return nullptr;
		}

		UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath, TAsset::StaticClass(), Factory);
		TAsset* Typed = Cast<TAsset>(Created);
		if (!Typed)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Could not create the asset '")) + PackageName + TEXT("'. Check the path.")));
		}
		return Typed;
	}

	static void FillEnum(UUserDefinedEnum* Enum, const FTPEnumDef& Def, FTPBlueprintResult& Result)
	{
		const int32 InitialCount = FMath::Max(0, Enum->NumEnums() - 1); // excluding _MAX
		int32 NumAdded = 0;
		bool bHasCustomValues = false;

		for (const FTPEnumEntry& Entry : Def.Entries)
		{
			if (Entry.bHidden)
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Def.Name + TEXT("::") + Entry.Name
					+ TEXT(" is UMETA(Hidden), so it was left out of the Blueprint enum. (Blueprint enums get _MAX automatically)")));
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
				// If the name cannot be used (like "None"), retry with a trailing _
				const FString Fallback = DisplayName + TEXT("_");
				if (FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(Fallback)))
				{
					Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Def.Name + TEXT("::") + Entry.Name + TEXT(": display name '") + DisplayName
						+ TEXT("' cannot be used, created as '") + Fallback + TEXT("' instead.")));
				}
				else
				{
					Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Def.Name + TEXT("::") + Entry.Name + TEXT(": could not set display name '") + DisplayName
						+ TEXT("'. (It may be a duplicate)")));
				}
			}

			if (!Entry.Comment.IsEmpty())
			{
				Enum->SetMetaData(TEXT("ToolTip"), *Entry.Comment, NewIndex);
			}
			++NumAdded;
		}

		// Remove entries the factory added by default
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
			Result.Messages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("Enum '")) + Def.Name
				+ TEXT("': explicit values (= 5 etc.) are ignored. Blueprint enums are numbered in order from 0.")));
		}
		if (HasSpecifierFlag(Def.MacroSpecifiers, TEXT("Bitflags")))
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("Enum '")) + Def.Name
				+ TEXT("' is Bitflags, but it is created as a regular Blueprint enum. Turn on the bitmask option in the asset if needed.")));
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
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(": default value '") + Member.DefaultExpr + TEXT("' was not applied: ")
					+ SkipReason + TEXT(". Set it in the asset if needed.")));
			}
			return;
		}
		if (!FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Guid, Value))
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(": could not apply default value '") + Value + TEXT("'. Set it in the asset.")));
		}
	}

	static void FillStruct(UUserDefinedStruct* Struct, const FTPStructDef& Def, const FTypeResolver& Resolver, FTPBlueprintResult& Result)
	{
		// The default member the factory added (MemberVar_0) is removed at the end
		TArray<FGuid> PlaceholderGuids;
		for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
		{
			PlaceholderGuids.Add(Var.VarGuid);
		}

		if (!Def.ParentName.IsEmpty())
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("Struct '")) + Def.Name + TEXT("': parent '") + Def.ParentName
				+ TEXT("' is ignored because Blueprint structs have no inheritance. (It still works as a data table row struct)")));
		}
		if (Def.NumRawItems() > 0)
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Info, FString(TEXT("Struct '")) + Def.Name + TEXT("': ")
				+ FString::FromInt(Def.NumRawItems()) + TEXT(" function(s)/constructor(s) etc. are not reflected in the Blueprint struct.")));
		}

		TArray<FString> UsedNames;
		int32 NumAdded = 0;
		for (const FTPStructMember& Member : Def.Members)
		{
			const FString Label = Def.Name + TEXT("::") + Member.Name;
			if (UsedNames.Contains(Member.Name))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(": duplicate name, skipped.")));
				continue;
			}

			FResolved Resolved = Resolver.Resolve(GetEffectiveType(Member));
			if (!Resolved.bOk)
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" skipped: ") + Resolved.Error));
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
					Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" skipped: fixed-size arrays of containers are not supported.")));
					continue;
				}
				Resolved.PinType.ContainerType = EPinContainerType::Array;
				Result.Messages.Add(FTPMessage(ETPSeverity::Info, Label + TEXT(" is a fixed-size array") + Member.ArrayDim + TEXT(", created as an Array.")));
			}

			FString Reason;
			if (!FStructureEditorUtils::CanHaveAMemberVariableOfType(Struct, Resolved.PinType, &Reason))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" skipped: ") + (Reason.IsEmpty() ? FString(TEXT("this type cannot be used as a struct member.")) : Reason)));
				continue;
			}
			if (!FStructureEditorUtils::AddVariable(Struct, Resolved.PinType))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(" skipped: could not add the member.")));
				continue;
			}

			const FGuid Guid = FStructureEditorUtils::GetVarDesc(Struct).Last().VarGuid;
			if (!FStructureEditorUtils::RenameVariable(Struct, Guid, Member.Name))
			{
				Result.Messages.Add(FTPMessage(ETPSeverity::Warning, Label + TEXT(": could not set the name. Rename it in the asset.")));
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
			Result.Messages.Add(FTPMessage(ETPSeverity::Warning, FString(TEXT("Struct '")) + Def.Name
				+ TEXT("': no member could be created, so only the default member (MemberVar_0) is left.")));
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

	FString Text = FString(TEXT("Blueprint asset preview  -  location: ")) + PackagePath + TEXT("\n");

	for (const FTPEnumDef& Def : Parsed.Enums)
	{
		const FString AssetName = MakeAssetName(Def.Name, true, Options.Naming);
		Text += FString(TEXT("\n[Enum] ")) + Def.Name + TEXT("  ->  ") + AssetName;
		if (Resolver.DoesAssetExist(AssetName))
		{
			Text += TEXT("   (already exists: the existing asset is reused)");
		}
		Text += TEXT("\n");

		int32 BlueprintIndex = 0;
		for (const FTPEnumEntry& Entry : Def.Entries)
		{
			if (Entry.bHidden)
			{
				Text += FString(TEXT("      - ")) + Entry.Name + TEXT(" (Hidden -> left out)\n");
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
		OutMessages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Structs contain each other (or themselves) by value: "))
			+ FString::Join(Cycles, TEXT(", ")) + TEXT(". Change one of them to a TArray or an object reference.")));
	}

	for (const int32 StructIndex : Order)
	{
		const FTPStructDef& Def = Parsed.Structs[StructIndex];
		const FString AssetName = MakeAssetName(Def.Name, false, Options.Naming);
		Text += FString(TEXT("\n[Struct] ")) + Def.Name + TEXT("  ->  ") + AssetName;
		if (Resolver.DoesAssetExist(AssetName))
		{
			Text += TEXT("   (already exists: the existing asset is reused)");
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
				Line += FString(TEXT("[x] ")) + Resolved.Error;
				OutMessages.Add(FTPMessage(ETPSeverity::Warning, Def.Name + TEXT("::") + Member.Name + TEXT(" will not be created: ") + Resolved.Error));
			}
			else
			{
				Line += Resolved.Description;
				if (!Member.ArrayDim.IsEmpty())
				{
					Line += FString(TEXT("  -> Array")) + Member.ArrayDim;
				}
				FString Value;
				FString SkipReason;
				if (!Member.DefaultExpr.IsEmpty() && GetBlueprintDefault(Member, Resolved, Value, SkipReason))
				{
					Line += FString(TEXT(" = ")) + Value;
				}
				else if (!SkipReason.IsEmpty())
				{
					Line += FString(TEXT("  (default ")) + Member.DefaultExpr + TEXT(" not applied)");
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
			Text += FString(TEXT("   (")) + FString::FromInt(Def.NumRawItems()) + TEXT(" function(s)/constructor(s) etc. are not reflected in Blueprints)\n");
		}
		if (!Def.ParentName.IsEmpty())
		{
			Text += FString(TEXT("   (parent ")) + Def.ParentName + TEXT(" is ignored in Blueprint structs)\n");
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
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, TEXT("The save path is empty. e.g. /Game/Types")));
		return Result;
	}

	FTypeResolver Resolver(Parsed, PackagePath, false);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TArray<UPackage*> PackagesToSave;

	// 1) Enums first (struct members may reference them)
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
		UE_LOG(LogTypePaste, Log, TEXT("Created enum: %s"), *Enum->GetPathName());
	}

	// 2) Structs: sorted so that a struct containing another by value comes later
	TArray<int32> Order;
	TArray<FString> Cycles;
	SortStructsByDependency(Parsed, Order, Cycles);
	if (Cycles.Num() > 0)
	{
		Result.Messages.Add(FTPMessage(ETPSeverity::Error, FString(TEXT("Structs contain each other (or themselves) by value, so some members cannot be created: "))
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
		UE_LOG(LogTypePaste, Log, TEXT("Created struct: %s"), *Struct->GetPathName());
	}

	// 3) Save
	if (Options.bSaveAssets && PackagesToSave.Num() > 0)
	{
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
		{
			Result.Messages.Add(FTPMessage(ETPSeverity::Warning, TEXT("Some assets could not be saved. Save them in the Content Browser.")));
		}
	}

	return Result;
}
