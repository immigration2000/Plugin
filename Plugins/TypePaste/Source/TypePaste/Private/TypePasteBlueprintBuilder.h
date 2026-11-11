// TypePaste - creates Blueprint enum (UserDefinedEnum) / struct (UserDefinedStruct) assets from parse results

#pragma once

#include "TypePasteTypes.h"

class UObject;

struct FTPBlueprintOptions
{
	/** Content path to create the assets in. e.g. /Game/Types */
	FString PackagePath = TEXT("/Game/Types");

	/** Asset naming rule */
	ETPAssetNaming Naming = ETPAssetNaming::KeepCppName;

	/** Save right after creating */
	bool bSaveAssets = true;
};

struct FTPBlueprintResult
{
	TArray<FTPMessage> Messages;
	TArray<UObject*> CreatedAssets;
	int32 NumCreatedEnums = 0;
	int32 NumCreatedStructs = 0;
	int32 NumReused = 0;
};

class FTypePasteBlueprintBuilder
{
public:
	/** Preview text describing what will be created (does not create or load assets) */
	static FString BuildPreview(const FTPParseResult& Parsed, const FTPBlueprintOptions& Options, TArray<FTPMessage>& OutMessages);

	/** Creates the assets */
	static FTPBlueprintResult Build(const FTPParseResult& Parsed, const FTPBlueprintOptions& Options);

	/** Normalizes a path string (/All/Game/... -> /Game/..., removes the trailing /) */
	static FString NormalizePackagePath(const FString& InPath);
};
