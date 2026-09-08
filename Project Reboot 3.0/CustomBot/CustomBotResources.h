#pragma once

#include "CustomBot.h"

#include "FortKismetLibrary.h"
#include "FortResourceItemDefinition.h"

// CustomBot - Materiales / Recursos.
//
// Consulta, otorga y gasta los materiales de construccion (madera, piedra/ladrillo,
// metal) usando el inventario real del bot. Los materiales son items de inventario
// de tipo UFortResourceItemDefinition.
//
// NOTA: el repositorio identifica el "ladrillo" (brick) como Stone (EFortResourceType::Stone).

namespace CustomBotResources
{
	// Cuenta cuantas unidades tiene el bot de un tipo de recurso concreto.
	static int GetResourceCount(CustomBot& Bot, EFortResourceType ResourceType)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return 0;

		UFortResourceItemDefinition* ResourceItemDef = UFortKismetLibrary::K2_GetResourceItemDefinition(ResourceType);

		if (!ResourceItemDef)
			return 0;

		// Buscar la instancia del recurso en el inventario (todos los stacks).
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

	// Cuenta total de materiales (suma de los tres recursos) del bot.
	static int GetTotalResourceCount(CustomBot& Bot)
	{
		return GetResourceCount(Bot, EFortResourceType::Wood)
			+ GetResourceCount(Bot, EFortResourceType::Stone)
			+ GetResourceCount(Bot, EFortResourceType::Metal);
	}

	// Otorga Count unidades de un recurso al inventario del bot.
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

	// Gasta Count unidades de un recurso. Devuelve true si habia suficiente y se gasto.
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

		// Eliminar Count unidades de los stacks del recurso.
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

	// Devuelve true si el bot puede pagar el coste del recurso.
	static bool HasEnough(CustomBot& Bot, EFortResourceType ResourceType, int Count)
	{
		return GetResourceCount(Bot, ResourceType) >= Count;
	}
}
