#pragma once

#include "CustomBot.h"

#include "FortKismetLibrary.h"
#include "FortResourceItemDefinition.h"


namespace CustomBotResources
{
	static int GetResourceCount(CustomBot& Bot, EFortResourceType ResourceType)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return 0;

		UFortResourceItemDefinition* ResourceItemDef = UFortKismetLibrary::K2_GetResourceItemDefinition(ResourceType);

		if (!ResourceItemDef)
			return 0;

		auto& List = Bot.WorldInventory->GetItemList();
		auto& ItemInstances = List.GetItemInstances();

		int Total = 0;

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);

			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();

			if (!Entry)
				continue;

			if (Entry->GetItemDefinition() == ResourceItemDef)
				Total += Entry->GetCount();
		}

		return Total;
	}

	static int GetTotalResourceCount(CustomBot& Bot)
	{
		return GetResourceCount(Bot, EFortResourceType::Wood)
			+ GetResourceCount(Bot, EFortResourceType::Stone)
			+ GetResourceCount(Bot, EFortResourceType::Metal);
	}

	static void GiveResource(CustomBot& Bot, EFortResourceType ResourceType, int Count)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory || Count <= 0)
			return;

		UFortResourceItemDefinition* ResourceItemDef = UFortKismetLibrary::K2_GetResourceItemDefinition(ResourceType);

		if (!ResourceItemDef)
			return;

		bool bShouldUpdate = false;
		Bot.WorldInventory->AddItem(ResourceItemDef, &bShouldUpdate, Count);

		if (bShouldUpdate)
			Bot.WorldInventory->Update();
	}

	static bool SpendResource(CustomBot& Bot, EFortResourceType ResourceType, int Count)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return false;

		if (Count <= 0)
			return true;

		if (GetResourceCount(Bot, ResourceType) < Count)
			return false;

		UFortResourceItemDefinition* ResourceItemDef = UFortKismetLibrary::K2_GetResourceItemDefinition(ResourceType);

		if (!ResourceItemDef)
			return false;

		auto& List = Bot.WorldInventory->GetItemList();
		auto& ItemInstances = List.GetItemInstances();

		bool bShouldUpdate = false;
		int RemainingToRemove = Count;

		for (int i = 0; i < ItemInstances.size() && RemainingToRemove > 0; ++i)
		{
			UFortItem* Item = ItemInstances.at(i);

			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();

			if (!Entry || Entry->GetItemDefinition() != ResourceItemDef)
				continue;

			int StackCount = Entry->GetCount();

			if (StackCount <= 0)
				continue;

			int NumToRemove = FMath::Min(StackCount, RemainingToRemove);
			Bot.WorldInventory->RemoveItem(Entry->GetItemGuid(), &bShouldUpdate, NumToRemove);

			RemainingToRemove -= NumToRemove;
		}

		if (bShouldUpdate)
			Bot.WorldInventory->Update();

		return RemainingToRemove <= 0;
	}

	static bool HasEnough(CustomBot& Bot, EFortResourceType ResourceType, int Count)
	{
		return GetResourceCount(Bot, ResourceType) >= Count;
	}
}
