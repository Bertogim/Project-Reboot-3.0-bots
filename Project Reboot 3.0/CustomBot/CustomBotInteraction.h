#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotCombat.h"

#include "FortPickup.h"
#include "BuildingContainer.h"

// CustomBot - Interaccion.
//
// Abre cofres/containers (BuildingContainer) y recoge pickups del suelo usando
// las mismas funciones server-side que un jugador real.

namespace CustomBotInteraction
{
	// Distancia maxima de interaccion (radio).
	inline constexpr float InteractionRadius = 250.0f;

	// Devuelve true si el bot esta suficientemente cerca para interactuar con actor.
	static bool CanInteract(CustomBot& Bot, AActor* Target)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Target)
			return false;

		return Bot.Pawn->GetDistanceTo(Target) <= InteractionRadius;
	}

	// Abre un cofre/container: genera su loot, lo marca como buscado y lo anima.
	// Devuelve true si el container existia y no estaba ya abierto.
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

	// Interacciona con un BuildingContainer (cofre) u otro objeto interactuable.
	// Parte 1: solo containers reales; otros tipos se pueden ampliar.
	static bool Interact(CustomBot& Bot, AActor* Target)
	{
		if (!Target)
			return false;

		auto Container = Cast<ABuildingContainer>(Target);

		if (Container)
			return OpenChest(Bot, Container);

		return false;
	}

	// Recoge un pickup del suelo (como un jugador que camina sobre el).
	// Devuelve true si el pickup existia y no estaba ya recogido.
	static bool PickupItem(CustomBot& Bot, AFortPickup* Pickup)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Pickup)
			return false;

		AFortPlayerPawn::ServerHandlePickupHook(Bot.Pawn, Pickup, 0.40f, FVector{}, true);
		return true;
	}

	// Recoge el pickup mas cercano dentro del radio si existe.
	static bool PickupNearest(CustomBot& Bot, float Radius = InteractionRadius)
	{
		AFortPickup* Pickup = CustomBotPerception::FindNearestPickup(Bot, Radius, CustomBotPerception::EItemType::Other, true);

		if (!Pickup)
			return false;

		return PickupItem(Bot, Pickup);
	}

	// Usa un consumible: busca el primer consumible del inventario, lo equipa
	// (igual que un jugador) y activa su ability real via GAS (pipeline de
	// CustomBotCombat::ActivatePrimaryAbility, el mismo que dispara el arma).
	// Devuelve true si habia un consumible que equipar/activar.
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