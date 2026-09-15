#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotDebug.h"

#include "GameplayStatics.h"

#include "../CustomAI/CustomBotAI.h"

#include <chrono>
#include <algorithm>
#include <vector>

#ifndef PSAPI_VERSION
#define PSAPI_VERSION 1
#endif
#include <psapi.h>

uint8 ToDeathCause(const FGameplayTagContainer& TagContainer, bool bWasDBNO, AFortPawn* Pawn);

namespace CustomBotSpawner
{
	extern "C" int SpawnBotSafeSEH(void (*cb)(void*), void* data);
	extern "C" void TickBotSafeSEH(void (*cb)(void*), void* data);
	inline std::vector<CustomBot> AllCustomBots;

	inline UClass* PawnClass = nullptr;
	inline UClass* ControllerClass = nullptr;

	static void SetCustomBotName(CustomBot& Bot, AFortGameModeAthena* GameMode);
	static void GrantAbilities(CustomBot& Bot);
	static bool SetupInventory(CustomBot& Bot, AFortGameModeAthena* GameMode);
	static void ApplyRandomCosmeticLoadout(CustomBot& Bot);

	inline bool bTickAllFirstLogDone = false;

	using TickHook = void (*)();
	inline TickHook DeferredBotOps = nullptr;

	inline int PendingSkinBudget = 0;

	static void LogMemDiag(unsigned TickCount);


	static void ProcessBotDeathCounters(CustomBot& Bot, bool bCountOnly = false)
	{
		if (Bot.bDeathHandled)
			return;
		Bot.bDeathHandled = true;

		auto DeadPawn = Bot.Pawn;
		auto DeadPlayerState = Bot.PlayerState
			? Cast<AFortPlayerStateAthena>(Bot.PlayerState)
			: (Bot.Controller ? Cast<AFortPlayerStateAthena>(Bot.Controller->GetPlayerState()) : nullptr);

		APawn* KillerPawn = nullptr;
		if (DeadPawn)
		{
			static int InstigatorOffset = -2;
			if (InstigatorOffset == -2)
				InstigatorOffset = DeadPawn->GetOffset("Instigator", false);
			if (InstigatorOffset != -1)
				KillerPawn = Cast<AFortPlayerPawn>(DeadPawn->Get<APawn*>(InstigatorOffset));
		}
		auto KillerPlayerState = KillerPawn
			? Cast<AFortPlayerStateAthena>(KillerPawn->GetPlayerState())
			: nullptr;

		LOG_INFO(LogBots, "[CustomBot] [death] pawn={} ps={} dbno={} hp={:.0f} killer pawn={} ps={}",
			bool(DeadPawn), bool(DeadPlayerState),
			DeadPawn ? DeadPawn->IsDBNO() : false,
			DeadPawn ? (double)DeadPawn->GetHealth() : 0.0,
			bool(KillerPawn), bool(KillerPlayerState));

		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());

		bool bReadPawn = DeadPawn && !DeadPawn->IsActorBeingDestroyed();

		if (!bCountOnly && DeadPlayerState && bReadPawn)
		{
			auto DeathInfo = DeadPlayerState->GetDeathInfo();

			if (DeathInfo)
			{
				DeadPlayerState->ClearDeathInfo();

				FGameplayTagContainer ClassicTags{};
				if (MemberOffsets::FortPlayerPawn::CorrectTags != 0)
					ClassicTags = DeadPawn->Get<FGameplayTagContainer>(MemberOffsets::FortPlayerPawn::CorrectTags);

				FGameplayTagContainer CopyTags;
				for (int i = 0; i < ClassicTags.GameplayTags.Num(); ++i)
					CopyTags.GameplayTags.Add(ClassicTags.GameplayTags.at(i));
				for (int i = 0; i < ClassicTags.ParentTags.Num(); ++i)
					CopyTags.ParentTags.Add(ClassicTags.ParentTags.at(i));

				bool bDeathInfoReady = (Fortnite_Version > 1.8 || Fortnite_Version == 1.11);

				if (bDeathInfoReady)
				{
					uint8 DeathCause = ToDeathCause(ClassicTags, false, DeadPawn);

					if (MemberOffsets::DeathInfo::bDBNO != -1)
						*(bool*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::bDBNO) = DeadPawn->IsDBNO();
					if (MemberOffsets::DeathInfo::DeathCause != -1)
						*(uint8*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathCause) = DeathCause;
					if (MemberOffsets::DeathInfo::DeathLocation != -1)
						*(FVector*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathLocation) = DeadPawn->GetActorLocation();
					if (MemberOffsets::DeathInfo::DeathTags != -1)
						*(FGameplayTagContainer*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathTags) = CopyTags;
					if (MemberOffsets::DeathInfo::bInitialized != -1)
						*(bool*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::bInitialized) = true;
					if (MemberOffsets::DeathInfo::Distance != -1)
						*(float*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::Distance) =
							(KillerPawn && KillerPawn != DeadPawn) ? DeadPawn->GetDistanceTo(KillerPawn) : 0.0f;

					DeadPlayerState->OnRep_DeathInfo();
				}
			}
		}

		if (!bCountOnly && KillerPlayerState && KillerPlayerState != DeadPlayerState)
		{
			if (MemberOffsets::FortPlayerStateAthena::KillScore != -1)
				KillerPlayerState->Get<int>(MemberOffsets::FortPlayerStateAthena::KillScore)++;
			if (MemberOffsets::FortPlayerStateAthena::TeamKillScore != -1)
				KillerPlayerState->Get<int>(MemberOffsets::FortPlayerStateAthena::TeamKillScore)++;

			if (DeadPlayerState)
				KillerPlayerState->ClientReportKill(DeadPlayerState);
		}

		if (GameState)
		{
			GameState->GetPlayersLeft() = FMath::Clamp(GameState->GetPlayersLeft() - 1, 0, 9999);
			GameState->OnRep_PlayersLeft();
		}

		if (GameMode)
		{
			auto& Alive = GameMode->GetAlivePlayers();
			for (int i = 0; i < Alive.Num(); ++i)
			{
				if (Alive.At(i) == Bot.Controller)
				{
					Alive.RemoveAt(i, 1);
					break;
				}
			}
		}

		CheckVictoryRoyale();

		LOG_INFO(LogBots, "[CustomBot] [death] done playersLeft={} botsLeft={}",
			GameState ? GameState->GetPlayersLeft() : -1, (int)AllCustomBots.size());
	}

	static void HandleBotDeath(CustomBot& Bot)
	{
		if (Bot.bDeathHandled)
			return;

		ProcessBotDeathCounters(Bot);
		Bot.Destroy();
	}

	static void TickAll()
	{
		static unsigned TickAllCounter = 0;
		unsigned tc = ++TickAllCounter;

		if (DeferredBotOps)
			DeferredBotOps();

		PendingSkinBudget = 2;

		if (!bTickAllFirstLogDone)
		{
			LOG_INFO(LogBots, "[CustomBot] [tickall] FIRST invoke. AllCustomBots.size={}",
				AllCustomBots.size());
			bTickAllFirstLogDone = true;
		}

		if (tc % 300 == 0)
		{
			LOG_INFO(LogBots, "[CustomBot] [tickall] invoke #{} bots={}",
				tc, AllCustomBots.size());
		}

		LogMemDiag(tc);

		auto T0 = std::chrono::steady_clock::now();

		if (AllCustomBots.empty())
			return;

		std::vector<size_t> ToRemove;

		for (size_t i = 0; i < AllCustomBots.size(); ++i)
		{
			CustomBot& Bot = AllCustomBots[i];

			{
				auto Life = Bot.GetLifeState();
				bool bDead = !Bot.IsValidActor() || Life == CBT::ELifeState::Dead;

				if (bDead)
				{
					if (!Bot.bDeathHandled)
					{
						struct DeathCtx { CustomBot* Bot; } Ctx{ &Bot };
						TickBotSafeSEH([](void* P) {
							CustomBot& C = *((DeathCtx*)P)->Bot;
							if (C.IsValidActor() && C.GetLifeState() == CBT::ELifeState::Dead)
							{
								ProcessBotDeathCounters(C);
							}
							else
							{
								HandleBotDeath(C);
							}
						}, &Ctx);
					}

					if (!Bot.IsValidActor())
					{
						Bot.Destroy();
						ToRemove.push_back(i);
						continue;
					}
				}
			}

			TickCustomBotSafe(&Bot);
		}

		for (size_t i = ToRemove.size(); i-- > 0;)
			AllCustomBots.erase(AllCustomBots.begin() + ToRemove[i]);

		{
			auto T1 = std::chrono::steady_clock::now();
			auto Us = std::chrono::duration_cast<std::chrono::microseconds>(T1 - T0).count();

			static uint64_t PerfAccumUs = 0;
			static unsigned PerfFrames = 0;
			PerfAccumUs += Us;
			PerfFrames++;

			unsigned FramesPerReport = 300;
			if (PerfFrames >= FramesPerReport)
			{
double AvgMs = (double)PerfAccumUs / 1000.0 / (double)PerfFrames;
			LOG_INFO(LogBots, "[perf] bots={} botTick avg={:.2f}ms/frame last={:.2f}ms (x{} frames)",
				(int)AllCustomBots.size(), AvgMs, (double)Us / 1000.0, PerfFrames);
			PerfAccumUs = 0;
			PerfFrames = 0;
			}
		}
	}

	static void LogMemDiag(unsigned TickCount)
	{
		static unsigned LastDiagTick = 0;
		if (TickCount - LastDiagTick < 300)
			return;
		LastDiagTick = TickCount;

		PROCESS_MEMORY_COUNTERS PMC;
		memset(&PMC, 0, sizeof(PMC));
		PMC.cb = sizeof(PMC);
		if (GetProcessMemoryInfo(GetCurrentProcess(), &PMC, sizeof(PMC)))
		{
			LOG_INFO(LogBots, "[memdiag] WS={:.0f}MB Private={:.0f}MB PageFaults={}",
				(double)PMC.WorkingSetSize / (1024.0 * 1024.0),
				(double)PMC.PagefileUsage / (1024.0 * 1024.0),
				(unsigned long long)PMC.PageFaultCount);
		}

		{
			static unsigned LastObjectCountTick = 0;
			if (TickCount - LastObjectCountTick >= 3000)
			{
				LastObjectCountTick = TickCount;
				auto ObjectNum = ChunkedObjects ? ChunkedObjects->Num() : UnchunkedObjects ? UnchunkedObjects->Num() : 0;
				int TotalUObjects = 0;
				for (int i = 0; i < ObjectNum; i++)
				{
					if (GetObjectByIndex(i))
						TotalUObjects++;
				}
				LOG_INFO(LogBots, "[memdiag] UObjects total={}", TotalUObjects);
			}
		}

		constexpr bool bScanUObjects = false;
		if (bScanUObjects)
		{
		struct ClassCount { const char* Sub; int Count = 0; };
		ClassCount Counts[] = {
			{ "FortPickup" }, { "BuildingContainer" }, { "BuildingWall" },
			{ "FortPlayerPawn" }, { "FortPlayerController" }, { "FortPlayerState" },
			{ "FortInventory" }, { "FortWeapon" }, { "AbilitySystemComponent" },
			{ "SkeletalMeshComponent" }, { "NavigationPath" }, { "AthenaNavSystem" },
			{ "FortCustomizationAssetLoader" }, { "FortCharacterPart" },
			{ "BuildingSMActor" }, { "FortGameStateAthena" }, { "UWorld" },
		};

		int TotalUObjects = 0;
		int NumCounts = sizeof(Counts) / sizeof(Counts[0]);

		auto ObjectNum = ChunkedObjects ? ChunkedObjects->Num() : UnchunkedObjects ? UnchunkedObjects->Num() : 0;

		for (int i = 0; i < ObjectNum; i++)
		{
			auto Object = GetObjectByIndex(i);

			if (!Object)
				continue;

			TotalUObjects++;

			auto* Cls = Object->ClassPrivate;
			if (!Cls)
				continue;

			char ClsBuf[128];
			Cls->GetName().copy(ClsBuf, sizeof(ClsBuf) - 1);
			ClsBuf[Cls->GetName().size()] = '\0';

			for (int c = 0; c < NumCounts; ++c)
			{
				if (strstr(ClsBuf, Counts[c].Sub))
				{
					Counts[c].Count++;
					break;
				}
			}
		}

		std::string Diag = std::format("total={}", TotalUObjects);
		for (int c = 0; c < NumCounts; ++c)
		{
			if (Counts[c].Count > 0)
				Diag += std::format(" | {}={}", Counts[c].Sub, Counts[c].Count);
		}

		LOG_INFO(LogBots, "[memdiag] UObjects {}", Diag);
		}

		{
			auto World = GetWorld();
			int SubLevels = -1;
			if (World)
			{
				static int LevelsOff = World->GetOffset("Levels", false);
				if (LevelsOff != -1)
					SubLevels = World->Get<TArray<UObject*>>(LevelsOff).Num();
			}
			LOG_INFO(LogBots, "[memdiag] diag: levels={} bots={} mode={}",
				SubLevels, (int)AllCustomBots.size(), gBotTickMode);
		}
	}

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

	inline const char* gSpawnStage = "none";

	static CustomBot* SpawnCustomBot(const FTransform& SpawnTransform, AActor* InSpawnLocator = nullptr);

	static CustomBot* SpawnCustomBotInner(const FTransform& SpawnTransform, AActor* InSpawnLocator, CustomBot& OutBot)
	{
		LOG_INFO(LogBots, "[CustomBot] === SpawnCustomBot start ===");
		gSpawnStage = "setup";

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

		gSpawnStage = "emplace";
		CustomBot& Bot = OutBot;

		LOG_INFO(LogBots, "[CustomBot] Spawning controller...");
		gSpawnStage = "spawn-controller";
		Bot.Controller = GetWorld()->SpawnActor<AFortPlayerControllerAthena>(ControllerClass);

		if (!Bot.Controller)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to spawn controller!");
			return nullptr;
		}

		LOG_INFO(LogBots, "[CustomBot] Getting PlayerState...");
		gSpawnStage = "playerstate";
		Bot.PlayerState = Cast<AFortPlayerStateAthena>(Bot.Controller->GetPlayerState());

		if (!Bot.PlayerState)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to get playerstate!");
			Bot.Controller->K2_DestroyActor();
			return nullptr;
		}

		LOG_INFO(LogBots, "[CustomBot] Spawning pawn via native SpawnDefaultPawnAtTransform...");
		gSpawnStage = "spawn-pawn";
		{
			auto TSpawn0 = std::chrono::steady_clock::now();

			static auto DefaultPawnClassOffset = GameMode->GetOffset("DefaultPawnClass");
			GameMode->Get<UClass*>(DefaultPawnClassOffset) = PawnClass;

			static auto SpawnDefaultPawnAtTransformFn = FindObject<UFunction>(L"/Script/Engine.GameModeBase.SpawnDefaultPawnAtTransform");

			struct
			{
				AController* NewPlayer;
				FTransform SpawnTransform;
				APawn* ReturnValue;
			} AGameModeBase_SpawnDefaultPawnAtTransform_Params{ Bot.Controller, SpawnTransform };

			GameMode->ProcessEvent(SpawnDefaultPawnAtTransformFn, &AGameModeBase_SpawnDefaultPawnAtTransform_Params);

			auto TSpawn1 = std::chrono::steady_clock::now();
			LOG_INFO(LogBots, "[CustomBot] Native spawn took {}ms",
				(int)std::chrono::duration_cast<std::chrono::milliseconds>(TSpawn1 - TSpawn0).count());

			Bot.Pawn = Cast<AFortPlayerPawnAthena>(AGameModeBase_SpawnDefaultPawnAtTransform_Params.ReturnValue);

			auto* SpawnedRawPtr = AGameModeBase_SpawnDefaultPawnAtTransform_Params.ReturnValue;
			LOG_INFO(LogBots, "[CustomBot] Native SpawnDefaultPawnAtTransform returned {} pawn=0x{:x}",
				SpawnedRawPtr ? "OK" : "null", __int64(Bot.Pawn));
		}

		if (!Bot.Pawn)
		{
			LOG_WARN(LogBots, "[CustomBot] Native spawn failed; falling back to manual SpawnActor...");
			Bot.Pawn = GetWorld()->SpawnActor<AFortPlayerPawnAthena>(
				PawnClass,
				SpawnTransform,
				CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn));
		}

		if (!Bot.Pawn)
		{
			LOG_ERROR(LogBots, "[CustomBot] Failed to spawn pawn!");
			Bot.Controller->K2_DestroyActor();
			return nullptr;
		}

		Bot.PlayerState->SetIsBot(true);
		LOG_INFO(LogBots, "[CustomBot] SetIsBot=true");

		if (Bot.Controller->GetPawn() != Bot.Pawn)
			Bot.Controller->Possess(Bot.Pawn);
		LOG_INFO(LogBots, "[CustomBot] Possess done");

		SetCustomBotName(Bot, GameMode);
		LOG_INFO(LogBots, "[CustomBot] Name set");

		Bot.PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Bot.Controller);

		static auto SquadIdOffset = Bot.PlayerState->GetOffset("SquadId", false);

		if (SquadIdOffset != -1)
			Bot.PlayerState->GetSquadId() = Bot.PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;

		LOG_INFO(LogBots, "[CustomBot] Team={} Squad={}", Bot.PlayerState->GetTeamIndex(),
			SquadIdOffset != -1 ? (int)Bot.PlayerState->GetSquadId() : -1);

		GameState->AddPlayerStateToGameMemberInfo(Bot.PlayerState);

		Bot.Pawn->SetHealth(100);
		Bot.Pawn->SetMaxHealth(100);
		Bot.Pawn->SetShield(0);
		Bot.Pawn->SetMaxShield(100);
		LOG_INFO(LogBots, "[CustomBot] Health/Shield set to 100/100 y 0/100");

		LOG_INFO(LogBots, "[CustomBot] Granting abilities...");
		GrantAbilities(Bot);

		LOG_INFO(LogBots, "[CustomBot] Setting up inventory...");
		SetupInventory(Bot, GameMode);

		Bot.bInitialized = true;

		if (auto* SpawnCM = CustomBotMovement::GetCharacterMovement(Bot))
		{
			int OffZ = SpawnCM->GetOffset(std::string("GravityZ"), false);
			int OffS = SpawnCM->GetOffset(std::string("GravityScale"), false);
			if (OffZ != -1)
				Bot.GroundGravityZ = SpawnCM->Get<float>(OffZ);
			if (OffS != -1)
				Bot.GroundGravityScale = SpawnCM->Get<float>(OffS);
			LOG_INFO(LogBots, "[CustomBot] Captured ground gravity: GravityZ={} GravityScale={}", Bot.GroundGravityZ, Bot.GroundGravityScale);
		}

		gSpawnStage = "cosmetics";
		LOG_INFO(LogBots, "[CustomBot] Applying cosmetic loadout...");
		ApplyRandomCosmeticLoadout(Bot);

		GameMode->GetAlivePlayers().Add(Bot.Controller);
		++GameState->GetPlayersLeft();
		GameState->OnRep_PlayersLeft();

		// FIX RUNPHYS: SetIsBot(false) + UnPossess + bRunPhysicsWithNoController.
		// Los bots SIEMPRE se desposeen; el servidor simula el CMC sin controller.
		gSpawnStage = "sim";
		CustomBotMovement::EnableServerSimulation(Bot);
		LOG_INFO(LogBots, "[CustomBot] enableServerSimulation done");

		Bot.bSkinPending = true;
		LOG_INFO(LogBots, "[CustomBot] skin pending (deferred to tick)");

		if (!Bot.Pawn || Bot.Pawn->IsActorBeingDestroyed())
		{
			LOG_ERROR(LogBots, "[CustomBot] Pawn became invalid after initialization, aborting bot");
			Bot.Destroy();
			return nullptr;
		}

		FVector BotPos = Bot.Pawn->GetActorLocation();
		LOG_INFO(LogBots, "[CustomBot] === SpawnCustomBot DONE pos=({:.0f},{:.0f},{:.0f}) ===",
			BotPos.X, BotPos.Y, BotPos.Z);
		gSpawnStage = "done";

		AllCustomBots.emplace_back(std::move(Bot));
		return &AllCustomBots.back();
	}


	struct SpawnCtx
	{
		FTransform Transform;
		AActor* Locator = nullptr;
		CustomBot* Result = nullptr;
		CustomBot Bot;
	};

	static void SpawnCustomBotSehCallback(void* data)
	{
		auto* Ctx = (SpawnCtx*)data;
		Ctx->Result = SpawnCustomBotInner(Ctx->Transform, Ctx->Locator, Ctx->Bot);
	}

	static void FailSpawnCleanupCallback(void* data)
	{
		CustomBot* B = (CustomBot*)data;

		if (B->Controller)
		{
			B->Controller->K2_DestroyActor();
			B->Controller = nullptr;
		}

		if (B->Pawn)
		{
			B->Pawn->K2_DestroyActor();
			B->Pawn = nullptr;
		}

		if (B->WorldInventory)
		{
			B->WorldInventory->K2_DestroyActor();
			B->WorldInventory = nullptr;
		}

		B->PlayerState = nullptr;
		B->bInitialized = false;
	}

	static CustomBot* SpawnCustomBot(const FTransform& SpawnTransform, AActor* InSpawnLocator)
	{
		SpawnCtx Ctx;
		Ctx.Transform = SpawnTransform;
		Ctx.Locator = InSpawnLocator;

		if (!SpawnBotSafeSEH(SpawnCustomBotSehCallback, &Ctx))
		{
			TickBotSafeSEH(FailSpawnCleanupCallback, &Ctx.Bot);

			LOG_ERROR(LogBots, "[CustomBot] [SEH] SpawnCustomBot CRASH at stage '{}' — bot removed",
				gSpawnStage ? gSpawnStage : "?");
			return nullptr;
		}

		return Ctx.Result;
	}

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

	static void ApplyRandomCosmeticLoadout(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Bot.PlayerState)
		{
			LOG_WARN(LogBots, "[CustomBot] ApplyRandomCosmeticLoadout: bot not ready");
			return;
		}

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

		auto SpecOffset = HeroType->GetOffset("Specializations", false);

		if (SpecOffset == -1)
		{
			LOG_WARN(LogBots, "[CustomBot] HeroType has no Specializations offset, skipping cosmetic");
			return;
		}

		auto& Specs = HeroType->Get<TArray<TSoftObjectPtr<UObject>>>(SpecOffset);

		if (Specs.Num() == 0)
		{
			LOG_WARN(LogBots, "[CustomBot] HeroType has 0 specializations, skipping cosmetic");
			return;
		}

		LOG_INFO(LogBots, "[CustomBot] HeroType has {} specializations", Specs.Num());

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

		auto Tg0 = std::chrono::steady_clock::now();
		ApplyHID(Bot.Pawn, HeroType, true);
		auto Tg1 = std::chrono::steady_clock::now();
		LOG_INFO(LogBots, "[CustomBot] ApplyHID completed (bUseServerChoosePart=true) in {}ms",
			(int)std::chrono::duration_cast<std::chrono::milliseconds>(Tg1 - Tg0).count());
	}

	static void GrantAbilities(CustomBot& Bot)
	{
		auto PlayerAbilitySet = GetPlayerAbilitySet();
		auto AbilitySystemComponent = Bot.PlayerState->GetAbilitySystemComponent();

		if (PlayerAbilitySet && AbilitySystemComponent)
		{
			PlayerAbilitySet->GiveToAbilitySystem(AbilitySystemComponent);
		}
	}

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

		Bot.Controller->GetWorldInventory() = Bot.WorldInventory;

		Bot.WorldInventory->GetInventoryType() = EFortInventoryType::World;

		static auto bHasInitializedWorldInventoryOffset = Bot.Controller->GetOffset("bHasInitializedWorldInventory");
		Bot.Controller->Get<bool>(bHasInitializedWorldInventoryOffset) = true;

		auto& StartingItems = GameMode->GetStartingItems();

		for (int i = 0; i < StartingItems.Num(); ++i)
		{
			auto& StartingItem = StartingItems.at(i, FItemAndCount::GetStructSize());
			Bot.WorldInventory->AddItem(StartingItem.GetItem(), nullptr, StartingItem.GetCount());
		}

		UFortItem* PickaxeInstance = Bot.Controller->AddPickaxeToInventory();

		if (PickaxeInstance)
		{
			Bot.Controller->ServerExecuteInventoryItemHook(Bot.Controller, PickaxeInstance->GetItemEntry()->GetItemGuid());
		}

		static auto WoodItemData = FindObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
		if (WoodItemData)
			Bot.WorldInventory->AddItem(WoodItemData, nullptr, 50);

		Bot.WorldInventory->Update();

		return true;
	}
};
