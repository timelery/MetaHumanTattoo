#pragma once

#include "CoreMinimal.h"

class UMetaHumanCharacter;

namespace UE::MetaHumanTattoo
{
	/** Opens the character's MetaHuman Character editor if needed and brings up its Tattoos tab. */
	bool OpenTattooTab(UMetaHumanCharacter* Character);
}
