#pragma once

#include "CoreMinimal.h"
#include "ImageCore.h"
#include "MetaHumanTypes.h"
#include "MetaHumanTattooTypes.h"
#include "MetaHumanTattooSvg.h"
#include "UObject/SoftObjectPtr.h"

struct FMetaHumanTattooMesh;
class UMetaHumanCharacter;
class UMetaHumanTattooSet;
class UTexture2D;

namespace UE::MetaHumanTattoo
{
	/** A tattoo ready to draw: its rasterized design and its projection frame in rest-pose mesh space. */
	struct FPreparedTattoo
	{
		TSharedPtr<const FMetaHumanTattooRaster> Raster;
		FVector3f Center = FVector3f::ZeroVector;
		FVector3f Normal = FVector3f(0, 1, 0);
		FVector3f Right = FVector3f(1, 0, 0);
		FVector3f Up = FVector3f(0, 0, 1);
		float Width = 10.0f;
		float Height = 10.0f;
		float Depth = 5.0f;
		float CosMaxAngle = 0.34f;
		float CosFadeStart = 0.5f;
		float Opacity = 1.0f;
		EMetaHumanTattooBlendMode BlendMode = EMetaHumanTattooBlendMode::HealedInk;
		bool bOnHead = true;
		bool bOnBody = true;
	};

	/** Raster size for a tattoo: about 10 texels per mm for baking, 4 per mm for the live preview. */
	int32 GetRasterSize(const FMetaHumanTattooLayer& Layer, bool bPreview);

	/** Rasterizes the design (cached) and builds the projection frame. Fails for hidden or unplaced tattoos. */
	bool PrepareTattoo(const FMetaHumanTattooLayer& Layer, bool bPreview, FPreparedTattoo& OutTattoo, FString& OutError);

	/** Projection frame of a placement (unit normal, up and right after rotation and mirroring). */
	void ComputeFrame(const FMetaHumanTattooLayer& Layer, FVector3f& OutNormal, FVector3f& OutRight, FVector3f& OutUp);

	/**
	 * A skin texture that tattoos are baked into, and which mesh's triangles draw into it.
	 * bHeadMesh also tells which skin material samples it: the head material samples the face textures and the Chest
	 * textures (the lower neck), the body material samples the Body textures.
	 */
	struct FSkinTextureSlot
	{
		bool bFaceTexture = true;
		EFaceTextureType FaceType = EFaceTextureType::Basecolor;
		EBodyTextureType BodyType = EBodyTextureType::Body_Basecolor;
		bool bHeadMesh = true;
		int32 UVChannel = 0;

		/** UV tile the texture covers; (INDEX_NONE, INDEX_NONE) = the mesh's dominant skin tile. */
		FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);

		FString GetName() const;
	};

	/** The base color slots tattoos go into. The preview uses only the always-visible ones. */
	TArray<FSkinTextureSlot> GetSkinTextureSlots(bool bPreviewOnly);

	/**
	 * Draws the tattoos into an 8-bit BGRA image of the slot's texture. Every texel is drawn once per tattoo; with bFillGutters,
	 * changed texels are also spread into the empty gutter texels around UV islands so seams do not show a light line.
	 * Returns the number of texels changed.
	 */
	int64 CompositeTattoos(const FMetaHumanTattooMesh& Mesh, const FSkinTextureSlot& Slot, TConstArrayView<FPreparedTattoo> Tattoos, FImage& Image, bool bFillGutters);

	/** The skin texture feeding a slot before any tattoo. */
	struct FSlotBase
	{
		/** The override texture the slot used before tattoos; null when the slot uses the texture MetaHuman Creator generates. */
		TSoftObjectPtr<UTexture2D> BaseRef;

		/** Texture whose compression and texture group the baked texture copies. */
		TWeakObjectPtr<UTexture2D> StyleTexture;

		/** The pixels, converted to 8-bit BGRA. */
		FImage Image;

		/**
		 * True for MetaHuman Creator's generated body and chest skin textures: they are stored linear and decoded by the skin
		 * material with the character's skin-tone bias and gain, so they hold tone data rather than color and ink cannot be
		 * drawn into them.
		 */
		bool bToneEncoded = false;
	};

	/** Reads the texture a slot shows without tattoos (never one of the set's own baked textures). */
	bool ReadSlotBase(UMetaHumanCharacter* Character, const UMetaHumanTattooSet* Set, const FSkinTextureSlot& Slot, FSlotBase& OutBase, FString& OutError);

	/** <Character folder>/Tattoos */
	FString GetTattooFolder(const UMetaHumanCharacter* Character);

	/** Long package name of the character's tattoo set. */
	FString GetTattooSetPackageName(const UMetaHumanCharacter* Character);

	/** Finds the character's tattoo set, loading it if needed; creates it when bCreate is set. */
	UMetaHumanTattooSet* FindOrCreateTattooSet(UMetaHumanCharacter* Character, bool bCreate);

	/**
	 * Bakes every visible, placed tattoo into the character's skin textures, points the character's texture overrides at the
	 * results, commits the skin settings and (with bSave) saves the textures, the set and the character.
	 * Slots no tattoo reaches any more get their previous texture back. The character must be registered for editing.
	 */
	bool ApplyTattoos(UMetaHumanCharacter* Character, UMetaHumanTattooSet* Set, bool bSave, FString& OutReport);

	/** Loads SVG text from a file and reads its proportions into the layer (name too, when it is still the default). */
	bool LoadSvgIntoLayer(const FString& FilePath, FMetaHumanTattooLayer& Layer, FString& OutError);
}
