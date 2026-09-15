#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotCombat.h"

#include "FortPickup.h"
#include "BuildingContainer.h"


namespace CustomBotInteraction
{
	inline constexpr float InteractionRadius = 250.0f;

	static bool CanInteract(CustomBot& Bot, AActor* Target)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Target)
			return false;

		return Bot.Pawn->GetDistanceTo(Target) <= InteractionRadius;
	}

	static bool OpenChest(CustomBot& Bot, ABuildingContainer* Container)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Container)
			return false;

		if (Container->IsAlreadySearched())
			return false;

		Container->SpawnLoot(Bot.Pawn);
		Container->SetAlreadySearched(true);
		Container->BounceContainer();

		return true;
	}

	static bool Interact(CustomBot& Bot, AActor* Target)
	{
		if (!Target)
			return false;

		auto Container = Cast<ABuildingContainer>(Target);

		if (Container)
			return OpenChest(Bot, Container);

		return false;
	}

	static bool PickupItem(CustomBot& Bot, AFortPickup* Pickup)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Pickup)
			return false;

		AFortPlayerPawn::ServerHandlePickupHook(Bot.Pawn, Pickup, 0.40f, FVector{}, true);
		return true;
	}

	static bool PickupNearest(CustomBot& Bot, float Radius = InteractionRadius)
	{
		AFortPickup* Pickup = CustomBotPerception::FindNearestPickup(Bot, Radius, CustomBotPerception::EItemType::Other, true);

		if (!Pickup)
			return false;

		return PickupItem(Bot, Pickup);
	}

	static bool UseConsumable(CustomBot& Bot)
	{
		if (!Bot.IsReady())
			return false;

		UFortItem* Consumable = CustomBotInventory::FindItemByType(Bot, CustomBotPerception::EItemType::Consumable);

		if (!Consumable)
			return false;

		if (!CustomBotInventory::EquipItem(Bot, Consumable))
			return false;

		return CustomBotCombat::ActivatePrimaryAbility(Bot);
	}
}