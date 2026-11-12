#include "MetaHumanTattooLibrary.h"

#include "MetaHumanTattooBaker.h"
#include "MetaHumanTattooLog.h"
#include "MetaHumanTattooMesh.h"
#include "MetaHumanTattooSession.h"
#include "MetaHumanTattooSet.h"
#include "MetaHumanTattooTabs.h"

#include "ImageUtils.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"

#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"

namespace
{
	/** Registers the character with the MetaHuman Character editor subsystem for the scope, unless it already is. */
	struct FScopedCharacterEditing
	{
		UMetaHumanCharacter* Character = nullptr;
		bool bAdded = false;
		bool bReady = false;

		explicit FScopedCharacterEditing(UMetaHumanCharacter* InCharacter)
			: Character(InCharacter)
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (!Character || !Subsystem)
			{
				return;
			}
			if (Subsystem->IsObjectAddedForEditing(Character))
			{
				bReady = true;
				return;
			}
			bAdded = Subsystem->TryAddObjectToEdit(Character);
			bReady = bAdded;
			if (!bReady)
			{
				UE_LOG(LogMetaHumanTattoo, Error, TEXT("%s could not be opened for editing by the MetaHuman Character editor subsystem."), *Character->GetName());
			}
		}

		~FScopedCharacterEditing()
		{
			if (bAdded)
			{
				if (UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get())
				{
					Subsystem->RemoveObjectToEdit(Character);
				}
			}
		}
	};

	FString DescribeTexture(const UTexture* Texture)
	{
		if (!Texture)
		{
			return TEXT("none");
		}
		const UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
		return FString::Printf(TEXT("%s (%dx%d, VT %d, sRGB %d)"), *Texture->GetPathName(),
			Texture2D ? Texture2D->GetSizeX() : 0, Texture2D ? Texture2D->GetSizeY() : 0, Texture->VirtualTextureStreaming ? 1 : 0, Texture->SRGB ? 1 : 0);
	}

	void DescribeMaterial(const UMaterialInterface* Material, const FString& Label, TArray<FString>& Lines)
	{
		if (!Material)
		{
			Lines.Add(Label + TEXT(": no material"));
			return;
		}

		FString Chain;
		for (const UMaterialInterface* Current = Material; Current; )
		{
			Chain += (Chain.IsEmpty() ? TEXT("") : TEXT(" <- ")) + Current->GetName();
			const UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
			Current = Instance ? Instance->Parent.Get() : nullptr;
		}
		Lines.Add(FString::Printf(TEXT("%s: %s"), *Label, *Chain));

		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material->GetAllTextureParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			UTexture* Value = nullptr;
			Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Info), Value);
			Lines.Add(FString::Printf(TEXT("    tex %s = %s"), *Info.Name.ToString(), *DescribeTexture(Value)));
		}

		Material->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const FString Name = Info.Name.ToString();
			if (Name.Contains(TEXT("Override")) || Name.Contains(TEXT("Bias")) || Name.Contains(TEXT("Gain")) || Name.Contains(TEXT("Tint"))
				|| Name.Contains(TEXT("Chest")) || Name.Contains(TEXT("Underwear")) || Name.Contains(TEXT("Use")))
			{
				float Value = 0.0f;
				Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Info), Value);
				Lines.Add(FString::Printf(TEXT("    scalar %s = %g"), *Name, Value));
			}
		}

		Material->GetAllVectorParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const FString Name = Info.Name.ToString();
			if (Name.Contains(TEXT("Bias")) || Name.Contains(TEXT("Gain")) || Name.Contains(TEXT("Tint")) || Name.Contains(TEXT("Chest")))
			{
				FLinearColor Value;
				Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Info), Value);
				Lines.Add(FString::Printf(TEXT("    vector %s = %s"), *Name, *Value.ToString()));
			}
		}
	}

	void DescribeMesh(const USkeletalMesh* Mesh, bool bHead, TArray<FString>& Lines)
	{
		const TCHAR* Label = bHead ? TEXT("Head") : TEXT("Body");
		if (!Mesh)
		{
			Lines.Add(FString::Printf(TEXT("%s edit mesh: none"), Label));
			return;
		}

		const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
		const FSkeletalMeshLODModel* LOD = Model && !Model->LODModels.IsEmpty() ? &Model->LODModels[0] : nullptr;
		Lines.Add(FString::Printf(TEXT("%s edit mesh %s: LOD0 %d vertices, %d sections, %d UV channels, %d LODs"), Label, *Mesh->GetPathName(),
			LOD ? static_cast<int32>(LOD->NumVertices) : 0, LOD ? LOD->Sections.Num() : 0, LOD ? static_cast<int32>(LOD->NumTexCoords) : 0, Model ? Model->LODModels.Num() : 0));

		TArray<FString> Slots;
		for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
		{
			Slots.Add(Material.MaterialSlotName.ToString());
		}
		Lines.Add(TEXT("    material slots: ") + FString::Join(Slots, TEXT(", ")));

		FMetaHumanTattooMesh Data;
		FString Error;
		if (Data.Build(Mesh, bHead, Error))
		{
			TArray<FString> SkinSlots;
			for (const FName& Name : Data.SkinSlotNames)
			{
				SkinSlots.Add(Name.ToString());
			}
			Lines.Add(FString::Printf(TEXT("    skin slots: %s; %d skin triangles"), *FString::Join(SkinSlots, TEXT(", ")), Data.SkinTriangles.Num()));
			Lines.Add(TEXT("    skin UV0 tiles: ") + Data.DescribeUVTiles(0));
			Lines.Add(TEXT("    skin UV1 tiles: ") + Data.DescribeUVTiles(1));
		}
		else
		{
			Lines.Add(TEXT("    ") + Error);
		}
	}
}

UMetaHumanTattooSet* UMetaHumanTattooLibrary::GetTattooSet(UMetaHumanCharacter* Character, bool bCreateIfMissing)
{
	return UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, bCreateIfMissing);
}

int32 UMetaHumanTattooLibrary::AddTattooFromSvgFile(UMetaHumanCharacter* Character, const FString& SvgFile, float WidthCm)
{
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, true);
	if (!Set)
	{
		UE_LOG(LogMetaHumanTattoo, Error, TEXT("AddTattooFromSvgFile: no character."));
		return INDEX_NONE;
	}

	FMetaHumanTattooLayer Layer;
	FString Error;
	if (!UE::MetaHumanTattoo::LoadSvgIntoLayer(FPaths::ConvertRelativePathToFull(SvgFile), Layer, Error))
	{
		UE_LOG(LogMetaHumanTattoo, Error, TEXT("AddTattooFromSvgFile: %s"), *Error);
		return INDEX_NONE;
	}
	Layer.WidthCm = FMath::Clamp(WidthCm, 0.5f, 200.0f);

	Set->Modify();
	const int32 Index = Set->Layers.Add(MoveTemp(Layer));
	Set->MarkPackageDirty();
	return Index;
}

bool UMetaHumanTattooLibrary::GetTattoo(UMetaHumanCharacter* Character, int32 Index, FMetaHumanTattooLayer& OutTattoo)
{
	const UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set || !Set->Layers.IsValidIndex(Index))
	{
		return false;
	}
	OutTattoo = Set->Layers[Index];
	return true;
}

bool UMetaHumanTattooLibrary::SetTattoo(UMetaHumanCharacter* Character, int32 Index, const FMetaHumanTattooLayer& Tattoo)
{
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set || !Set->Layers.IsValidIndex(Index))
	{
		return false;
	}
	Set->Modify();
	Set->Layers[Index] = Tattoo;
	Set->MarkPackageDirty();
	return true;
}

bool UMetaHumanTattooLibrary::RemoveTattoo(UMetaHumanCharacter* Character, int32 Index)
{
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set || !Set->Layers.IsValidIndex(Index))
	{
		return false;
	}
	Set->Modify();
	Set->Layers.RemoveAt(Index);
	Set->MarkPackageDirty();
	return true;
}

bool UMetaHumanTattooLibrary::PlaceTattooAlongRay(UMetaHumanCharacter* Character, int32 Index, FVector RayOrigin, FVector RayDirection, FVector UpHint)
{
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set || !Set->Layers.IsValidIndex(Index))
	{
		UE_LOG(LogMetaHumanTattoo, Error, TEXT("PlaceTattooAlongRay: no tattoo %d."), Index);
		return false;
	}

	FScopedCharacterEditing Editing(Character);
	if (!Editing.bReady)
	{
		return false;
	}

	FMetaHumanTattooCharacterMeshes Meshes;
	FString Error;
	if (!Meshes.Build(Character, Error))
	{
		UE_LOG(LogMetaHumanTattoo, Error, TEXT("PlaceTattooAlongRay: %s"), *Error);
		return false;
	}

	FMetaHumanTattooLayer& Layer = Set->Layers[Index];
	const FVector3f Origin(RayOrigin);
	const FVector3f Direction = FVector3f(RayDirection).GetSafeNormal();

	float Distance = TNumericLimits<float>::Max();
	const FMetaHumanTattooMesh* HitMesh = nullptr;
	int32 HitTriangle = INDEX_NONE;
	FVector3f HitBarycentric;
	if (Layer.Target != EMetaHumanTattooTarget::BodyOnly && Meshes.Head.Raycast(Origin, Direction, nullptr, Distance, HitTriangle, HitBarycentric))
	{
		HitMesh = &Meshes.Head;
	}
	int32 BodyTriangle = INDEX_NONE;
	FVector3f BodyBarycentric;
	if (Layer.Target != EMetaHumanTattooTarget::HeadOnly && Meshes.Body.Raycast(Origin, Direction, nullptr, Distance, BodyTriangle, BodyBarycentric))
	{
		HitMesh = &Meshes.Body;
		HitTriangle = BodyTriangle;
		HitBarycentric = BodyBarycentric;
	}
	if (!HitMesh)
	{
		UE_LOG(LogMetaHumanTattoo, Warning, TEXT("PlaceTattooAlongRay: the ray misses the skin of %s."), *Character->GetName());
		return false;
	}

	FVector3f Location, Normal;
	HitMesh->GetRestSurface(HitTriangle, HitBarycentric, Location, Normal);

	Set->Modify();
	Layer.Placement.bPlaced = true;
	Layer.Placement.Location = FVector(Location);
	Layer.Placement.Normal = FVector(Normal);
	Layer.Placement.Up = UpHint.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Set->MarkPackageDirty();
	UE_LOG(LogMetaHumanTattoo, Display, TEXT("Placed %s on the %s at %s, normal %s."), *Layer.Name, HitMesh->bIsHead ? TEXT("head") : TEXT("body"),
		*Layer.Placement.Location.ToString(), *Layer.Placement.Normal.ToString());
	return true;
}

bool UMetaHumanTattooLibrary::GetRestBoneLocation(UMetaHumanCharacter* Character, FName BoneName, FVector& OutLocation)
{
	FScopedCharacterEditing Editing(Character);
	if (!Editing.bReady)
	{
		return false;
	}

	const USkeletalMesh* Mesh = UMetaHumanCharacterEditorSubsystem::Get()->GetBodyEditMesh(Character);
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	int32 BoneIndex = Skeleton.FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogMetaHumanTattoo, Warning, TEXT("GetRestBoneLocation: %s has no bone %s."), *Mesh->GetName(), *BoneName.ToString());
		return false;
	}

	FTransform Transform = FTransform::Identity;
	while (BoneIndex != INDEX_NONE)
	{
		Transform = Transform * Skeleton.GetRefBonePose()[BoneIndex];
		BoneIndex = Skeleton.GetParentIndex(BoneIndex);
	}
	OutLocation = Transform.GetLocation();
	return true;
}

FString UMetaHumanTattooLibrary::ApplyTattoos(UMetaHumanCharacter* Character, bool& bSuccess)
{
	bSuccess = false;
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set)
	{
		return TEXT("The character has no tattoos.");
	}

	FScopedCharacterEditing Editing(Character);
	if (!Editing.bReady)
	{
		return TEXT("The character could not be opened for editing.");
	}

	FString Report;
	bSuccess = UE::MetaHumanTattoo::ApplyTattoos(Character, Set, true, Report);
	return Report;
}

FString UMetaHumanTattooLibrary::RemoveAllTattoos(UMetaHumanCharacter* Character, bool& bSuccess)
{
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set)
	{
		bSuccess = true;
		return TEXT("The character has no tattoos.");
	}

	Set->Modify();
	Set->Layers.Reset();
	return ApplyTattoos(Character, bSuccess);
}

bool UMetaHumanTattooLibrary::OpenTattooPanel(UMetaHumanCharacter* Character)
{
	return UE::MetaHumanTattoo::OpenTattooTab(Character);
}

FString UMetaHumanTattooLibrary::SimulateViewportClick(UMetaHumanCharacter* Character, FVector2D NormalizedPosition, bool& bOutHit)
{
	bOutHit = false;
	const TSharedPtr<FMetaHumanTattooSession> Session = FMetaHumanTattooSession::FindSession(Character);
	if (!Session.IsValid())
	{
		return TEXT("The Tattoos panel is not open for this character.");
	}
	FSlateRect Rect;
	if (!Session->GetViewportScreenRect(Rect))
	{
		return TEXT("No MetaHuman Character editor viewport for this character.");
	}
	if (!Session->GetSelectedLayer())
	{
		return TEXT("No tattoo is selected.");
	}

	const FVector2D Position(FMath::Lerp(Rect.Left, Rect.Right, NormalizedPosition.X), FMath::Lerp(Rect.Top, Rect.Bottom, NormalizedPosition.Y));
	const bool bWasPlacing = Session->IsPlacing();
	Session->SetPlacing(true);
	const bool bOver = Session->IsOverViewport(Position);
	bOutHit = Session->PlaceAtScreenPosition(Position, true);
	Session->SetPlacing(bWasPlacing);

	const FMetaHumanTattooLayer* Layer = Session->GetSelectedLayer();
	return FString::Printf(TEXT("viewport %.0f,%.0f-%.0f,%.0f; click %.0f,%.0f over viewport %d, hit skin %d; %s at %s normal %s"),
		Rect.Left, Rect.Top, Rect.Right, Rect.Bottom, Position.X, Position.Y, bOver ? 1 : 0, bOutHit ? 1 : 0,
		*Layer->Name, *Layer->Placement.Location.ToString(), *Layer->Placement.Normal.ToString());
}

FString UMetaHumanTattooLibrary::GetTattooPanelStatus(UMetaHumanCharacter* Character)
{
	const TSharedPtr<FMetaHumanTattooSession> Session = FMetaHumanTattooSession::FindSession(Character);
	return Session.IsValid() ? Session->DescribePreview() : FString(TEXT("The Tattoos panel is not open for this character."));
}

bool UMetaHumanTattooLibrary::DiscardTattooChanges(UMetaHumanCharacter* Character)
{
	if (const TSharedPtr<FMetaHumanTattooSession> Session = FMetaHumanTattooSession::FindSession(Character))
	{
		Session->DiscardChanges();
		return true;
	}
	UMetaHumanTattooSet* Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character, false);
	if (!Set)
	{
		return false;
	}
	Set->Modify();
	Set->Layers = Set->AppliedLayers;
	Set->MarkPackageDirty();
	return true;
}

FString UMetaHumanTattooLibrary::CompareTextures(UTexture2D* A, UTexture2D* B, const FString& PngPath)
{
	FImage ImageA, ImageB;
	if (!A || !B || !A->Source.IsValid() || !B->Source.IsValid() || !A->Source.GetMipImage(ImageA, 0) || !B->Source.GetMipImage(ImageB, 0))
	{
		return TEXT("Both textures need source pixels.");
	}
	ImageA.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	ImageB.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	if (ImageA.SizeX != ImageB.SizeX || ImageA.SizeY != ImageB.SizeY)
	{
		return FString::Printf(TEXT("Different sizes: %dx%d and %dx%d."), ImageA.SizeX, ImageA.SizeY, ImageB.SizeX, ImageB.SizeY);
	}

	const int32 Width = ImageA.SizeX;
	const int32 Height = ImageA.SizeY;
	int64 Changed = 0;
	int64 AlphaChanged = 0;
	int32 MaxDelta = 0;
	FIntRect Bounds(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const uint8* PA = ImageA.RawData.GetData() + (static_cast<int64>(Y) * Width + X) * 4;
			const uint8* PB = ImageB.RawData.GetData() + (static_cast<int64>(Y) * Width + X) * 4;
			const int32 Delta = FMath::Max3(FMath::Abs(PA[0] - PB[0]), FMath::Abs(PA[1] - PB[1]), FMath::Abs(PA[2] - PB[2]));
			if (PA[3] != PB[3])
			{
				++AlphaChanged;
			}
			if (Delta > 0)
			{
				++Changed;
				MaxDelta = FMath::Max(MaxDelta, Delta);
				Bounds.Min.X = FMath::Min(Bounds.Min.X, X);
				Bounds.Min.Y = FMath::Min(Bounds.Min.Y, Y);
				Bounds.Max.X = FMath::Max(Bounds.Max.X, X);
				Bounds.Max.Y = FMath::Max(Bounds.Max.Y, Y);
			}
		}
	}

	FString Summary = FString::Printf(TEXT("%dx%d: %lld texels differ (%.3f%%), max channel change %d, alpha changed %lld"), Width, Height, Changed,
		100.0 * Changed / (static_cast<double>(Width) * Height), MaxDelta, AlphaChanged);
	if (Changed == 0)
	{
		return Summary;
	}
	Summary += FString::Printf(TEXT(", region %d,%d - %d,%d"), Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y);

	if (!PngPath.IsEmpty())
	{
		const int32 Margin = 16;
		const int32 X0 = FMath::Max(0, Bounds.Min.X - Margin);
		const int32 Y0 = FMath::Max(0, Bounds.Min.Y - Margin);
		const int32 X1 = FMath::Min(Width - 1, Bounds.Max.X + Margin);
		const int32 Y1 = FMath::Min(Height - 1, Bounds.Max.Y + Margin);
		const int32 CropW = X1 - X0 + 1;
		const int32 CropH = Y1 - Y0 + 1;

		FImage Crop(CropW * 2 + 8, CropH, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FMemory::Memzero(Crop.RawData.GetData(), Crop.RawData.Num());
		for (int32 Y = 0; Y < CropH; ++Y)
		{
			for (int32 X = 0; X < CropW; ++X)
			{
				const int64 Source = (static_cast<int64>(Y0 + Y) * Width + X0 + X) * 4;
				uint8* Left = Crop.RawData.GetData() + (static_cast<int64>(Y) * Crop.SizeX + X) * 4;
				uint8* Right = Crop.RawData.GetData() + (static_cast<int64>(Y) * Crop.SizeX + X + CropW + 8) * 4;
				FMemory::Memcpy(Left, ImageA.RawData.GetData() + Source, 4);
				FMemory::Memcpy(Right, ImageB.RawData.GetData() + Source, 4);
				Left[3] = 255;
				Right[3] = 255;
			}
		}
		const bool bSaved = FImageUtils::SaveImageByExtension(*PngPath, Crop);
		Summary += bSaved ? TEXT(", crop written to ") + PngPath : TEXT(", crop could not be written");
	}
	return Summary;
}

FString UMetaHumanTattooLibrary::ProbeCharacter(UMetaHumanCharacter* Character)
{
	TArray<FString> Lines;
	if (!Character)
	{
		return TEXT("No character.");
	}

	FScopedCharacterEditing Editing(Character);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	Lines.Add(FString::Printf(TEXT("Character %s, editing ready %d (added by probe %d), preview material %s"), *Character->GetPathName(), Editing.bReady ? 1 : 0,
		Editing.bAdded ? 1 : 0, *StaticEnum<EMetaHumanCharacterSkinPreviewMaterial>()->GetNameStringByValue(static_cast<int64>(Character->PreviewMaterialType))));

	const TCHAR* Menus[] = { TEXT("AssetEditor.MetaHumanCharacterEditor.ToolBar"), TEXT("AssetEditor.MetaHumanCharacterAssetEditor.ToolBar"),
		TEXT("MetaHumanCharacterEditorTools.MainToolbar"), TEXT("AssetEditor.MetaHumanCharacterEditor.MainMenu") };
	for (const TCHAR* Menu : Menus)
	{
		Lines.Add(FString::Printf(TEXT("Menu %s registered %d"), Menu, UToolMenus::Get()->IsMenuRegistered(Menu) ? 1 : 0));
	}

	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			IAssetEditorInstance* Editor = AssetEditors->FindEditorForAsset(Character, false);
			Lines.Add(FString::Printf(TEXT("Asset editor open: %s"), Editor ? *Editor->GetEditorName().ToString() : TEXT("no")));
			if (Editor)
			{
				const TSharedPtr<FTabManager> TabManager = Editor->GetAssociatedTabManager();
				Lines.Add(FString::Printf(TEXT("Tattoo tab spawner registered %d"), TabManager.IsValid() && TabManager->HasTabSpawner(TEXT("MetaHumanCharacterEditor_TattoosTab")) ? 1 : 0));
			}
		}
	}

	if (!Editing.bReady || !Subsystem)
	{
		return FString::Join(Lines, TEXT("\n"));
	}

	int32 Actors = 0;
	const UWorld* PreviewWorld = nullptr;
	Subsystem->ForEachCharacterActor(Character, [&Actors, &PreviewWorld, &Lines](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
	{
		if (!Actor.GetInterface())
		{
			return;
		}
		++Actors;
		const USkeletalMeshComponent* Face = Actor->GetFaceComponent();
		const UWorld* World = Face->GetWorld();
		Lines.Add(FString::Printf(TEXT("Editor actor %s in world %s (type %d), face %s, body %s"), *Actor.GetObject()->GetName(), World ? *World->GetName() : TEXT("none"),
			World ? static_cast<int32>(World->WorldType.GetValue()) : -1, *Face->GetName(), *Actor->GetBodyComponent()->GetName()));
		if (!PreviewWorld)
		{
			PreviewWorld = World;
		}
	});
	Lines.Add(FString::Printf(TEXT("Editor actors: %d"), Actors));

	if (GEditor && PreviewWorld)
	{
		int32 Matching = 0;
		for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
		{
			if (Client && Client->GetWorld() == PreviewWorld)
			{
				++Matching;
				const FIntPoint Size = Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
				Lines.Add(FString::Printf(TEXT("Viewport client for the preview world: %dx%d"), Size.X, Size.Y));
			}
		}
		Lines.Add(FString::Printf(TEXT("Viewport clients for the preview world: %d of %d"), Matching, GEditor->GetAllViewportClients().Num()));
	}

	const USkeletalMesh* FaceMesh = Subsystem->GetFaceEditMesh(Character);
	const USkeletalMesh* BodyMesh = Subsystem->GetBodyEditMesh(Character);
	DescribeMesh(FaceMesh, true, Lines);
	DescribeMesh(BodyMesh, false, Lines);

	if (FaceMesh)
	{
		const FMetaHumanCharacterFaceMaterialSet FaceSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(const_cast<USkeletalMesh*>(FaceMesh));
		FaceSet.ForEachSkinMaterial<UMaterialInstance>([&Lines](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstance* Material)
		{
			if (Slot == EMetaHumanCharacterSkinMaterialSlot::LOD0)
			{
				DescribeMaterial(Material, TEXT("Head skin LOD0"), Lines);
			}
		});
	}
	if (BodyMesh)
	{
		for (const FSkeletalMaterial& Material : BodyMesh->GetMaterials())
		{
			if (Material.MaterialSlotName == FMetaHumanCharacterSkinMaterials::BodySlotName)
			{
				DescribeMaterial(Material.MaterialInterface, TEXT("Body skin"), Lines);
			}
		}
	}

	for (const EFaceTextureType Type : { EFaceTextureType::Basecolor, EFaceTextureType::Basecolor_Animated_CM1 })
	{
		Lines.Add(FString::Printf(TEXT("Face param %s: %s / VT %s"), *StaticEnum<EFaceTextureType>()->GetNameStringByValue(static_cast<int64>(Type)),
			*FMetaHumanCharacterSkinMaterials::GetFaceTextureParameterName(Type, false).ToString(), *FMetaHumanCharacterSkinMaterials::GetFaceTextureParameterName(Type, true).ToString()));
	}
	for (const EBodyTextureType Type : { EBodyTextureType::Body_Basecolor, EBodyTextureType::Body_Underwear_Basecolor, EBodyTextureType::Chest_Basecolor })
	{
		Lines.Add(FString::Printf(TEXT("Body param %s: %s / VT %s"), *StaticEnum<EBodyTextureType>()->GetNameStringByValue(static_cast<int64>(Type)),
			*FMetaHumanCharacterSkinMaterials::GetBodyTextureParameterName(Type, false).ToString(), *FMetaHumanCharacterSkinMaterials::GetBodyTextureParameterName(Type, true).ToString()));
	}

	const FMetaHumanCharacterTextureMaterialOverrides& Overrides = Character->SkinSettings.TextureMaterialOverrides;
	Lines.Add(FString::Printf(TEXT("Texture overrides enabled %d, material overrides enabled %d, inherit UI %d"), Overrides.bEnableTextureOverrides ? 1 : 0,
		Overrides.bEnableMaterialOverrides ? 1 : 0, Overrides.bInheritUIParamsAndSrcTextures ? 1 : 0));
	for (const TPair<EFaceTextureType, TSoftObjectPtr<UTexture2D>>& Pair : Overrides.TextureOverrides.Face)
	{
		Lines.Add(FString::Printf(TEXT("    face override %s = %s"), *StaticEnum<EFaceTextureType>()->GetNameStringByValue(static_cast<int64>(Pair.Key)), *Pair.Value.ToString()));
	}
	for (const TPair<EBodyTextureType, TSoftObjectPtr<UTexture2D>>& Pair : Overrides.TextureOverrides.Body)
	{
		Lines.Add(FString::Printf(TEXT("    body override %s = %s"), *StaticEnum<EBodyTextureType>()->GetNameStringByValue(static_cast<int64>(Pair.Key)), *Pair.Value.ToString()));
	}
	for (const TPair<EFaceTextureType, FMetaHumanCharacterTextureInfo>& Pair : Character->SynthesizedFaceTexturesInfo)
	{
		Lines.Add(FString::Printf(TEXT("    synthesized face %s: %dx%d format %d gamma %d, texture %s"), *StaticEnum<EFaceTextureType>()->GetNameStringByValue(static_cast<int64>(Pair.Key)),
			Pair.Value.SizeX, Pair.Value.SizeY, Pair.Value.Format, Pair.Value.GammaSpace, *DescribeTexture(Character->SynthesizedFaceTextures.FindRef(Pair.Key))));
	}
	for (const TPair<EBodyTextureType, TObjectPtr<UTexture2D>>& Pair : Character->BodyTextures)
	{
		Lines.Add(FString::Printf(TEXT("    body texture %s: %s (high-res info %d)"), *StaticEnum<EBodyTextureType>()->GetNameStringByValue(static_cast<int64>(Pair.Key)),
			*DescribeTexture(Pair.Value), Character->HighResBodyTexturesInfo.Contains(Pair.Key) ? 1 : 0));
	}

	return FString::Join(Lines, TEXT("\n"));
}
