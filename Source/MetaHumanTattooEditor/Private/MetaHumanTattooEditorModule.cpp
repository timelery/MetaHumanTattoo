#include "MetaHumanTattooLog.h"
#include "MetaHumanTattooInputProcessor.h"
#include "MetaHumanTattooTabs.h"
#include "SMetaHumanTattooPanel.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorModule.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "MetaHumanTattoo"

DEFINE_LOG_CATEGORY(LogMetaHumanTattoo);

namespace UE::MetaHumanTattoo
{
	/** The Tattoos tab, registered on each MetaHuman Character editor's own tab manager. */
	const FName TattooTabId(TEXT("MetaHumanCharacterEditor_TattoosTab"));

	/** Toolbar of the MetaHuman Character editor (FAssetEditorToolkit naming: AssetEditor.<ToolkitFName>.ToolBar). */
	const FName MetaHumanEditorToolBar(TEXT("AssetEditor.MetaHumanCharacterEditor.ToolBar"));

	/** A tab the MetaHuman Character editor always has, used to dock the Tattoos tab next to it. */
	const FName DockNextToTabId(TEXT("MetaHumanCharacterEditor_PreviewSettingsTab"));

	void EnsureTattooTab(const TSharedPtr<FTabManager>& TabManager, UMetaHumanCharacter* Character, const TSharedPtr<FWorkspaceItem>& Group)
	{
		if (!TabManager.IsValid() || !Character || TabManager->HasTabSpawner(TattooTabId))
		{
			return;
		}

		TWeakObjectPtr<UMetaHumanCharacter> WeakCharacter(Character);
		FTabSpawnerEntry& Entry = TabManager->RegisterTabSpawner(TattooTabId, FOnSpawnTab::CreateLambda([WeakCharacter](const FSpawnTabArgs&)
		{
			TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(LOCTEXT("TattoosTab", "Tattoos"));
			if (UMetaHumanCharacter* Edited = WeakCharacter.Get())
			{
				Tab->SetContent(SNew(SMetaHumanTattooPanel, Edited));
			}
			return Tab;
		}))
		.SetDisplayName(LOCTEXT("TattoosTab", "Tattoos"))
		.SetTooltipText(LOCTEXT("TattoosTabTip", "Place SVG tattoos on the character and bake them into its skin."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.DecalActor"));
		if (Group.IsValid())
		{
			Entry.SetGroup(Group.ToSharedRef());
		}
	}

	bool OpenTattooTab(UMetaHumanCharacter* Character)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Character || !AssetEditors)
		{
			return false;
		}
		IAssetEditorInstance* Editor = AssetEditors->FindEditorForAsset(Character, true);
		if (!Editor)
		{
			AssetEditors->OpenEditorForAsset(Character);
			Editor = AssetEditors->FindEditorForAsset(Character, true);
		}
		if (!Editor)
		{
			return false;
		}
		const TSharedPtr<FTabManager> TabManager = Editor->GetAssociatedTabManager();
		EnsureTattooTab(TabManager, Character, nullptr);
		return TabManager.IsValid() && TabManager->TryInvokeTab(TattooTabId).IsValid();
	}

	UMetaHumanCharacter* FindEditedCharacter(const FAssetEditorToolkit& Toolkit)
	{
		if (const TArray<UObject*>* Objects = Toolkit.GetObjectsCurrentlyBeingEdited())
		{
			for (UObject* Object : *Objects)
			{
				if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(Object))
				{
					return Character;
				}
			}
		}
		return nullptr;
	}
}

class FMetaHumanTattooEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMetaHumanTattooEditorModule::RegisterMenus));

		FMetaHumanCharacterEditorModule& MetaHumanEditor = FModuleManager::LoadModuleChecked<FMetaHumanCharacterEditorModule>(TEXT("MetaHumanCharacterEditor"));
		LayoutHandle = MetaHumanEditor.OnRegisterLayoutExtensions().AddRaw(this, &FMetaHumanTattooEditorModule::ExtendLayout);

		if (GEditor)
		{
			HookEditor();
		}
		else
		{
			PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FMetaHumanTattooEditorModule::HookEditor);
		}
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);

		if (FMetaHumanCharacterEditorModule* MetaHumanEditor = FModuleManager::GetModulePtr<FMetaHumanCharacterEditorModule>(TEXT("MetaHumanCharacterEditor")))
		{
			MetaHumanEditor->OnRegisterLayoutExtensions().Remove(LayoutHandle);
		}

		if (GEditor)
		{
			if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditors->OnAssetEditorOpened().Remove(AssetEditorOpenedHandle);
			}
		}

		if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		}
		InputProcessor.Reset();

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void HookEditor()
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
		{
			AssetEditorOpenedHandle = AssetEditors->OnAssetEditorOpened().AddRaw(this, &FMetaHumanTattooEditorModule::OnAssetEditorOpened);
		}

		if (FSlateApplication::IsInitialized() && !InputProcessor.IsValid())
		{
			InputProcessor = MakeShared<FMetaHumanTattooInputProcessor>();
			FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
		}
	}

	/** Registers the Tattoos tab on every MetaHuman Character editor, so a saved layout with the tab open can restore it. */
	void OnAssetEditorOpened(UObject* Asset)
	{
		UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(Asset);
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Character || !AssetEditors)
		{
			return;
		}
		if (IAssetEditorInstance* Editor = AssetEditors->FindEditorForAsset(Character, false))
		{
			UE::MetaHumanTattoo::EnsureTattooTab(Editor->GetAssociatedTabManager(), Character, nullptr);
		}
	}

	void ExtendLayout(FLayoutExtender& Extender)
	{
		Extender.ExtendLayout(UE::MetaHumanTattoo::DockNextToTabId, ELayoutExtensionPosition::After,
			FTabManager::FTab(UE::MetaHumanTattoo::TattooTabId, ETabState::ClosedTab));
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(UE::MetaHumanTattoo::MetaHumanEditorToolBar);
		FToolMenuSection& Section = ToolBar->FindOrAddSection(TEXT("MetaHumanTattoo"));

		FToolUIAction Action;
		Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
		{
			const UAssetEditorToolkitMenuContext* MenuContext = Context.FindContext<UAssetEditorToolkitMenuContext>();
			const TSharedPtr<FAssetEditorToolkit> Toolkit = MenuContext ? MenuContext->Toolkit.Pin() : nullptr;
			if (!Toolkit.IsValid())
			{
				return;
			}
			UMetaHumanCharacter* Character = UE::MetaHumanTattoo::FindEditedCharacter(*Toolkit);
			const TSharedPtr<FTabManager> TabManager = Toolkit->GetTabManager();
			UE::MetaHumanTattoo::EnsureTattooTab(TabManager, Character, nullptr);
			if (TabManager.IsValid())
			{
				TabManager->TryInvokeTab(UE::MetaHumanTattoo::TattooTabId);
			}
		});

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("MetaHumanTattoos"),
			FToolUIActionChoice(Action),
			LOCTEXT("TattoosButton", "Tattoos"),
			LOCTEXT("TattoosButtonTip", "Open the Tattoos panel: add SVG tattoos, click on the character to place them, scale and turn them, then Apply to Skin to bake them into the skin textures."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.DecalActor")));
	}

	FDelegateHandle LayoutHandle;
	FDelegateHandle AssetEditorOpenedHandle;
	FDelegateHandle PostEngineInitHandle;
	TSharedPtr<FMetaHumanTattooInputProcessor> InputProcessor;
};

IMPLEMENT_MODULE(FMetaHumanTattooEditorModule, MetaHumanTattooEditor)

#undef LOCTEXT_NAMESPACE
