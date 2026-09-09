#pragma once

// CustomBot Manager - Parte 2.
//
// Sistema central de gestion de los bots custom (Section 29-31). La UI (pestana
// "Bots") consulta este manager; las decisiones de cada bot viven en su propia
// BotAIContext (CustomBot.AI). No duplica sistemas del proyecto: reutiliza
// CustomBotSpawner::AllCustomBots como almacen de instancias.

#include "CustomBotAI.h"
#include "CustomBot/CustomBotSpawner.h"

namespace CustomBotManager
{
	// --- Configuracion global -------------------------------------------------
	inline int DesiredBotCount = 5;   // cuantos bots quiere el operador
	inline EBotDifficulty Difficulty = EBotDifficulty::Normal;
	inline EBotPersonalityType PersonalityPool = EBotPersonalityType::Random;
	inline bool bAutoSpawnInProgress = false;

	// --- Acceso a las instancias ----------------------------------------------

	static std::vector<CustomBot>& GetBots()
	{
		return CustomBotSpawner::AllCustomBots;
	}

	// Logica viva: un bot "vivo" es aquel cuyo estado de vida no es Dead.
	static bool IsBotAlive(CustomBot& Bot)
	{
		if (!Bot.IsReady())
			return false;

		if (Bot.GetLifeState() == CBT::ELifeState::Dead)
			return false;

		if (Bot.AI && Bot.AI->State == EBotState::Dead)
			return false;

		return true;
	}

	// Obtiene los bots vivos.
	static std::vector<CustomBot*> GetAliveBots()
	{
		std::vector<CustomBot*> Alive;
		auto& Bots = GetBots();

		for (size_t i = 0; i < Bots.size(); ++i)
		{
			if (IsBotAlive(Bots[i]))
				Alive.push_back(&Bots[i]);
		}

		return Alive;
	}

	// Obtiene los bots muertos.
	static std::vector<CustomBot*> GetDeadBots()
	{
		std::vector<CustomBot*> Dead;
		auto& Bots = GetBots();

		for (size_t i = 0; i < Bots.size(); ++i)
		{
			if (!IsBotAlive(Bots[i]))
				Dead.push_back(&Bots[i]);
		}

		return Dead;
	}

	// --- Contadores de estado (Section 28) ------------------------------------
	// Devuelve el recuento real de bots por estado. Los estados con 0 se pueden
	// mostrar u ocultar, pero los numeros SON los reales.
	struct FStateCounts
	{
		int InBus = 0;
		int ChoosingLanding = 0;
		int Jumping = 0;
		int Gliding = 0;
		int Landing = 0;
		int Looting = 0;
		int Farming = 0;
		int Exploring = 0;
		int SearchingEnemy = 0;
		int Fighting = 0;
		int Defending = 0;
		int Healing = 0;
		int Rotating = 0;
		int EndGame = 0;
		int Warmup = 0;
		int Dead = 0;
	};

	static FStateCounts GetStateCounts()
	{
		FStateCounts Counts;
		auto& Bots = GetBots();

		for (size_t i = 0; i < Bots.size(); ++i)
		{
			CustomBot& Bot = Bots[i];

			if (!Bot.IsReady() || !Bot.AI)
			{
				++Counts.Dead;
				continue;
			}

			switch (Bot.AI->State)
			{
			case EBotState::InBus:           ++Counts.InBus; break;
			case EBotState::ChoosingLanding: ++Counts.ChoosingLanding; break;
			case EBotState::Jumping:         ++Counts.Jumping; break;
			case EBotState::Gliding:         ++Counts.Gliding; break;
			case EBotState::Landing:         ++Counts.Landing; break;
			case EBotState::Looting:         ++Counts.Looting; break;
			case EBotState::Farming:         ++Counts.Farming; break;
			case EBotState::Exploring:       ++Counts.Exploring; break;
			case EBotState::SearchingEnemy:  ++Counts.SearchingEnemy; break;
			case EBotState::Fighting:        ++Counts.Fighting; break;
			case EBotState::Defending:       ++Counts.Defending; break;
			case EBotState::Healing:         ++Counts.Healing; break;
			case EBotState::Rotating:        ++Counts.Rotating; break;
			case EBotState::EndGame:         ++Counts.EndGame; break;
			case EBotState::Warmup:          ++Counts.Warmup; break;
			case EBotState::Dead:            ++Counts.Dead; break;
			default:                         ++Counts.Dead; break;
			}
		}

		return Counts;
	}

	static int GetTotalCount()
	{
		return (int)GetBots().size();
	}

	static int GetAliveCount()
	{
		return (int)GetAliveBots().size();
	}

	static int GetDeadCount()
	{
		return (int)GetDeadBots().size();
	}

	// --- Creacion de la IA de un bot al spawnear ------------------------------
	static void InitializeAI(CustomBot& Bot)
	{
		if (Bot.AI)
		{
			delete Bot.AI;
			Bot.AI = nullptr;
		}

		Bot.AI = new BotAIContext();
		Bot.AI->Difficulty = Difficulty;
		Bot.AI->PersonalityType = PersonalityPool;

		CustomBotAI::BuildPersonality(Bot.AI->Personality, PersonalityPool, Difficulty);

		// Estado inicial: subir al bus (entrar en la fase de avion).
		Bot.AI->State = EBotState::InBus;
	}

	// --- Spawn ----------------------------------------------------------------
	// Spawnea un bot en la posicion dada (como un jugador real) y le crea su IA.
	// Devuelve nullptr si fallo.
	static CustomBot* SpawnBotAt(const FVector& Location, const FRotator& Rotation)
	{
		CustomBot* Bot = nullptr;

		// Posiciones de spawn de jugador si no se indica una ubicacion.
		// fallback: el jugador local.
		FTransform SpawnTransform{};
		SpawnTransform.Translation = Location;
		SpawnTransform.Rotation = Rotation.Quaternion();
		SpawnTransform.Scale3D = { 1, 1, 1 };

		Bot = CustomBotSpawner::SpawnCustomBot(SpawnTransform);

		if (!Bot)
		{
			LOG_ERROR(LogBots, "[BotManager] Failed to spawn custom bot!");
			return nullptr;
		}

		InitializeAI(*Bot);
		LOG_INFO(LogBots, "[BotManager] Spawned bot #{} state={} difficulty={}",
			GetTotalCount(), (int)Bot->AI->State, (int)Bot->AI->Difficulty);

		return Bot;
	}

	// Spawnea un bot en un PlayerStart (respeta los puntos de spawn nativos de Fortnite).
	static CustomBot* SpawnBotNearLocalPlayer()
	{
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!GameMode)
		{
			LOG_ERROR(LogBots, "[BotManager] No GameMode for spawn");
			return nullptr;
		}

		// Buscar un PlayerStart existente en el mapa.
		static auto PlayerStartClass = FindObject<UClass>(L"/Script/Engine.PlayerStart");
		auto AllPlayerStarts = GetAllObjectsOfClass(PlayerStartClass);

		if (AllPlayerStarts.empty())
		{
			LOG_WARN(LogBots, "[BotManager] No PlayerStarts found in map");
			return nullptr;
		}

		// Elegir un PlayerStart aleatorio.
		AActor* ChosenStart = (AActor*)AllPlayerStarts[std::rand() % AllPlayerStarts.size()];
		FVector SpawnLoc = ChosenStart->GetActorLocation();
		FRotator SpawnRot = ChosenStart->GetActorRotation();

		return SpawnBotAt(SpawnLoc, SpawnRot);
	}

	// Spawnea Count bots (sin duplicar los ya existentes).
	// Usa Sleep() entre spawns para dar tiempo al engine a procesar cada bot.
	static int SpawnBots(int Count)
	{
		int Spawned = 0;

		for (int i = 0; i < Count; ++i)
		{
			if (SpawnBotNearLocalPlayer())
				++Spawned;
			else
				break; // si un bot falla, paramos (el engine esta saturado)

			if (i < Count - 1)
				Sleep(200); // 200ms entre spawns para no saturar el engine
		}

		LOG_INFO(LogBots, "[BotManager] Spawned {} bots (total={})", Spawned, GetTotalCount());
		return Spawned;
	}

	// --- Eliminacion ------------------------------------------------------------

	// Destruye un bot concreto (lo borra de la partida).
	static void RemoveBot(CustomBot& Bot)
	{
		Bot.Destroy();

		auto& Bots = GetBots();

		for (size_t i = 0; i < Bots.size(); ++i)
		{
			if (&Bots[i] == &Bot)
			{
				Bots.erase(Bots.begin() + i);
				break;
			}
		}
	}

	// Marca todos como muertos / los destruye.
	static void RemoveAllBots()
	{
		auto& Bots = GetBots();

		// Destruir es invalido si el vector se modifica; copiamos y vaciamos.
		while (!Bots.empty())
		{
			CustomBot& Bot = Bots.back();
			Bot.Destroy();
			Bots.pop_back();
		}
	}

	// Rellena la partida hasta 100 jugadores (contando jugadores reales + bots).
	static int FillTo100()
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());

		if (!GameState)
			return 0;

		int CurrentTotal = GameState->GetPlayersLeft();
		int Needed = 100 - CurrentTotal;

		if (Needed <= 0)
		{
			LOG_INFO(LogBots, "[BotManager] Lobby already at {} players, no bots needed", CurrentTotal);
			return 0;
		}

		LOG_INFO(LogBots, "[BotManager] FillTo100: current={}, needed={}", CurrentTotal, Needed);
		return SpawnBots(Needed);
	}
}