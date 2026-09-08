#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotInventory.h"
#include "CustomBotPerception.h"

// CustomBot - Combate.
//
// Equipa el arma, consulta y ajusta municion, apunta y dispara usando el
// pipeline GAS real (AbilitySystemComponent). Recarga restableciendo la
// municion cargada del arma.
//
// NOTA (research 04): no existe un helper end-to-end de disparo en el repo.
// El disparo real del jugador ocurre via la weapon ability (GAS). Para el bot
// activamos esa ability una vez que el arma esta equipada.

namespace CustomBotCombat
{
	// Devuelve true si el pawn tiene un arma equipada.
	static bool IsWeaponEquipped(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);
		return Weapon != nullptr;
	}

	// Municion actual del arma equipada.
	static int GetCurrentAmmo(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);
		return Weapon ? Weapon->GetAmmoCount() : 0;
	}

	// Recarga: restablece la municion cargada del arma equipada (capacidad cosmetica;
	// el almacen real se gestiona por el inventario). Devuelve la new ammo.
	static int Reload(CustomBot& Bot, int NewAmmoCount)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);

		if (!Weapon)
			return 0;

		Weapon->GetAmmoCount() = NewAmmoCount;

		CustomBotInventory::SetLoadedAmmo(Bot, Weapon->GetItemEntryGuid(), NewAmmoCount);
		return NewAmmoCount;
	}

	// Dispara el arma equipada activando su weapon ability via GAS.

	// Activa la primera spec activable del ASC (definida debajo; FireWeapon la usa).
	static bool ActivatePrimaryAbility(CustomBot& Bot);

	static bool FireWeapon(CustomBot& Bot)
	{
		return ActivatePrimaryAbility(Bot);
	}

	// Activa la primer spec activable del ASC (mejor esfuerzo; en la practica
	// tras equipar el arma su ability es la que hay que disparar, y tras equipar
	// un consumible es la ability de consumo). Pipeline GAS real (research 04).
	static bool ActivatePrimaryAbility(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.PlayerState)
			return false;

		auto ASC = Bot.PlayerState->GetAbilitySystemComponent();

		if (!ASC)
			return false;

		auto Container = ASC->GetActivatableAbilities();

		if (!Container)
			return false;

		auto& Items = Container->GetItems();

		if (Items.Num() == 0)
			return false;

		// Intentar activar la primer spec activable (habitualmente la weapon ability
		// del arma equipada tras EquipWeapon).
		auto& Spec = Items.At(0, FGameplayAbilitySpec::GetStructSize());
		FGameplayAbilitySpecHandle Handle = Spec.GetHandle();

		PadHex18 PredictionKey{};
		UObject* OutInstancedAbility = nullptr;

		return UAbilitySystemComponent::InternalTryActivateAbilityOriginal2(
			ASC, Handle, PredictionKey, &OutInstancedAbility, nullptr, nullptr);
	}

	// Suelta el gatillo (detiene el disparo del arma actual) via la UFunction nativa.
	static void StopFiring(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);

		if (!Weapon)
			return;

		static auto ServerReleaseWeaponAbilityFn = FindObject<UFunction>(L"/Script/FortniteGame.FortWeapon.ServerReleaseWeaponAbility");
		Weapon->ProcessEvent(ServerReleaseWeaponAbilityFn);
	}

	// Apunta el pawn hacia el objetivo (rota el control via LookAt del movimiento).
	static void AimAt(CustomBot& Bot, const FVector& TargetLocation)
	{
		CustomBotMovement::LookAt(Bot, TargetLocation);
	}
}
