#include "MetaHumanTattooSet.h"

bool UMetaHumanTattooSet::HasUnappliedChanges() const
{
	if (Layers.Num() != AppliedLayers.Num())
	{
		return true;
	}

	UScriptStruct* LayerStruct = FMetaHumanTattooLayer::StaticStruct();
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		if (!LayerStruct->CompareScriptStruct(&Layers[Index], &AppliedLayers[Index], PPF_None))
		{
			return true;
		}
	}
	return false;
}
