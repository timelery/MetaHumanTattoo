#include "MetaHumanTattooSession.h"

#include "MetaHumanTattooLog.h"
#include "MetaHumanTattooSet.h"
#include "MetaHumanTattooTypes.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"

#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "SceneView.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace
{
	/** Longest side of the live preview textures. */
	constexpr int32 PreviewTextureSize = 2048;

	/** Minimum time between preview rebuilds while dragging. */
	constexpr double PreviewInterval = 1.0 / 20.0;

	UMetaHumanCharacterEditorSubsystem* GetSubsystem()
	{
		return UMetaHumanCharacterEditorSubsystem::Get();
	}
}

TArray<TWeakPtr<FMetaHumanTattooSession>>& FMetaHumanTattooSession::GetAllSessions()
{
	static TArray<TWeakPtr<FMetaHumanTattooSession>> Sessions;
	return Sessions;
}

TSharedPtr<FMetaHumanTattooSession> FMetaHumanTattooSession::FindSession(const UMetaHumanCharacter* InCharacter)
{
	for (const TWeakPtr<FMetaHumanTattooSession>& Weak : GetAllSessions())
	{
		const TSharedPtr<FMetaHumanTattooSession> Session = Weak.Pin();
		if (Session.IsValid() && Session->GetCharacter() == InCharacter)
		{
			return Session;
		}
	}
	return nullptr;
}

bool FMetaHumanTattooSession::GetViewportScreenRect(FSlateRect& OutRect) const
{
	const USkeletalMeshComponent* Face = nullptr;
	const USkeletalMeshComponent* Body = nullptr;
	if (!GEditor || !GetPreviewComponents(Face, Body))
	{
		return false;
	}
	for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
	{
		if (!Client || Client->GetWorld() != Face->GetWorld() || !Client->Viewport)
		{
			continue;
		}
		const TSharedPtr<SViewport> Widget = static_cast<FSceneViewport*>(Client->Viewport)->GetViewportWidget().Pin();
		if (Widget.IsValid())
		{
			OutRect = Widget->GetTickSpaceGeometry().GetLayoutBoundingRect();
			return true;
		}
	}
	return false;
}

FString FMetaHumanTattooSession::DescribePreview() const
{
	int32 ValidSlots = 0;
	for (const FPreviewSlot& Slot : PreviewSlots)
	{
		ValidSlots += Slot.bValid ? 1 : 0;
	}
	int32 LiveBindings = 0;
	for (const FBinding& Binding : Bindings)
	{
		LiveBindings += Binding.Material.IsValid() ? 1 : 0;
	}
	return FString::Printf(TEXT("unapplied %d, preview slots %d/%d, bound skin materials %d, placing %d, warning: %s"),
		HasUnappliedChanges() ? 1 : 0, ValidSlots, PreviewSlots.Num(), LiveBindings, bPlacing ? 1 : 0,
		PreviewWarning.IsEmpty() ? TEXT("none") : *PreviewWarning);
}

FMetaHumanTattooSession::FMetaHumanTattooSession(UMetaHumanCharacter* InCharacter)
	: Character(InCharacter)
{
	Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(InCharacter, false);
	if (Set && Set->Layers.Num() > 0)
	{
		SelectedIndex = 0;
	}
}

FMetaHumanTattooSession::~FMetaHumanTattooSession()
{
	Shutdown();
}

void FMetaHumanTattooSession::Initialize()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	GetAllSessions().RemoveAll([](const TWeakPtr<FMetaHumanTattooSession>& Weak) { return !Weak.IsValid(); });
	GetAllSessions().Add(AsShared());

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FMetaHumanTattooSession::Tick), 0.0f);

	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem();
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	if (Subsystem && EditedCharacter && Subsystem->IsObjectAddedForEditing(EditedCharacter))
	{
		TWeakPtr<FMetaHumanTattooSession> WeakThis = AsShared();
		FaceStateHandle = Subsystem->OnFaceStateChanged(EditedCharacter).AddLambda([WeakThis]()
		{
			if (TSharedPtr<FMetaHumanTattooSession> This = WeakThis.Pin())
			{
				This->bMeshesDirty = true;
				This->RequestPreview();
			}
		});
		BodyStateHandle = Subsystem->OnBodyStateChanged(EditedCharacter).AddLambda([WeakThis]()
		{
			if (TSharedPtr<FMetaHumanTattooSession> This = WeakThis.Pin())
			{
				This->bMeshesDirty = true;
				This->RequestPreview();
			}
		});
		PreviewMaterialHandle = Subsystem->OnPreviewMaterialChanged(EditedCharacter).AddLambda([WeakThis]()
		{
			if (TSharedPtr<FMetaHumanTattooSession> This = WeakThis.Pin())
			{
				// The editor made new skin materials: forget the old bindings, they belong to discarded instances.
				This->Bindings.Reset();
				This->RequestPreview();
			}
		});
	}

	// An editor reopened with unapplied tattoo edits shows them straight away.
	RequestPreview();
}

void FMetaHumanTattooSession::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}
	bInitialized = false;
	bPlacing = false;

	RestoreBindings();

	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem();
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	if (Subsystem && EditedCharacter && Subsystem->IsObjectAddedForEditing(EditedCharacter))
	{
		Subsystem->OnFaceStateChanged(EditedCharacter).Remove(FaceStateHandle);
		Subsystem->OnBodyStateChanged(EditedCharacter).Remove(BodyStateHandle);
		Subsystem->OnPreviewMaterialChanged(EditedCharacter).Remove(PreviewMaterialHandle);
	}

	GetAllSessions().RemoveAll([this](const TWeakPtr<FMetaHumanTattooSession>& Weak)
	{
		return !Weak.IsValid() || Weak.Pin().Get() == this;
	});
}

void FMetaHumanTattooSession::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Set);
	for (FPreviewSlot& Slot : PreviewSlots)
	{
		Collector.AddReferencedObject(Slot.Texture);
	}
}

int32 FMetaHumanTattooSession::GetNumLayers() const
{
	return Set ? Set->Layers.Num() : 0;
}

FMetaHumanTattooLayer* FMetaHumanTattooSession::GetLayer(int32 Index) const
{
	return Set && Set->Layers.IsValidIndex(Index) ? &Set->Layers[Index] : nullptr;
}

void FMetaHumanTattooSession::SetSelectedIndex(int32 Index)
{
	const int32 NewIndex = Set && Set->Layers.IsValidIndex(Index) ? Index : INDEX_NONE;
	if (NewIndex != SelectedIndex)
	{
		SelectedIndex = NewIndex;
		OnLayersChanged.Broadcast();
	}
}

UMetaHumanTattooSet* FMetaHumanTattooSession::GetOrCreateSet()
{
	if (!Set)
	{
		Set = UE::MetaHumanTattoo::FindOrCreateTattooSet(Character.Get(), true);
	}
	return Set;
}

int32 FMetaHumanTattooSession::AddLayerFromFile(const FString& FilePath, FString& OutError)
{
	UMetaHumanTattooSet* TattooSet = GetOrCreateSet();
	if (!TattooSet)
	{
		OutError = TEXT("Could not create the tattoo set for this character.");
		return INDEX_NONE;
	}

	FMetaHumanTattooLayer Layer;
	if (!UE::MetaHumanTattoo::LoadSvgIntoLayer(FilePath, Layer, OutError))
	{
		return INDEX_NONE;
	}

	TattooSet->Modify();
	const int32 Index = TattooSet->Layers.Add(MoveTemp(Layer));
	TattooSet->MarkPackageDirty();
	SelectedIndex = Index;
	OnLayersChanged.Broadcast();
	RequestPreview();
	return Index;
}

void FMetaHumanTattooSession::DuplicateSelected()
{
	FMetaHumanTattooLayer* Selected = GetSelectedLayer();
	if (!Selected)
	{
		return;
	}

	FMetaHumanTattooLayer Copy = *Selected;
	Copy.Name += TEXT(" copy");
	// Offset the copy along the skin so it does not sit exactly on top of the original.
	if (Copy.Placement.bPlaced)
	{
		FVector3f Normal, Right, Up;
		UE::MetaHumanTattoo::ComputeFrame(Copy, Normal, Right, Up);
		Copy.Placement.Location += FVector(Right) * (Copy.WidthCm * 0.5);
	}

	Set->Modify();
	SelectedIndex = Set->Layers.Insert(MoveTemp(Copy), SelectedIndex + 1);
	Set->MarkPackageDirty();
	OnLayersChanged.Broadcast();
	RequestPreview();
}

void FMetaHumanTattooSession::DeleteSelected()
{
	if (!GetSelectedLayer())
	{
		return;
	}

	Set->Modify();
	Set->Layers.RemoveAt(SelectedIndex);
	Set->MarkPackageDirty();
	SelectedIndex = FMath::Min(SelectedIndex, Set->Layers.Num() - 1);
	OnLayersChanged.Broadcast();
	RequestPreview();
}

void FMetaHumanTattooSession::MoveSelected(int32 Delta)
{
	const int32 Target = SelectedIndex + Delta;
	if (!GetSelectedLayer() || !Set->Layers.IsValidIndex(Target))
	{
		return;
	}

	Set->Modify();
	Set->Layers.Swap(SelectedIndex, Target);
	Set->MarkPackageDirty();
	SelectedIndex = Target;
	OnLayersChanged.Broadcast();
	RequestPreview();
}

void FMetaHumanTattooSession::NotifyLayerEdited(int32 Index, bool bReloadSvg, FString* OutError)
{
	FMetaHumanTattooLayer* Layer = GetLayer(Index);
	if (!Layer)
	{
		return;
	}

	if (bReloadSvg && !Layer->SvgFile.FilePath.IsEmpty())
	{
		FString Error;
		const FString Path = FPaths::ConvertRelativePathToFull(Layer->SvgFile.FilePath);
		if (!UE::MetaHumanTattoo::LoadSvgIntoLayer(Path, *Layer, Error))
		{
			UE_LOG(LogMetaHumanTattoo, Warning, TEXT("%s"), *Error);
			if (OutError)
			{
				*OutError = Error;
			}
		}
	}

	Set->MarkPackageDirty();
	RequestPreview();
}

void FMetaHumanTattooSession::SetPlacing(bool bInPlacing)
{
	bPlacing = bInPlacing && GetSelectedLayer() != nullptr;
	OnLayersChanged.Broadcast();
}

bool FMetaHumanTattooSession::GetPreviewComponents(const USkeletalMeshComponent*& OutFace, const USkeletalMeshComponent*& OutBody) const
{
	OutFace = nullptr;
	OutBody = nullptr;
	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem();
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	if (!Subsystem || !EditedCharacter || !Subsystem->IsObjectAddedForEditing(EditedCharacter))
	{
		return false;
	}

	Subsystem->ForEachCharacterActor(EditedCharacter, [&OutFace, &OutBody](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
	{
		if (OutFace || !Actor.GetInterface())
		{
			return;
		}
		const USkeletalMeshComponent* Face = Actor->GetFaceComponent();
		const USkeletalMeshComponent* Body = Actor->GetBodyComponent();
		const UWorld* World = Face ? Face->GetWorld() : nullptr;
		if (World && (World->WorldType == EWorldType::EditorPreview || World->WorldType == EWorldType::Editor))
		{
			OutFace = Face;
			OutBody = Body;
		}
	});
	return OutFace != nullptr && OutBody != nullptr;
}

bool FMetaHumanTattooSession::FindViewport(const FVector2D& ScreenPosition, FEditorViewportClient*& OutClient, FVector2D& OutPixel) const
{
	OutClient = nullptr;
	const USkeletalMeshComponent* Face = nullptr;
	const USkeletalMeshComponent* Body = nullptr;
	if (!GEditor || !FSlateApplication::IsInitialized() || !GetPreviewComponents(Face, Body))
	{
		return false;
	}

	const UWorld* PreviewWorld = Face->GetWorld();
	for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
	{
		if (!Client || Client->GetWorld() != PreviewWorld || !Client->Viewport)
		{
			continue;
		}

		FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(Client->Viewport);
		const TSharedPtr<SViewport> Widget = SceneViewport->GetViewportWidget().Pin();
		if (!Widget.IsValid())
		{
			continue;
		}

		const FGeometry& Geometry = Widget->GetTickSpaceGeometry();
		if (!Geometry.IsUnderLocation(ScreenPosition))
		{
			continue;
		}

		// Ignore positions where another window or a menu covers the viewport.
		const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(ScreenPosition, FSlateApplication::Get().GetInteractiveTopLevelWindows());
		if (!Path.IsValid() || !Path.ContainsWidget(Widget.Get()))
		{
			continue;
		}

		const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
		const FVector2D LocalSize = Geometry.GetLocalSize();
		const FIntPoint Size = Client->Viewport->GetSizeXY();
		if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0 || Size.X <= 0 || Size.Y <= 0)
		{
			continue;
		}

		OutClient = Client;
		OutPixel = FVector2D(Local.X * Size.X / LocalSize.X, Local.Y * Size.Y / LocalSize.Y);
		return true;
	}
	return false;
}

bool FMetaHumanTattooSession::IsOverViewport(const FVector2D& ScreenPosition) const
{
	FEditorViewportClient* Client = nullptr;
	FVector2D Pixel;
	return FindViewport(ScreenPosition, Client, Pixel);
}

bool FMetaHumanTattooSession::EnsureMeshes(FString& OutError)
{
	if (Meshes.IsSet() && !bMeshesDirty)
	{
		return true;
	}

	FMetaHumanTattooCharacterMeshes NewMeshes;
	if (!NewMeshes.Build(Character.Get(), OutError))
	{
		Meshes.Reset();
		return false;
	}
	Meshes = MoveTemp(NewMeshes);
	bMeshesDirty = false;
	return true;
}

bool FMetaHumanTattooSession::RaycastPreview(const FVector& Origin, const FVector& Direction, const FVector& CameraUp,
	FVector3f& OutLocation, FVector3f& OutNormal, FVector3f& OutUp)
{
	const USkeletalMeshComponent* Face = nullptr;
	const USkeletalMeshComponent* Body = nullptr;
	FString Error;
	if (!GetPreviewComponents(Face, Body) || !EnsureMeshes(Error))
	{
		return false;
	}

	struct FCandidate
	{
		const FMetaHumanTattooMesh* Mesh;
		const USkeletalMeshComponent* Component;
	};
	const FCandidate Candidates[2] = { { &Meshes->Head, Face }, { &Meshes->Body, Body } };

	double BestDistance = TNumericLimits<double>::Max();
	const FMetaHumanTattooMesh* HitMesh = nullptr;
	const USkeletalMeshComponent* HitComponent = nullptr;
	TArray<FMatrix44f> HitRefToLocal;
	int32 HitTriangle = INDEX_NONE;
	FVector3f HitBarycentric;

	for (const FCandidate& Candidate : Candidates)
	{
		TArray<FMatrix44f> RefToLocal;
		Candidate.Component->CacheRefToLocalMatrices(RefToLocal);
		TArray<FVector3f> Posed;
		Candidate.Mesh->SkinPositions(RefToLocal, Posed);

		const FTransform& ComponentTransform = Candidate.Component->GetComponentTransform();
		const FVector3f LocalOrigin(ComponentTransform.InverseTransformPosition(Origin));
		const FVector3f LocalDirection = FVector3f(ComponentTransform.InverseTransformVector(Direction)).GetSafeNormal();

		float LocalDistance = TNumericLimits<float>::Max();
		int32 Triangle = INDEX_NONE;
		FVector3f Barycentric;
		if (Candidate.Mesh->Raycast(LocalOrigin, LocalDirection, &Posed, LocalDistance, Triangle, Barycentric))
		{
			const FVector WorldHit = ComponentTransform.TransformPosition(FVector(LocalOrigin + LocalDirection * LocalDistance));
			const double Distance = FVector::Dist(WorldHit, Origin);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				HitMesh = Candidate.Mesh;
				HitComponent = Candidate.Component;
				HitRefToLocal = MoveTemp(RefToLocal);
				HitTriangle = Triangle;
				HitBarycentric = Barycentric;
			}
		}
	}

	if (!HitMesh)
	{
		return false;
	}

	HitMesh->GetRestSurface(HitTriangle, HitBarycentric, OutLocation, OutNormal);

	// Bring the camera's up direction into rest pose through the skinning at the hit, so a posed character still gets an
	// upright tattoo.
	FVector3f LocalUp(HitComponent->GetComponentTransform().InverseTransformVector(CameraUp));
	const int32 Vertex = HitMesh->SkinTriangles[HitTriangle].X;
	FMatrix44f Blend(ForceInitToZero);
	float WeightSum = 0.0f;
	for (int32 Influence = 0; Influence < HitMesh->InfluencesPerVertex; ++Influence)
	{
		const float Weight = HitMesh->InfluenceWeights[Vertex * HitMesh->InfluencesPerVertex + Influence];
		const int32 Bone = HitMesh->InfluenceBones[Vertex * HitMesh->InfluencesPerVertex + Influence];
		if (Weight > 0.0f && HitRefToLocal.IsValidIndex(Bone))
		{
			Blend += HitRefToLocal[Bone] * Weight;
			WeightSum += Weight;
		}
	}
	if (WeightSum > KINDA_SMALL_NUMBER)
	{
		LocalUp = FVector3f(Blend.InverseTransformVector(LocalUp));
	}
	OutUp = LocalUp.GetSafeNormal();
	return true;
}

bool FMetaHumanTattooSession::PlaceAtScreenPosition(const FVector2D& ScreenPosition, bool bNewPlacement)
{
	FMetaHumanTattooLayer* Layer = GetSelectedLayer();
	if (!Layer)
	{
		return false;
	}

	FEditorViewportClient* Client = nullptr;
	FVector2D Pixel;
	if (!FindViewport(ScreenPosition, Client, Pixel))
	{
		return false;
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags).SetRealtimeUpdate(true));
	FSceneView* View = Client->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}
	const FViewportCursorLocation Cursor(View, Client, FMath::RoundToInt(Pixel.X), FMath::RoundToInt(Pixel.Y));

	FVector3f Location, Normal, Up;
	if (!RaycastPreview(Cursor.GetOrigin(), Cursor.GetDirection(), View->GetViewUp(), Location, Normal, Up))
	{
		return false;
	}

	Layer->Placement.Location = FVector(Location);
	Layer->Placement.Normal = FVector(Normal);
	// A new placement starts upright as seen from the camera; dragging keeps the tattoo's own up direction.
	if (bNewPlacement || !Layer->Placement.bPlaced)
	{
		Layer->Placement.Up = FVector(Up);
	}
	Layer->Placement.bPlaced = true;
	Set->MarkPackageDirty();
	OnLayersChanged.Broadcast();
	RequestPreview();
	return true;
}

void FMetaHumanTattooSession::AdjustSelected(float ScaleFactor, float RotationDeltaDegrees)
{
	FMetaHumanTattooLayer* Layer = GetSelectedLayer();
	if (!Layer)
	{
		return;
	}

	Layer->WidthCm = FMath::Clamp(Layer->WidthCm * ScaleFactor, 0.5f, 200.0f);
	Layer->RotationDegrees = FMath::Fmod(Layer->RotationDegrees + RotationDeltaDegrees + 540.0f, 360.0f) - 180.0f;
	Set->MarkPackageDirty();
	OnLayersChanged.Broadcast();
	RequestPreview();
}

bool FMetaHumanTattooSession::HasUnappliedChanges() const
{
	return Set && Set->HasUnappliedChanges();
}

bool FMetaHumanTattooSession::HasAppliedTattoos() const
{
	return Set && Set->HasAppliedTattoos();
}

bool FMetaHumanTattooSession::NeedsSkinPreview() const
{
	const UMetaHumanCharacter* EditedCharacter = Character.Get();
	return EditedCharacter && EditedCharacter->PreviewMaterialType != EMetaHumanCharacterSkinPreviewMaterial::Editable;
}

void FMetaHumanTattooSession::SwitchToSkinPreview()
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem();
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	if (Subsystem && EditedCharacter && Subsystem->IsObjectAddedForEditing(EditedCharacter))
	{
		Subsystem->UpdateCharacterPreviewMaterial(EditedCharacter, EMetaHumanCharacterSkinPreviewMaterial::Editable);
		Bindings.Reset();
		RequestPreview();
	}
}

bool FMetaHumanTattooSession::SkinChangedSinceApply() const
{
	const UMetaHumanCharacter* EditedCharacter = Character.Get();
	return Set && EditedCharacter && Set->HasAppliedTattoos() && Set->bHasAppliedSkinProperties
		&& !Set->AppliedSkinProperties.EqualForTextureSynthesis(EditedCharacter->SkinSettings.Skin);
}

bool FMetaHumanTattooSession::Apply(FString& OutReport)
{
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	UMetaHumanTattooSet* TattooSet = GetOrCreateSet();
	if (!EditedCharacter || !TattooSet)
	{
		OutReport = TEXT("No character or tattoo set.");
		return false;
	}

	bPlacing = false;
	RestoreBindings();
	const bool bResult = UE::MetaHumanTattoo::ApplyTattoos(EditedCharacter, TattooSet, true, OutReport);
	InvalidatePreviewBases();
	OnLayersChanged.Broadcast();
	return bResult;
}

void FMetaHumanTattooSession::DiscardChanges()
{
	if (!Set)
	{
		return;
	}
	Set->Modify();
	Set->Layers = Set->AppliedLayers;
	Set->MarkPackageDirty();
	SelectedIndex = Set->Layers.IsValidIndex(SelectedIndex) ? SelectedIndex : (Set->Layers.Num() > 0 ? 0 : INDEX_NONE);
	bPlacing = false;
	OnLayersChanged.Broadcast();
	RequestPreview();
}

bool FMetaHumanTattooSession::RemoveAll(FString& OutReport)
{
	if (!Set)
	{
		OutReport = TEXT("This character has no tattoos.");
		return true;
	}
	Set->Modify();
	Set->Layers.Reset();
	SelectedIndex = INDEX_NONE;
	return Apply(OutReport);
}

void FMetaHumanTattooSession::RequestPreview()
{
	bPreviewDirty = true;
}

bool FMetaHumanTattooSession::Tick(float DeltaTime)
{
	if (!Character.IsValid())
	{
		return true;
	}

	const double Now = FPlatformTime::Seconds();
	if (bPreviewDirty && Now - LastPreviewSeconds >= PreviewInterval)
	{
		bPreviewDirty = false;
		LastPreviewSeconds = Now;
		RebuildPreview();
	}
	return true;
}

void FMetaHumanTattooSession::InvalidatePreviewBases()
{
	PreviewSlots.Reset();
	bPreviewSlotsLoaded = false;
}

void FMetaHumanTattooSession::LoadPreviewSlots()
{
	PreviewSlots.Reset();
	bPreviewSlotsLoaded = true;

	for (const UE::MetaHumanTattoo::FSkinTextureSlot& Slot : UE::MetaHumanTattoo::GetSkinTextureSlots(true))
	{
		FPreviewSlot& Preview = PreviewSlots.AddDefaulted_GetRef();
		Preview.Slot = Slot;

		UE::MetaHumanTattoo::FSlotBase Base;
		FString Error;
		if (!UE::MetaHumanTattoo::ReadSlotBase(Character.Get(), Set, Slot, Base, Error))
		{
			UE_LOG(LogMetaHumanTattoo, Verbose, TEXT("No live preview for %s"), *Error);
			continue;
		}
		if (Base.bToneEncoded)
		{
			// Tone data, not color: tattoos are not drawn into it (see ApplyTattoos), so the preview leaves it alone too.
			continue;
		}

		const int32 LongSide = FMath::Max(Base.Image.SizeX, Base.Image.SizeY);
		if (LongSide > PreviewTextureSize)
		{
			const float Scale = static_cast<float>(PreviewTextureSize) / LongSide;
			const int32 Width = FMath::Max(1, FMath::RoundToInt(Base.Image.SizeX * Scale));
			const int32 Height = FMath::Max(1, FMath::RoundToInt(Base.Image.SizeY * Scale));
			FImageCore::ResizeImageAllocDest(Base.Image, Preview.Base, Width, Height);
		}
		else
		{
			Preview.Base = MoveTemp(Base.Image);
		}
		if (Preview.Base.Format != ERawImageFormat::BGRA8)
		{
			Preview.Base.ChangeFormat(ERawImageFormat::BGRA8, Preview.Base.GammaSpace == EGammaSpace::Linear ? EGammaSpace::Linear : EGammaSpace::sRGB);
		}

		Preview.Texture = UTexture2D::CreateTransient(Preview.Base.SizeX, Preview.Base.SizeY, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("TattooPreview")));
		if (!Preview.Texture)
		{
			continue;
		}
		Preview.Texture->SRGB = Preview.Base.GammaSpace != EGammaSpace::Linear;
		Preview.Texture->NeverStream = true;
		Preview.Texture->UpdateResource();
		Preview.bValid = true;
	}
}

void FMetaHumanTattooSession::RebuildPreview()
{
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem();
	if (!EditedCharacter || !Subsystem || !Subsystem->IsObjectAddedForEditing(EditedCharacter))
	{
		return;
	}

	// Without unapplied edits the editor's own (baked) textures are exactly right: show those.
	if (!HasUnappliedChanges())
	{
		RestoreBindings();
		PreviewWarning.Reset();
		return;
	}

	FString Error;
	if (!EnsureMeshes(Error))
	{
		PreviewWarning = Error;
		return;
	}
	if (!bPreviewSlotsLoaded)
	{
		LoadPreviewSlots();
	}

	TArray<UE::MetaHumanTattoo::FPreparedTattoo> Prepared;
	for (const FMetaHumanTattooLayer& Layer : Set->Layers)
	{
		UE::MetaHumanTattoo::FPreparedTattoo Tattoo;
		if (UE::MetaHumanTattoo::PrepareTattoo(Layer, true, Tattoo, Error))
		{
			Prepared.Add(MoveTemp(Tattoo));
		}
	}

	for (FPreviewSlot& Preview : PreviewSlots)
	{
		if (!Preview.bValid || !Preview.Texture)
		{
			continue;
		}

		Preview.Work = Preview.Base;
		const FMetaHumanTattooMesh& Mesh = Preview.Slot.bHeadMesh ? Meshes->Head : Meshes->Body;
		UE::MetaHumanTattoo::CompositeTattoos(Mesh, Preview.Slot, Prepared, Preview.Work, false);

		const int32 Width = Preview.Work.SizeX;
		const int32 Height = Preview.Work.SizeY;
		const int64 Bytes = static_cast<int64>(Width) * Height * 4;
		uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
		FMemory::Memcpy(Copy, Preview.Work.RawData.GetData(), Bytes);
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
		Preview.Texture->UpdateTextureRegions(0, 1, Region, Width * 4, 4, Copy, [](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
	}

	BindPreview();
}

void FMetaHumanTattooSession::BindPreview()
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem();
	UMetaHumanCharacter* EditedCharacter = Character.Get();
	if (!Subsystem || !EditedCharacter)
	{
		return;
	}

	const USkeletalMesh* ConstFaceMesh = Subsystem->GetFaceEditMesh(EditedCharacter);
	const USkeletalMesh* ConstBodyMesh = Subsystem->GetBodyEditMesh(EditedCharacter);
	USkeletalMesh* FaceMesh = const_cast<USkeletalMesh*>(ConstFaceMesh);
	USkeletalMesh* BodyMesh = const_cast<USkeletalMesh*>(ConstBodyMesh);

	TArray<UMaterialInstanceDynamic*> FaceMaterials;
	if (FaceMesh)
	{
		const FMetaHumanCharacterFaceMaterialSet FaceSet = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(FaceMesh);
		FaceSet.ForEachSkinMaterial<UMaterialInstanceDynamic>([&FaceMaterials](EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceDynamic* Material)
		{
			FaceMaterials.AddUnique(Material);
		});
	}
	TArray<UMaterialInstanceDynamic*> BodyMaterials;
	if (BodyMesh)
	{
		for (const FSkeletalMaterial& Material : BodyMesh->GetMaterials())
		{
			if (Material.MaterialSlotName == FMetaHumanCharacterSkinMaterials::BodySlotName)
			{
				if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material.MaterialInterface))
				{
					BodyMaterials.AddUnique(Dynamic);
				}
			}
		}
	}

	TArray<FString> Problems;
	for (int32 SlotIndex = 0; SlotIndex < PreviewSlots.Num(); ++SlotIndex)
	{
		FPreviewSlot& Preview = PreviewSlots[SlotIndex];
		if (!Preview.bValid || !Preview.Texture)
		{
			continue;
		}

		const FName NonVirtual = Preview.Slot.bFaceTexture
			? FMetaHumanCharacterSkinMaterials::GetFaceTextureParameterName(Preview.Slot.FaceType, false)
			: FMetaHumanCharacterSkinMaterials::GetBodyTextureParameterName(Preview.Slot.BodyType, false);
		const FName Virtual = Preview.Slot.bFaceTexture
			? FMetaHumanCharacterSkinMaterials::GetFaceTextureParameterName(Preview.Slot.FaceType, true)
			: FMetaHumanCharacterSkinMaterials::GetBodyTextureParameterName(Preview.Slot.BodyType, true);

		for (UMaterialInstanceDynamic* Material : Preview.Slot.bHeadMesh ? FaceMaterials : BodyMaterials)
		{
			// The skin materials carry both a regular and a virtual texture parameter for each map; the unused one holds one of
			// MetaHuman's placeholder textures. Only a real virtual texture in the virtual parameter means virtual texturing is in use.
			UTexture* VirtualValue = nullptr;
			const bool bHasVirtual = Virtual != NonVirtual && Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Virtual), VirtualValue);
			const bool bVirtualIsPlaceholder = VirtualValue && VirtualValue->GetPathName().Contains(TEXT("/Placeholders/"));
			if (bHasVirtual && VirtualValue && VirtualValue->VirtualTextureStreaming && !bVirtualIsPlaceholder)
			{
				Problems.AddUnique(FString::Printf(TEXT("%s uses virtual textures: the live preview cannot show tattoos there; Apply to Skin to see them."), *Preview.Slot.GetName()));
				continue;
			}

			UTexture* Current = nullptr;
			if (!Material->GetTextureParameterValue(FHashedMaterialParameterInfo(NonVirtual), Current))
			{
				continue;
			}

			FBinding* Binding = Bindings.FindByPredicate([Material, &NonVirtual](const FBinding& Existing)
			{
				return Existing.Material.Get() == Material && Existing.Parameter == NonVirtual;
			});
			if (!Binding)
			{
				Binding = &Bindings.AddDefaulted_GetRef();
				Binding->Material = Material;
				Binding->Parameter = NonVirtual;
				Binding->SlotIndex = SlotIndex;
				Binding->Original = Current;
			}
			else if (Current != Preview.Texture)
			{
				// The editor put its own texture back (skin edit): that is the new original.
				Binding->Original = Current;
			}

			if (Current != Preview.Texture)
			{
				Material->SetTextureParameterValue(NonVirtual, Preview.Texture);
			}
		}
	}

	PreviewWarning = FString::Join(Problems, TEXT("\n"));
}

void FMetaHumanTattooSession::RestoreBindings()
{
	for (const FBinding& Binding : Bindings)
	{
		UMaterialInstanceDynamic* Material = Binding.Material.Get();
		if (!Material)
		{
			continue;
		}
		UTexture* Current = nullptr;
		Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Binding.Parameter), Current);
		const bool bShowsPreview = PreviewSlots.IsValidIndex(Binding.SlotIndex) && Current == PreviewSlots[Binding.SlotIndex].Texture;
		if (bShowsPreview)
		{
			Material->SetTextureParameterValue(Binding.Parameter, Binding.Original.Get());
		}
	}
	Bindings.Reset();
}
