#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"

#include "FortItemDefinition.h"
#include "FortWeaponItemDefinition.h"
#include "BuildingSMActor.h"


namespace CustomBotInventory
{
	static AFortWeapon* GetCurrentWeapon(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return nullptr;

		return Bot.Pawn->GetCurrentWeapon();
	}

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

	static constexpr float kEquipVerifyTimeout = 1.5f;

	static bool EquipViaController(CustomBot& Bot, const FGuid& ItemGuid)
	{
		if (!Bot.IsReady() || !Bot.Controller || !Bot.Pawn)
			return false;

		static int ControllerPawnOff = Bot.Controller->GetOffset("Pawn", false);
		static int PawnControllerOff = Bot.Pawn->GetOffset("Controller", false);

		if (ControllerPawnOff == -1 || PawnControllerOff == -1)
			return false;

		APawn* SavedControllerPawn = Bot.Controller->Get<APawn*>(ControllerPawnOff);
		AController* SavedPawnController = Bot.Pawn->Get<AController*>(PawnControllerOff);

		Bot.Controller->Get<APawn*>(ControllerPawnOff) = Bot.Pawn;
		Bot.Pawn->Get<AController*>(PawnControllerOff) = Bot.Controller;

		Bot.Controller->ServerExecuteInventoryItemHook(Bot.Controller, ItemGuid);

		Bot.Controller->Get<APawn*>(ControllerPawnOff) = SavedControllerPawn;
		Bot.Pawn->Get<AController*>(PawnControllerOff) = SavedPawnController;

		return true;
	}

	static bool EquipItem(CustomBot& Bot, UFortItem* Item)
	{
		if (!Item)
			return false;

		auto* Entry = Item->GetItemEntry();

		if (!Entry)
			return false;

		auto* ItemDefinition = Entry->GetItemDefinition();

		if (!ItemDefinition)
			return false;

		auto Verify = [&]() -> bool {
			auto* W = Bot.IsReady() ? Bot.Pawn->GetCurrentWeapon() : nullptr;
			auto* D = W ? W->GetWeaponData() : nullptr;
			return D && D == ItemDefinition;
		};

		if (EquipViaController(Bot, Entry->GetItemGuid()))
		{
			if (Verify())
			{
				Bot.bPendingEquip = false;
				return true;
			}

			Bot.bPendingEquip = true;
			Bot.PendingEquipGuid = Entry->GetItemGuid();
			Bot.PendingEquipTime = UGameplayStatics::GetTimeSeconds(GetWorld());
			return false;
		}

		auto* WeaponDef = Cast<UFortWeaponItemDefinition>(ItemDefinition);

		if (WeaponDef && Bot.Pawn)
			Bot.Pawn->EquipWeaponDefinition(WeaponDef, Entry->GetItemGuid());

		if (Verify())
		{
			Bot.bPendingEquip = false;
			return true;
		}

		Bot.bPendingEquip = true;
		Bot.PendingEquipGuid = Entry->GetItemGuid();
		Bot.PendingEquipTime = UGameplayStatics::GetTimeSeconds(GetWorld());
		return false;
	}

	static void TickPendingEquips(CustomBot& Bot)
	{
		if (!Bot.bPendingEquip)
			return;

		if (!Bot.IsReady() || !Bot.Pawn || !Bot.WorldInventory)
			return;

		UFortItem* Pending = Bot.WorldInventory->FindItemInstance(Bot.PendingEquipGuid);

		if (!Pending || !Pending->GetItemEntry())
		{
			Bot.bPendingEquip = false;
			return;
		}

		auto* Current = Bot.Pawn->GetCurrentWeapon();
		auto* CurrentDef = Current ? Current->GetWeaponData() : nullptr;

		if (CurrentDef == Pending->GetItemEntry()->GetItemDefinition())
		{
			Bot.bPendingEquip = false;
			return;
		}

		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

		if (Now - Bot.PendingEquipTime < kEquipVerifyTimeout)
			return;

		Bot.PendingEquipTime = Now;

		EquipViaController(Bot, Bot.PendingEquipGuid);

		if (++Bot.PendingEquipAttempts >= 2)
		{
			LOG_WARN(LogBots, "[CustomBot] EquipItem FAILED: wanted={}",
				Pending->GetItemEntry()->GetItemDefinition()->GetPathName().c_str());
			Bot.bPendingEquip = false;
			Bot.PendingEquipAttempts = 0;
		}
	}

	static bool EquipPickaxe(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return false;

		UFortItem* Pickaxe = Bot.WorldInventory->GetPickaxeInstance();

		if (!Pickaxe && Bot.Controller)
			Pickaxe = Bot.Controller->AddPickaxeToInventory();

		if (!Pickaxe)
			return false;

		return EquipItem(Bot, Pickaxe);
	}

	static bool EquipFirstWeapon(CustomBot& Bot)
	{
		UFortItem* Weapon = FindItemByType(Bot, CustomBotPerception::EItemType::Weapon);
		return Weapon ? EquipItem(Bot, Weapon) : false;
	}

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

	static bool DropItemByDefinition(CustomBot& Bot, UFortItemDefinition* ItemDefinition, int Count = 1)
	{
		UFortItem* Item = FindItemByDefinition(Bot, ItemDefinition);
		return Item ? DropItem(Bot, Item, Count) : false;
	}

	static void SetLoadedAmmo(CustomBot& Bot, const FGuid& ItemGuid, int NewAmmoCount)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return;

		Bot.WorldInventory->CorrectLoadedAmmo(ItemGuid, NewAmmoCount);
	}
}
