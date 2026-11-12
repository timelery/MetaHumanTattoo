#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UMetaHumanCharacter;

/**
 * The skin of one MetaHuman edit mesh (head or body), read from LOD 0 of its editable LOD model:
 * rest-pose positions and normals, UVs, skin weights, and the triangles of the skin sections only
 * (eyes, teeth, eyelashes and the like are left out).
 */
struct FMetaHumanTattooMesh
{
	bool bIsHead = false;

	/** Rest-pose (bind pose) position of every LOD 0 vertex, mesh space, cm. */
	TArray<FVector3f> Positions;

	/** Rest-pose normal of every LOD 0 vertex. */
	TArray<FVector3f> Normals;

	/** UV channels 0 and 1 of every LOD 0 vertex (UV1 empty when the mesh has one channel). */
	TArray<FVector2f> UV0;
	TArray<FVector2f> UV1;

	/** Skin triangles (vertex indices into the arrays above). */
	TArray<FIntVector3> SkinTriangles;

	/** Names of the material slots treated as skin. */
	TArray<FName> SkinSlotNames;

	/** UV0 tile holding most skin triangles: (0,0) for the MetaHuman head, (1,0) for the MetaHuman body (UDIM 1001 / 1002). */
	FIntPoint DominantTile = FIntPoint(0, 0);

	/** Skin weights: MAX_TOTAL_INFLUENCES entries per vertex (mesh bone index, weight 0..1). */
	TArray<uint16> InfluenceBones;
	TArray<float> InfluenceWeights;
	int32 InfluencesPerVertex = 0;

	bool IsValid() const
	{
		return Positions.Num() > 0 && SkinTriangles.Num() > 0;
	}

	/** Reads the mesh. Returns false (with OutError) when the mesh has no editable LOD 0 or no skin section. */
	bool Build(const USkeletalMesh* Mesh, bool bInIsHead, FString& OutError);

	/** Skins the rest positions with per-bone ref-to-local matrices (from USkinnedMeshComponent::CacheRefToLocalMatrices). */
	void SkinPositions(const TArray<FMatrix44f>& RefToLocal, TArray<FVector3f>& OutPositions) const;

	/**
	 * Nearest skin triangle hit by a ray against the given vertex positions (rest positions when null).
	 * Updates InOutDistance only when the hit is nearer. Returns true on a nearer hit.
	 */
	bool Raycast(const FVector3f& Origin, const FVector3f& Direction, const TArray<FVector3f>* PosedPositions,
		float& InOutDistance, int32& OutTriangle, FVector3f& OutBarycentric) const;

	/** Rest-pose position and normal at a point on a skin triangle. */
	void GetRestSurface(int32 Triangle, const FVector3f& Barycentric, FVector3f& OutPosition, FVector3f& OutNormal) const;

	/** Bounds of each UV tile that skin triangles use, for diagnostics. */
	FString DescribeUVTiles(int32 Channel) const;
};

/** Head and body skin of a MetaHuman Character, read from the MetaHuman Character editor subsystem's edit meshes. */
struct FMetaHumanTattooCharacterMeshes
{
	FMetaHumanTattooMesh Head;
	FMetaHumanTattooMesh Body;

	/** The character must be registered for editing (UMetaHumanCharacterEditorSubsystem::TryAddObjectToEdit). */
	bool Build(UMetaHumanCharacter* Character, FString& OutError);
};
