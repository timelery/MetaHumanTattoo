#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FMetaHumanTattooSession;
class FStructOnScope;
class IStructureDetailsView;
class ITableRow;
class STableViewBase;
class UMetaHumanCharacter;
struct FPropertyChangedEvent;

/** The Tattoos tab of the MetaHuman Character editor. */
class SMetaHumanTattooPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanTattooPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMetaHumanCharacter* InCharacter);
	virtual ~SMetaHumanTattooPanel() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	using FItem = TSharedPtr<int32>;

	void RefreshList();
	void RefreshDetails();
	TSharedRef<ITableRow> OnGenerateRow(FItem Item, const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(FItem Item, ESelectInfo::Type SelectInfo);
	void OnDetailsChanged(const FPropertyChangedEvent& Event);

	FReply OnAddSvg();
	FReply OnDuplicate();
	FReply OnDelete();
	FReply OnMove(int32 Delta);
	FReply OnApply();
	FReply OnDiscard();
	FReply OnRemoveAll();
	FReply OnSwitchToSkin();

	void Notify(const FText& Message, bool bSuccess) const;

	FText GetTitleText() const;
	FText GetStatusText() const;
	FText GetPlacementHintText() const;
	FText GetPreviewWarningText() const;

	TSharedPtr<FMetaHumanTattooSession> Session;
	TArray<FItem> Items;
	TSharedPtr<SListView<FItem>> LayerList;
	TSharedPtr<IStructureDetailsView> DetailsView;
	TSharedPtr<FStructOnScope> DetailsStruct;
	const void* BoundLayer = nullptr;
	bool bListDirty = true;
	bool bSyncingSelection = false;
	FText LastReport;
	FString LastDirectory;
};
