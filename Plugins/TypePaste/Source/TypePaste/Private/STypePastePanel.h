// TypePaste - 에디터 탭 UI

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
	// UI 구성
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildTargetOptions();
	TSharedRef<SWidget> BuildBlueprintOptions();
	TSharedRef<SWidget> BuildCppOptions();
	TSharedRef<SWidget> MakePathPickerMenu();
	static TSharedRef<SWidget> MakeRow(const FText& InLabel, const TSharedRef<SWidget>& InContent);

	// 입력/미리보기
	void OnInputChanged(const FText& NewText);
	void Reparse();
	void RefreshPreview();

	// 버튼
	FReply OnCopyPromptClicked();
	FReply OnPasteClicked();
	FReply OnClearClicked();
	FReply OnGenerateClicked();
	void OnPathPicked(const FString& Path);

	// 상태 표시
	FText GetSummaryText() const;
	FSlateColor GetSummaryColor() const;
	bool IsGenerateEnabled() const;
	FText GetGenerateButtonText() const;
	void ShowResult(const TArray<FTPMessage>& Messages, const FString& Headline, bool bSuccess);

	// 옵션
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

	// 블루프린트 옵션
	FString PackagePath = TEXT("/Game/Types");
	TArray<TSharedPtr<FString>> NamingOptions;
	TSharedPtr<FString> SelectedNaming;
	bool bSaveAssets = true;

	// C++ 옵션
	TArray<TSharedPtr<FString>> ModuleOptions;
	TSharedPtr<FString> SelectedModule;
	FString SubFolder = TEXT("Types");
	bool bSingleFile = true;
	FString FileBaseName;
	bool bOverwrite = false;
	bool bOpenInIDE = true;
};
