// TypePaste - generator that turns parse results into Unreal C++ header (.h) text

#pragma once

#include "TypePasteTypes.h"

/** Interface that reports where existing engine/project types are declared (implemented with reflection in the editor) */
class ITPExternalTypeInfo
{
public:
	virtual ~ITPExternalTypeInfo() = default;

	/**
	 * Finds the include path and module name of the header that defines TypeName (e.g. FHitResult).
	 * @return true if found. An empty OutIncludePath means no include is needed (covered by CoreMinimal etc.)
	 */
	virtual bool FindTypeHeader(const FString& TypeName, FString& OutIncludePath, FString& OutModuleName) const = 0;
};

struct FTPCppWriteOptions
{
	/** e.g. "MYGAME_API". Omitted if empty */
	FString ApiMacro;

	/** Adds a default UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=...) to members without UPROPERTY */
	bool bAddMissingUProperty = true;

	/** Adds default values (0, false, nullptr, first enum entry) to primitive members without an initializer */
	bool bAddDefaultInitializers = true;

	/** Converts raw UObject pointers in UPROPERTYs (UTexture2D*) to TObjectPtr<> */
	bool bConvertRawObjectPointers = true;

	/** Line ending */
	FString LineEnding = TEXT("\n");
};

struct FTPCppFile
{
	/** e.g. "WeaponTypes.h" */
	FString FileName;
	FString Content;
};

class FTypePasteCppWriter
{
public:
	/** Puts every type in one file */
	static FTPCppFile WriteSingleFile(const FTPParseResult& Parsed, const FString& FileBaseName, const FTPCppWriteOptions& Options,
		const ITPExternalTypeInfo* TypeInfo, TArray<FTPMessage>& OutMessages);

	/** One file per type (file name = type name without prefix) */
	static TArray<FTPCppFile> WritePerType(const FTPParseResult& Parsed, const FTPCppWriteOptions& Options,
		const ITPExternalTypeInfo* TypeInfo, TArray<FTPMessage>& OutMessages);

	/** Suggests a default file name when writing a single file */
	static FString SuggestFileBaseName(const FTPParseResult& Parsed);

	/** Cleans a string up for use as a file name (removes the extension, keeps letters/digits/_ only) */
	static FString SanitizeFileBaseName(const FString& In);
};
