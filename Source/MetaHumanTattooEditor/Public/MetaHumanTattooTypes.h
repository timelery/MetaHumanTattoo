#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "MetaHumanTattooTypes.generated.h"

/** How a tattoo's colors are chosen. */
UENUM(BlueprintType)
enum class EMetaHumanTattooInkMode : uint8
{
	SingleInk UMETA(DisplayName = "Single Ink Color", ToolTip = "Every shape in the SVG is drawn in Ink Color. The SVG supplies only the shapes and their transparency. Use for black-work and other one-color tattoos."),
	SvgColors UMETA(DisplayName = "Colors From SVG", ToolTip = "Every shape keeps the fill and stroke colors written in the SVG file. Use for colored tattoos. Ink Color is ignored.")
};

/** How the ink is combined with the skin underneath. */
UENUM(BlueprintType)
enum class EMetaHumanTattooBlendMode : uint8
{
	HealedInk UMETA(DisplayName = "Healed Ink (Multiply)", ToolTip = "Skin color x ink color, the way healed ink sits under the skin: pores, freckles, blotches and skin tone stay visible through it. Recommended for black and colored ink."),
	Opaque UMETA(DisplayName = "Opaque (Paint Over)", ToolTip = "The ink replaces the skin color in proportion to Opacity. Use for white or very light ink, which Healed Ink cannot show on darker skin.")
};

/** Which MetaHuman meshes a tattoo is projected onto. */
UENUM(BlueprintType)
enum class EMetaHumanTattooTarget : uint8
{
	HeadAndBody UMETA(DisplayName = "Head and Body", ToolTip = "Project onto the head mesh and the body mesh, so a tattoo that crosses the neck seam continues across it. Recommended."),
	BodyOnly UMETA(DisplayName = "Body Only", ToolTip = "Project onto the body mesh only (from the neck seam down)."),
	HeadOnly UMETA(DisplayName = "Head Only", ToolTip = "Project onto the head mesh only (face, scalp, ears and the neck down to the seam).")
};

/** Where a tattoo sits on the character. Filled in by clicking on the skin, or by the placement functions in UMetaHumanTattooLibrary. */
USTRUCT(BlueprintType)
struct FMetaHumanTattooPlacement
{
	GENERATED_BODY()

	/** True once the tattoo has been placed on the skin. An unplaced tattoo is kept in the list but is not drawn or baked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "True once the tattoo has been placed on the skin (click on the character with Place on Skin active, or call a placement function). An unplaced tattoo is kept in the list but is not drawn or baked."))
	bool bPlaced = false;

	/** Centre of the tattoo on the skin, in centimetres, in the character's rest-pose (A-pose) mesh space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "Centre of the tattoo on the skin, in centimetres, in the character's rest-pose (A-pose) mesh space. Set by clicking on the character; drag with the left mouse button held to slide it over the skin."))
	FVector Location = FVector::ZeroVector;

	/** Outward skin direction at the centre (unit vector). The tattoo is projected along this direction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "Outward skin direction at the tattoo centre (unit vector). The SVG is projected onto the skin along this direction, like a stencil held against the skin."))
	FVector Normal = FVector(0.0, 1.0, 0.0);

	/** Direction of the top of the SVG along the skin, before Rotation is applied (unit vector). */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "Direction of the top of the SVG along the skin before Rotation is applied (unit vector). Taken from the viewport camera's up direction when the tattoo is first placed, so it starts upright as you see it."))
	FVector Up = FVector(0.0, 0.0, 1.0);
};

/** One tattoo: an SVG design and how and where it is put on the skin. */
USTRUCT(BlueprintType)
struct FMetaHumanTattooLayer
{
	GENERATED_BODY()

	/** Name shown in the tattoo list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo", meta = (ToolTip = "Name shown in the tattoo list. Defaults to the SVG file name."))
	FString Name = TEXT("Tattoo");

	/** Draw and bake this tattoo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo", meta = (ToolTip = "Draw and bake this tattoo. Untick to hide it without deleting it; Apply to Skin then removes it from the skin textures until you tick it again."))
	bool bVisible = true;

	/** The SVG file the design was loaded from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (FilePathFilter = "Scalable Vector Graphics (*.svg)|*.svg", ToolTip = "SVG file holding the design. Choosing a file copies its contents into the tattoo, so the tattoo keeps working if the file later moves or changes. Pick the file again to reload it. Filled shapes, strokes, even-odd and non-zero fills, groups and transforms are supported; text, images, gradients and filters are not."))
	FFilePath SvgFile;

	/** Copy of the SVG text. Filled from SvgFile. */
	UPROPERTY(BlueprintReadWrite, Category = "Design")
	FString SvgData;

	/** Height divided by width of the SVG canvas. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Design", meta = (ToolTip = "Height divided by width of the SVG canvas, read from the file. The tattoo's height on the skin is Width x this value."))
	float SvgAspect = 1.0f;

	/** Single ink color or the colors from the SVG. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ToolTip = "Single Ink Color: every shape is drawn in Ink Color (black-work, one-color tattoos). Colors From SVG: every shape keeps its own fill and stroke color from the file (colored tattoos)."))
	EMetaHumanTattooInkMode InkMode = EMetaHumanTattooInkMode::SingleInk;

	/** Ink color for Single Ink Color mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (EditCondition = "InkMode == EMetaHumanTattooInkMode::SingleInk", HideAlphaChannel, ToolTip = "Ink color used in Single Ink Color mode (linear color). Default is the blue-black of healed black ink. Pure black (0,0,0) looks painted on; real black ink reads as a very dark blue-green under the skin. Ignored in Colors From SVG mode."))
	FLinearColor InkColor = FLinearColor(0.012f, 0.016f, 0.020f, 1.0f);

	/** How strongly the ink shows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", ToolTip = "How strongly the ink shows, 0 to 1. 1 = fresh, saturated ink. 0.75 to 0.9 = typical healed tattoo. 0.3 to 0.5 = old, faded tattoo. 0 hides it. Default 0.9."))
	float Opacity = 0.9f;

	/** How the ink combines with the skin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ToolTip = "Healed Ink (Multiply): skin x ink, so skin detail and tone show through, as with real tattoos (recommended). Opaque (Paint Over): the ink replaces the skin color, needed for white or very light ink."))
	EMetaHumanTattooBlendMode BlendMode = EMetaHumanTattooBlendMode::HealedInk;

	/** Softening of the ink edges, in millimetres on the skin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0", ClampMax = "5", UIMin = "0", UIMax = "2", Units = "mm", ToolTip = "Blurs the ink edges by this distance on the skin, in millimetres, like ink spreading in the skin as a tattoo heals. 0 = razor sharp (fresh linework or a decal look). 0.2 to 0.5 = healed tattoo (default 0.3). 1 to 2 = old, blown-out tattoo. Range 0 to 5."))
	float InkSpreadMm = 0.3f;

	/** Width of the tattoo on the skin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size and Orientation", meta = (ClampMin = "0.5", ClampMax = "200", UIMin = "1", UIMax = "60", Units = "cm", ToolTip = "Width of the tattoo on the skin, in centimetres; the height follows the SVG's proportions. Ctrl + mouse wheel over the viewport scales it while Place on Skin is active. Typical: 3 to 6 cm (wrist, finger), 8 to 15 cm (forearm, upper arm), 20 to 40 cm (chest, back). Range 0.5 to 200, default 10."))
	float WidthCm = 10.0f;

	/** Rotation around the skin direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size and Orientation", meta = (ClampMin = "-360", ClampMax = "360", UIMin = "-180", UIMax = "180", Units = "deg", ToolTip = "Turns the tattoo on the skin, in degrees, clockwise as seen when looking at the skin. 0 keeps the top of the SVG pointing the way the camera's up pointed when the tattoo was placed. Shift + mouse wheel over the viewport turns it in 5 degree steps while Place on Skin is active. Range -360 to 360, default 0."))
	float RotationDegrees = 0.0f;

	/** Mirror the design left to right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Size and Orientation", meta = (ToolTip = "Mirrors the design left to right, for example to make a matching tattoo on the other arm. Off by default."))
	bool bMirror = false;

	/** Which meshes receive the tattoo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection", meta = (ToolTip = "Head and Body (recommended) projects onto both meshes so a tattoo can cross the neck seam. Body Only or Head Only keeps it on one mesh, for example a chest tattoo whose top edge should not reach the jaw."))
	EMetaHumanTattooTarget Target = EMetaHumanTattooTarget::HeadAndBody;

	/** How far in front of and behind the centre the stencil reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection", meta = (ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "30", Units = "cm", ToolTip = "How far the projection reaches in front of and behind the tattoo centre, in centimetres, measured along the skin direction. 0 = automatic: half the tattoo's larger side, at least 2 cm (right for most placements). Lower it if the tattoo also lands on a nearby surface, such as the torso under an arm tattoo; raise it if a tattoo on a strongly curved surface gets cut off. Range 0 to 100."))
	float ProjectionDepthCm = 0.0f;

	/** Steepest skin angle that still receives ink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projection", meta = (ClampMin = "10", ClampMax = "89", UIMin = "30", UIMax = "89", Units = "deg", ToolTip = "Skin that turns away from the projection direction by more than this many degrees gets no ink; the ink fades out over the last 10 degrees. Stops the design smearing round the sides of an arm or onto the far side of the body. 60 to 75 suits most placements (default 70). Raise it to wrap further round an arm; lower it if the edges look stretched. Range 10 to 89."))
	float MaxSurfaceAngleDegrees = 70.0f;

	/** Where the tattoo sits. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "Where the tattoo sits on the character. Set it by turning on Place on Skin and clicking (or dragging) on the character in the viewport."))
	FMetaHumanTattooPlacement Placement;

	/** Height of the tattoo on the skin, in centimetres. */
	float GetHeightCm() const
	{
		return WidthCm * FMath::Max(SvgAspect, 0.01f);
	}
};
