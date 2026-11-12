#include "MetaHumanTattooBaker.h"

#include "MetaHumanTattooLog.h"
#include "MetaHumanTattooMesh.h"
#include "MetaHumanTattooSet.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UE::MetaHumanTattoo
{
	namespace
	{
		/** sRGB byte -> linear float. */
		struct FGammaTables
		{
			float SrgbToLinear[256];
			uint8 LinearToSrgb[4096];

			FGammaTables()
			{
				for (int32 Index = 0; Index < 256; ++Index)
				{
					const float C = Index / 255.0f;
					SrgbToLinear[Index] = C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
				}
				for (int32 Index = 0; Index < 4096; ++Index)
				{
					const float L = Index / 4095.0f;
					const float S = L <= 0.0031308f ? L * 12.92f : 1.055f * FMath::Pow(L, 1.0f / 2.4f) - 0.055f;
					LinearToSrgb[Index] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(S * 255.0f), 0, 255));
				}
			}
		};

		const FGammaTables& GetGammaTables()
		{
			static const FGammaTables Tables;
			return Tables;
		}

		FORCEINLINE uint8 EncodeChannel(float Linear, bool bSRGB)
		{
			const float Clamped = FMath::Clamp(Linear, 0.0f, 1.0f);
			if (bSRGB)
			{
				return GetGammaTables().LinearToSrgb[FMath::RoundToInt(Clamped * 4095.0f)];
			}
			return static_cast<uint8>(FMath::RoundToInt(Clamped * 255.0f));
		}

		FORCEINLINE float DecodeChannel(uint8 Value, bool bSRGB)
		{
			return bSRGB ? GetGammaTables().SrgbToLinear[Value] : Value / 255.0f;
		}

		/** Edge function rasterizer: calls Visit(X, Y, B0, B1, B2) for every texel centre inside the triangle. */
		template <typename FVisit>
		void RasterizeTriangle(const FVector2f& A, const FVector2f& B, const FVector2f& C, int32 Width, int32 Height, FVisit&& Visit)
		{
			const float Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (FMath::Abs(Area) < 1e-12f)
			{
				return;
			}
			const float InvArea = 1.0f / Area;

			const int32 MinX = FMath::Max(0, FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)));
			const int32 MaxX = FMath::Min(Width - 1, FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)));
			const int32 MinY = FMath::Max(0, FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)));
			const int32 MaxY = FMath::Min(Height - 1, FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)));
			constexpr float Epsilon = -1e-5f;

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const FVector2f P(X, Y);
					const float W0 = ((B.X - P.X) * (C.Y - P.Y) - (B.Y - P.Y) * (C.X - P.X)) * InvArea;
					const float W1 = ((C.X - P.X) * (A.Y - P.Y) - (C.Y - P.Y) * (A.X - P.X)) * InvArea;
					const float W2 = 1.0f - W0 - W1;
					if (W0 >= Epsilon && W1 >= Epsilon && W2 >= Epsilon)
					{
						Visit(X, Y, W0, W1, W2);
					}
				}
			}
		}

		/** Texel-space position of a UV inside a tile (texel centres at integers). */
		FORCEINLINE FVector2f ToTexel(const FVector2f& UV, const FIntPoint& Tile, int32 Width, int32 Height)
		{
			return FVector2f((UV.X - Tile.X) * Width - 0.5f, (UV.Y - Tile.Y) * Height - 0.5f);
		}

		FORCEINLINE bool IsTriangleInTile(const FVector2f& A, const FVector2f& B, const FVector2f& C, const FIntPoint& Tile)
		{
			const FVector2f Centroid = (A + B + C) / 3.0f;
			return FMath::FloorToInt(Centroid.X) == Tile.X && FMath::FloorToInt(Centroid.Y) == Tile.Y;
		}

		bool CopyBufferToImage(const FSharedBuffer& Buffer, const FImageInfo& Info, FImage& OutImage)
		{
			if (Buffer.IsNull() || Info.SizeX <= 0 || Info.SizeY <= 0)
			{
				return false;
			}
			if (static_cast<int64>(Buffer.GetSize()) != Info.GetImageSizeBytes())
			{
				return false;
			}
			OutImage.Init(Info);
			FMemory::Memcpy(OutImage.RawData.GetData(), Buffer.GetData(), Buffer.GetSize());
			return true;
		}

		bool ReadTextureSource(UTexture2D* Texture, FImage& OutImage)
		{
			if (!Texture || !Texture->Source.IsValid())
			{
				return false;
			}
			return Texture->Source.GetMipImage(OutImage, 0);
		}

		TSoftObjectPtr<UTexture2D> FindOverride(const FMetaHumanCharacterSkinTextureSoftSet& Set, const FSkinTextureSlot& Slot)
		{
			const TSoftObjectPtr<UTexture2D>* Found = Slot.bFaceTexture ? Set.Face.Find(Slot.FaceType) : Set.Body.Find(Slot.BodyType);
			return Found ? *Found : TSoftObjectPtr<UTexture2D>();
		}

		void SetOverride(FMetaHumanCharacterSkinTextureSoftSet& Set, const FSkinTextureSlot& Slot, const TSoftObjectPtr<UTexture2D>& Texture)
		{
			if (Slot.bFaceTexture)
			{
				if (Texture.IsNull())
				{
					Set.Face.Remove(Slot.FaceType);
				}
				else
				{
					Set.Face.Add(Slot.FaceType, Texture);
				}
			}
			else
			{
				if (Texture.IsNull())
				{
					Set.Body.Remove(Slot.BodyType);
				}
				else
				{
					Set.Body.Add(Slot.BodyType, Texture);
				}
			}
		}

		TSoftObjectPtr<UTexture2D> FindSlotEntry(const TMap<EFaceTextureType, TSoftObjectPtr<UTexture2D>>& Face, const TMap<EBodyTextureType, TSoftObjectPtr<UTexture2D>>& Body, const FSkinTextureSlot& Slot, bool& bOutFound)
		{
			const TSoftObjectPtr<UTexture2D>* Found = Slot.bFaceTexture ? Face.Find(Slot.FaceType) : Body.Find(Slot.BodyType);
			bOutFound = Found != nullptr;
			return Found ? *Found : TSoftObjectPtr<UTexture2D>();
		}

		UTexture2D* WriteTattooTexture(const UMetaHumanCharacter* Character, const FSkinTextureSlot& Slot, const FImage& Image, const UTexture2D* Style, FString& OutError)
		{
			const FString AssetName = FString::Printf(TEXT("T_%s_Tattoo_%s"), *Character->GetName(), *Slot.GetName());
			const FString PackageName = GetTattooFolder(Character) / AssetName;

			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				OutError = TEXT("Could not create package ") + PackageName;
				return nullptr;
			}
			Package->FullyLoad();

			UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
			const bool bCreated = Texture == nullptr;
			if (bCreated)
			{
				Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			}

			Texture->PreEditChange(nullptr);
			Texture->Source.Init(Image.SizeX, Image.SizeY, 1, 1, TSF_BGRA8, Image.RawData.GetData());
			Texture->SRGB = Image.GammaSpace != EGammaSpace::Linear;
			if (Style)
			{
				Texture->CompressionSettings = Style->CompressionSettings;
				Texture->CompressionNoAlpha = Style->CompressionNoAlpha;
				Texture->LODGroup = Style->LODGroup;
			}
			else
			{
				Texture->CompressionSettings = TC_Default;
				Texture->LODGroup = TEXTUREGROUP_Character;
			}
			Texture->VirtualTextureStreaming = false;
			Texture->MipGenSettings = TMGS_FromTextureGroup;
			Texture->PostEditChange();
			Package->MarkPackageDirty();

			if (bCreated)
			{
				FAssetRegistryModule::AssetCreated(Texture);
			}
			return Texture;
		}
	}

	FString FSkinTextureSlot::GetName() const
	{
		return bFaceTexture
			? StaticEnum<EFaceTextureType>()->GetNameStringByValue(static_cast<int64>(FaceType))
			: StaticEnum<EBodyTextureType>()->GetNameStringByValue(static_cast<int64>(BodyType));
	}

	TArray<FSkinTextureSlot> GetSkinTextureSlots(bool bPreviewOnly)
	{
		TArray<FSkinTextureSlot> Slots;

		auto AddFace = [&Slots](EFaceTextureType Type)
		{
			FSkinTextureSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.bFaceTexture = true;
			Slot.FaceType = Type;
			Slot.bHeadMesh = true;
		};
		auto AddBody = [&Slots](EBodyTextureType Type, bool bHeadMesh)
		{
			FSkinTextureSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.bFaceTexture = false;
			Slot.BodyType = Type;
			Slot.bHeadMesh = bHeadMesh;
		};

		// Head skin: the face base color, plus the Chest texture the head material blends in over the lower neck (it is laid
		// out in head UV space). Body skin: the body base color.
		// Left alone on purpose: the animated face maps (Basecolor_Animated_CM1-3) are deltas the material adds on top of the
		// base color while the face moves, so the tattoo stays visible through them; the underwear textures draw underwear
		// over the skin, which should cover a tattoo rather than show it.
		// Chest and Body are baked only when they are plain color overrides (ReadSlotBase marks the generated ones as tone data).
		AddFace(EFaceTextureType::Basecolor);
		AddBody(EBodyTextureType::Chest_Basecolor, true);
		AddBody(EBodyTextureType::Body_Basecolor, false);
		return Slots;
	}

	int32 GetRasterSize(const FMetaHumanTattooLayer& Layer, bool bPreview)
	{
		const float LargerSideCm = FMath::Max(Layer.WidthCm, Layer.GetHeightCm());
		const float TexelsPerCm = bPreview ? 40.0f : 100.0f;
		return FMath::Clamp(FMath::RoundToInt(LargerSideCm * TexelsPerCm), bPreview ? 128 : 256, bPreview ? 1024 : 4096);
	}

	void ComputeFrame(const FMetaHumanTattooLayer& Layer, FVector3f& OutNormal, FVector3f& OutRight, FVector3f& OutUp)
	{
		FVector3f Normal = FVector3f(Layer.Placement.Normal);
		if (!Normal.Normalize())
		{
			Normal = FVector3f(0, 1, 0);
		}

		FVector3f Up = FVector3f(Layer.Placement.Up);
		Up -= Normal * FVector3f::DotProduct(Up, Normal);
		if (!Up.Normalize())
		{
			Up = FMath::Abs(Normal.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
			Up -= Normal * FVector3f::DotProduct(Up, Normal);
			Up.Normalize();
		}

		// Positive rotation turns the top towards the right: clockwise as seen looking at the skin.
		const float Radians = FMath::DegreesToRadians(Layer.RotationDegrees);
		const FVector3f RightBefore = FVector3f::CrossProduct(Normal, Up);
		const FVector3f Rotated = Up * FMath::Cos(Radians) + RightBefore * FMath::Sin(Radians);
		FVector3f Right = FVector3f::CrossProduct(Normal, Rotated);
		if (Layer.bMirror)
		{
			Right = -Right;
		}

		OutNormal = Normal;
		OutUp = Rotated.GetSafeNormal();
		OutRight = Right.GetSafeNormal();
	}

	bool PrepareTattoo(const FMetaHumanTattooLayer& Layer, bool bPreview, FPreparedTattoo& OutTattoo, FString& OutError)
	{
		if (!Layer.bVisible)
		{
			OutError = Layer.Name + TEXT(" is hidden.");
			return false;
		}
		if (!Layer.Placement.bPlaced)
		{
			OutError = Layer.Name + TEXT(" has not been placed on the skin yet.");
			return false;
		}
		if (Layer.SvgData.IsEmpty())
		{
			OutError = Layer.Name + TEXT(" has no SVG design.");
			return false;
		}

		const float Width = FMath::Clamp(Layer.WidthCm, 0.5f, 200.0f);
		const float Aspect = FMath::Max(Layer.SvgAspect, 0.01f);
		const float Height = Width * Aspect;
		const int32 RasterSize = GetRasterSize(Layer, bPreview);
		const float RasterWidthPixels = Aspect <= 1.0f ? RasterSize : RasterSize / Aspect;
		const float PixelsPerMm = RasterWidthPixels / (Width * 10.0f);
		const float SpreadPixels = FMath::Clamp(Layer.InkSpreadMm, 0.0f, 5.0f) * PixelsPerMm;

		OutTattoo.Raster = GetCachedRaster(Layer.SvgData, RasterSize, Layer.InkMode, Layer.InkColor, SpreadPixels, OutError);
		if (!OutTattoo.Raster.IsValid())
		{
			OutError = Layer.Name + TEXT(": ") + OutError;
			return false;
		}

		ComputeFrame(Layer, OutTattoo.Normal, OutTattoo.Right, OutTattoo.Up);
		OutTattoo.Center = FVector3f(Layer.Placement.Location);
		OutTattoo.Width = Width;
		OutTattoo.Height = Height;
		OutTattoo.Depth = Layer.ProjectionDepthCm > 0.0f ? Layer.ProjectionDepthCm : FMath::Max(2.0f, 0.5f * FMath::Max(Width, Height));
		const float MaxAngle = FMath::Clamp(Layer.MaxSurfaceAngleDegrees, 10.0f, 89.0f);
		OutTattoo.CosMaxAngle = FMath::Cos(FMath::DegreesToRadians(MaxAngle));
		OutTattoo.CosFadeStart = FMath::Cos(FMath::DegreesToRadians(FMath::Max(0.0f, MaxAngle - 10.0f)));
		OutTattoo.Opacity = FMath::Clamp(Layer.Opacity, 0.0f, 1.0f);
		OutTattoo.BlendMode = Layer.BlendMode;
		OutTattoo.bOnHead = Layer.Target != EMetaHumanTattooTarget::BodyOnly;
		OutTattoo.bOnBody = Layer.Target != EMetaHumanTattooTarget::HeadOnly;
		return true;
	}

	int64 CompositeTattoos(const FMetaHumanTattooMesh& Mesh, const FSkinTextureSlot& Slot, TConstArrayView<FPreparedTattoo> Tattoos, FImage& Image, bool bFillGutters)
	{
		if (Image.Format != ERawImageFormat::BGRA8 || Image.NumSlices != 1)
		{
			UE_LOG(LogMetaHumanTattoo, Error, TEXT("CompositeTattoos needs a single 8-bit BGRA image (slot %s)."), *Slot.GetName());
			return 0;
		}

		const TArray<FVector2f>& UVs = Slot.UVChannel == 0 ? Mesh.UV0 : Mesh.UV1;
		if (UVs.Num() != Mesh.Positions.Num())
		{
			return 0;
		}

		const int32 Width = Image.SizeX;
		const int32 Height = Image.SizeY;
		const bool bSRGB = Image.GammaSpace != EGammaSpace::Linear;
		uint8* Pixels = Image.RawData.GetData();
		const FIntPoint Tile = Slot.Tile.X == INDEX_NONE ? Mesh.DominantTile : Slot.Tile;

		// Triangles of this slot's tile, found once.
		TArray<int32> TileTriangles;
		TileTriangles.Reserve(Mesh.SkinTriangles.Num());
		for (int32 Triangle = 0; Triangle < Mesh.SkinTriangles.Num(); ++Triangle)
		{
			const FIntVector3& Tri = Mesh.SkinTriangles[Triangle];
			if (IsTriangleInTile(UVs[Tri.X], UVs[Tri.Y], UVs[Tri.Z], Tile))
			{
				TileTriangles.Add(Triangle);
			}
		}
		if (TileTriangles.IsEmpty())
		{
			return 0;
		}

		// Modified: texel changed by any tattoo. Visited: texel already drawn by the current tattoo (texels on shared
		// triangle edges are reached twice and must not get the ink twice); reset after each tattoo from Touched.
		TArray<uint8> Modified;
		TArray<uint8> Visited;
		TArray<int32> Touched;
		FIntRect Changed(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
		int64 TexelsChanged = 0;

		for (const FPreparedTattoo& Tattoo : Tattoos)
		{
			if (!(Mesh.bIsHead ? Tattoo.bOnHead : Tattoo.bOnBody) || !Tattoo.Raster.IsValid() || Tattoo.Opacity <= 0.0f)
			{
				continue;
			}
			if (Visited.IsEmpty())
			{
				Visited.SetNumZeroed(Width * Height);
				Modified.SetNumZeroed(Width * Height);
			}
			for (const int32 Index : Touched)
			{
				Visited[Index] = 0;
			}
			Touched.Reset();

			const FMetaHumanTattooRaster& Raster = *Tattoo.Raster;
			const float FadeDepthStart = Tattoo.Depth * 0.85f;

			for (const int32 Triangle : TileTriangles)
			{
				const FIntVector3& Tri = Mesh.SkinTriangles[Triangle];
				const FVector3f P[3] = { Mesh.Positions[Tri.X], Mesh.Positions[Tri.Y], Mesh.Positions[Tri.Z] };
				const FVector3f N[3] = { Mesh.Normals[Tri.X], Mesh.Normals[Tri.Y], Mesh.Normals[Tri.Z] };

				// Reject triangles entirely outside the stencil box or facing away.
				float S[3], T[3], Z[3];
				bool bAnyFacing = false;
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const FVector3f D = P[Corner] - Tattoo.Center;
					S[Corner] = FVector3f::DotProduct(D, Tattoo.Right) / Tattoo.Width + 0.5f;
					T[Corner] = 0.5f - FVector3f::DotProduct(D, Tattoo.Up) / Tattoo.Height;
					Z[Corner] = FVector3f::DotProduct(D, Tattoo.Normal);
					bAnyFacing |= FVector3f::DotProduct(N[Corner], Tattoo.Normal) >= Tattoo.CosMaxAngle;
				}
				if (!bAnyFacing
					|| (S[0] < 0.0f && S[1] < 0.0f && S[2] < 0.0f) || (S[0] > 1.0f && S[1] > 1.0f && S[2] > 1.0f)
					|| (T[0] < 0.0f && T[1] < 0.0f && T[2] < 0.0f) || (T[0] > 1.0f && T[1] > 1.0f && T[2] > 1.0f)
					|| (Z[0] > Tattoo.Depth && Z[1] > Tattoo.Depth && Z[2] > Tattoo.Depth)
					|| (Z[0] < -Tattoo.Depth && Z[1] < -Tattoo.Depth && Z[2] < -Tattoo.Depth))
				{
					continue;
				}

				const FVector2f A = ToTexel(UVs[Tri.X], Tile, Width, Height);
				const FVector2f B = ToTexel(UVs[Tri.Y], Tile, Width, Height);
				const FVector2f C = ToTexel(UVs[Tri.Z], Tile, Width, Height);

				RasterizeTriangle(A, B, C, Width, Height, [&](int32 X, int32 Y, float B0, float B1, float B2)
				{
					const int32 Index = Y * Width + X;
					if (Visited[Index])
					{
						return;
					}
					Visited[Index] = 1;
					Touched.Add(Index);

					const FVector3f Position = P[0] * B0 + P[1] * B1 + P[2] * B2;
					const FVector3f D = Position - Tattoo.Center;
					const float Along = FVector3f::DotProduct(D, Tattoo.Normal);
					const float AbsAlong = FMath::Abs(Along);
					if (AbsAlong > Tattoo.Depth)
					{
						return;
					}

					const FVector3f Normal = (N[0] * B0 + N[1] * B1 + N[2] * B2).GetSafeNormal();
					const float Facing = FVector3f::DotProduct(Normal, Tattoo.Normal);
					if (Facing < Tattoo.CosMaxAngle)
					{
						return;
					}

					const float SampleS = FVector3f::DotProduct(D, Tattoo.Right) / Tattoo.Width + 0.5f;
					const float SampleT = 0.5f - FVector3f::DotProduct(D, Tattoo.Up) / Tattoo.Height;
					const FLinearColor Ink = Raster.Sample(SampleS, SampleT);
					if (Ink.A <= 1.0f / 1024.0f)
					{
						return;
					}

					float Fade = 1.0f;
					if (Tattoo.CosFadeStart > Tattoo.CosMaxAngle)
					{
						Fade *= FMath::SmoothStep(Tattoo.CosMaxAngle, Tattoo.CosFadeStart, Facing);
					}
					Fade *= 1.0f - FMath::SmoothStep(FadeDepthStart, Tattoo.Depth, AbsAlong);

					const float Alpha = FMath::Clamp(Ink.A * Tattoo.Opacity * Fade, 0.0f, 1.0f);
					if (Alpha <= 0.0f)
					{
						return;
					}
					const float InvInkAlpha = 1.0f / Ink.A;
					const FLinearColor InkColor(Ink.R * InvInkAlpha, Ink.G * InvInkAlpha, Ink.B * InvInkAlpha, 1.0f);

					uint8* Texel = Pixels + static_cast<int64>(Index) * 4;
					const float SkinB = DecodeChannel(Texel[0], bSRGB);
					const float SkinG = DecodeChannel(Texel[1], bSRGB);
					const float SkinR = DecodeChannel(Texel[2], bSRGB);

					float OutR, OutG, OutB;
					if (Tattoo.BlendMode == EMetaHumanTattooBlendMode::HealedInk)
					{
						OutR = SkinR * (1.0f - Alpha + Alpha * InkColor.R);
						OutG = SkinG * (1.0f - Alpha + Alpha * InkColor.G);
						OutB = SkinB * (1.0f - Alpha + Alpha * InkColor.B);
					}
					else
					{
						OutR = FMath::Lerp(SkinR, InkColor.R, Alpha);
						OutG = FMath::Lerp(SkinG, InkColor.G, Alpha);
						OutB = FMath::Lerp(SkinB, InkColor.B, Alpha);
					}

					Texel[0] = EncodeChannel(OutB, bSRGB);
					Texel[1] = EncodeChannel(OutG, bSRGB);
					Texel[2] = EncodeChannel(OutR, bSRGB);

					if (!Modified[Index])
					{
						Modified[Index] = 1;
						++TexelsChanged;
						Changed.Min.X = FMath::Min(Changed.Min.X, X);
						Changed.Min.Y = FMath::Min(Changed.Min.Y, Y);
						Changed.Max.X = FMath::Max(Changed.Max.X, X);
						Changed.Max.Y = FMath::Max(Changed.Max.Y, Y);
					}
				});
			}
		}

		if (!bFillGutters || TexelsChanged == 0)
		{
			return TexelsChanged;
		}

		// Spread the changed texels into the gutters (texels no skin triangle covers) so bilinear filtering and mips
		// at UV seams sample tattooed color instead of the old skin color.
		const int32 GutterPasses = FMath::Clamp(Width / 512, 2, 16);
		const FIntRect Region(
			FMath::Max(0, Changed.Min.X - GutterPasses - 1), FMath::Max(0, Changed.Min.Y - GutterPasses - 1),
			FMath::Min(Width - 1, Changed.Max.X + GutterPasses + 1), FMath::Min(Height - 1, Changed.Max.Y + GutterPasses + 1));

		TArray<uint8> Covered;
		Covered.SetNumZeroed(Width * Height);
		for (const int32 Triangle : TileTriangles)
		{
			const FIntVector3& Tri = Mesh.SkinTriangles[Triangle];
			const FVector2f A = ToTexel(UVs[Tri.X], Tile, Width, Height);
			const FVector2f B = ToTexel(UVs[Tri.Y], Tile, Width, Height);
			const FVector2f C = ToTexel(UVs[Tri.Z], Tile, Width, Height);
			if (FMath::Max3(A.X, B.X, C.X) < Region.Min.X - 1 || FMath::Min3(A.X, B.X, C.X) > Region.Max.X + 1
				|| FMath::Max3(A.Y, B.Y, C.Y) < Region.Min.Y - 1 || FMath::Min3(A.Y, B.Y, C.Y) > Region.Max.Y + 1)
			{
				continue;
			}
			RasterizeTriangle(A, B, C, Width, Height, [&](int32 X, int32 Y, float, float, float)
			{
				Covered[Y * Width + X] = 1;
			});
		}

		struct FGutterWrite
		{
			int32 Index;
			uint8 B, G, R;
		};
		TArray<FGutterWrite> Writes;
		for (int32 GutterPass = 0; GutterPass < GutterPasses; ++GutterPass)
		{
			Writes.Reset();
			for (int32 Y = Region.Min.Y; Y <= Region.Max.Y; ++Y)
			{
				for (int32 X = Region.Min.X; X <= Region.Max.X; ++X)
				{
					const int32 Index = Y * Width + X;
					if (Covered[Index] || Modified[Index])
					{
						continue;
					}
					int32 SumB = 0, SumG = 0, SumR = 0, Count = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const int32 NX = X + DX;
							const int32 NY = Y + DY;
							if ((DX == 0 && DY == 0) || NX < 0 || NY < 0 || NX >= Width || NY >= Height)
							{
								continue;
							}
							const int32 Neighbour = NY * Width + NX;
							if (Modified[Neighbour])
							{
								const uint8* Source = Pixels + static_cast<int64>(Neighbour) * 4;
								SumB += Source[0];
								SumG += Source[1];
								SumR += Source[2];
								++Count;
							}
						}
					}
					if (Count > 0)
					{
						Writes.Add({ Index, static_cast<uint8>(SumB / Count), static_cast<uint8>(SumG / Count), static_cast<uint8>(SumR / Count) });
					}
				}
			}
			if (Writes.IsEmpty())
			{
				break;
			}
			for (const FGutterWrite& Write : Writes)
			{
				uint8* Texel = Pixels + static_cast<int64>(Write.Index) * 4;
				Texel[0] = Write.B;
				Texel[1] = Write.G;
				Texel[2] = Write.R;
				Modified[Write.Index] = 2;
			}
		}

		return TexelsChanged;
	}

	bool ReadSlotBase(UMetaHumanCharacter* Character, const UMetaHumanTattooSet* Set, const FSkinTextureSlot& Slot, FSlotBase& OutBase, FString& OutError)
	{
		OutBase = FSlotBase();
		if (!Character)
		{
			OutError = TEXT("No character.");
			return false;
		}

		const FMetaHumanCharacterTextureMaterialOverrides& Overrides = Character->SkinSettings.TextureMaterialOverrides;
		const TSoftObjectPtr<UTexture2D> Current = Overrides.bEnableTextureOverrides ? FindOverride(Overrides.TextureOverrides, Slot) : TSoftObjectPtr<UTexture2D>();

		bool bHasOutput = false;
		const TSoftObjectPtr<UTexture2D> Ours = Set ? FindSlotEntry(Set->FaceOutputs, Set->BodyOutputs, Slot, bHasOutput) : TSoftObjectPtr<UTexture2D>();
		if (!Current.IsNull() && bHasOutput && Current.ToSoftObjectPath() == Ours.ToSoftObjectPath())
		{
			bool bHasBase = false;
			OutBase.BaseRef = FindSlotEntry(Set->FaceBases, Set->BodyBases, Slot, bHasBase);
		}
		else
		{
			OutBase.BaseRef = Current;
		}

		bool bRead = false;
		if (!OutBase.BaseRef.IsNull())
		{
			UTexture2D* Texture = OutBase.BaseRef.LoadSynchronous();
			if (!Texture)
			{
				OutError = FString::Printf(TEXT("%s: could not load the override texture %s."), *Slot.GetName(), *OutBase.BaseRef.ToString());
				return false;
			}
			bRead = ReadTextureSource(Texture, OutBase.Image);
			OutBase.StyleTexture = Texture;
			if (!bRead)
			{
				OutError = FString::Printf(TEXT("%s: the override texture %s has no source pixels."), *Slot.GetName(), *Texture->GetPathName());
				return false;
			}
		}
		else if (Slot.bFaceTexture)
		{
			UTexture2D* Generated = Character->SynthesizedFaceTextures.FindRef(Slot.FaceType);
			OutBase.StyleTexture = Generated;
			if (const FMetaHumanCharacterTextureInfo* Info = Character->SynthesizedFaceTexturesInfo.Find(Slot.FaceType))
			{
				bRead = CopyBufferToImage(Character->GetSynthesizedFaceTextureDataAsync(Slot.FaceType).Get(), Info->ToImageInfo(), OutBase.Image);
			}
			if (!bRead)
			{
				bRead = ReadTextureSource(Generated, OutBase.Image);
			}
		}
		else
		{
			UTexture2D* Generated = Character->BodyTextures.FindRef(Slot.BodyType);
			OutBase.StyleTexture = Generated;
			if (const FMetaHumanCharacterTextureInfo* Info = Character->HighResBodyTexturesInfo.Find(Slot.BodyType))
			{
				bRead = CopyBufferToImage(Character->GetHighResBodyTextureDataAsync(Slot.BodyType).Get(), Info->ToImageInfo(), OutBase.Image);
			}
			if (!bRead)
			{
				bRead = ReadTextureSource(Generated, OutBase.Image);
			}
		}

		if (!bRead)
		{
			OutError = FString::Printf(TEXT("%s: the character has no texture in this slot."), *Slot.GetName());
			return false;
		}

		if (OutBase.Image.NumSlices != 1 || OutBase.Image.SizeX <= 0 || OutBase.Image.SizeY <= 0)
		{
			OutError = FString::Printf(TEXT("%s: unsupported texture layout."), *Slot.GetName());
			return false;
		}
		// MetaHuman Creator's generated body and chest base colors are desaturated, tone-neutral maps that the skin material
		// colors with the character's skin-tone bias and gain (rbias/rgain...). Ink drawn into them would only come out as a
		// darker skin tone, so only plain override textures are baked in those slots.
		OutBase.bToneEncoded = OutBase.BaseRef.IsNull() && !Slot.bFaceTexture
			&& (Slot.BodyType == EBodyTextureType::Body_Basecolor || Slot.BodyType == EBodyTextureType::Chest_Basecolor);

		// The texture's own sRGB flag decides how its 8-bit values are read and written (texture info can disagree with it).
		if (const UTexture2D* Style = OutBase.StyleTexture.Get())
		{
			if (OutBase.Image.Format == ERawImageFormat::BGRA8 || OutBase.Image.Format == ERawImageFormat::G8)
			{
				OutBase.Image.GammaSpace = Style->SRGB ? EGammaSpace::sRGB : EGammaSpace::Linear;
			}
		}
		if (OutBase.Image.Format != ERawImageFormat::BGRA8)
		{
			OutBase.Image.ChangeFormat(ERawImageFormat::BGRA8, OutBase.Image.GammaSpace == EGammaSpace::Linear ? EGammaSpace::Linear : EGammaSpace::sRGB);
		}
		return true;
	}

	FString GetTattooFolder(const UMetaHumanCharacter* Character)
	{
		return FPackageName::GetLongPackagePath(Character->GetOutermost()->GetName()) / TEXT("Tattoos");
	}

	FString GetTattooSetPackageName(const UMetaHumanCharacter* Character)
	{
		return GetTattooFolder(Character) / (TEXT("TS_") + Character->GetName());
	}

	UMetaHumanTattooSet* FindOrCreateTattooSet(UMetaHumanCharacter* Character, bool bCreate)
	{
		if (!Character)
		{
			return nullptr;
		}

		const FString PackageName = GetTattooSetPackageName(Character);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;

		if (UMetaHumanTattooSet* Loaded = FindObject<UMetaHumanTattooSet>(nullptr, *ObjectPath))
		{
			return Loaded;
		}
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (UMetaHumanTattooSet* Loaded = LoadObject<UMetaHumanTattooSet>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Loaded;
			}
		}
		if (!bCreate)
		{
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackageName);
		UMetaHumanTattooSet* Set = NewObject<UMetaHumanTattooSet>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		Set->Character = Character;
		FAssetRegistryModule::AssetCreated(Set);
		Package->MarkPackageDirty();
		return Set;
	}

	bool LoadSvgIntoLayer(const FString& FilePath, FMetaHumanTattooLayer& Layer, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			OutError = TEXT("Could not read ") + FilePath;
			return false;
		}

		float Aspect = 1.0f;
		if (!ReadSvgAspect(Text, Aspect, OutError))
		{
			OutError = FPaths::GetCleanFilename(FilePath) + TEXT(": ") + OutError;
			return false;
		}

		const FString PreviousFile = Layer.SvgFile.FilePath;
		Layer.SvgFile.FilePath = FilePath;
		Layer.SvgData = MoveTemp(Text);
		Layer.SvgAspect = Aspect;
		if (Layer.Name.IsEmpty() || Layer.Name == TEXT("Tattoo") || Layer.Name == FPaths::GetBaseFilename(PreviousFile))
		{
			Layer.Name = FPaths::GetBaseFilename(FilePath);
		}
		return true;
	}

	bool ApplyTattoos(UMetaHumanCharacter* Character, UMetaHumanTattooSet* Set, bool bSave, FString& OutReport)
	{
		TArray<FString> Report;
		ON_SCOPE_EXIT { OutReport = FString::Join(Report, TEXT("\n")); };

		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		if (!Character || !Set || !Subsystem)
		{
			Report.Add(TEXT("No character, tattoo set or MetaHuman Character editor subsystem."));
			return false;
		}
		if (!Subsystem->IsObjectAddedForEditing(Character))
		{
			Report.Add(Character->GetName() + TEXT(" is not open for editing."));
			return false;
		}

		FMetaHumanTattooCharacterMeshes Meshes;
		FString Error;
		if (!Meshes.Build(Character, Error))
		{
			Report.Add(Error);
			return false;
		}

		TArray<FPreparedTattoo> Prepared;
		for (const FMetaHumanTattooLayer& Layer : Set->Layers)
		{
			FPreparedTattoo Tattoo;
			if (PrepareTattoo(Layer, false, Tattoo, Error))
			{
				Prepared.Add(MoveTemp(Tattoo));
			}
			else if (Layer.bVisible)
			{
				Report.Add(TEXT("Skipped: ") + Error);
			}
		}

		const bool bWereEnabled = Character->SkinSettings.TextureMaterialOverrides.bEnableTextureOverrides;
		if (!bWereEnabled && !Prepared.IsEmpty())
		{
			// Switching texture overrides on makes the skin material read its textures as plain color. A body still on the
			// generated, tone-encoded texture would then render wrongly, so do not switch them on for such a character.
			FSkinTextureSlot BodySlot;
			BodySlot.bFaceTexture = false;
			BodySlot.BodyType = EBodyTextureType::Body_Basecolor;
			BodySlot.bHeadMesh = false;
			FSlotBase BodyBase;
			if (ReadSlotBase(Character, Set, BodySlot, BodyBase, Error) && BodyBase.bToneEncoded)
			{
				Report.Add(TEXT("Not applied: this character's skin uses MetaHuman Creator's generated body texture and its texture overrides are off. ")
					TEXT("Tattoos are baked through texture overrides, and switching those on would change how its generated body skin renders. ")
					TEXT("Give the character base color texture overrides first (Texture & Material Overrides tool), then apply again."));
				return false;
			}
		}

		Set->Modify();
		const FMetaHumanCharacterSkinSettings OldSettings = Character->SkinSettings;
		FMetaHumanCharacterSkinSettings Settings = OldSettings;
		FMetaHumanCharacterTextureMaterialOverrides& Overrides = Settings.TextureMaterialOverrides;

		// Inactive entries must not take effect when tattoos switch overrides on, so work from an empty set in that case.
		FMetaHumanCharacterSkinTextureSoftSet Working = bWereEnabled ? Overrides.TextureOverrides : FMetaHumanCharacterSkinTextureSoftSet();
		TArray<UTexture*> Written;
		TArray<UObject*> Obsolete;
		TSet<UPackage*> PackagesToSave;

		const TArray<FSkinTextureSlot> AllSlots = GetSkinTextureSlots(false);
		for (const FSkinTextureSlot& Slot : AllSlots)
		{
			FSlotBase Base;
			if (!ReadSlotBase(Character, Set, Slot, Base, Error))
			{
				Report.Add(TEXT("Not baked: ") + Error);
				continue;
			}

			const FMetaHumanTattooMesh& Mesh = Slot.bHeadMesh ? Meshes.Head : Meshes.Body;
			const int64 TexelsChanged = Prepared.IsEmpty() || Base.bToneEncoded ? 0 : CompositeTattoos(Mesh, Slot, Prepared, Base.Image, true);
			if (Base.bToneEncoded && !Prepared.IsEmpty())
			{
				Report.Add(FString::Printf(TEXT("Not baked: %s is MetaHuman Creator's generated, tone-encoded skin texture (no ink can be drawn into it). ")
					TEXT("Tattoos still go into the other skin textures; give this slot a base color override to tattoo it too."), *Slot.GetName()));
			}

			bool bHadOutput = false;
			const TSoftObjectPtr<UTexture2D> PreviousOutput = FindSlotEntry(Set->FaceOutputs, Set->BodyOutputs, Slot, bHadOutput);

			if (TexelsChanged > 0)
			{
				UTexture2D* Output = WriteTattooTexture(Character, Slot, Base.Image, Base.StyleTexture.Get(), Error);
				if (!Output)
				{
					Report.Add(TEXT("Failed: ") + Error);
					continue;
				}
				Written.Add(Output);
				PackagesToSave.Add(Output->GetOutermost());
				if (Slot.bFaceTexture)
				{
					Set->FaceBases.Add(Slot.FaceType, Base.BaseRef);
					Set->FaceOutputs.Add(Slot.FaceType, Output);
				}
				else
				{
					Set->BodyBases.Add(Slot.BodyType, Base.BaseRef);
					Set->BodyOutputs.Add(Slot.BodyType, Output);
				}
				SetOverride(Working, Slot, Output);
				Report.Add(FString::Printf(TEXT("Baked %s: %lld texels changed (%dx%d, from %s)."), *Slot.GetName(), TexelsChanged, Base.Image.SizeX, Base.Image.SizeY,
					Base.BaseRef.IsNull() ? TEXT("the generated MetaHuman texture") : *Base.BaseRef.ToString()));
			}
			else if (bHadOutput)
			{
				// No tattoo reaches this slot any more: give it back its previous texture.
				SetOverride(Working, Slot, Base.BaseRef);
				if (UTexture2D* OldTexture = PreviousOutput.LoadSynchronous())
				{
					Obsolete.Add(OldTexture);
				}
				if (Slot.bFaceTexture)
				{
					Set->FaceOutputs.Remove(Slot.FaceType);
					Set->FaceBases.Remove(Slot.FaceType);
				}
				else
				{
					Set->BodyOutputs.Remove(Slot.BodyType);
					Set->BodyBases.Remove(Slot.BodyType);
				}
				Report.Add(FString::Printf(TEXT("Restored %s to %s."), *Slot.GetName(), Base.BaseRef.IsNull() ? TEXT("the generated MetaHuman texture") : *Base.BaseRef.ToString()));
			}
		}

		// Baked textures of slots this version no longer bakes (for example the animated face delta maps): give those
		// slots their previous texture back and delete the baked copies.
		TSet<EFaceTextureType> KnownFace;
		TSet<EBodyTextureType> KnownBody;
		for (const FSkinTextureSlot& Slot : AllSlots)
		{
			if (Slot.bFaceTexture)
			{
				KnownFace.Add(Slot.FaceType);
			}
			else
			{
				KnownBody.Add(Slot.BodyType);
			}
		}
		TArray<FSkinTextureSlot> Stale;
		for (const TPair<EFaceTextureType, TSoftObjectPtr<UTexture2D>>& Pair : Set->FaceOutputs)
		{
			if (!KnownFace.Contains(Pair.Key))
			{
				FSkinTextureSlot& Slot = Stale.AddDefaulted_GetRef();
				Slot.bFaceTexture = true;
				Slot.FaceType = Pair.Key;
			}
		}
		for (const TPair<EBodyTextureType, TSoftObjectPtr<UTexture2D>>& Pair : Set->BodyOutputs)
		{
			if (!KnownBody.Contains(Pair.Key))
			{
				FSkinTextureSlot& Slot = Stale.AddDefaulted_GetRef();
				Slot.bFaceTexture = false;
				Slot.BodyType = Pair.Key;
			}
		}
		for (const FSkinTextureSlot& Slot : Stale)
		{
			bool bFound = false;
			const TSoftObjectPtr<UTexture2D> Base = FindSlotEntry(Set->FaceBases, Set->BodyBases, Slot, bFound);
			const TSoftObjectPtr<UTexture2D> Old = FindSlotEntry(Set->FaceOutputs, Set->BodyOutputs, Slot, bFound);
			if (FindOverride(Working, Slot).ToSoftObjectPath() == Old.ToSoftObjectPath())
			{
				SetOverride(Working, Slot, Base);
			}
			if (UTexture2D* OldTexture = Old.LoadSynchronous())
			{
				Obsolete.Add(OldTexture);
			}
			if (Slot.bFaceTexture)
			{
				Set->FaceOutputs.Remove(Slot.FaceType);
				Set->FaceBases.Remove(Slot.FaceType);
			}
			else
			{
				Set->BodyOutputs.Remove(Slot.BodyType);
				Set->BodyBases.Remove(Slot.BodyType);
			}
			Report.Add(FString::Printf(TEXT("Restored %s (no longer baked) to %s."), *Slot.GetName(), Base.IsNull() ? TEXT("the generated MetaHuman texture") : *Base.ToString()));
		}

		if (Set->HasAppliedTattoos())
		{
			if (!bWereEnabled)
			{
				Set->StashedInactiveOverrides = Overrides.TextureOverrides;
				Set->bTattoosEnabledTextureOverrides = true;
			}
			Overrides.TextureOverrides = Working;
			Overrides.bEnableTextureOverrides = true;
		}
		else if (bWereEnabled)
		{
			if (Set->bTattoosEnabledTextureOverrides && Working.Face.IsEmpty() && Working.Body.IsEmpty())
			{
				Overrides.TextureOverrides = Set->StashedInactiveOverrides;
				Overrides.bEnableTextureOverrides = false;
			}
			else
			{
				Overrides.TextureOverrides = Working;
			}
			Set->StashedInactiveOverrides = FMetaHumanCharacterSkinTextureSoftSet();
			Set->bTattoosEnabledTextureOverrides = false;
		}

		if (!Written.IsEmpty())
		{
			FTextureCompilingManager::Get().FinishCompilation(Written);
		}

		const FMetaHumanCharacterTextureMaterialOverrides& Before = OldSettings.TextureMaterialOverrides;
		const bool bSettingsChanged = Before.bEnableTextureOverrides != Overrides.bEnableTextureOverrides
			|| !Before.TextureOverrides.Face.OrderIndependentCompareEqual(Overrides.TextureOverrides.Face)
			|| !Before.TextureOverrides.Body.OrderIndependentCompareEqual(Overrides.TextureOverrides.Body);
		if (bSettingsChanged || !Written.IsEmpty())
		{
			Subsystem->CommitSkinSettings(Character, Settings);
			PackagesToSave.Add(Character->GetOutermost());
		}

		Set->AppliedLayers = Set->Layers;
		Set->AppliedSkinProperties = Character->SkinSettings.Skin;
		Set->bHasAppliedSkinProperties = true;
		Set->Character = Character;
		Set->MarkPackageDirty();
		PackagesToSave.Add(Set->GetOutermost());

		if (bSave)
		{
			TArray<UPackage*> Packages = PackagesToSave.Array();
			if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
			{
				Report.Add(TEXT("Warning: not every package could be saved (is another editor or a game holding them?)."));
			}
		}

		if (!Obsolete.IsEmpty())
		{
			const int32 Deleted = ObjectTools::ForceDeleteObjects(Obsolete, false);
			Report.Add(FString::Printf(TEXT("Deleted %d baked texture(s) no tattoo uses any more."), Deleted));
		}

		if (Prepared.IsEmpty())
		{
			Report.Add(TEXT("No visible, placed tattoos: the skin shows no tattoos."));
		}
		UE_LOG(LogMetaHumanTattoo, Display, TEXT("Apply tattoos to %s:\n%s"), *Character->GetName(), *FString::Join(Report, TEXT("\n")));
		return true;
	}
}
