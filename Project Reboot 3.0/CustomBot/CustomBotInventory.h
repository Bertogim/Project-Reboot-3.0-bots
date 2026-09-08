#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"

#include "FortItemDefinition.h"
#include "FortWeaponItemDefinition.h"
#include "BuildingSMActor.h"

// CustomBot - Inventario.
//
// Recoge, suelta, equipa, cambia y consulta items usando el WorldInventory real
// del bot y las APIs nativas del jugador.

namespace CustomBotInventory
{
	// Devuelve el item actualmente equipado (arma) del pawn.
	static AFortWeapon* GetCurrentWeapon(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return nullptr;

		return Bot.Pawn->GetCurrentWeapon();
	}

	// Consulta la cantidad total de un item dado por su definicion.
	static int GetItemCount(CustomBot& Bot, UFortItemDefinition* ItemDefinition)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory || !ItemDefinition)
			return 0;

		auto& ItemInstances = Bot.WorldInventory->GetItemList().GetItemInstances();

		int Total = 0;

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);

			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();

			if (!Entry || Entry->GetItemDefinition() != ItemDefinition)
				continue;

			Total += Entry->GetCount();
		}

		return Total;
	}

	// Devuelve el primer UFortItem del inventario cuya definicion coincida.
	static UFortItem* FindItemByDefinition(CustomBot& Bot, UFortItemDefinition* ItemDefinition)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory || !ItemDefinition)
			return nullptr;

		auto& ItemInstances = Bot.WorldInventory->GetItemList().GetItemInstances();

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);

			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();

			if (Entry && Entry->GetItemDefinition() == ItemDefinition)
				return Item;
		}

		return nullptr;
	}

	// Devuelve el primer item del inventario cuya definicion sea de un tipo dado.
	// El tipo se clasifica con CustomBotPerception::ClassifyItemDefinition.
	static UFortItem* FindItemByType(CustomBot& Bot, CustomBotPerception::EItemType Type)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return nullptr;

		auto& ItemInstances = Bot.WorldInventory->GetItemList().GetItemInstances();

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);

			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();

			if (!Entry)
				continue;

			if (CustomBotPerception::ClassifyItemDefinition(Entry->GetItemDefinition()) == Type)
				return Item;
		}

		return nullptr;
	}

	// Equipa el item dado por su GUID (arma, deco, building piece, gadget...).
	static bool EquipItemByGuid(CustomBot& Bot, const FGuid& ItemGuid)
	{
		if (!Bot.IsReady() || !Bot.Controller)
			return false;

		Bot.Controller->ServerExecuteInventoryItemHook(Bot.Controller, ItemGuid);
		return true;
	}

	// Equipa el item (por su instancia UFortItem*). Primaria: FortPawn:EquipWeaponDefinition
	// directo sobre el pawn (funciona en servidor y SIN controller, requisito del fix
	// RUNPHYS de research 08). Fallback: ServerExecuteInventoryItemHook, que REQUIERE
	// Controller->GetPawn(); tras EnableServerSimulation el controller ya no posee pawn,
	// asi que esa via sola no equipa nada. Verifica al final contra la definicion pedida.
	static bool EquipItem(CustomBot& Bot, UFortItem* Item)
	{
		if (!Item)
			return false;

		auto* Entry = Item->GetItemEntry();

		if (!Entry)
			return false;

		auto* WeaponDef = Cast<UFortWeaponItemDefinition>(Entry->GetItemDefinition());

		if (WeaponDef && Bot.Pawn)
			Bot.Pawn->EquipWeaponDefinition(WeaponDef, Entry->GetItemGuid());

		auto Verify = [&]() -> bool {
			auto* W = Bot.IsReady() ? Bot.Pawn->GetCurrentWeapon() : nullptr;
			auto* D = W ? W->GetWeaponData() : nullptr;
			return D && Entry->GetItemDefinition() && D == Entry->GetItemDefinition();
		};

		if (!Verify())
		{
			EquipItemByGuid(Bot, Entry->GetItemGuid());

			if (!Verify())
				LOG_WARN(LogBots, "[CustomBot] EquipItem FAILED: wanted={}",
					Entry->GetItemDefinition()->GetPathName().c_str());
		}

		return Verify();
	}

	// Equipa el pickaxe del bot. Usa la instancia de pickaxe ya existente en el
	// WorldInventory (que SetupInventory crea en el spawn); AddPickaxeToInventory()
	// devuelve nullptr si el pickaxe ya existe, asi que no sirve para re-equipar.
	static bool EquipPickaxe(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Controller || !Bot.WorldInventory)
			return false;

		UFortItem* Pickaxe = Bot.WorldInventory->GetPickaxeInstance();

		if (!Pickaxe)
			Pickaxe = Bot.Controller->AddPickaxeToInventory();

		if (!Pickaxe)
			return false;

		return EquipItemByGuid(Bot, Pickaxe->GetItemEntry()->GetItemGuid());
	}

	// Busca una arma en el inventario y la equipa.
	static bool EquipFirstWeapon(CustomBot& Bot)
	{
		UFortItem* Weapon = FindItemByType(Bot, CustomBotPerception::EItemType::Weapon);
		return Weapon ? EquipItem(Bot, Weapon) : false;
	}

	// Da (anade) un item al inventario del bot. Devuelve true si se anadio.
	static bool GiveItem(CustomBot& Bot, UFortItemDefinition* ItemDefinition, int Count = 1, int LoadedAmmo = -1)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory || !ItemDefinition || Count <= 0)
			return false;

		bool bShouldUpdate = false;
		Bot.WorldInventory->AddItem(ItemDefinition, &bShouldUpdate, Count, LoadedAmmo);

		if (bShouldUpdate)
			Bot.WorldInventory->Update();

		return true;
	}

	// Elimina un item del inventario del bot.
	static bool RemoveItemByGuid(CustomBot& Bot, const FGuid& ItemGuid, int Count = 1, bool bForceRemoval = false)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return false;

		bool bShouldUpdate = false;
		bool bRemoved = Bot.WorldInventory->RemoveItem(ItemGuid, &bShouldUpdate, Count, bForceRemoval);

		if (bShouldUpdate)
			Bot.WorldInventory->Update();

		return bRemoved;
	}

	// Suelta (droppea) un item del inventario al suelo, como un jugador real
	// (ServerAttemptInventoryDropHook). Devuelve true si habia item que soltar.
	static bool DropItem(CustomBot& Bot, UFortItem* Item, int Count = 1)
	{
		if (!Bot.IsReady() || !Bot.Controller || !Item || Count <= 0)
			return false;

		auto Entry = Item->GetItemEntry();

		if (!Entry)
			return false;

		Bot.Controller->ServerAttemptInventoryDropHook(Bot.Controller, Entry->GetItemGuid(), Count);
		return true;
	}

	// Suelta Count unidades del item con la definicion dada (si existe en el bot).
	static bool DropItemByDefinition(CustomBot& Bot, UFortItemDefinition* ItemDefinition, int Count = 1)
	{
		UFortItem* Item = FindItemByDefinition(Bot, ItemDefinition);
		return Item ? DropItem(Bot, Item, Count) : false;
	}

	// Cambia la municion cargada del item dado por su GUID.
	static void SetLoadedAmmo(CustomBot& Bot, const FGuid& ItemGuid, int NewAmmoCount)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return;

		Bot.WorldInventory->CorrectLoadedAmmo(ItemGuid, NewAmmoCount);
	}
}
