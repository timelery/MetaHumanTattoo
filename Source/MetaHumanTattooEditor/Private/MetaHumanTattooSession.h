#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ImageCore.h"
#include "Layout/SlateRect.h"
#include "UObject/GCObject.h"
#include "MetaHumanTattooBaker.h"
#include "MetaHumanTattooMesh.h"

class FEditorViewportClient;
class UMaterialInstanceDynamic;
class UMetaHumanCharacter;
class UMetaHumanTattooSet;
class USkeletalMeshComponent;
class UTexture;
class UTexture2D;
struct FMetaHumanTattooLayer;

/**
 * Tattoo editing for one MetaHuman Character while its Tattoos panel is open: the selected tattoo, click-to-place in the
 * MetaHuman Character editor viewport, the live preview (unapplied tattoos drawn into preview copies of the base color
 * textures and shown on the editor's skin materials), and Apply / Discard.
 */
class FMetaHumanTattooSession : public FGCObject, public TSharedFromThis<FMetaHumanTattooSession>
{
public:
	explicit FMetaHumanTattooSession(UMetaHumanCharacter* InCharacter);
	virtual ~FMetaHumanTattooSession() override;

	/** Starts ticking and listening to the character. Call once after construction. */
	void Initialize();

	/** Puts the editor's own textures back and stops ticking. */
	void Shutdown();

	/** Every live session, for the viewport input processor. */
	static TArray<TWeakPtr<FMetaHumanTattooSession>>& GetAllSessions();

	/** The live session of a character (its Tattoos panel is open), or null. */
	static TSharedPtr<FMetaHumanTattooSession> FindSession(const UMetaHumanCharacter* InCharacter);

	/** Screen-space rectangle of the character's editor viewport. */
	bool GetViewportScreenRect(FSlateRect& OutRect) const;

	/** One-line summary of the live preview (bound materials, preview textures, warnings), for scripts and diagnostics. */
	FString DescribePreview() const;

	UMetaHumanCharacter* GetCharacter() const { return Character.Get(); }
	UMetaHumanTattooSet* GetSet() const { return Set; }

	int32 GetNumLayers() const;
	FMetaHumanTattooLayer* GetLayer(int32 Index) const;
	int32 GetSelectedIndex() const { return SelectedIndex; }
	void SetSelectedIndex(int32 Index);
	FMetaHumanTattooLayer* GetSelectedLayer() const { return GetLayer(SelectedIndex); }

	/** Adds a tattoo from an SVG file and selects it. Returns its index or INDEX_NONE. */
	int32 AddLayerFromFile(const FString& FilePath, FString& OutError);
	void DuplicateSelected();
	void DeleteSelected();
	void MoveSelected(int32 Delta);

	/** Call after any edit of a tattoo. Reloads the SVG when bReloadSvg is set. */
	void NotifyLayerEdited(int32 Index, bool bReloadSvg, FString* OutError = nullptr);

	/** Click-to-place mode. */
	bool IsPlacing() const { return bPlacing; }
	void SetPlacing(bool bInPlacing);

	/** True when the screen position is over this character's editor viewport (and nothing covers it there). */
	bool IsOverViewport(const FVector2D& ScreenPosition) const;

	/** Places the selected tattoo where the screen position hits the skin. Returns false when it misses. */
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition, bool bNewPlacement);

	/** Scales and turns the selected tattoo (mouse wheel while placing). */
	void AdjustSelected(float ScaleFactor, float RotationDeltaDegrees);

	/** Bakes the tattoos into the skin. */
	bool Apply(FString& OutReport);

	/** Returns the tattoo list to the last applied state. */
	void DiscardChanges();

	/** Removes every tattoo from the list and the skin. */
	bool RemoveAll(FString& OutReport);

	bool HasUnappliedChanges() const;
	bool HasAppliedTattoos() const;

	/** True when the viewport shows the Topology or Clay material, where skin textures (and tattoos) are not visible. */
	bool NeedsSkinPreview() const;
	void SwitchToSkinPreview();

	/** True when the skin tone or texture choice changed after the tattoos were last applied. */
	bool SkinChangedSinceApply() const;

	/** Problems with the live preview, for the panel. Empty when the preview works. */
	const FString& GetPreviewWarning() const { return PreviewWarning; }

	/** Fired when the tattoo list or selection changes, so the panel can refresh. */
	FSimpleMulticastDelegate OnLayersChanged;

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FMetaHumanTattooSession"); }

private:
	struct FPreviewSlot
	{
		UE::MetaHumanTattoo::FSkinTextureSlot Slot;
		FImage Base;
		FImage Work;
		TObjectPtr<UTexture2D> Texture;
		bool bValid = false;
	};

	struct FBinding
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		FName Parameter;
		TWeakObjectPtr<UTexture> Original;
		int32 SlotIndex = INDEX_NONE;
	};

	bool Tick(float DeltaTime);
	UMetaHumanTattooSet* GetOrCreateSet();
	bool EnsureMeshes(FString& OutError);
	bool GetPreviewComponents(const USkeletalMeshComponent*& OutFace, const USkeletalMeshComponent*& OutBody) const;
	bool FindViewport(const FVector2D& ScreenPosition, FEditorViewportClient*& OutClient, FVector2D& OutPixel) const;
	bool RaycastPreview(const FVector& Origin, const FVector& Direction, const FVector& CameraUp, FVector3f& OutLocation, FVector3f& OutNormal, FVector3f& OutUp);

	void RequestPreview();
	void LoadPreviewSlots();
	void RebuildPreview();
	void BindPreview();
	void RestoreBindings();
	void InvalidatePreviewBases();

	TWeakObjectPtr<UMetaHumanCharacter> Character;
	TObjectPtr<UMetaHumanTattooSet> Set;
	int32 SelectedIndex = INDEX_NONE;
	bool bPlacing = false;

	TOptional<FMetaHumanTattooCharacterMeshes> Meshes;
	bool bMeshesDirty = true;

	TArray<FPreviewSlot> PreviewSlots;
	bool bPreviewSlotsLoaded = false;
	TArray<FBinding> Bindings;
	bool bPreviewDirty = false;
	double LastPreviewSeconds = 0.0;
	FString PreviewWarning;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle FaceStateHandle;
	FDelegateHandle BodyStateHandle;
	FDelegateHandle PreviewMaterialHandle;
	bool bInitialized = false;
};
