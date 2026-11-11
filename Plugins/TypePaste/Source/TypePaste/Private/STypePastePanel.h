// TypePaste - editor tab UI

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "TypePasteTypes.h"

class SMultiLineEditableTextBox;
struct FTPBlueprintOptions;
struct FTPCppExportOptions;

enum class ETPTargetMode : uint8
{
	Blueprint,
	Cpp
};

class STypePastePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STypePastePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// UI construction
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildTargetOptions();
	TSharedRef<SWidget> BuildBlueprintOptions();
	TSharedRef<SWidget> BuildCppOptions();
	TSharedRef<SWidget> MakePathPickerMenu();
	static TSharedRef<SWidget> MakeRow(const FText& InLabel, const TSharedRef<SWidget>& InContent);

	// Input/preview
	void OnInputChanged(const FText& NewText);
	void Reparse();
	void RefreshPreview();

	// Buttons
	FReply OnCopyPromptClicked();
	FReply OnPasteClicked();
	FReply OnClearClicked();
	FReply OnGenerateClicked();
	FReply OnBuildAndReopenClicked();
	void StartBuildAndReopen();
	void OnPathPicked(const FString& Path);

	// Status display
	FText GetSummaryText() const;
	FSlateColor GetSummaryColor() const;
	bool IsGenerateEnabled() const;
	FText GetGenerateButtonText() const;
	void ShowResult(const TArray<FTPMessage>& Messages, const FString& Headline, bool bSuccess);

	// Options
	FTPBlueprintOptions MakeBlueprintOptions() const;
	FTPCppExportOptions MakeCppOptions() const;
	void LoadSettings();
	void SaveSettings() const;

	TSharedPtr<SMultiLineEditableTextBox> InputBox;
	TSharedPtr<SMultiLineEditableTextBox> PreviewBox;

	FString InputText;
	FTPParseResult LastParse;
	FString LastResultText;

	ETPTargetMode Mode = ETPTargetMode::Blueprint;

	// Blueprint options
	FString PackagePath = TEXT("/Game/Types");
	TArray<TSharedPtr<FString>> NamingOptions;
	TSharedPtr<FString> SelectedNaming;
	bool bSaveAssets = true;

	// C++ options
	TArray<TSharedPtr<FString>> ModuleOptions;
	TSharedPtr<FString> SelectedModule;
	FString SubFolder = TEXT("Types");
	bool bSingleFile = true;
	FString FileBaseName;
	bool bOverwrite = false;
	bool bOpenInIDE = true;
};
