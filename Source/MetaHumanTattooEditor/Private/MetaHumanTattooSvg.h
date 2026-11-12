#pragma once

#include "CoreMinimal.h"
#include "MetaHumanTattooTypes.h"

/** A rasterized tattoo design: premultiplied linear color, row 0 at the top of the SVG. */
struct FMetaHumanTattooRaster
{
	int32 Width = 0;
	int32 Height = 0;

	/** Premultiplied linear color (RGB already multiplied by A). */
	TArray<FLinearColor> Pixels;

	bool IsValid() const
	{
		return Width > 0 && Height > 0 && Pixels.Num() == Width * Height;
	}

	/** Bilinear sample at S (left to right) and T (top to bottom), both 0..1. Outside the design returns transparent. */
	FLinearColor Sample(float S, float T) const;
};

namespace UE::MetaHumanTattoo
{
	/** Reads the canvas size of an SVG. OutAspect = height / width. */
	bool ReadSvgAspect(const FString& SvgText, float& OutAspect, FString& OutError);

	/**
	 * Rasterizes an SVG so its longer side is MaxDimension pixels.
	 * SingleInk replaces every color with InkColor (linear). SpreadPixels blurs the result (ink spread) by about that many pixels.
	 */
	bool RasterizeSvg(const FString& SvgText, int32 MaxDimension, EMetaHumanTattooInkMode InkMode, const FLinearColor& InkColor,
		float SpreadPixels, FMetaHumanTattooRaster& OutRaster, FString& OutError);

	/** Cached rasterization keyed on every input. Returns null and fills OutError on failure. */
	TSharedPtr<const FMetaHumanTattooRaster> GetCachedRaster(const FString& SvgText, int32 MaxDimension, EMetaHumanTattooInkMode InkMode,
		const FLinearColor& InkColor, float SpreadPixels, FString& OutError);

	/** Drops every cached rasterization. */
	void ClearRasterCache();
}
