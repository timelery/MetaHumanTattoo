#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MetaHumanTattooTypes.h"

#include "MetaHumanTattooLibrary.generated.h"

class UMetaHumanCharacter;
class UMetaHumanTattooSet;
class UTexture2D;

/**
 * Script access to MetaHuman tattoos (Python: unreal.MetaHumanTattooLibrary).
 *
 * Every function works on a MetaHuman Character asset whether or not its editor is open. When it is not open, the
 * character is registered with the MetaHuman Character editor subsystem for the duration of the call.
 * Placement functions work in the character's rest pose (A-pose), in centimetres, in mesh space.
 */
UCLASS()
class METAHUMANTATTOOEDITOR_API UMetaHumanTattooLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns the character's tattoo set (<Character folder>/Tattoos/TS_<Character>), creating it if asked. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static UMetaHumanTattooSet* GetTattooSet(UMetaHumanCharacter* Character, bool bCreateIfMissing);

	/** Adds a tattoo from an SVG file. Returns its index in the set, or -1 (see the log) on failure. The tattoo still has to be placed. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static int32 AddTattooFromSvgFile(UMetaHumanCharacter* Character, const FString& SvgFile, float WidthCm = 10.0f);

	/** Reads one tattoo. Returns false if the index is out of range. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool GetTattoo(UMetaHumanCharacter* Character, int32 Index, FMetaHumanTattooLayer& OutTattoo);

	/** Replaces one tattoo (all fields, placement included). Returns false if the index is out of range. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool SetTattoo(UMetaHumanCharacter* Character, int32 Index, const FMetaHumanTattooLayer& Tattoo);

	/** Removes one tattoo from the list. Call ApplyTattoos afterwards to take it off the skin. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool RemoveTattoo(UMetaHumanCharacter* Character, int32 Index);

	/**
	 * Places a tattoo where a ray first hits the skin, in rest-pose mesh space (cm).
	 * UpHint gives the direction of the top of the design; it is projected onto the skin.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool PlaceTattooAlongRay(UMetaHumanCharacter* Character, int32 Index, FVector RayOrigin, FVector RayDirection, FVector UpHint);

	/** Rest-pose (A-pose) location of a body skeleton bone, in mesh space (cm). Useful for aiming placement rays. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool GetRestBoneLocation(UMetaHumanCharacter* Character, FName BoneName, FVector& OutLocation);

	/**
	 * Bakes every visible, placed tattoo into the character's skin textures and saves the result.
	 * Returns a report of what was baked, skipped or restored. Python: report, ok = apply_tattoos(character)
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static FString ApplyTattoos(UMetaHumanCharacter* Character, bool& bSuccess);

	/**
	 * Removes every tattoo from the list and from the skin, restoring the textures the character had before.
	 * Python: report, ok = remove_all_tattoos(character)
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static FString RemoveAllTattoos(UMetaHumanCharacter* Character, bool& bSuccess);

	/** Diagnostic report: skin sections and UV tiles of the edit meshes, the skin material parameters, and the open editor. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static FString ProbeCharacter(UMetaHumanCharacter* Character);

	/** Opens the character in the MetaHuman Character editor (if needed) and shows its Tattoos tab. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool OpenTattooPanel(UMetaHumanCharacter* Character);

	/**
	 * Automation: acts as a left click with Place on Skin active at a point of the open editor's viewport
	 * (0..1 across, 0..1 down) for the selected tattoo. Requires the Tattoos panel to be open. Returns a status line.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static FString SimulateViewportClick(UMetaHumanCharacter* Character, FVector2D NormalizedPosition, bool& bOutHit);

	/** Status of the open Tattoos panel's live preview, or a note that the panel is not open. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static FString GetTattooPanelStatus(UMetaHumanCharacter* Character);

	/** Returns the tattoo list to its last applied state (the panel's Discard Changes). */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static bool DiscardTattooChanges(UMetaHumanCharacter* Character);

	/**
	 * Diagnostic: compares two textures of the same size (source pixels) and reports how many texels differ and where.
	 * With a PNG path, writes the differing region side by side (A left, B right).
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Tattoo")
	static FString CompareTextures(UTexture2D* A, UTexture2D* B, const FString& PngPath);
};
