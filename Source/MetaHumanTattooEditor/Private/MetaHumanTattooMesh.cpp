#include "MetaHumanTattooMesh.h"

#include "MetaHumanTattooLog.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"

#include "Async/ParallelFor.h"
#include "Engine/SkeletalMesh.h"
#include "GPUSkinPublicDefs.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

bool FMetaHumanTattooMesh::Build(const USkeletalMesh* Mesh, bool bInIsHead, FString& OutError)
{
	*this = FMetaHumanTattooMesh();
	bIsHead = bInIsHead;
	const TCHAR* MeshLabel = bIsHead ? TEXT("head") : TEXT("body");

	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("The character has no %s edit mesh."), MeshLabel);
		return false;
	}

	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (!Model || Model->LODModels.IsEmpty())
	{
		OutError = FString::Printf(TEXT("The %s edit mesh %s has no editable LOD model."), MeshLabel, *Mesh->GetName());
		return false;
	}

	const FSkeletalMeshLODModel& LOD = Model->LODModels[0];
	const int32 NumVertices = static_cast<int32>(LOD.NumVertices);
	if (NumVertices <= 0)
	{
		OutError = FString::Printf(TEXT("The %s edit mesh %s has no vertices in LOD 0."), MeshLabel, *Mesh->GetName());
		return false;
	}

	TSet<FName> SkinSlots;
	if (bIsHead)
	{
		for (EMetaHumanCharacterSkinMaterialSlot Slot : TEnumRange<EMetaHumanCharacterSkinMaterialSlot>())
		{
			SkinSlots.Add(FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(Slot));
		}
	}
	else
	{
		SkinSlots.Add(FMetaHumanCharacterSkinMaterials::BodySlotName);
	}

	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(0);
	const bool bHasUV1 = LOD.NumTexCoords > 1;

	Positions.SetNumZeroed(NumVertices);
	Normals.SetNumZeroed(NumVertices);
	UV0.SetNumZeroed(NumVertices);
	if (bHasUV1)
	{
		UV1.SetNumZeroed(NumVertices);
	}
	InfluencesPerVertex = MAX_TOTAL_INFLUENCES;
	InfluenceBones.SetNumZeroed(NumVertices * InfluencesPerVertex);
	InfluenceWeights.SetNumZeroed(NumVertices * InfluencesPerVertex);

	TArray<FName> AllSlotNames;
	for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
	{
		const FSkelMeshSection& Section = LOD.Sections[SectionIndex];

		for (int32 Local = 0; Local < Section.SoftVertices.Num(); ++Local)
		{
			const int32 Vertex = static_cast<int32>(Section.BaseVertexIndex) + Local;
			if (!Positions.IsValidIndex(Vertex))
			{
				continue;
			}

			const FSoftSkinVertex& Soft = Section.SoftVertices[Local];
			Positions[Vertex] = Soft.Position;
			Normals[Vertex] = FVector3f(Soft.TangentZ.X, Soft.TangentZ.Y, Soft.TangentZ.Z).GetSafeNormal();
			UV0[Vertex] = Soft.UVs[0];
			if (bHasUV1)
			{
				UV1[Vertex] = Soft.UVs[1];
			}

			for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
			{
				const uint16 Weight = Soft.InfluenceWeights[Influence];
				if (Weight == 0)
				{
					continue;
				}
				const uint16 LocalBone = Soft.InfluenceBones[Influence];
				InfluenceBones[Vertex * InfluencesPerVertex + Influence] = Section.BoneMap.IsValidIndex(LocalBone) ? Section.BoneMap[LocalBone] : 0;
				InfluenceWeights[Vertex * InfluencesPerVertex + Influence] = Weight / 65535.0f;
			}
		}

		int32 MaterialIndex = Section.MaterialIndex;
		if (LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex) && LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE)
		{
			MaterialIndex = LODInfo->LODMaterialMap[SectionIndex];
		}
		const FName SlotName = Materials.IsValidIndex(MaterialIndex) ? Materials[MaterialIndex].MaterialSlotName : NAME_None;
		AllSlotNames.AddUnique(SlotName);
		if (!SkinSlots.Contains(SlotName))
		{
			continue;
		}

		SkinSlotNames.AddUnique(SlotName);
		SkinTriangles.Reserve(SkinTriangles.Num() + Section.NumTriangles);
		for (uint32 Triangle = 0; Triangle < Section.NumTriangles; ++Triangle)
		{
			const int64 First = static_cast<int64>(Section.BaseIndex) + Triangle * 3;
			if (First + 2 >= LOD.IndexBuffer.Num())
			{
				break;
			}
			const FIntVector3 Indices(static_cast<int32>(LOD.IndexBuffer[First]), static_cast<int32>(LOD.IndexBuffer[First + 1]), static_cast<int32>(LOD.IndexBuffer[First + 2]));
			if (Positions.IsValidIndex(Indices.X) && Positions.IsValidIndex(Indices.Y) && Positions.IsValidIndex(Indices.Z))
			{
				SkinTriangles.Add(Indices);
			}
		}
	}

	if (SkinTriangles.IsEmpty())
	{
		TArray<FString> Names;
		for (const FName& Name : AllSlotNames)
		{
			Names.Add(Name.ToString());
		}
		OutError = FString::Printf(TEXT("The %s edit mesh %s has no skin section in LOD 0 (material slots: %s)."), MeshLabel, *Mesh->GetName(), *FString::Join(Names, TEXT(", ")));
		return false;
	}

	TMap<FIntPoint, int32> TileCounts;
	for (const FIntVector3& Tri : SkinTriangles)
	{
		const FVector2f Centroid = (UV0[Tri.X] + UV0[Tri.Y] + UV0[Tri.Z]) / 3.0f;
		TileCounts.FindOrAdd(FIntPoint(FMath::FloorToInt(Centroid.X), FMath::FloorToInt(Centroid.Y)))++;
	}
	int32 BestCount = -1;
	for (const TPair<FIntPoint, int32>& Pair : TileCounts)
	{
		if (Pair.Value > BestCount)
		{
			BestCount = Pair.Value;
			DominantTile = Pair.Key;
		}
	}

	return true;
}

void FMetaHumanTattooMesh::SkinPositions(const TArray<FMatrix44f>& RefToLocal, TArray<FVector3f>& OutPositions) const
{
	OutPositions.SetNumUninitialized(Positions.Num());
	ParallelFor(Positions.Num(), [&](int32 Vertex)
	{
		FVector3f Skinned(0.0f);
		float WeightSum = 0.0f;
		for (int32 Influence = 0; Influence < InfluencesPerVertex; ++Influence)
		{
			const float Weight = InfluenceWeights[Vertex * InfluencesPerVertex + Influence];
			if (Weight <= 0.0f)
			{
				continue;
			}
			const int32 Bone = InfluenceBones[Vertex * InfluencesPerVertex + Influence];
			if (!RefToLocal.IsValidIndex(Bone))
			{
				continue;
			}
			Skinned += FVector3f(RefToLocal[Bone].TransformPosition(Positions[Vertex])) * Weight;
			WeightSum += Weight;
		}
		OutPositions[Vertex] = WeightSum > KINDA_SMALL_NUMBER ? Skinned / WeightSum : Positions[Vertex];
	}, EParallelForFlags::Unbalanced);
}

bool FMetaHumanTattooMesh::Raycast(const FVector3f& Origin, const FVector3f& Direction, const TArray<FVector3f>* PosedPositions,
	float& InOutDistance, int32& OutTriangle, FVector3f& OutBarycentric) const
{
	const TArray<FVector3f>& Points = PosedPositions && PosedPositions->Num() == Positions.Num() ? *PosedPositions : Positions;
	bool bHit = false;

	for (int32 Triangle = 0; Triangle < SkinTriangles.Num(); ++Triangle)
	{
		const FIntVector3& Tri = SkinTriangles[Triangle];
		const FVector3f& A = Points[Tri.X];
		const FVector3f& B = Points[Tri.Y];
		const FVector3f& C = Points[Tri.Z];

		// Moller-Trumbore, two-sided.
		const FVector3f E1 = B - A;
		const FVector3f E2 = C - A;
		const FVector3f PVec = FVector3f::CrossProduct(Direction, E2);
		const float Det = FVector3f::DotProduct(E1, PVec);
		if (FMath::Abs(Det) < 1e-10f)
		{
			continue;
		}
		const float InvDet = 1.0f / Det;
		const FVector3f TVec = Origin - A;
		const float U = FVector3f::DotProduct(TVec, PVec) * InvDet;
		if (U < 0.0f || U > 1.0f)
		{
			continue;
		}
		const FVector3f QVec = FVector3f::CrossProduct(TVec, E1);
		const float V = FVector3f::DotProduct(Direction, QVec) * InvDet;
		if (V < 0.0f || U + V > 1.0f)
		{
			continue;
		}
		const float Distance = FVector3f::DotProduct(E2, QVec) * InvDet;
		if (Distance > 0.0f && Distance < InOutDistance)
		{
			InOutDistance = Distance;
			OutTriangle = Triangle;
			OutBarycentric = FVector3f(1.0f - U - V, U, V);
			bHit = true;
		}
	}
	return bHit;
}

void FMetaHumanTattooMesh::GetRestSurface(int32 Triangle, const FVector3f& Barycentric, FVector3f& OutPosition, FVector3f& OutNormal) const
{
	const FIntVector3& Tri = SkinTriangles[Triangle];
	OutPosition = Positions[Tri.X] * Barycentric.X + Positions[Tri.Y] * Barycentric.Y + Positions[Tri.Z] * Barycentric.Z;
	FVector3f Normal = Normals[Tri.X] * Barycentric.X + Normals[Tri.Y] * Barycentric.Y + Normals[Tri.Z] * Barycentric.Z;
	if (!Normal.Normalize())
	{
		Normal = FVector3f::CrossProduct(Positions[Tri.Z] - Positions[Tri.X], Positions[Tri.Y] - Positions[Tri.X]).GetSafeNormal();
	}
	OutNormal = Normal;
}

FString FMetaHumanTattooMesh::DescribeUVTiles(int32 Channel) const
{
	const TArray<FVector2f>& UVs = Channel == 0 ? UV0 : UV1;
	if (UVs.IsEmpty())
	{
		return TEXT("(no such UV channel)");
	}

	TMap<FIntPoint, FBox2f> Tiles;
	TMap<FIntPoint, int32> Counts;
	for (const FIntVector3& Tri : SkinTriangles)
	{
		const FVector2f Centroid = (UVs[Tri.X] + UVs[Tri.Y] + UVs[Tri.Z]) / 3.0f;
		const FIntPoint Tile(FMath::FloorToInt(Centroid.X), FMath::FloorToInt(Centroid.Y));
		FBox2f& Box = Tiles.FindOrAdd(Tile, FBox2f(ForceInit));
		Box += UVs[Tri.X];
		Box += UVs[Tri.Y];
		Box += UVs[Tri.Z];
		Counts.FindOrAdd(Tile)++;
	}

	FString Result;
	for (const TPair<FIntPoint, FBox2f>& Pair : Tiles)
	{
		Result += FString::Printf(TEXT("[tile %d,%d: %d tris, U %.3f..%.3f V %.3f..%.3f] "), Pair.Key.X, Pair.Key.Y, Counts[Pair.Key],
			Pair.Value.Min.X, Pair.Value.Max.X, Pair.Value.Min.Y, Pair.Value.Max.Y);
	}
	return Result;
}

bool FMetaHumanTattooCharacterMeshes::Build(UMetaHumanCharacter* Character, FString& OutError)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (!Character || !Subsystem)
	{
		OutError = TEXT("No character or no MetaHuman Character editor subsystem.");
		return false;
	}
	if (!Subsystem->IsObjectAddedForEditing(Character))
	{
		OutError = FString::Printf(TEXT("%s is not open for editing."), *Character->GetName());
		return false;
	}

	const USkeletalMesh* FaceMesh = Subsystem->GetFaceEditMesh(Character);
	const USkeletalMesh* BodyMesh = Subsystem->GetBodyEditMesh(Character);
	return Head.Build(FaceMesh, true, OutError) && Body.Build(BodyMesh, false, OutError);
}
