#pragma once

#include "CustomBot.h"

#include "CustomBotInventory.h"


namespace CustomBotDestruction
{
	static float GetStructureHealth(ABuildingActor* Building)
	{
		return Building ? Building->GetHealth() : 0.0f;
	}

	static float GetStructureMaxHealth(ABuildingActor* Building)
	{
		return Building ? Building->GetMaxHealth() : 0.0f;
	}

	static float GetStructureHealthPercent(ABuildingActor* Building)
	{
		return Building ? Building->GetHealthPercent() : 0.0f;
	}

	static bool IsStructureDestroyed(ABuildingActor* Building)
	{
		return Building && Building->IsDestroyed();
	}

	static bool CanDestroy(ABuildingActor* Building)
	{
		if (!Building)
			return false;

		if (Building->IsDestroyed())
			return false;

		return true;
	}

	static bool DestroyStructure(ABuildingActor* Building)
	{
		if (!Building || Building->IsDestroyed())
			return false;

		Building->SilentDie();
		return true;
	}

	static bool DestroyTarget(ABuildingActor* Building, bool bUseWeapon = false)
	{
		if (!Building || Building->IsDestroyed())
			return false;

		return DestroyStructure(Building);
	}
}