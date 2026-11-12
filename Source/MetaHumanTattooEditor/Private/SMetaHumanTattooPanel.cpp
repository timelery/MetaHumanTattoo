#include "SMetaHumanTattooPanel.h"

#include "MetaHumanTattooSession.h"
#include "MetaHumanTattooSet.h"
#include "MetaHumanTattooTypes.h"

#include "MetaHumanCharacter.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDesktopPlatform.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "MetaHumanTattoo"

void SMetaHumanTattooPanel::Construct(const FArguments& InArgs, UMetaHumanCharacter* InCharacter)
{
	Session = MakeShared<FMetaHumanTattooSession>(InCharacter);
	Session->Initialize();
	Session->OnLayersChanged.AddSP(this, &SMetaHumanTattooPanel::RefreshList);

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bShowOptions = false;
	FStructureDetailsViewArgs StructArgs;
	DetailsView = PropertyEditor.CreateStructureDetailView(DetailsArgs, StructArgs, nullptr);
	DetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SMetaHumanTattooPanel::OnDetailsChanged);

	auto MakeWarning = [](TAttribute<FText> Text, TAttribute<EVisibility> Visibility, TSharedRef<SWidget> Extra) -> TSharedRef<SWidget>
	{
		return SNew(SBorder)
			.Visibility(Visibility)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 8, 0)
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Warning")).ColorAndOpacity(FSlateColor(EStyleColor::Warning))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Text).AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					Extra
				]
			];
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		// Title and status.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SMetaHumanTattooPanel::GetTitleText).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SMetaHumanTattooPanel::GetStatusText).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			MakeWarning(
				LOCTEXT("NeedSkinPreview", "The viewport shows the Topology or Clay material, which hides the skin textures and tattoos."),
				TAttribute<EVisibility>::CreateLambda([this]() { return Session->NeedsSkinPreview() ? EVisibility::Visible : EVisibility::Collapsed; }),
				SNew(SButton).Text(LOCTEXT("ShowSkin", "Show Skin")).OnClicked(this, &SMetaHumanTattooPanel::OnSwitchToSkin))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			MakeWarning(
				LOCTEXT("SkinChanged", "The skin tone or skin texture changed after the tattoos were applied. Apply to Skin again so they sit on the new skin."),
				TAttribute<EVisibility>::CreateLambda([this]() { return Session->SkinChangedSinceApply() ? EVisibility::Visible : EVisibility::Collapsed; }),
				SNullWidget::NullWidget)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			MakeWarning(
				TAttribute<FText>::CreateLambda([this]() { return GetPreviewWarningText(); }),
				TAttribute<EVisibility>::CreateLambda([this]() { return Session->GetPreviewWarning().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }),
				SNullWidget::NullWidget)
		]

		// List tools.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.ToolTipText(LOCTEXT("AddTip", "Add one or more tattoos from SVG files. Each new tattoo is selected and Place on Skin is switched on: click on the character to put it there."))
				.OnClicked(this, &SMetaHumanTattooPanel::OnAddSvg)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("Add", "Add SVG..."))
					]
				]
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton).Text(LOCTEXT("Duplicate", "Duplicate"))
				.ToolTipText(LOCTEXT("DuplicateTip", "Copy the selected tattoo, offset half its width to the side. Tick Mirror on the copy for a matching tattoo on the other arm."))
				.IsEnabled_Lambda([this]() { return Session->GetSelectedLayer() != nullptr; })
				.OnClicked(this, &SMetaHumanTattooPanel::OnDuplicate)
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton).Text(LOCTEXT("Delete", "Delete"))
				.ToolTipText(LOCTEXT("DeleteTip", "Remove the selected tattoo from the list. Apply to Skin takes it off the skin."))
				.IsEnabled_Lambda([this]() { return Session->GetSelectedLayer() != nullptr; })
				.OnClicked(this, &SMetaHumanTattooPanel::OnDelete)
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton).Text(LOCTEXT("MoveUp", "Up"))
				.ToolTipText(LOCTEXT("MoveUpTip", "Draw the selected tattoo earlier, under the tattoos after it where they overlap."))
				.IsEnabled_Lambda([this]() { return Session->GetSelectedIndex() > 0; })
				.OnClicked(this, &SMetaHumanTattooPanel::OnMove, -1)
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton).Text(LOCTEXT("MoveDown", "Down"))
				.ToolTipText(LOCTEXT("MoveDownTip", "Draw the selected tattoo later, over the tattoos before it where they overlap."))
				.IsEnabled_Lambda([this]() { return Session->GetSelectedLayer() && Session->GetSelectedIndex() < Session->GetNumLayers() - 1; })
				.OnClicked(this, &SMetaHumanTattooPanel::OnMove, 1)
			]
		]

		// Tattoo list.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SBox).HeightOverride(132.0f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder")).Padding(2.0f)
				[
					SAssignNew(LayerList, SListView<FItem>)
					.ListItemsSource(&Items)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SMetaHumanTattooPanel::OnGenerateRow)
					.OnSelectionChanged(this, &SMetaHumanTattooPanel::OnSelectionChanged)
				]
			]
		]

		// Placement.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("PlaceTip", "While on, a left click on the character in the viewport puts the selected tattoo there; hold the button and drag to slide it over the skin. Ctrl + mouse wheel scales it, Shift + mouse wheel turns it, Esc switches placing off. Clicks that miss the character still orbit the camera; Alt + left drag always orbits."))
				.IsEnabled_Lambda([this]() { return Session->GetSelectedLayer() != nullptr; })
				.IsChecked_Lambda([this]() { return Session->IsPlacing() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Session->SetPlacing(State == ECheckBoxState::Checked); })
				[
					SNew(SBox).Padding(FMargin(10, 4))
					[
						SNew(STextBlock).Text(LOCTEXT("Place", "Place on Skin"))
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(STextBlock).Text(this, &SMetaHumanTattooPanel::GetPlacementHintText).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		// Selected tattoo settings.
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 4)
		[
			DetailsView->GetWidget().ToSharedRef()
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SSeparator)
		]

		// Apply.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Text(LOCTEXT("Apply", "Apply to Skin"))
				.ToolTipText(LOCTEXT("ApplyTip", "Bake every visible, placed tattoo into the character's skin base color textures (head and body), set them as the character's texture overrides and save. From then on the tattoos are part of the skin: assembly, LODs and lighting treat them as skin. Sweat systems keep copies of the skin materials, so author sweat after applying (and again whenever the tattoos change)."))
				.IsEnabled_Lambda([this]() { return Session->HasUnappliedChanges() || Session->SkinChangedSinceApply(); })
				.OnClicked(this, &SMetaHumanTattooPanel::OnApply)
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.Text(LOCTEXT("Discard", "Discard Changes"))
				.ToolTipText(LOCTEXT("DiscardTip", "Return the tattoo list to how it was when it was last applied."))
				.IsEnabled_Lambda([this]() { return Session->HasUnappliedChanges(); })
				.OnClicked(this, &SMetaHumanTattooPanel::OnDiscard)
			]
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveAll", "Remove All Tattoos"))
				.ToolTipText(LOCTEXT("RemoveAllTip", "Delete every tattoo and put back the skin textures the character had before tattoos were applied."))
				.IsEnabled_Lambda([this]() { return Session->GetNumLayers() > 0 || Session->HasAppliedTattoos(); })
				.OnClicked(this, &SMetaHumanTattooPanel::OnRemoveAll)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 8)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return LastReport; })
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];

	RefreshList();
	RefreshDetails();
}

SMetaHumanTattooPanel::~SMetaHumanTattooPanel()
{
	if (Session.IsValid())
	{
		Session->OnLayersChanged.RemoveAll(this);
		Session->Shutdown();
	}
}

void SMetaHumanTattooPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bListDirty)
	{
		bListDirty = false;

		const int32 Count = Session->GetNumLayers();
		if (Items.Num() != Count)
		{
			Items.Reset();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Items.Add(MakeShared<int32>(Index));
			}
		}
		LayerList->RequestListRefresh();

		const int32 Selected = Session->GetSelectedIndex();
		TGuardValue<bool> Guard(bSyncingSelection, true);
		if (Items.IsValidIndex(Selected))
		{
			if (!LayerList->IsItemSelected(Items[Selected]))
			{
				LayerList->SetSelection(Items[Selected]);
			}
		}
		else
		{
			LayerList->ClearSelection();
		}
	}

	// Rebind the settings view only when it would point at a different tattoo: value changes show by themselves.
	if (Session->GetSelectedLayer() != BoundLayer)
	{
		RefreshDetails();
	}
}

void SMetaHumanTattooPanel::RefreshList()
{
	bListDirty = true;
}

void SMetaHumanTattooPanel::RefreshDetails()
{
	FMetaHumanTattooLayer* Layer = Session->GetSelectedLayer();
	BoundLayer = Layer;
	DetailsStruct.Reset();
	if (Layer)
	{
		DetailsStruct = MakeShared<FStructOnScope>(FMetaHumanTattooLayer::StaticStruct(), reinterpret_cast<uint8*>(Layer));
	}
	DetailsView->SetStructureData(DetailsStruct);
}

TSharedRef<ITableRow> SMetaHumanTattooPanel::OnGenerateRow(FItem Item, const TSharedRef<STableViewBase>& Owner)
{
	const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;
	return SNew(STableRow<FItem>, Owner)
		.Padding(FMargin(4, 2))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("VisibleTip", "Show this tattoo. Untick to hide it without deleting it."))
				.IsChecked_Lambda([this, Index]()
				{
					const FMetaHumanTattooLayer* Layer = Session->GetLayer(Index);
					return Layer && Layer->bVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
				{
					if (FMetaHumanTattooLayer* Layer = Session->GetLayer(Index))
					{
						Layer->bVisible = State == ECheckBoxState::Checked;
						Session->NotifyLayerEdited(Index, false);
					}
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this, Index]()
				{
					const FMetaHumanTattooLayer* Layer = Session->GetLayer(Index);
					return Layer ? FText::FromString(Layer->Name) : FText::GetEmpty();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([this, Index]()
				{
					const FMetaHumanTattooLayer* Layer = Session->GetLayer(Index);
					if (!Layer)
					{
						return FText::GetEmpty();
					}
					if (!Layer->Placement.bPlaced)
					{
						return LOCTEXT("NotPlaced", "not placed");
					}
					return FText::Format(LOCTEXT("SizeFormat", "{0} x {1} cm"), FText::AsNumber(FMath::RoundToInt(Layer->WidthCm * 10) / 10.0f), FText::AsNumber(FMath::RoundToInt(Layer->GetHeightCm() * 10) / 10.0f));
				})
			]
		];
}

void SMetaHumanTattooPanel::OnSelectionChanged(FItem Item, ESelectInfo::Type SelectInfo)
{
	if (bSyncingSelection)
	{
		return;
	}
	Session->SetSelectedIndex(Item.IsValid() ? *Item : INDEX_NONE);
	if (!Session->GetSelectedLayer())
	{
		Session->SetPlacing(false);
	}
}

void SMetaHumanTattooPanel::OnDetailsChanged(const FPropertyChangedEvent& Event)
{
	const FName Member = Event.GetMemberPropertyName();
	const bool bReloadSvg = Member == GET_MEMBER_NAME_CHECKED(FMetaHumanTattooLayer, SvgFile) && Event.ChangeType != EPropertyChangeType::Interactive;
	FString Error;
	Session->NotifyLayerEdited(Session->GetSelectedIndex(), bReloadSvg, &Error);
	if (!Error.IsEmpty())
	{
		Notify(FText::FromString(Error), false);
	}
	RefreshList();
}

FReply SMetaHumanTattooPanel::OnAddSvg()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> Files;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	const bool bChosen = DesktopPlatform->OpenFileDialog(ParentWindow, LOCTEXT("ChooseSvg", "Choose SVG tattoo designs").ToString(), LastDirectory, FString(),
		TEXT("Scalable Vector Graphics (*.svg)|*.svg"), EFileDialogFlags::Multiple, Files);
	if (!bChosen || Files.IsEmpty())
	{
		return FReply::Handled();
	}

	LastDirectory = FPaths::GetPath(Files[0]);
	int32 Added = 0;
	for (const FString& File : Files)
	{
		FString Error;
		if (Session->AddLayerFromFile(FPaths::ConvertRelativePathToFull(File), Error) == INDEX_NONE)
		{
			Notify(FText::FromString(Error), false);
		}
		else
		{
			++Added;
		}
	}

	if (Added > 0)
	{
		Session->SetPlacing(true);
		LastReport = LOCTEXT("PlaceNow", "Click on the character to place the new tattoo.");
	}
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnDuplicate()
{
	Session->DuplicateSelected();
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnDelete()
{
	Session->DeleteSelected();
	if (!Session->GetSelectedLayer())
	{
		Session->SetPlacing(false);
	}
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnMove(int32 Delta)
{
	Session->MoveSelected(Delta);
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnApply()
{
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("Applying", "Baking tattoos into the skin textures..."));
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	FString Report;
	const bool bOk = Session->Apply(Report);
	LastReport = FText::FromString(Report);
	Notify(bOk ? LOCTEXT("Applied", "Tattoos applied to the skin.") : LOCTEXT("ApplyFailed", "Applying tattoos failed; see the panel and the Output Log."), bOk);
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnDiscard()
{
	Session->DiscardChanges();
	LastReport = LOCTEXT("Discarded", "Changes discarded.");
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnRemoveAll()
{
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("Removing", "Removing tattoos from the skin textures..."));
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	FString Report;
	const bool bOk = Session->RemoveAll(Report);
	LastReport = FText::FromString(Report);
	Notify(bOk ? LOCTEXT("Removed", "All tattoos removed.") : LOCTEXT("RemoveFailed", "Removing tattoos failed; see the panel and the Output Log."), bOk);
	return FReply::Handled();
}

FReply SMetaHumanTattooPanel::OnSwitchToSkin()
{
	Session->SwitchToSkinPreview();
	return FReply::Handled();
}

void SMetaHumanTattooPanel::Notify(const FText& Message, bool bSuccess) const
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = bSuccess ? 3.0f : 6.0f;
	Info.bUseSuccessFailIcons = true;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

FText SMetaHumanTattooPanel::GetTitleText() const
{
	const UMetaHumanCharacter* Character = Session->GetCharacter();
	return Character ? FText::Format(LOCTEXT("Title", "Tattoos: {0}"), FText::FromString(Character->GetName())) : LOCTEXT("NoCharacter", "Tattoos");
}

FText SMetaHumanTattooPanel::GetStatusText() const
{
	const int32 Count = Session->GetNumLayers();
	if (Count == 0)
	{
		return Session->HasAppliedTattoos() ? LOCTEXT("StatusRemovePending", "list empty, skin still tattooed") : LOCTEXT("StatusEmpty", "no tattoos");
	}
	return Session->HasUnappliedChanges()
		? FText::Format(LOCTEXT("StatusUnapplied", "{0} tattoo(s), not applied yet"), Count)
		: FText::Format(LOCTEXT("StatusApplied", "{0} tattoo(s), applied"), Count);
}

FText SMetaHumanTattooPanel::GetPlacementHintText() const
{
	if (!Session->GetSelectedLayer())
	{
		return Session->GetNumLayers() == 0 ? LOCTEXT("HintEmpty", "Add an SVG to start.") : LOCTEXT("HintSelect", "Select a tattoo.");
	}
	if (Session->IsPlacing())
	{
		return LOCTEXT("HintPlacing", "Click or drag on the skin. Ctrl+wheel: size. Shift+wheel: turn. Esc: done.");
	}
	return Session->GetSelectedLayer()->Placement.bPlaced
		? LOCTEXT("HintPlaced", "Turn on to move the selected tattoo.")
		: LOCTEXT("HintUnplaced", "Turn on, then click on the character to place the selected tattoo.");
}

FText SMetaHumanTattooPanel::GetPreviewWarningText() const
{
	return FText::FromString(Session->GetPreviewWarning());
}

#undef LOCTEXT_NAMESPACE
