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
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		UFortItem* Consumable = CustomBotInventory::FindItemByType(Bot, CustomBotPerception::EItemType::Consumable);

		if (!Consumable)
			return false;

		auto Entry = Consumable->GetItemEntry();
		auto Def = Entry ? Entry->GetItemDefinition() : nullptr;

		if (!Def)
			return false;

		const std::string& Path = CustomBotPerception::CachedPathForDef(Def);

		CustomBotInventory::EquipItem(Bot, Consumable);

		auto Has = [&](const char* Sub) { return Path.find(Sub) != std::string::npos; };

		float Heal = 0.0f;
		float ShieldRestore = 0.0f;
		float MaxHealth = 100.0f;
		float MaxShield = 100.0f;

		if (Has("Bandage"))
			Heal = 15.0f;
		else if (Has("Medkit") || Has("MedKit"))
			Heal = 75.0f;
		else if (Has("SmallShield") || Has("ShieldPotion_Small"))
			ShieldRestore = 25.0f;
		else if (Has("ShieldPotion") || Has("ShieldPotion_Big"))
			ShieldRestore = 50.0f;
		else if (Has("Slurp"))
		{
			Heal = 25.0f;
			ShieldRestore = 25.0f;
		}
		else if (Has("Chug") || Has("ChugJug"))
		{
			Heal = 100.0f;
			ShieldRestore = 100.0f;
		}
		else
			return false;

		float CurHealth = Bot.GetHealth();
		float CurShield = Bot.GetShield();

		if (CurHealth >= MaxHealth && CurShield >= MaxShield)
			return false;

		if (Heal > 0.0f)
			Bot.Pawn->SetHealth(FMath::Min(MaxHealth, CurHealth + Heal));

		if (ShieldRestore > 0.0f)
			Bot.Pawn->SetShield(FMath::Min(MaxShield, CurShield + ShieldRestore));

		CustomBotInventory::RemoveItemByGuid(Bot, Entry->GetItemGuid(), 1, false);

		LOG_INFO(LogBots, "[BotAI] used consumable {} (heal+{:.0f} shield+{:.0f})",
			Path.c_str(), Heal, ShieldRestore);

		return true;
	}
}