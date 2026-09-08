#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"

#include "GameplayStatics.h"

// CustomBot - Spawner.
//
// Crea instancias reales de jugador para el bot (AFortPlayerControllerAthena +
// AFortPlayerPawnAthena), replicando el patron de PlayerBot::Initialize del
// sistema antiguo (bots.h) pero como una entidad CustomBot independiente.
// NO modifica el sistema antiguo en absoluto.

namespace CustomBotSpawner
{
	// Todos los bots custom vivos, para tickearlos desde el game loop.
	// IMPORTANTE: inline SIN `static` (linkage externo). `static inline` en un
	// header a scope de namespace crea UNA COPIA POR TU; SpawnCustomBot (desde
	// CustomBotDebug.cpp) y TickAll (desde NetDriver.cpp) son TUs distintas, y
	// el bot nunca llegaba a verse desde el ticker (bugs=0 TODO el rato).
	inline std::vector<CustomBot> AllCustomBots;

	inline UClass* PawnClass = nullptr;
	inline UClass* ControllerClass = nullptr;

	// Forward declarations (definidas debajo; SpawnCustomBot las usa antes).
	static void SetCustomBotName(CustomBot& Bot, AFortGameModeAthena* GameMode);
	static void GrantAbilities(CustomBot& Bot);
	static bool SetupInventory(CustomBot& Bot, AFortGameModeAthena* GameMode);
	static void ApplyRandomCosmeticLoadout(CustomBot& Bot);

	// Diagnostico: log una vez al primer invocarse y despues cada ~30 llamadas.
	inline bool bTickAllFirstLogDone = false;

	static void TickAll()
	{
		static unsigned TickAllCounter = 0;
		unsigned tc = ++TickAllCounter;

		if (!bTickAllFirstLogDone)
		{
			LOG_INFO(LogBots, "[CustomBot] [tickall] FIRST invoke. AllCustomBots.size={}",
				AllCustomBots.size());
			bTickAllFirstLogDone = true;
		}

		if (tc % 30 == 0)
		{
			LOG_INFO(LogBots, "[CustomBot] [tickall] invoke #{} bots={}",
				tc, AllCustomBots.size());
		}

		if (AllCustomBots.empty())
			return;

		// Marcar los bots a eliminar y borrarlos DESPUES del bucle: un bot puede
		// marcarse como destruido mientras Tick()/UpdateMovement() ejecutan (p.ej.
		// la secuencia debugbot), y borrar dentro del iterador las invalidaria.
		std::vector<size_t> ToRemove;

		for (size_t i = 0; i < AllCustomBots.size(); ++i)
		{
			CustomBot& Bot = AllCustomBots[i];

			if (!Bot.IsValidActor())
			{
				LOG_INFO(LogBots, "[CustomBot] [tickall] INVALID bot idx={} controller={} pawn={}, removing",
					i, bool(Bot.Controller), bool(Bot.Pawn));

				ToRemove.push_back(i);
				continue;
			}

			if (tc % 30 == 0)
				LOG_INFO(LogBots, "[CustomBot] [tickall] idx={} ready={} dbgTick={} life={}",
					i, Bot.IsReady(), Bot.DebugTick != nullptr, (int)Bot.GetLifeState());

			Bot.Tick();                              // Parte 2 + secuencia debugbot
			CustomBotMovement::UpdateMovement(Bot);  // pipeline de movimiento real
		}

		for (size_t i = ToRemove.size(); i-- > 0;)
			AllCustomBots.erase(AllCustomBots.begin() + ToRemove[i]);
	}

	// Inicializa las clases de pawn/controller (una sola vez).
	static bool InitializeClasses()
	{
		static bool bInitialized = false;

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
		LOG_INFO(LogBots, "[CustomBot] === SpawnCustomBot start ===");

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
		LOG_INFO(LogBots, "[CustomBot] Spawning controller...");
		Bot.Controller = GetWorld()->SpawnActor<AFortPlayerControllerAthena>(ControllerClass);

		if (!Bot.Controller)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to spawn controller!");
			AllCustomBots.pop_back();
			return nullptr;
		}

		LOG_INFO(LogBots, "[CustomBot] Spawning pawn...");
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

		LOG_INFO(LogBots, "[CustomBot] Getting PlayerState...");
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
		LOG_INFO(LogBots, "[CustomBot] SetIsBot=true");

		// Poseer el pawn.
		if (Bot.Controller->GetPawn() != Bot.Pawn)
			Bot.Controller->Possess(Bot.Pawn);
		LOG_INFO(LogBots, "[CustomBot] Possess done");

		// Nombre.
		SetCustomBotName(Bot, GameMode);
		LOG_INFO(LogBots, "[CustomBot] Name set");

		// Team + squad.
		Bot.PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Bot.Controller);

		static auto SquadIdOffset = Bot.PlayerState->GetOffset("SquadId", false);

		if (SquadIdOffset != -1)
			Bot.PlayerState->GetSquadId() = Bot.PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;

		LOG_INFO(LogBots, "[CustomBot] Team={} Squad={}", Bot.PlayerState->GetTeamIndex(),
			SquadIdOffset != -1 ? (int)Bot.PlayerState->GetSquadId() : -1);

		// Registrar en GameState.
		GameState->AddPlayerStateToGameMemberInfo(Bot.PlayerState);

		// Vida/escudo base.
		Bot.Pawn->SetHealth(100);
		Bot.Pawn->SetMaxHealth(100);
		LOG_INFO(LogBots, "[CustomBot] Health set to 100/100");

		// Abilities.
		LOG_INFO(LogBots, "[CustomBot] Granting abilities...");
		GrantAbilities(Bot);

		// Inventario.
		LOG_INFO(LogBots, "[CustomBot] Setting up inventory...");
		SetupInventory(Bot, GameMode);

		// Marcar como listo para que las funciones IsReady() funcionen abajo.
		Bot.bInitialized = true;

		// Skin/cosmetico.
		LOG_INFO(LogBots, "[CustomBot] Applying cosmetic loadout...");
		ApplyRandomCosmeticLoadout(Bot);

		// Registrar en GameMode.
		GameMode->GetAlivePlayers().Add(Bot.Controller);
		++GameState->GetPlayersLeft();
		GameState->OnRep_PlayersLeft();

		FVector BotPos = Bot.Pawn->GetActorLocation();
		LOG_INFO(LogBots, "[CustomBot] === SpawnCustomBot DONE pos=({:.0f},{:.0f},{:.0f}) ===",
			BotPos.X, BotPos.Y, BotPos.Z);
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

	// Aplica una skin aleatoria al bot (replica PickRandomLoadout + ApplyCosmeticLoadout
	// del sistema antiguo usa ApplyHID con ServerChoosePart=true, que solo procesa la
	// primera specialization. El sistema real del jugador usa ApplyCID con
	// bUseServerChoosePart=false → ApplyCharacterCosmetics que procesa TODAS las
	// specializations. Replicamos ese flujo aqui.
	static void ApplyRandomCosmeticLoadout(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Bot.PlayerState)
		{
			LOG_WARN(LogBots, "[CustomBot] ApplyRandomCosmeticLoadout: bot not ready");
			return;
		}

		// Buscar un HeroType: primero GetAllObjectsOfClass, luego FindObject con paths conocidos.
		auto HeroTypeClass = FindObject<UClass>(L"/Script/FortniteGame.FortHeroType");
		UFortItemDefinition* HeroType = nullptr;

		if (HeroTypeClass)
		{
			auto AllHeroTypes = GetAllObjectsOfClass(HeroTypeClass);
			std::vector<UFortItemDefinition*> AthenaHeroTypes;

			for (size_t i = 0; i < AllHeroTypes.size(); ++i)
			{
				auto CurrentHeroType = (UFortItemDefinition*)AllHeroTypes.at(i);

				if (!CurrentHeroType)
					continue;

				if (CurrentHeroType->GetPathName().starts_with("/Game/Athena/Heroes/"))
					AthenaHeroTypes.push_back(CurrentHeroType);
			}

			LOG_INFO(LogBots, "[CustomBot] Found {} Athena HeroTypes via GetAllObjectsOfClass", AthenaHeroTypes.size());

			if (!AthenaHeroTypes.empty())
				HeroType = AthenaHeroTypes.at(std::rand() % AthenaHeroTypes.size());
		}

		// Fallback: FindObject con paths conocidos (varian por version).
		if (!HeroType)
		{
			static const wchar_t* FallbackPaths[] = {
				L"/Game/Athena/Heroes/HID_088_Athena_Commando_M_SpaceBlack.HID_088_Athena_Commando_M_SpaceBlack",
				L"/Game/Athena/Heroes/HID_030_Athena_Commando_M_Halloween.HID_030_Athena_Commando_M_Halloween",
				L"/Game/Athena/Heroes/HID_021_Athena_Commando_M_Soldier.HID_021_Athena_Commando_M_Soldier",
			};

			for (auto* Path : FallbackPaths)
			{
				HeroType = FindObject<UFortItemDefinition>(Path);

				if (HeroType)
				{
					LOG_INFO(LogBots, "[CustomBot] Fallback HeroType found: {}", HeroType->GetPathName());
					break;
				}
			}
		}

		if (!HeroType)
		{
			LOG_WARN(LogBots, "[CustomBot] No HeroType found, bot will have no skin");
			return;
		}

		LOG_INFO(LogBots, "[CustomBot] Applying cosmetic: {}", HeroType->GetPathName());

		// Guardar en PlayerState (como hace el sistema antiguo).
		static auto HeroTypeOffset = Bot.PlayerState->GetOffset("HeroType", false);

		if (HeroTypeOffset != -1)
		{
			Bot.PlayerState->Get(HeroTypeOffset) = HeroType;
			LOG_INFO(LogBots, "[CustomBot] HeroType set on PlayerState (offset={})", HeroTypeOffset);
		}
		else
		{
			LOG_WARN(LogBots, "[CustomBot] HeroType offset not found on PlayerState, skipping PS set");
		}

		// Aplicar character parts usando bUseServerChoosePart=false (como el jugador real).
		// Esto usa ApplyCharacterCosmetics que procesa TODAS las specializations.
		ApplyHID(Bot.Pawn, HeroType, false);

		LOG_INFO(LogBots, "[CustomBot] ApplyHID completed (bUseServerChoosePart=false)");
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
