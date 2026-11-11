// TypePaste - editor tab UI implementation

#include "STypePastePanel.h"
#include "TypePasteBlueprintBuilder.h"
#include "TypePasteBuildLauncher.h"
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
#include "Misc/MessageDialog.h"
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
			{ ETPSeverity::Error, TEXT("[Error] ") },
			{ ETPSeverity::Warning, TEXT("[Warning] ") },
			{ ETPSeverity::Info, TEXT("[Info] ") },
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
	NamingOptions.Add(MakeShared<FString>(TEXT("Keep C++ name  (EWeaponType / FWeaponData)")));
	NamingOptions.Add(MakeShared<FString>(TEXT("E_ / S_ prefix  (E_WeaponType / S_WeaponData)")));
	NamingOptions.Add(MakeShared<FString>(TEXT("E_ / F_ prefix  (E_WeaponType / F_WeaponData)")));
	NamingOptions.Add(MakeShared<FString>(TEXT("No prefix  (WeaponType / WeaponData)")));
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
					.Text(LOCTEXT("InputLabel", "Paste AI answer"))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(InputBox, SMultiLineEditableTextBox)
					.Font(MonoFont)
					.AutoWrapText(false)
					.HintText(LOCTEXT("InputHint", "Paste the UENUM / USTRUCT code suggested by the AI here. (A whole answer with prose mixed in is fine too)"))
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
					.Text(LOCTEXT("PreviewLabel", "Preview / Result"))
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
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(12.f, 4.f))
				.Visibility_Lambda([this]() { return Mode == ETPTargetMode::Cpp ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("BuildAndReopenTip", "Closes the editor, builds the project (no solution file needed) and opens it again. If there are unsaved assets, you are asked to save them first."))
				.OnClicked(this, &STypePastePanel::OnBuildAndReopenClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BuildAndReopen", "Build and Reopen"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
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
		]
	];

	RefreshPreview();
}

// ---------------------------------------------------------------------------
// UI construction
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
			.Text(LOCTEXT("CopyPrompt", "Copy AI Prompt"))
			.ToolTipText(LOCTEXT("CopyPromptTip", "Copies a request to the clipboard that makes the AI answer in a format TypePaste reads well (English only)."))
			.OnClicked(this, &STypePastePanel::OnCopyPromptClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("PasteClipboard", "Paste from Clipboard"))
			.OnClicked(this, &STypePastePanel::OnPasteClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Clear", "Clear"))
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

	return MakeRow(LOCTEXT("TargetLabel", "Generate"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.f, 0.f, 16.f, 0.f)
		[
			MakeRadio(ETPTargetMode::Blueprint, LOCTEXT("ModeBlueprint", "Blueprint assets"),
				LOCTEXT("ModeBlueprintTip", "Creates enum (UserDefinedEnum) / struct (UserDefinedStruct) assets in the Content Browser."))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeRadio(ETPTargetMode::Cpp, LOCTEXT("ModeCpp", "C++ headers"),
				LOCTEXT("ModeCppTip", "Creates UENUM / USTRUCT header files in a project module's source folder."))
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
			MakeRow(LOCTEXT("PathLabel", "Save path"),
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
						.Text(LOCTEXT("BrowseFolder", "Browse"))
					]
				])
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeRow(LOCTEXT("NamingLabel", "Asset naming"),
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
					.Text(LOCTEXT("SaveAssets", "Save after creating"))
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
			.Text(LOCTEXT("NoModules", "This project has no C++ module. (Add a class with Tools > New C++ Class to make it a C++ project)"))
		];
		return Box;
	}

	Box->AddSlot()
	.AutoHeight()
	[
		MakeRow(LOCTEXT("ModuleLabel", "Module"),
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
		MakeRow(LOCTEXT("SubFolderLabel", "Subfolder"),
			SNew(SEditableTextBox)
			.Text_Lambda([this]() { return FText::FromString(SubFolder); })
			.HintText(LOCTEXT("SubFolderHint", "Empty = directly under the Public folder"))
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				SubFolder = NewText.ToString();
				RefreshPreview();
			}))
	];

	Box->AddSlot()
	.AutoHeight()
	[
		MakeRow(LOCTEXT("FileLabel", "File"),
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
					.Text(LOCTEXT("SingleFile", "Single file"))
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
					return FText::Format(LOCTEXT("FileNameHint", "File name (empty = {0}.h)"), FText::FromString(FTypePasteCppWriter::SuggestFileBaseName(LastParse)));
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
					.Text(LOCTEXT("Overwrite", "Overwrite existing files"))
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
					.Text(LOCTEXT("OpenInIDE", "Open in IDE after creating"))
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
// Input / preview
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
				Detail = FString(TEXT("C++ header preview  -  folder: ")) + (Dir.IsEmpty() ? FString(TEXT("(module not found)")) : Dir) + TEXT("\n");
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
// Buttons
// ---------------------------------------------------------------------------

FReply STypePastePanel::OnCopyPromptClicked()
{
	FPlatformApplicationMisc::ClipboardCopy(TypePastePrompt::GetRequestPrompt());
	TypePastePanelPrivate::Notify(LOCTEXT("PromptCopied", "Copied the AI prompt to the clipboard. Fill in [Feature to design] and send it to the AI."), true);
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
		Notify(LOCTEXT("CannotGenerate", "Cannot generate. Check the [Error] lines in the preview first."), false);
		RefreshPreview();
		return FReply::Handled();
	}

	SaveSettings();

	if (Mode == ETPTargetMode::Blueprint)
	{
		const FTPBlueprintResult Result = FTypePasteBlueprintBuilder::Build(LastParse, MakeBlueprintOptions());
		const bool bAnyCreated = Result.CreatedAssets.Num() > 0;
		const FString Headline = FString::Printf(TEXT("Created Blueprint assets: %d enum(s), %d struct(s) (%d existing asset(s) reused)"),
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
			? FString::Printf(TEXT("Created %d C++ header(s)"), Result.WrittenFiles.Num())
			: FString(TEXT("Could not create the C++ headers"));
		for (const FString& Path : Result.WrittenFiles)
		{
			Headline += TEXT("\n   ");
			Headline += Path;
		}
		ShowResult(Result.Messages, Headline, bAnyWritten);

		if (bAnyWritten)
		{
			const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
				LOCTEXT("AskBuildAndReopen", "The C++ headers were created. New types only appear after a build.\n\nClose the editor, build, and reopen the project now?\n(You can also do this later with [Build and Reopen] or the Build.bat in the project root)"));
			if (Answer == EAppReturnType::Yes)
			{
				StartBuildAndReopen();
			}
		}
	}
	return FReply::Handled();
}

FReply STypePastePanel::OnBuildAndReopenClicked()
{
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel,
		LOCTEXT("ConfirmBuildAndReopen", "The editor will close, the project will be built, and then it will be opened again.\nBuild progress is shown in a new console window."));
	if (Answer == EAppReturnType::Ok)
	{
		StartBuildAndReopen();
	}
	return FReply::Handled();
}

void STypePastePanel::StartBuildAndReopen()
{
	SaveSettings();
	TArray<FTPMessage> Messages;
	if (!FTypePasteBuildLauncher::LaunchBuildAndReopen(Messages))
	{
		ShowResult(Messages, TEXT("Could not run the build script"), false);
	}
}

// ---------------------------------------------------------------------------
// Status display
// ---------------------------------------------------------------------------

FText STypePastePanel::GetSummaryText() const
{
	if (InputText.TrimStartAndEnd().IsEmpty())
	{
		return LOCTEXT("SummaryEmpty", "Waiting for input...");
	}
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
	for (const FTPMessage& Message : LastParse.Messages)
	{
		NumErrors += Message.Severity == ETPSeverity::Error ? 1 : 0;
		NumWarnings += Message.Severity == ETPSeverity::Warning ? 1 : 0;
	}
	return FText::Format(LOCTEXT("SummaryFormat", "{0} enum(s), {1} struct(s)  |  {2} error(s), {3} warning(s)"),
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
		? LOCTEXT("GenerateBlueprint", "Create Blueprint Assets")
		: LOCTEXT("GenerateCpp", "Create C++ Headers");
}

void STypePastePanel::ShowResult(const TArray<FTPMessage>& Messages, const FString& Headline, bool bSuccess)
{
	LastResultText = FString(TEXT("* ")) + Headline + TEXT("\n") + TypePastePanelPrivate::FormatMessages(Messages);
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
// Options
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
