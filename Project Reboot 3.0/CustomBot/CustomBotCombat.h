#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotInventory.h"
#include "CustomBotPerception.h"


namespace CustomBotCombat
{
	static bool IsWeaponEquipped(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);
		return Weapon != nullptr;
	}

	static bool IsPickaxeEquipped(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		auto* Weapon = Bot.Pawn->GetCurrentWeapon();

		if (!Weapon)
			return false;

		auto* WeaponDef = Weapon->GetWeaponData();

		if (!WeaponDef)
			return false;

		static auto FortWeaponMeleeItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponMeleeItemDefinition");
		return FortWeaponMeleeItemDefinitionClass && WeaponDef->IsA(FortWeaponMeleeItemDefinitionClass);
	}

	static bool EnemyHasRealWeapon(AActor* Enemy)
	{
		if (!Enemy)
			return false;

		auto* Pawn = Cast<AFortPlayerPawn>(Enemy);

		if (!Pawn)
			return false;

		auto* Weapon = Pawn->GetCurrentWeapon();

		if (!Weapon)
			return false;

		auto* WeaponDef = Weapon->GetWeaponData();

		if (!WeaponDef)
			return false;

		static auto FortWeaponMeleeItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponMeleeItemDefinition");
		return FortWeaponMeleeItemDefinitionClass && !WeaponDef->IsA(FortWeaponMeleeItemDefinitionClass);
	}

	static int GetCurrentAmmo(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);
		return Weapon ? Weapon->GetAmmoCount() : 0;
	}

	static int Reload(CustomBot& Bot, int NewAmmoCount)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);

		if (!Weapon)
			return 0;

		Weapon->GetAmmoCount() = NewAmmoCount;

		CustomBotInventory::SetLoadedAmmo(Bot, Weapon->GetItemEntryGuid(), NewAmmoCount);
		return NewAmmoCount;
	}


	static bool ActivatePrimaryAbility(CustomBot& Bot);

	static bool FireWeapon(CustomBot& Bot)
	{
		Bot.bFiringWeapon = true;
		Bot.bFiringWeaponTime = UGameplayStatics::GetTimeSeconds(GetWorld());
		return ActivatePrimaryAbility(Bot);
	}

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

		auto& Spec = Items.At(0, FGameplayAbilitySpec::GetStructSize());
		FGameplayAbilitySpecHandle Handle = Spec.GetHandle();

		PadHex18 PredictionKey{};
		UObject* OutInstancedAbility = nullptr;

		return UAbilitySystemComponent::InternalTryActivateAbilityOriginal2(
			ASC, Handle, PredictionKey, &OutInstancedAbility, nullptr, nullptr);
	}

	static void StopFiring(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);

		Bot.bFiringWeapon = false;

		if (!Weapon)
			return;

		static auto ServerReleaseWeaponAbilityFn = FindObject<UFunction>(L"/Script/FortniteGame.FortWeapon.ServerReleaseWeaponAbility");
		Weapon->ProcessEvent(ServerReleaseWeaponAbilityFn);
	}

	static void AimAt(CustomBot& Bot, const FVector& TargetLocation)
	{
		CustomBotMovement::LookAt(Bot, TargetLocation);
	}
}
