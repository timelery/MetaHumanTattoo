#include "MetaHumanTattooSvg.h"

#include "Async/ParallelFor.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"

THIRD_PARTY_INCLUDES_START
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"
#undef NANOSVG_IMPLEMENTATION
#undef NANOSVGRAST_IMPLEMENTATION
THIRD_PARTY_INCLUDES_END

FLinearColor FMetaHumanTattooRaster::Sample(float S, float T) const
{
	if (!IsValid() || S < 0.0f || S > 1.0f || T < 0.0f || T > 1.0f)
	{
		return FLinearColor::Transparent;
	}

	const float X = S * Width - 0.5f;
	const float Y = T * Height - 0.5f;
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const float FX = X - X0;
	const float FY = Y - Y0;

	auto Fetch = [this](int32 PX, int32 PY) -> FLinearColor
	{
		if (PX < 0 || PY < 0 || PX >= Width || PY >= Height)
		{
			return FLinearColor::Transparent;
		}
		return Pixels[PY * Width + PX];
	};

	const FLinearColor C00 = Fetch(X0, Y0);
	const FLinearColor C10 = Fetch(X0 + 1, Y0);
	const FLinearColor C01 = Fetch(X0, Y0 + 1);
	const FLinearColor C11 = Fetch(X0 + 1, Y0 + 1);
	return FMath::Lerp(FMath::Lerp(C00, C10, FX), FMath::Lerp(C01, C11, FX), FY);
}

namespace UE::MetaHumanTattoo
{
	namespace
	{
		/** nanosvg parses in place, so it needs a writable, zero-terminated UTF-8 copy. */
		TArray<char> MakeParseBuffer(const FString& SvgText)
		{
			const FTCHARToUTF8 Utf8(*SvgText);
			TArray<char> Buffer;
			Buffer.Reserve(Utf8.Length() + 1);
			Buffer.Append(Utf8.Get(), Utf8.Length());
			Buffer.Add('\0');
			return Buffer;
		}

		/** Separable Gaussian blur of a premultiplied raster; Sigma is half the spread. */
		void BlurRaster(FMetaHumanTattooRaster& Raster, float SpreadPixels)
		{
			const float Sigma = FMath::Max(0.25f, SpreadPixels * 0.5f);
			const int32 Radius = FMath::Clamp(FMath::CeilToInt(Sigma * 2.5f), 1, 64);

			TArray<float> Kernel;
			Kernel.SetNumUninitialized(Radius * 2 + 1);
			float Sum = 0.0f;
			for (int32 Index = -Radius; Index <= Radius; ++Index)
			{
				const float Weight = FMath::Exp(-(Index * Index) / (2.0f * Sigma * Sigma));
				Kernel[Index + Radius] = Weight;
				Sum += Weight;
			}
			for (float& Weight : Kernel)
			{
				Weight /= Sum;
			}

			const int32 W = Raster.Width;
			const int32 H = Raster.Height;
			TArray<FLinearColor> Temp;
			Temp.SetNumZeroed(W * H);

			ParallelFor(H, [&](int32 Y)
			{
				const FLinearColor* Row = Raster.Pixels.GetData() + Y * W;
				FLinearColor* Out = Temp.GetData() + Y * W;
				for (int32 X = 0; X < W; ++X)
				{
					FLinearColor Acc(0, 0, 0, 0);
					for (int32 K = -Radius; K <= Radius; ++K)
					{
						const int32 SX = X + K;
						if (SX >= 0 && SX < W)
						{
							Acc += Row[SX] * Kernel[K + Radius];
						}
					}
					Out[X] = Acc;
				}
			});

			ParallelFor(W, [&](int32 X)
			{
				for (int32 Y = 0; Y < H; ++Y)
				{
					FLinearColor Acc(0, 0, 0, 0);
					for (int32 K = -Radius; K <= Radius; ++K)
					{
						const int32 SY = Y + K;
						if (SY >= 0 && SY < H)
						{
							Acc += Temp[SY * W + X] * Kernel[K + Radius];
						}
					}
					Raster.Pixels[Y * W + X] = Acc;
				}
			});
		}

		struct FRasterCacheEntry
		{
			uint32 Key = 0;
			FString SvgText;
			int32 MaxDimension = 0;
			EMetaHumanTattooInkMode InkMode = EMetaHumanTattooInkMode::SingleInk;
			FLinearColor InkColor;
			float SpreadPixels = 0.0f;
			TSharedPtr<const FMetaHumanTattooRaster> Raster;
		};

		FCriticalSection GRasterCacheLock;
		TArray<FRasterCacheEntry> GRasterCache;
		constexpr int32 MaxCachedRasters = 6;
	}

	bool ReadSvgAspect(const FString& SvgText, float& OutAspect, FString& OutError)
	{
		TArray<char> Buffer = MakeParseBuffer(SvgText);
		NSVGimage* Image = nsvgParse(Buffer.GetData(), "px", 96.0f);
		if (!Image)
		{
			OutError = TEXT("The SVG could not be parsed.");
			return false;
		}
		ON_SCOPE_EXIT { nsvgDelete(Image); };

		if (Image->width <= 0.0f || Image->height <= 0.0f)
		{
			OutError = TEXT("The SVG has no width and height (give the <svg> element width/height or a viewBox).");
			return false;
		}
		if (!Image->shapes)
		{
			OutError = TEXT("The SVG has no drawable shapes (only paths, basic shapes, fills and strokes are supported).");
			return false;
		}

		OutAspect = Image->height / Image->width;
		return true;
	}

	bool RasterizeSvg(const FString& SvgText, int32 MaxDimension, EMetaHumanTattooInkMode InkMode, const FLinearColor& InkColor,
		float SpreadPixels, FMetaHumanTattooRaster& OutRaster, FString& OutError)
	{
		OutRaster = FMetaHumanTattooRaster();
		if (SvgText.IsEmpty())
		{
			OutError = TEXT("The tattoo has no SVG design.");
			return false;
		}

		TArray<char> Buffer = MakeParseBuffer(SvgText);
		NSVGimage* Image = nsvgParse(Buffer.GetData(), "px", 96.0f);
		if (!Image)
		{
			OutError = TEXT("The SVG could not be parsed.");
			return false;
		}
		ON_SCOPE_EXIT { nsvgDelete(Image); };

		if (Image->width <= 0.0f || Image->height <= 0.0f || !Image->shapes)
		{
			OutError = TEXT("The SVG has no canvas size or no drawable shapes.");
			return false;
		}

		MaxDimension = FMath::Clamp(MaxDimension, 16, 8192);
		const float Scale = MaxDimension / FMath::Max(Image->width, Image->height);
		const int32 W = FMath::Max(1, FMath::CeilToInt(Image->width * Scale));
		const int32 H = FMath::Max(1, FMath::CeilToInt(Image->height * Scale));

		TArray<uint8> Bytes;
		Bytes.SetNumZeroed(W * H * 4);
		NSVGrasterizer* Rasterizer = nsvgCreateRasterizer();
		if (!Rasterizer)
		{
			OutError = TEXT("Could not create the SVG rasterizer.");
			return false;
		}
		nsvgRasterize(Rasterizer, Image, 0.0f, 0.0f, Scale, Bytes.GetData(), W, H, W * 4);
		nsvgDeleteRasterizer(Rasterizer);

		OutRaster.Width = W;
		OutRaster.Height = H;
		OutRaster.Pixels.SetNumUninitialized(W * H);
		const FLinearColor Ink(InkColor.R, InkColor.G, InkColor.B, 1.0f);

		ParallelFor(H, [&](int32 Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 Index = Y * W + X;
				const uint8* Pixel = Bytes.GetData() + Index * 4;
#if defined(NSVG_USE_BGRA) && NSVG_USE_BGRA
				const uint8 B = Pixel[0], G = Pixel[1], R = Pixel[2], A = Pixel[3];
#else
				const uint8 R = Pixel[0], G = Pixel[1], B = Pixel[2], A = Pixel[3];
#endif
				const float Alpha = A / 255.0f;
				const FLinearColor Color = InkMode == EMetaHumanTattooInkMode::SingleInk ? Ink : FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
				OutRaster.Pixels[Index] = FLinearColor(Color.R * Alpha, Color.G * Alpha, Color.B * Alpha, Alpha);
			}
		});

		if (SpreadPixels > 0.25f)
		{
			BlurRaster(OutRaster, SpreadPixels);
		}
		return true;
	}

	TSharedPtr<const FMetaHumanTattooRaster> GetCachedRaster(const FString& SvgText, int32 MaxDimension, EMetaHumanTattooInkMode InkMode,
		const FLinearColor& InkColor, float SpreadPixels, FString& OutError)
	{
		// Quantize the float inputs so slider jitter does not defeat the cache.
		const float QuantizedSpread = FMath::RoundToFloat(SpreadPixels * 4.0f) / 4.0f;
		const FLinearColor QuantizedInk = InkMode == EMetaHumanTattooInkMode::SingleInk
			? FLinearColor(FMath::RoundToFloat(InkColor.R * 4096.0f) / 4096.0f, FMath::RoundToFloat(InkColor.G * 4096.0f) / 4096.0f, FMath::RoundToFloat(InkColor.B * 4096.0f) / 4096.0f, 1.0f)
			: FLinearColor::White;

		uint32 Key = GetTypeHash(SvgText);
		Key = HashCombine(Key, GetTypeHash(MaxDimension));
		Key = HashCombine(Key, GetTypeHash(static_cast<uint8>(InkMode)));
		Key = HashCombine(Key, GetTypeHash(QuantizedInk));
		Key = HashCombine(Key, GetTypeHash(QuantizedSpread));

		{
			FScopeLock Lock(&GRasterCacheLock);
			for (int32 Index = 0; Index < GRasterCache.Num(); ++Index)
			{
				const FRasterCacheEntry& Entry = GRasterCache[Index];
				if (Entry.Key == Key && Entry.MaxDimension == MaxDimension && Entry.InkMode == InkMode && Entry.InkColor == QuantizedInk
					&& Entry.SpreadPixels == QuantizedSpread && Entry.SvgText.Equals(SvgText, ESearchCase::CaseSensitive))
				{
					FRasterCacheEntry Found = Entry;
					GRasterCache.RemoveAt(Index);
					GRasterCache.Add(Found);
					return Found.Raster;
				}
			}
		}

		TSharedPtr<FMetaHumanTattooRaster> Raster = MakeShared<FMetaHumanTattooRaster>();
		if (!RasterizeSvg(SvgText, MaxDimension, InkMode, QuantizedInk, QuantizedSpread, *Raster, OutError))
		{
			return nullptr;
		}

		FScopeLock Lock(&GRasterCacheLock);
		FRasterCacheEntry& Entry = GRasterCache.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.SvgText = SvgText;
		Entry.MaxDimension = MaxDimension;
		Entry.InkMode = InkMode;
		Entry.InkColor = QuantizedInk;
		Entry.SpreadPixels = QuantizedSpread;
		Entry.Raster = Raster;
		while (GRasterCache.Num() > MaxCachedRasters)
		{
			GRasterCache.RemoveAt(0);
		}
		return Raster;
	}

	void ClearRasterCache()
	{
		FScopeLock Lock(&GRasterCacheLock);
		GRasterCache.Empty();
	}
}
