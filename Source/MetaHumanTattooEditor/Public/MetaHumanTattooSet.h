#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "MetaHumanTypes.h"
#include "MetaHumanCharacterSkin.h"
#include "MetaHumanTattooTypes.h"

#include "MetaHumanTattooSet.generated.h"

class UMetaHumanCharacter;
class UTexture2D;

/**
 * The tattoos of one MetaHuman Character, stored next to it as <Character folder>/Tattoos/TS_<Character>.
 *
 * Holds the tattoo list (Layers), the list as it was last baked (AppliedLayers), and the bookkeeping that makes baking
 * repeatable and reversible: which skin texture each baked texture was made from (the bases), which textures were
 * written, and any texture override entries that were set aside when tattoos switched texture overrides on.
 */
UCLASS(BlueprintType)
class METAHUMANTATTOOEDITOR_API UMetaHumanTattooSet : public UObject
{
	GENERATED_BODY()

public:
	/** The character these tattoos belong to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tattoos", meta = (ToolTip = "The MetaHuman Character these tattoos belong to."))
	TSoftObjectPtr<UMetaHumanCharacter> Character;

	/** The tattoo list being edited, in drawing order (later tattoos are drawn over earlier ones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoos", meta = (ToolTip = "The tattoo list, in drawing order: a later tattoo is drawn over an earlier one where they overlap. Edit it in the MetaHuman Character editor's Tattoos panel; Apply to Skin bakes it."))
	TArray<FMetaHumanTattooLayer> Layers;

	/** The tattoo list as it was last baked into the skin. Discard Changes returns to it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Applied", meta = (ToolTip = "The tattoo list as it was last baked into the skin textures. Discard Changes in the Tattoos panel returns Layers to this."))
	TArray<FMetaHumanTattooLayer> AppliedLayers;

	/** Face texture each baked face texture was made from. A null entry means the texture MetaHuman Creator generates. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "For each baked face texture slot, the texture it was made from: the override texture that was in that slot before tattoos were baked, or none when the slot used the texture MetaHuman Creator generates. Removing tattoos puts these back."))
	TMap<EFaceTextureType, TSoftObjectPtr<UTexture2D>> FaceBases;

	/** Body texture each baked body texture was made from. A null entry means the texture MetaHuman Creator generates. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "For each baked body texture slot, the texture it was made from: the override texture that was in that slot before tattoos were baked, or none when the slot used the texture MetaHuman Creator generates. Removing tattoos puts these back."))
	TMap<EBodyTextureType, TSoftObjectPtr<UTexture2D>> BodyBases;

	/** Face textures written by Apply to Skin. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "Face textures with the tattoos baked in, written by Apply to Skin into the Tattoos folder and set as the character's face texture overrides."))
	TMap<EFaceTextureType, TSoftObjectPtr<UTexture2D>> FaceOutputs;

	/** Body textures written by Apply to Skin. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "Body textures with the tattoos baked in, written by Apply to Skin into the Tattoos folder and set as the character's body texture overrides."))
	TMap<EBodyTextureType, TSoftObjectPtr<UTexture2D>> BodyOutputs;

	/** True when baking tattoos switched the character's texture overrides on. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "True when baking switched the character's texture overrides on (they were off before). Removing every tattoo switches them off again."))
	bool bTattoosEnabledTextureOverrides = false;

	/** Override entries that were present but inactive (overrides off) before tattoos switched overrides on. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "Texture override entries that were stored on the character but inactive (texture overrides were off) when tattoos switched overrides on. They are set aside so they do not suddenly take effect, and put back when every tattoo is removed."))
	FMetaHumanCharacterSkinTextureSoftSet StashedInactiveOverrides;

	/** Skin settings that drove the generated textures when tattoos were last baked. */
	UPROPERTY(VisibleAnywhere, Category = "Applied", meta = (ToolTip = "The character's skin properties when tattoos were last baked. If the skin tone or texture choice changes afterwards, the panel asks you to apply again so the tattoos sit on the new skin."))
	FMetaHumanCharacterSkinProperties AppliedSkinProperties;

	UPROPERTY()
	bool bHasAppliedSkinProperties = false;

	/** True when Layers differs from AppliedLayers. */
	bool HasUnappliedChanges() const;

	/** True when any baked texture is currently in use. */
	bool HasAppliedTattoos() const
	{
		return !FaceOutputs.IsEmpty() || !BodyOutputs.IsEmpty();
	}
};
