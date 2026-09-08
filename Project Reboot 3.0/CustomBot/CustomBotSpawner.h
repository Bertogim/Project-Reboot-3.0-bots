#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"

// CustomBot - Spawner.
//
// Crea instancias reales de jugador para el bot (AFortPlayerControllerAthena +
// AFortPlayerPawnAthena), replicando el patron de PlayerBot::Initialize del
// sistema antiguo (bots.h) pero como una entidad CustomBot independiente.
// NO modifica el sistema antiguo en absoluto.

namespace CustomBotSpawner
{
	// Todos los bots custom vivos, para tickearlos desde el game loop.
	static inline std::vector<CustomBot> AllCustomBots;

	static inline UClass* PawnClass = nullptr;
	static inline UClass* ControllerClass = nullptr;

	static void TickAll()
	{
		for (auto& Bot : AllCustomBots)
		{
			if (!Bot.IsValidActor())
				continue;

			Bot.Tick();                              // Parte 2: decisiones de IA
			CustomBotMovement::UpdateMovement(Bot);  // pipeline de movimiento real
		}
	}

	// Inicializa las clases de pawn/controller (una sola vez).
	static bool InitializeClasses()
	{
		static inline bool bInitialized = false;

		if (!bInitialized)
		{
			PawnClass = FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");
			ControllerClass = AFortPlayerControllerAthena::StaticClass();

			bInitialized = true;
		}

		return PawnClass && ControllerClass;
	}

	static bool IsReadyToSpawn()
	{
		return InitializeClasses();
	}

	// Spawna un bot custom en SpawnTransform y lo devuelve (o nullptr si falla).
	static CustomBot* SpawnCustomBot(const FTransform& SpawnTransform, AActor* InSpawnLocator = nullptr)
	{
		if (!IsReadyToSpawn())
		{
			LOG_ERROR(LogBots, "[CustomBot] Classes not ready to spawn bot!");
			return nullptr;
		}

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!GameState || !GameMode)
		{
			LOG_ERROR(LogBots, "[CustomBot] No GameState/GameMode to spawn bot!");
			return nullptr;
		}

		// Crea la entidad en el contenedor global.
		AllCustomBots.emplace_back();
		CustomBot& Bot = AllCustomBots.back();

		// Orden de spawn correcto (ver bots.h Initialize): controller, luego pawn,
		// luego recuperar el playerstate de la posesion del controller.
		Bot.Controller = GetWorld()->SpawnActor<AFortPlayerControllerAthena>(ControllerClass);

		if (!Bot.Controller)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to spawn controller!");
			AllCustomBots.pop_back();
			return nullptr;
		}

		Bot.Pawn = GetWorld()->SpawnActor<AFortPlayerPawnAthena>(
			PawnClass,
			SpawnTransform,
			CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn));

		if (!Bot.Pawn)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to spawn pawn!");
			Bot.Controller->K2_DestroyActor();
			AllCustomBots.pop_back();
			return nullptr;
		}

		Bot.PlayerState = Cast<AFortPlayerStateAthena>(Bot.Controller->GetPlayerState());

		if (!Bot.PlayerState)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to get playerstate!");
			Bot.Pawn->K2_DestroyActor();
			Bot.Controller->K2_DestroyActor();
			AllCustomBots.pop_back();
			return nullptr;
		}

		// Marcar como bot para el sistema nativo.
		Bot.PlayerState->SetIsBot(true);

		// Poseer el pawn.
		if (Bot.Controller->GetPawn() != Bot.Pawn)
			Bot.Controller->Possess(Bot.Pawn);

		// Nombre.
		SetCustomBotName(Bot, GameMode);

		// Team + squad.
		Bot.PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Bot.Controller);

		static auto SquadIdOffset = Bot.PlayerState->GetOffset("SquadId", false);

		if (SquadIdOffset != -1)
			Bot.PlayerState->GetSquadId() = Bot.PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;

		// Registrar en GameState.
		GameState->AddPlayerStateToGameMemberInfo(Bot.PlayerState);

		// Vida/escudo base.
		Bot.Pawn->SetHealth(100);
		Bot.Pawn->SetMaxHealth(100);

		// Abilities.
		GrantAbilities(Bot);

		// Inventario.
		SetupInventory(Bot, GameMode);

		// Registrar en GameMode.
		GameMode->GetAlivePlayers().Add(Bot.Controller);
		++GameState->GetPlayersLeft();
		GameState->OnRep_PlayersLeft();

		Bot.bInitialized = true;

		LOG_INFO(LogBots, "[CustomBot] Finished spawning custom bot!");
		return &AllCustomBots.back();
	}

	// Asigna un nombre aleatorio al bot.
	static void SetCustomBotName(CustomBot& Bot, AFortGameModeAthena* GameMode)
	{
		static int CurrentBotNum = 1;

		std::wstring BotNumWStr = std::to_wstring(CurrentBotNum++ + 200);
		FString NewName;

		if (Fortnite_Version < 11)
			NewName = (L"CustomBot" + BotNumWStr).c_str();
		else
			NewName = (std::format(L"Anonymous[{}]", BotNumWStr)).c_str();

		if (Fortnite_Version < 9)
		{
			Bot.Controller->ServerChangeName(NewName);
		}
		else
		{
			GameMode->ChangeName(Bot.Controller, NewName, true);
		}

		Bot.PlayerState->OnRep_PlayerName();
	}

	// Otorga las abilities default de jugador.
	static void GrantAbilities(CustomBot& Bot)
	{
		auto PlayerAbilitySet = GetPlayerAbilitySet();
		auto AbilitySystemComponent = Bot.PlayerState->GetAbilitySystemComponent();

		if (PlayerAbilitySet && AbilitySystemComponent)
		{
			PlayerAbilitySet->GiveToAbilitySystem(AbilitySystemComponent);
		}
	}

	// Crea el world inventory del bot y le da los items de salida.
	static bool SetupInventory(CustomBot& Bot, AFortGameModeAthena* GameMode)
	{
		static auto FortInventoryClass = FindObject<UClass>(L"/Script/FortniteGame.FortInventory");

		Bot.WorldInventory = GetWorld()->SpawnActor<AFortInventory>(
			FortInventoryClass,
			FTransform{},
			CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AlwaysSpawn, false, Bot.Controller));

		if (!Bot.WorldInventory)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to spawn WorldInventory!");
			return false;
		}

		// Registrar el inventario en el controller del bot (campo "WorldInventory"),
		// igual que hace el sistema antiguo con &FortPlayerController->GetWorldInventory().
		Bot.Controller->GetWorldInventory() = Bot.WorldInventory;

		Bot.WorldInventory->GetInventoryType() = EFortInventoryType::World;

		static auto bHasInitializedWorldInventoryOffset = Bot.Controller->GetOffset("bHasInitializedWorldInventory");
		Bot.Controller->Get<bool>(bHasInitializedWorldInventoryOffset) = true;

		// Items de salida (starting items del playlist).
		auto& StartingItems = GameMode->GetStartingItems();

		for (int i = 0; i < StartingItems.Num(); ++i)
		{
			auto& StartingItem = StartingItems.at(i, FItemAndCount::GetStructSize());
			Bot.WorldInventory->AddItem(StartingItem.GetItem(), nullptr, StartingItem.GetCount());
		}

		// Pickaxe y equiparlo.
		UFortItem* PickaxeInstance = Bot.Controller->AddPickaxeToInventory();

		if (PickaxeInstance)
		{
			Bot.Controller->ServerExecuteInventoryItemHook(Bot.Controller, PickaxeInstance->GetItemEntry()->GetItemGuid());
		}

		Bot.WorldInventory->Update();

		return true;
	}
};
