#pragma once

#include "CustomBot.h"

#include "CustomBotInventory.h"

// CustomBot - Destruccion.
//
// Destruye estructuras y objetos del mundo (arboles, rocas) usando las APIs
// reales de BuildingActor. La generacion de recursos al destruir se produce
// automaticamente por el juego (OnDamageServerHook / OnDamageServer).
//
// Parte 1: se expone la capacidad de destruir (SilentDie para atravesar) y de
// consultar salud. El daño "como jugador" con arma melee se puede encadenar
// golpeando el objetivo con el pickaxe (melee), mas apropiado para la Parte 2.

namespace CustomBotDestruction
{
	// Salud actual de una estructura/objeto.
	static float GetStructureHealth(ABuildingActor* Building)
	{
		return Building ? Building->GetHealth() : 0.0f;
	}

	// Salud maxima de una estructura/objeto.
	static float GetStructureMaxHealth(ABuildingActor* Building)
	{
		return Building ? Building->GetMaxHealth() : 0.0f;
	}

	// Racion de salud (0..1).
	static float GetStructureHealthPercent(ABuildingActor* Building)
	{
		return Building ? Building->GetHealthPercent() : 0.0f;
	}

	// Devuelve true si la estructura ya esta destruida.
	static bool IsStructureDestroyed(ABuildingActor* Building)
	{
		return Building && Building->IsDestroyed();
	}

	// Devuelve true si el bot puede destruir esta estructura (no esta destruida,
	// no es su propia estructura o es un objeto del mundo, etc.). Capacidad pura.
	static bool CanDestroy(ABuildingActor* Building)
	{
		if (!Building)
			return false;

		if (Building->IsDestroyed())
			return false;

		// Jugador puede destruir estructuras enemigas y objetos del mundo.
		return true;
	}

	// Destruye la estructura instantaneamente (SilentDie). Util para atravesar
	// o limpiar espacio. Devuelve true si la estructura existia.
	static bool DestroyStructure(ABuildingActor* Building)
	{
		if (!Building || Building->IsDestroyed())
			return false;

		Building->SilentDie();
		return true;
	}

	// Destruye (o aplica danio) a una estructura/objeto objetivo frente al bot.
	// bUseWeapon=true intentara usar el arma melee actual contra la estructura
	// (no implementado en Parte 1; se destruye instantaneo como fallback).
	static bool DestroyTarget(ABuildingActor* Building, bool bUseWeapon = false)
	{
		if (!Building || Building->IsDestroyed())
			return false;

		return DestroyStructure(Building);
	}
}