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
		bool bActivated = ActivatePrimaryAbility(Bot);
		Bot.bFiringWeapon = bActivated;
		Bot.bFiringWeaponTime = UGameplayStatics::GetTimeSeconds(GetWorld());
		return bActivated;
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

		static unsigned DiagnosticCount = 0;
		bool bLog = DiagnosticCount++ < 12;
		if (bLog)
		{
			auto* Weapon = CustomBotInventory::GetCurrentWeapon(Bot);
			LOG_INFO(LogBots, "[bot-action-diag] fire pawn=0x{:x} weapon={} pendingEquip={} specs={}",
				__int64(Bot.Pawn), Weapon ? Weapon->GetPathName().c_str() : "NONE",
				Bot.bPendingEquip, Items.Num());
			for (int i = 0; i < Items.Num() && i < 64; ++i)
			{
				auto& Candidate = Items.At(i, FGameplayAbilitySpec::GetStructSize());
				auto* Ability = Candidate.GetAbility();
				LOG_INFO(LogBots, "[bot-action-diag] spec index={} handle={} ability={}",
					i, Candidate.GetHandle().Handle,
					Ability ? Ability->GetPathName().c_str() : "NONE");
			}
		}

		if (!Spec.GetAbility())
			return false;

		static auto PredictionStruct = FindObject<UStruct>(L"/Script/GameplayAbilities.PredictionKey");
		if (!PredictionStruct)
			return false;

		int PredictionSize = PredictionStruct->GetPropertiesSize();
		UObject* OutInstancedAbility = nullptr;
		bool bActivated = false;

		if (PredictionSize == 0x10 && UAbilitySystemComponent::InternalTryActivateAbilityOriginal)
		{
			PadHex10 PredictionKey{};
			bActivated = UAbilitySystemComponent::InternalTryActivateAbilityOriginal(
				ASC, Handle, PredictionKey, &OutInstancedAbility, nullptr, nullptr);
		}
		else if (PredictionSize == 0x18 && UAbilitySystemComponent::InternalTryActivateAbilityOriginal2)
		{
			PadHex18 PredictionKey{};
			bActivated = UAbilitySystemComponent::InternalTryActivateAbilityOriginal2(
				ASC, Handle, PredictionKey, &OutInstancedAbility, nullptr, nullptr);
		}

		if (bLog)
			LOG_INFO(LogBots, "[bot-action-diag] activation index=0 ability={} predictionSize={} result={}",
				Spec.GetAbility()->GetPathName().c_str(), PredictionSize, bActivated);

		return bActivated;
	}

	static void StopFiring(CustomBot& Bot)
	{
		auto Weapon = CustomBotInventory::GetCurrentWeapon(Bot);

		Bot.bFiringWeapon = false;

		if (!Weapon)
			return;

		static auto ServerReleaseWeaponAbilityFn = FindObject<UFunction>(L"/Script/FortniteGame.FortWeapon.ServerReleaseWeaponAbility");
		if (ServerReleaseWeaponAbilityFn)
			Weapon->ProcessEvent(ServerReleaseWeaponAbilityFn);
	}

	static void AimAt(CustomBot& Bot, const FVector& TargetLocation)
	{
		CustomBotMovement::LookAt(Bot, TargetLocation);
	}
}
