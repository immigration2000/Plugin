// TypePaste - 에디터 탭 UI 구현

#include "STypePastePanel.h"
#include "TypePasteBlueprintBuilder.h"
#include "TypePasteCppExporter.h"
#include "TypePasteModule.h"
#include "TypePasteParser.h"
#include "TypePastePrompt.h"

#include "ContentBrowserModule.h"
#include "CoreGlobals.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STypePastePanel"

namespace TypePastePanelPrivate
{
	static const TCHAR* ConfigSection = TEXT("TypePaste");

	static FString FormatMessages(const TArray<FTPMessage>& Messages)
	{
		if (Messages.Num() == 0)
		{
			return FString();
		}

		struct FGroup
		{
			ETPSeverity Severity;
			const TCHAR* Label;
		};
		static const FGroup Groups[] =
		{
			{ ETPSeverity::Error, TEXT("[오류] ") },
			{ ETPSeverity::Warning, TEXT("[경고] ") },
			{ ETPSeverity::Info, TEXT("[정보] ") },
		};

		FString Out;
		for (const FGroup& Group : Groups)
		{
			for (const FTPMessage& Message : Messages)
			{
				if (Message.Severity == Group.Severity)
				{
					Out += Group.Label;
					Out += Message.Text;
					Out += TEXT("\n");
				}
			}
		}
		return Out + TEXT("\n");
	}

	static void Notify(const FText& Text, bool bSuccess)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 5.f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	static ETPAssetNaming NamingFromIndex(int32 Index)
	{
		switch (Index)
		{
		case 1: return ETPAssetNaming::PrefixE_S_;
		case 2: return ETPAssetNaming::PrefixE_F_;
		case 3: return ETPAssetNaming::NoPrefix;
		default: return ETPAssetNaming::KeepCppName;
		}
	}
}

void STypePastePanel::Construct(const FArguments& InArgs)
{
	NamingOptions.Add(MakeShared<FString>(TEXT("C++ 이름 그대로  (EWeaponType / FWeaponData)")));
	NamingOptions.Add(MakeShared<FString>(TEXT("E_ / S_ 접두사  (E_WeaponType / S_WeaponData)")));
	NamingOptions.Add(MakeShared<FString>(TEXT("E_ / F_ 접두사  (E_WeaponType / F_WeaponData)")));
	NamingOptions.Add(MakeShared<FString>(TEXT("접두사 없음  (WeaponType / WeaponData)")));
	SelectedNaming = NamingOptions[0];

	for (const FString& ModuleName : FTypePasteCppExporter::GetProjectModuleNames())
	{
		ModuleOptions.Add(MakeShared<FString>(ModuleName));
	}
	if (ModuleOptions.Num() > 0)
	{
		SelectedModule = ModuleOptions[0];
	}

	LoadSettings();

	const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 6.f, 6.f, 2.f)
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(6.f, 2.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.45f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("InputLabel", "AI 답변 붙여넣기"))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(InputBox, SMultiLineEditableTextBox)
					.Font(MonoFont)
					.AutoWrapText(false)
					.HintText(LOCTEXT("InputHint", "여기에 AI 가 추천한 UENUM / USTRUCT 코드를 붙여넣으세요. (설명이 섞인 답변 전체도 괜찮습니다)"))
					.OnTextChanged(this, &STypePastePanel::OnInputChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(this, &STypePastePanel::GetSummaryText)
					.ColorAndOpacity(this, &STypePastePanel::GetSummaryColor)
				]
			]

			+ SSplitter::Slot()
			.Value(0.55f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewLabel", "미리보기 / 결과"))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(PreviewBox, SMultiLineEditableTextBox)
					.Font(MonoFont)
					.AutoWrapText(false)
					.IsReadOnly(true)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 4.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildTargetOptions()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 6.f, 0.f, 0.f)
				[
					BuildBlueprintOptions()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 6.f, 0.f, 0.f)
				[
					BuildCppOptions()
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(6.f, 2.f, 6.f, 8.f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(16.f, 4.f))
			.IsEnabled(this, &STypePastePanel::IsGenerateEnabled)
			.OnClicked(this, &STypePastePanel::OnGenerateClicked)
			[
				SNew(STextBlock)
				.Text(this, &STypePastePanel::GetGenerateButtonText)
			]
		]
	];

	RefreshPreview();
}

// ---------------------------------------------------------------------------
// UI 구성
// ---------------------------------------------------------------------------

TSharedRef<SWidget> STypePastePanel::MakeRow(const FText& InLabel, const TSharedRef<SWidget>& InContent)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 2.f, 8.f, 2.f)
		[
			SNew(SBox)
			.WidthOverride(110.f)
			[
				SNew(STextBlock)
				.Text(InLabel)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(0.f, 2.f)
		[
			InContent
		];
}

TSharedRef<SWidget> STypePastePanel::BuildToolbar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CopyPrompt", "AI 요청 프롬프트 복사"))
			.ToolTipText(LOCTEXT("CopyPromptTip", "TypePaste 가 잘 읽는 형식으로 답하도록 AI 에게 줄 요청문을 클립보드에 복사합니다."))
			.OnClicked(this, &STypePastePanel::OnCopyPromptClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("PasteClipboard", "클립보드에서 붙여넣기"))
			.OnClicked(this, &STypePastePanel::OnPasteClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Clear", "지우기"))
			.OnClicked(this, &STypePastePanel::OnClearClicked)
		];
}

TSharedRef<SWidget> STypePastePanel::BuildTargetOptions()
{
	auto MakeRadio = [this](ETPTargetMode InMode, const FText& InLabel, const FText& InToolTip) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("RadioButton"))
			.ToolTipText(InToolTip)
			.IsChecked_Lambda([this, InMode]()
			{
				return Mode == InMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, InMode](ECheckBoxState NewState)
			{
				if (NewState == ECheckBoxState::Checked)
				{
					Mode = InMode;
					LastResultText.Reset();
					RefreshPreview();
				}
			})
			[
				SNew(STextBlock)
				.Text(InLabel)
			];
	};

	return MakeRow(LOCTEXT("TargetLabel", "생성 대상"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 16.f, 0.f)
		[
			MakeRadio(ETPTargetMode::Blueprint, LOCTEXT("ModeBlueprint", "블루프린트 에셋"),
				LOCTEXT("ModeBlueprintTip", "콘텐츠 브라우저에 열거형(UserDefinedEnum) / 구조체(UserDefinedStruct) 에셋을 만듭니다."))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeRadio(ETPTargetMode::Cpp, LOCTEXT("ModeCpp", "C++ 헤더"),
				LOCTEXT("ModeCppTip", "프로젝트 모듈 소스 폴더에 UENUM / USTRUCT 헤더 파일을 만듭니다."))
		]);
}

TSharedRef<SWidget> STypePastePanel::BuildBlueprintOptions()
{
	return SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			return Mode == ETPTargetMode::Blueprint ? EVisibility::Visible : EVisibility::Collapsed;
		})

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeRow(LOCTEXT("PathLabel", "저장 경로"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(PackagePath); })
					.OnTextChanged_Lambda([this](const FText& NewText)
					{
						PackagePath = NewText.ToString();
						RefreshPreview();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &STypePastePanel::MakePathPickerMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BrowseFolder", "폴더 선택"))
					]
				])
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeRow(LOCTEXT("NamingLabel", "에셋 이름 규칙"),
				SNew(STextComboBox)
				.OptionsSource(&NamingOptions)
				.InitiallySelectedItem(SelectedNaming)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (Item.IsValid())
					{
						SelectedNaming = Item;
						RefreshPreview();
					}
				}))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeRow(FText::GetEmpty(),
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bSaveAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bSaveAssets = NewState == ECheckBoxState::Checked; })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SaveAssets", "생성 후 바로 저장"))
				])
		];
}

TSharedRef<SWidget> STypePastePanel::BuildCppOptions()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			return Mode == ETPTargetMode::Cpp ? EVisibility::Visible : EVisibility::Collapsed;
		});

	if (ModuleOptions.Num() == 0)
	{
		Box->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("NoModules", "이 프로젝트에는 C++ 모듈이 없습니다. (Tools > New C++ Class 로 클래스를 하나 추가하면 C++ 프로젝트가 됩니다)"))
		];
		return Box;
	}

	Box->AddSlot()
	.AutoHeight()
	[
		MakeRow(LOCTEXT("ModuleLabel", "모듈"),
			SNew(STextComboBox)
			.OptionsSource(&ModuleOptions)
			.InitiallySelectedItem(SelectedModule)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				if (Item.IsValid())
				{
					SelectedModule = Item;
					RefreshPreview();
				}
			}))
	];

	Box->AddSlot()
	.AutoHeight()
	[
		MakeRow(LOCTEXT("SubFolderLabel", "하위 폴더"),
			SNew(SEditableTextBox)
			.Text_Lambda([this]() { return FText::FromString(SubFolder); })
			.HintText(LOCTEXT("SubFolderHint", "비우면 Public 폴더 바로 아래"))
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				SubFolder = NewText.ToString();
				RefreshPreview();
			}))
	];

	Box->AddSlot()
	.AutoHeight()
	[
		MakeRow(LOCTEXT("FileLabel", "파일"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bSingleFile ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bSingleFile = NewState == ECheckBoxState::Checked;
					RefreshPreview();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SingleFile", "한 파일로 묶기"))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.IsEnabled_Lambda([this]() { return bSingleFile; })
				.Text_Lambda([this]() { return FText::FromString(FileBaseName); })
				.HintText_Lambda([this]()
				{
					return FText::Format(LOCTEXT("FileNameHint", "파일 이름 (비우면 {0}.h)"), FText::FromString(FTypePasteCppWriter::SuggestFileBaseName(LastParse)));
				})
				.OnTextChanged_Lambda([this](const FText& NewText)
				{
					FileBaseName = NewText.ToString();
					RefreshPreview();
				})
			])
	];

	Box->AddSlot()
	.AutoHeight()
	[
		MakeRow(FText::GetEmpty(),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 16.f, 0.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bOverwrite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bOverwrite = NewState == ECheckBoxState::Checked; })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Overwrite", "기존 파일 덮어쓰기"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bOpenInIDE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bOpenInIDE = NewState == ECheckBoxState::Checked; })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OpenInIDE", "생성 후 IDE 에서 열기"))
				]
			])
	];

	return Box;
}

TSharedRef<SWidget> STypePastePanel::MakePathPickerMenu()
{
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FPathPickerConfig Config;
	Config.DefaultPath = FTypePasteBlueprintBuilder::NormalizePackagePath(PackagePath);
	Config.OnPathSelected = FOnPathSelected::CreateSP(this, &STypePastePanel::OnPathPicked);

	return SNew(SBox)
		.WidthOverride(320.f)
		.HeightOverride(420.f)
		[
			ContentBrowser.Get().CreatePathPicker(Config)
		];
}

void STypePastePanel::OnPathPicked(const FString& Path)
{
	PackagePath = FTypePasteBlueprintBuilder::NormalizePackagePath(Path);
	FSlateApplication::Get().DismissAllMenus();
	RefreshPreview();
}

// ---------------------------------------------------------------------------
// 입력 / 미리보기
// ---------------------------------------------------------------------------

void STypePastePanel::OnInputChanged(const FText& NewText)
{
	InputText = NewText.ToString();
	LastResultText.Reset();
	Reparse();
	RefreshPreview();
}

void STypePastePanel::Reparse()
{
	LastParse = FTypePasteParser::Parse(InputText);
}

void STypePastePanel::RefreshPreview()
{
	using namespace TypePastePanelPrivate;

	if (!PreviewBox.IsValid())
	{
		return;
	}

	FString Body;
	if (InputText.TrimStartAndEnd().IsEmpty())
	{
		Body = TypePastePrompt::GetHelpText();
	}
	else
	{
		TArray<FTPMessage> Messages = LastParse.Messages;
		FString Detail;
		if (LastParse.HasAnyType())
		{
			if (Mode == ETPTargetMode::Blueprint)
			{
				Detail = FTypePasteBlueprintBuilder::BuildPreview(LastParse, MakeBlueprintOptions(), Messages);
			}
			else
			{
				const FTPCppExportOptions Options = MakeCppOptions();
				const FString Dir = FTypePasteCppExporter::GetTargetDirectory(Options);
				Detail = FString(TEXT("C++ 헤더 미리보기  —  저장 폴더: ")) + (Dir.IsEmpty() ? FString(TEXT("(모듈을 찾지 못함)")) : Dir) + TEXT("\n");
				for (const FTPCppFile& File : FTypePasteCppExporter::GenerateFiles(LastParse, Options, Messages))
				{
					Detail += FString(TEXT("\n===== ")) + File.FileName + TEXT(" =====\n") + File.Content;
				}
			}
		}
		Body = FormatMessages(Messages) + Detail;
	}

	if (!LastResultText.IsEmpty())
	{
		Body = LastResultText + TEXT("------------------------------------------------------------\n\n") + Body;
	}
	PreviewBox->SetText(FText::FromString(Body));
}

// ---------------------------------------------------------------------------
// 버튼
// ---------------------------------------------------------------------------

FReply STypePastePanel::OnCopyPromptClicked()
{
	FPlatformApplicationMisc::ClipboardCopy(TypePastePrompt::GetRequestPrompt());
	TypePastePanelPrivate::Notify(LOCTEXT("PromptCopied", "AI 요청 프롬프트를 클립보드에 복사했습니다. [설계할 기능] 부분을 채워서 AI 에게 보내세요."), true);
	return FReply::Handled();
}

FReply STypePastePanel::OnPasteClicked()
{
	FString Clipboard;
	FPlatformApplicationMisc::ClipboardPaste(Clipboard);
	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::FromString(Clipboard));
	}
	OnInputChanged(FText::FromString(Clipboard));
	return FReply::Handled();
}

FReply STypePastePanel::OnClearClicked()
{
	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::GetEmpty());
	}
	OnInputChanged(FText::GetEmpty());
	return FReply::Handled();
}

FReply STypePastePanel::OnGenerateClicked()
{
	using namespace TypePastePanelPrivate;

	Reparse();
	if (!LastParse.HasAnyType() || LastParse.HasErrors())
	{
		Notify(LOCTEXT("CannotGenerate", "생성할 수 없습니다. 미리보기의 [오류] 를 먼저 확인해 주세요."), false);
		RefreshPreview();
		return FReply::Handled();
	}

	SaveSettings();

	if (Mode == ETPTargetMode::Blueprint)
	{
		const FTPBlueprintResult Result = FTypePasteBlueprintBuilder::Build(LastParse, MakeBlueprintOptions());
		const bool bAnyCreated = Result.CreatedAssets.Num() > 0;
		const FString Headline = FString::Printf(TEXT("블루프린트 에셋 생성: 열거형 %d개, 구조체 %d개 (기존 에셋 재사용 %d개)"),
			Result.NumCreatedEnums, Result.NumCreatedStructs, Result.NumReused);
		ShowResult(Result.Messages, Headline, bAnyCreated);

		if (bAnyCreated)
		{
			FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			ContentBrowser.Get().SyncBrowserToAssets(Result.CreatedAssets);
		}
	}
	else
	{
		const FTPCppExportResult Result = FTypePasteCppExporter::Export(LastParse, MakeCppOptions());
		const bool bAnyWritten = Result.WrittenFiles.Num() > 0;
		FString Headline = bAnyWritten
			? FString::Printf(TEXT("C++ 헤더 %d개 생성"), Result.WrittenFiles.Num())
			: FString(TEXT("C++ 헤더를 만들지 못했습니다"));
		for (const FString& Path : Result.WrittenFiles)
		{
			Headline += TEXT("\n   ");
			Headline += Path;
		}
		ShowResult(Result.Messages, Headline, bAnyWritten);
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------------------
// 상태 표시
// ---------------------------------------------------------------------------

FText STypePastePanel::GetSummaryText() const
{
	if (InputText.TrimStartAndEnd().IsEmpty())
	{
		return LOCTEXT("SummaryEmpty", "붙여넣기를 기다리는 중...");
	}
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
	for (const FTPMessage& Message : LastParse.Messages)
	{
		NumErrors += Message.Severity == ETPSeverity::Error ? 1 : 0;
		NumWarnings += Message.Severity == ETPSeverity::Warning ? 1 : 0;
	}
	return FText::Format(LOCTEXT("SummaryFormat", "열거형 {0}개 · 구조체 {1}개  |  오류 {2} · 경고 {3}"),
		LastParse.Enums.Num(), LastParse.Structs.Num(), NumErrors, NumWarnings);
}

FSlateColor STypePastePanel::GetSummaryColor() const
{
	if (LastParse.HasErrors())
	{
		return FSlateColor(FLinearColor(1.f, 0.35f, 0.35f));
	}
	if (!LastParse.HasAnyType())
	{
		return FSlateColor::UseSubduedForeground();
	}
	return FSlateColor(FLinearColor(0.45f, 0.85f, 0.45f));
}

bool STypePastePanel::IsGenerateEnabled() const
{
	if (!LastParse.HasAnyType() || LastParse.HasErrors())
	{
		return false;
	}
	return Mode == ETPTargetMode::Blueprint || SelectedModule.IsValid();
}

FText STypePastePanel::GetGenerateButtonText() const
{
	return Mode == ETPTargetMode::Blueprint
		? LOCTEXT("GenerateBlueprint", "블루프린트 에셋 생성")
		: LOCTEXT("GenerateCpp", "C++ 헤더 생성");
}

void STypePastePanel::ShowResult(const TArray<FTPMessage>& Messages, const FString& Headline, bool bSuccess)
{
	LastResultText = FString(TEXT("■ ")) + Headline + TEXT("\n") + TypePastePanelPrivate::FormatMessages(Messages);
	if (Messages.Num() == 0)
	{
		LastResultText += TEXT("\n");
	}
	RefreshPreview();

	FString FirstLine;
	if (!Headline.Split(TEXT("\n"), &FirstLine, nullptr))
	{
		FirstLine = Headline;
	}
	TypePastePanelPrivate::Notify(FText::FromString(FirstLine), bSuccess);
}

// ---------------------------------------------------------------------------
// 옵션
// ---------------------------------------------------------------------------

FTPBlueprintOptions STypePastePanel::MakeBlueprintOptions() const
{
	FTPBlueprintOptions Options;
	Options.PackagePath = FTypePasteBlueprintBuilder::NormalizePackagePath(PackagePath);
	Options.Naming = TypePastePanelPrivate::NamingFromIndex(NamingOptions.Find(SelectedNaming));
	Options.bSaveAssets = bSaveAssets;
	return Options;
}

FTPCppExportOptions STypePastePanel::MakeCppOptions() const
{
	FTPCppExportOptions Options;
	Options.ModuleName = SelectedModule.IsValid() ? *SelectedModule : FString();
	Options.SubFolder = SubFolder;
	Options.bSingleFile = bSingleFile;
	Options.FileBaseName = FileBaseName;
	Options.bOverwrite = bOverwrite;
	Options.bOpenInIDE = bOpenInIDE;
	return Options;
}

void STypePastePanel::LoadSettings()
{
	using namespace TypePastePanelPrivate;

	if (!GConfig)
	{
		return;
	}

	FString StringValue;
	if (GConfig->GetString(ConfigSection, TEXT("PackagePath"), StringValue, GEditorPerProjectIni) && !StringValue.IsEmpty())
	{
		PackagePath = StringValue;
	}
	int32 IntValue = 0;
	if (GConfig->GetInt(ConfigSection, TEXT("Naming"), IntValue, GEditorPerProjectIni) && NamingOptions.IsValidIndex(IntValue))
	{
		SelectedNaming = NamingOptions[IntValue];
	}
	if (GConfig->GetInt(ConfigSection, TEXT("Mode"), IntValue, GEditorPerProjectIni))
	{
		Mode = IntValue == 1 ? ETPTargetMode::Cpp : ETPTargetMode::Blueprint;
	}
	if (GConfig->GetString(ConfigSection, TEXT("Module"), StringValue, GEditorPerProjectIni))
	{
		for (const TSharedPtr<FString>& Option : ModuleOptions)
		{
			if (Option->Equals(StringValue))
			{
				SelectedModule = Option;
			}
		}
	}
	GConfig->GetString(ConfigSection, TEXT("SubFolder"), SubFolder, GEditorPerProjectIni);
	GConfig->GetBool(ConfigSection, TEXT("SaveAssets"), bSaveAssets, GEditorPerProjectIni);
	GConfig->GetBool(ConfigSection, TEXT("SingleFile"), bSingleFile, GEditorPerProjectIni);
	GConfig->GetBool(ConfigSection, TEXT("OpenInIDE"), bOpenInIDE, GEditorPerProjectIni);
}

void STypePastePanel::SaveSettings() const
{
	using namespace TypePastePanelPrivate;

	if (!GConfig)
	{
		return;
	}

	GConfig->SetString(ConfigSection, TEXT("PackagePath"), *FTypePasteBlueprintBuilder::NormalizePackagePath(PackagePath), GEditorPerProjectIni);
	GConfig->SetInt(ConfigSection, TEXT("Naming"), FMath::Max(0, NamingOptions.Find(SelectedNaming)), GEditorPerProjectIni);
	GConfig->SetInt(ConfigSection, TEXT("Mode"), Mode == ETPTargetMode::Cpp ? 1 : 0, GEditorPerProjectIni);
	if (SelectedModule.IsValid())
	{
		GConfig->SetString(ConfigSection, TEXT("Module"), **SelectedModule, GEditorPerProjectIni);
	}
	GConfig->SetString(ConfigSection, TEXT("SubFolder"), *SubFolder, GEditorPerProjectIni);
	GConfig->SetBool(ConfigSection, TEXT("SaveAssets"), bSaveAssets, GEditorPerProjectIni);
	GConfig->SetBool(ConfigSection, TEXT("SingleFile"), bSingleFile, GEditorPerProjectIni);
	GConfig->SetBool(ConfigSection, TEXT("OpenInIDE"), bOpenInIDE, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

#undef LOCTEXT_NAMESPACE
