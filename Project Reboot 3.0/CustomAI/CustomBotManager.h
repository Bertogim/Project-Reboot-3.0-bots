#pragma once


#include <atomic>

#include "CustomBotAI.h"
#include "CustomBot/CustomBotSpawner.h"
#include "NetDriver.h"
#include "FortPlayerControllerAthena.h"

namespace CustomBotManager
{
	inline int DesiredBotCount = 5;
	inline EBotDifficulty Difficulty = EBotDifficulty::Normal;
	inline EBotPersonalityType PersonalityPool = EBotPersonalityType::Random;
	inline bool bAutoSpawnInProgress = false;

	inline std::atomic<int> PendingSpawnCount = 0;
	inline std::atomic<bool> bPendingFillTo100 = false;
	inline std::atomic<bool> bPendingRemoveAll = false;

	inline std::atomic<bool> bPendingTeleportToBot = false;
	inline std::atomic<bool> bPendingBringRandomBot = false;


	static std::vector<CustomBot>& GetBots()
	{
		return CustomBotSpawner::AllCustomBots;
	}

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

	struct FStateCounts
	{
		int InBus = 0;
		int Ejecting = 0;
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
			case EBotState::Ejecting:         ++Counts.Ejecting; break;
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

		Bot.AI->State = EBotState::InBus;
	}


	static void QueueSpawnBots(int Count)
	{
		PendingSpawnCount += Count;
		LOG_INFO(LogBots, "[BotManager] Queued {} bot spawn(s) (pending={})", Count, (int)PendingSpawnCount);
	}

	static void QueueFillTo100()
	{
		bPendingFillTo100 = true;
		LOG_INFO(LogBots, "[BotManager] Queued FillTo100");
	}

	static void QueueRemoveAll()
	{
		bPendingRemoveAll = true;
		LOG_INFO(LogBots, "[BotManager] Queued RemoveAllBots");
	}

	static void QueueTeleportToRandomBot()
	{
		bPendingTeleportToBot = true;
		LOG_INFO(LogBots, "[BotManager] Queued TeleportToRandomBot");
	}

	static void QueueBringRandomBot()
	{
		bPendingBringRandomBot = true;
		LOG_INFO(LogBots, "[BotManager] Queued BringRandomBot");
	}

	static AFortPlayerControllerAthena* GetFirstRealPlayerController()
	{
		auto World = GetWorld();
		if (!World)
			return nullptr;

		auto NetDriver = World->GetNetDriver();
		if (!NetDriver)
			return nullptr;

		auto& Conn = NetDriver->GetClientConnections();

		for (int i = 0; i < Conn.Num(); ++i)
		{
			auto Connection = Conn.at(i);
			if (!Connection)
				continue;

			auto Controller = Connection->GetPlayerController();
			if (!Controller)
				continue;

			bool bIsBotController = false;
			auto& Bots = GetBots();
			for (size_t b = 0; b < Bots.size(); ++b)
			{
				if (Bots[b].Controller == Controller)
				{
					bIsBotController = true;
					break;
				}
			}

			if (!bIsBotController)
				return Cast<AFortPlayerControllerAthena>(Controller);
		}

		return nullptr;
	}

	static CustomBot* GetRandomAliveBot()
	{
		auto Alive = GetAliveBots();

		std::vector<CustomBot*> Valid;
		for (size_t i = 0; i < Alive.size(); ++i)
		{
			if (Alive[i] && Alive[i]->Pawn)
				Valid.push_back(Alive[i]);
		}

		if (Valid.empty())
			return nullptr;

		return Valid[std::rand() % Valid.size()];
	}

	static void TeleportToRandomBot()
	{
		auto* PC = GetFirstRealPlayerController();

		if (!PC || !PC->GetPawn())
		{
			LOG_WARN(LogBots, "[BotManager] TeleportToRandomBot: no real player pawn");
			return;
		}

		auto* Bot = GetRandomAliveBot();

		if (!Bot || !Bot->Pawn)
		{
			LOG_WARN(LogBots, "[BotManager] TeleportToRandomBot: no alive bot with pawn");
			return;
		}

		FVector Dest = Bot->Pawn->GetActorLocation();
		Dest.X += 250.0f;
		Dest.Y += 250.0f;
		Dest.Z += 100.0f;

		PC->GetPawn()->TeleportTo(Dest, PC->GetPawn()->GetActorRotation());

		LOG_INFO(LogBots, "[BotManager] Teleported player to bot at ({:.0f},{:.0f},{:.0f})",
			Dest.X, Dest.Y, Dest.Z);
	}

	static void BringRandomBotToPlayer()
	{
		auto* PC = GetFirstRealPlayerController();

		if (!PC || !PC->GetPawn())
		{
			LOG_WARN(LogBots, "[BotManager] BringRandomBot: no real player pawn");
			return;
		}

		auto* Bot = GetRandomAliveBot();

		if (!Bot || !Bot->Pawn)
		{
			LOG_WARN(LogBots, "[BotManager] BringRandomBot: no alive bot with pawn");
			return;
		}

		FVector Dest = PC->GetPawn()->GetActorLocation();
		Dest.X += 250.0f;
		Dest.Y += 250.0f;
		Dest.Z += 100.0f;

		Bot->Pawn->TeleportTo(Dest, Bot->Pawn->GetActorRotation());

		LOG_INFO(LogBots, "[BotManager] Brought random bot to player at ({:.0f},{:.0f},{:.0f})",
			Dest.X, Dest.Y, Dest.Z);
	}

	static CustomBot* SpawnBotAt(const FVector& Location, const FRotator& Rotation)
	{
		CustomBot* Bot = nullptr;

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

	static CustomBot* SpawnBotNearLocalPlayer()
	{
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!GameMode)
		{
			LOG_ERROR(LogBots, "[BotManager] No GameMode for spawn");
			return nullptr;
		}

		static auto PlayerStartClass = FindObject<UClass>(L"/Script/Engine.PlayerStart");
		auto AllPlayerStarts = GetAllObjectsOfClass(PlayerStartClass);

		if (AllPlayerStarts.empty())
		{
			LOG_WARN(LogBots, "[BotManager] No PlayerStarts found in map");
			return nullptr;
		}

		AActor* ChosenStart = (AActor*)AllPlayerStarts[std::rand() % AllPlayerStarts.size()];
		FVector SpawnLoc = ChosenStart->GetActorLocation();
		FRotator SpawnRot = ChosenStart->GetActorRotation();

		return SpawnBotAt(SpawnLoc, SpawnRot);
	}

	static int SpawnBots(int Count)
	{
		int Spawned = 0;

		for (int i = 0; i < Count; ++i)
		{
			if (SpawnBotNearLocalPlayer())
				++Spawned;
			else
				break;

			if (i < Count - 1)
				Sleep(200);
		}

		LOG_INFO(LogBots, "[BotManager] Spawned {} bots (total={})", Spawned, GetTotalCount());
		return Spawned;
	}


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

	static void RemoveAllBots()
	{
		auto& Bots = GetBots();

		while (!Bots.empty())
		{
			CustomBot& Bot = Bots.back();

			CustomBotSpawner::ProcessBotDeathCounters(Bot, true);

			Bot.Destroy();
			Bots.pop_back();
		}
	}

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

	static void ProcessPendingOps()
	{
		if (bPendingRemoveAll.exchange(false))
		{
			RemoveAllBots();
			return;
		}

		if (bPendingTeleportToBot.exchange(false))
		{
			TeleportToRandomBot();
			return;
		}

		if (bPendingBringRandomBot.exchange(false))
		{
			BringRandomBotToPlayer();
			return;
		}

		if (bPendingFillTo100.exchange(false))
		{
			auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());

			if (!GameState)
				return;

			int CurrentTotal = GameState->GetPlayersLeft();
			int Needed = 100 - CurrentTotal;

			if (Needed > 0)
			{
				PendingSpawnCount += Needed;
				LOG_INFO(LogBots, "[BotManager] FillTo100: current={}, needed={}", CurrentTotal, Needed);
			}
			else
			{
				LOG_INFO(LogBots, "[BotManager] Lobby already at {} players, no bots needed", CurrentTotal);
			}
		}

		if (PendingSpawnCount > 0)
		{
			--PendingSpawnCount;

			CustomBot* Bot = SpawnBotNearLocalPlayer();

			if (!Bot)
			{
				LOG_ERROR(LogBots, "[BotManager] Failed to spawn custom bot!");
				PendingSpawnCount = 0;
			}
			else
			{
				LOG_INFO(LogBots, "[BotManager] Spawned bot #{} state={} difficulty={} (pending={})",
					GetTotalCount(), (int)Bot->AI->State, (int)Bot->AI->Difficulty, (int)PendingSpawnCount);
			}
		}
	}

	inline bool bBotOpsHook = (CustomBotSpawner::DeferredBotOps = &ProcessPendingOps, true);
}