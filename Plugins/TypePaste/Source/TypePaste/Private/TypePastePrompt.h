// TypePaste - prompt to give the AI when asking for a design (so it answers in a format TypePaste reads well)

#pragma once

#include "CoreMinimal.h"

namespace TypePastePrompt
{
	inline const TCHAR* GetRequestPrompt()
	{
		return TEXT(
			"Design the enums and structs needed for the feature below, for Unreal Engine 5 C++.\n"
			"Put all of the code in a single code block and follow these rules.\n"
			"\n"
			"[Rules]\n"
			"- ENGLISH ONLY: every identifier, comment, DisplayName, ToolTip, Category and string value must be plain English (ASCII).\n"
			"  Do not use any other language in the code, even if the feature description below is written in another language.\n"
			"- Enums: UENUM(BlueprintType) + enum class EName : uint8. Add UMETA(DisplayName = \"Readable Name\") to each entry\n"
			"- Structs: USTRUCT(BlueprintType) + struct FName { GENERATED_BODY() ... }\n"
			"- Members: add UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"...\") and initialize numeric/bool/enum members with a default value\n"
			"- Describe each enum entry/member with a // comment on the line directly above it (it becomes the tooltip).\n"
			"  Do not also add a ToolTip specifier when there is a comment.\n"
			"- Allowed types: bool, int32, int64, uint8, float, double, FString, FName, FText,\n"
			"  FVector, FRotator, FTransform, FLinearColor, FGameplayTag, other enums/structs defined here,\n"
			"  TArray<>, TSet<>, TMap<> (no nested containers), TObjectPtr<UClass>, TSubclassOf<>, TSoftObjectPtr<>, TSoftClassPtr<>\n"
			"- Do not use functions, constructors, static members, bitfields or fixed-size arrays\n"
			"- Structs must not contain each other by value in a cycle\n"
			"- Structs used as data table rows inherit from FTableRowBase\n"
			"\n"
			"[Feature to design]\n"
			"(Describe the feature here. Example: RPG inventory - item types, rarity, item data, slots)\n"
		);
	}

	inline const TCHAR* GetHelpText()
	{
		return TEXT(
			"How to use\n"
			"\n"
			"1) Press [Copy AI Prompt] and paste it to an AI (Claude, ChatGPT ...) to request a design.\n"
			"   (Most answers to a normal question without the prompt can be read as well)\n"
			"2) Paste the AI answer into the left box. The code block alone or the whole answer with prose both work.\n"
			"3) Choose what to generate below.\n"
			"   - Blueprint assets: enum (UserDefinedEnum) / struct (UserDefinedStruct) assets in the Content Browser\n"
			"   - C++ headers: UENUM/USTRUCT headers (.h) in a project module's source folder\n"
			"4) Check this preview, then press the Create button.\n"
			"\n"
			"What can be read\n"
			"- UENUM / enum class / enum, USTRUCT / struct (recognized without the macros too)\n"
			"- UMETA(DisplayName, Hidden, ToolTip), UPROPERTY(...), default values (= 10.f, { 5 }), // and /** */ comments -> tooltips\n"
			"- Statements that cannot be interpreted (functions, constructors ...) are kept verbatim in C++ output\n"
			"\n"
			"Generated output is English only\n"
			"- Non-English comments, DisplayName, ToolTip, Category and default values are left out (shown as warnings in the preview)\n"
			"- With the [Copy AI Prompt] request, the AI answers in English from the start\n"
			"\n"
			"After creating C++ headers, [Build and Reopen] closes the editor, builds, and opens the project again (no solution file needed)\n"
		);
	}
}
