#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotDebug.h"

#include "GameplayStatics.h"

#include "../CustomAI/CustomBotAI.h"

#include <chrono>
#include <algorithm>
#include <vector>

// DBLOCK: GetProcessMemoryInfo para el diagnostico de RAM del proceso.
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 1
#endif
#include <psapi.h>
//
// Crea instancias reales de jugador para el bot (AFortPlayerControllerAthena +
// AFortPlayerPawnAthena), replicando el patron de PlayerBot::Initialize del
// sistema antiguo (bots.h) pero como una entidad CustomBot independiente.
// NO modifica el sistema antiguo en absoluto.

namespace CustomBotSpawner
{
	// Envolturas SEH (definidas en CustomBotSEH.cpp, TU pure-C). Necesarias para
	// proteger el spawn: el __try no puede vivir en este header.
	extern "C" int SpawnBotSafeSEH(void (*cb)(void*), void* data);
	extern "C" void TickBotSafeSEH(void (*cb)(void*), void* data);
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

	// Hook opcional ejecutado en el game thread al principio de cada TickAll.
	// CustomBotManager lo usa para drenar las peticiones de spawn de la UI
	// (la GUI corre en el hilo render/GUI, NO en el game thread; spawinear
	// Fort actors desde ahi crashea: AssembleReferenceTokenStream en
	// non-game thread while GC is not locked).
	using TickHook = void (*)();
	inline TickHook DeferredBotOps = nullptr;

	// Presupuesto de skins pendientes por TickAll (se reinicia en cada tick): el
	// rebuild de mesh + replicacion de la skin se reparte como maximo 2 por tick
	// para que una rafaga de 5-22 bots no sature el loader/replicacion (causa del
	// hitch ~1s + crash al spawnear en rafaga).
	inline int PendingSkinBudget = 0;

	// Diagnostico de RAM + conteo UObjects (definido debajo; TickAll lo llama).
	static void LogMemDiag(unsigned TickCount);

	static void TickAll()
	{
		static unsigned TickAllCounter = 0;
		unsigned tc = ++TickAllCounter;

		// Drena la cola de spawn/ops de la UI (un bot por tick como maximo).
		if (DeferredBotOps)
			DeferredBotOps();

		// Reparto de skins diferidas: 2 como maximo por tick.
		PendingSkinBudget = 2;

		if (!bTickAllFirstLogDone)
		{
			LOG_INFO(LogBots, "[CustomBot] [tickall] FIRST invoke. AllCustomBots.size={}",
				AllCustomBots.size());
			bTickAllFirstLogDone = true;
		}

		if (tc % 300 == 0) // cada ~30s a 10tps (30 ticks * 10); produccion
		{
			LOG_INFO(LogBots, "[CustomBot] [tickall] invoke #{} bots={}",
				tc, AllCustomBots.size());
		}

		// Diagnostico de leak (RAM + conteo UObjects). Se mantiene activo cada
		// ~40s mientras el problema de memoria no este resuelto (el gate de 300
		// ticks vive dentro de LogMemDiag). OJO: barre TODO el object array
		// (~600k UObjects) con GetName() en el game thread; cada corrida puede
		// causar un pequeno stall periodico.
		LogMemDiag(tc);

		if (AllCustomBots.empty())
			return;

		std::vector<size_t> ToRemove;

		for (size_t i = 0; i < AllCustomBots.size(); ++i)
		{
			CustomBot& Bot = AllCustomBots[i];

			if (!Bot.IsValidActor())
			{
				LOG_INFO(LogBots, "[CustomBot] [tickall] INVALID bot idx={} controller={} pawn={}, removing",
					i, bool(Bot.Controller), bool(Bot.Pawn));

				Bot.Destroy();
				ToRemove.push_back(i);
				continue;
			}

			// SEH protection per-bot: si un bot crashea, lo saltamos sin matar el juego
			TickCustomBotSafe(&Bot);
		}

		for (size_t i = ToRemove.size(); i-- > 0;)
			AllCustomBots.erase(AllCustomBots.begin() + ToRemove[i]);
	}

	// Diagnostico de RAM + conteo de UObjects por clase (UNA sola pasada sobre
	// el object array). Llama a LogMemDiag cada ~300 invocaciones (~5s a 60tps).
	// Objetivo: ver que clase de objeto crece sin parar (el leak) y cuanta RAM
	// consume el proceso real del server.
	static void LogMemDiag(unsigned TickCount)
	{
		static unsigned LastDiagTick = 0;
		if (TickCount - LastDiagTick < 300)
			return;
		LastDiagTick = TickCount;

		// RAM del proceso (working set + private bytes).
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

		// Conteo por clase en una pasada. Nombre de clase -> subcadena a buscar.
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

			// FName -> string corta (usa buffer stack, sin heap alloc).
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

		// Diagnostico extra (hipotesis leak): sublevels cargados en el UWorld y
		// contador de replicaciones manuales del hook (NetDriver). Si sublevels
		// crece => streaming leak; si crece SRA/seg con bots => buffers de net.
		{
			auto World = GetWorld();
			int SubLevels = -1;
			if (World)
			{
				static int LevelsOff = World->GetOffset("Levels", false);
				if (LevelsOff != -1)
					SubLevels = World->Get<TArray<UObject*>>(LevelsOff).Num();
			}
			LOG_INFO(LogBots, "[memdiag] diag: levels={} sraCalls={} bots={}",
				SubLevels, gManualSraCalls, (int)AllCustomBots.size());
		}

		// Probe de regiones committed privadas (cada ~60s): enumera las N
		// regiones de memoria mas grandes del proceso con VirtualQuery. Sirve
		// para ver QUÉ heap crece ~10MB/s (arena del malloc del juego, buffers
		// de net, texturas RHI, stacks...). Si una region concreta crece de
		// forma lineal entre snapshots, ese es el allocator que fuga.
		if (TickCount % 600 == 0)
		{
			struct Region { size_t Size = 0; size_t Base = 0; };
			std::vector<Region> Big;
			size_t TotalCommitted = 0;
			size_t TotalRegions = 0;
			MEMORY_BASIC_INFORMATION MBI{};
			for (unsigned char* P = nullptr; ; P += MBI.RegionSize)
			{
				if (!VirtualQuery(P, &MBI, sizeof(MBI)))
					break;
				if (MBI.State == MEM_COMMIT && MBI.Type == MEM_PRIVATE)
				{
					TotalCommitted += MBI.RegionSize;
					TotalRegions++;
					if (MBI.RegionSize >= (8ull << 20))
						Big.push_back({ MBI.RegionSize, (size_t)P });
				}
			}
			std::sort(Big.begin(), Big.end(), [](const Region& a, const Region& b) { return a.Size > b.Size; });
			std::string R = std::format("VQ: committed={:.0f}MB regions={}", (double)TotalCommitted / (1024.0*1024.0), TotalRegions);
			for (size_t i = 0; i < Big.size() && i < 8; ++i)
				R += std::format(" | 0x{:08x}={:.0f}MB", (unsigned int)Big[i].Base, (double)Big[i].Size / (1024.0 * 1024.0));
			LOG_INFO(LogBots, "[memdiag] {}", R);
		}
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

	// Etapa actual del spawn (diagnostico): la envuelve el SEH de SpawnCustomBot
	// para saber EXACTAMENTE donde cayo un bot si algo crashea.
	inline const char* gSpawnStage = "none";

	// Spawna un bot custom en SpawnTransform. Envuelto en SEH (SpawnBotSafeSEH):
	// si un paso crashea se limpia el bot a medio construir y se devuelve nullptr.
	static CustomBot* SpawnCustomBot(const FTransform& SpawnTransform, AActor* InSpawnLocator = nullptr);

	// Implementacion real del spawn (la envuelve el SEH de arriba).
	// IMPORTANTE: el bot se construye en `OutBot` (stack/Ctx) y SOLO se commitea
	// a AllCustomBots al final, ya 100% inicializado. Antes se hacia
	// `AllCustomBots.emplace_back()` al inicio y cada tick del engine (TickAll)
	// veia el bot a medio construir (controller=false pawn=false), lo marcaba
	// INVALID y lo borraba, dejando una referencia colgante en este spawn ->
	// crash SEH + bots desapareciendo. Construyendolo fuera del vector, TickAll
	// nunca llega a verlo hasta que esta listo.
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

		// Construir la entidad como variable local (AUN no en AllCustomBots).
		gSpawnStage = "emplace";
		CustomBot& Bot = OutBot;

		// Orden de spawn correcto (ver bots.h Initialize): controller, luego pawn,
		// luego recuperar el playerstate de la posesion del controller.
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

		// FLUJO NATIVO: materializar el pawn EXACTAMENTE como lo hace el juego para
		// los jugadores reales (SpawnDefaultPawnForHook -> SpawnDefaultPawnAtTransform,
		// ver GameModeBase.cpp:125 y el drop del avion en FortPlayerController.cpp:761).
		// Replica la construccion real del PlayerPawn_Athena_C (BeginPlay nativo,
		// registro de componentes, dormancia y tick de movimiento).
		LOG_INFO(LogBots, "[CustomBot] Spawning pawn via native SpawnDefaultPawnAtTransform...");
		gSpawnStage = "spawn-pawn";
		{
			auto TSpawn0 = std::chrono::steady_clock::now();

			static auto DefaultPawnClassOffset = GameMode->GetOffset("DefaultPawnClass");
			GameMode->Get<UClass*>(DefaultPawnClassOffset) = PawnClass;

			static auto SpawnDefaultPawnAtTransformFn = FindObject<UFunction>(L"/Script/Engine.GameModeBase.SpawnDefaultPawnAtTransform");

			struct
			{
				AController* NewPlayer;                                            // (Parm, ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
				FTransform SpawnTransform;                                         // (Skipping exact layout; matches SpawnDefaultPawnAtTransform params)
				APawn* ReturnValue;                                                // (Parm, OutParm, ZeroConstructor, ReturnParm, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
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

		// Skin/cosmetico: ApplyHID con bUseServerChoosePart=true (ServerChoosePart).
		gSpawnStage = "cosmetics";
		LOG_INFO(LogBots, "[CustomBot] Applying cosmetic loadout...");
		ApplyRandomCosmeticLoadout(Bot);

		// Registrar en GameMode.
		GameMode->GetAlivePlayers().Add(Bot.Controller);
		++GameState->GetPlayersLeft();
		GameState->OnRep_PlayersLeft();

		// FIX RUNPHYS: SetIsBot(false) + UnPossess + bRunPhysicsWithNoController.
		gSpawnStage = "sim";
		CustomBotMovement::EnableServerSimulation(Bot);
		LOG_INFO(LogBots, "[CustomBot] enableServerSimulation done");

		// La visualizacion del mesh (rebuild de character parts + replicacion) se
		// DIFIERTE al tick del servidor: una rafaga de bots aplicando la skin
		// sincrona en el spawn satura el async loader ("Flushing async loaders" +
		// hitch ~1s) y provoca el crash al spawnear en rafaga. Se procesa como
		// maximo 2 skins por tick (CustomBotMovement::ApplyPendingSkin).
		Bot.bSkinPending = true;
		LOG_INFO(LogBots, "[CustomBot] skin pending (deferred to tick)");

		// Verificar que el pawn sigue vivo tras toda la inicializacion.
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

		// Commit al contenedor global SOLO aqui, ya 100% inicializado: a partir
		// de este punto TickAll lo ve y puede tickearlo, pero nunca a medio spawn.
		AllCustomBots.emplace_back(std::move(Bot));
		return &AllCustomBots.back();
	}

	// --- Envoltura SEH del spawn --------------------------------------------
	// SpawnCustomBot llama al inner DENTRO de SpawnBotSafeSEH (pure-C, CustomBotSEH.cpp).
	// Si un paso del spawn crashea, se limpia el bot a medio construir y se
	// devuelve nullptr en lugar de tumbar el servidor (igual que el tick, que ya
	// esta protegido con TickCustomBotSafe). gSpawnStage dice la etapa exacta.

	struct SpawnCtx
	{
		FTransform Transform;
		AActor* Locator = nullptr;
		CustomBot* Result = nullptr;
		CustomBot Bot;   // bot a medio construir (commit al vector al final)
	};

	static void SpawnCustomBotSehCallback(void* data)
	{
		auto* Ctx = (SpawnCtx*)data;
		Ctx->Result = SpawnCustomBotInner(Ctx->Transform, Ctx->Locator, Ctx->Bot);
	}

	// Limpieza de un bot a medio construir tras un crash en el spawn. Se invoca
	// bajo SEH (TickBotSafeSEH) para no morir de nuevo limpiando.
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
			// Limpiar el bot a medio construir (aun NO esta en AllCustomBots,
			// por eso se limpia Ctx.Bot directamente en lugar de back()).
			TickBotSafeSEH(FailSpawnCleanupCallback, &Ctx.Bot);

			LOG_ERROR(LogBots, "[CustomBot] [SEH] SpawnCustomBot CRASH at stage '{}' — bot removed",
				gSpawnStage ? gSpawnStage : "?");
			return nullptr;
		}

		return Ctx.Result;
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

	// Aplica una skin aleatoria al bot. Replica lo que hace el sistema antiguo
	// (FortServerBotManagerAthena::SpawnBotHook): HeroDefinition del CID ->
	// ApplyHID con bUseServerChoosePart=true (ServerChoosePart por parte). Es la
	// unica via que muestra skin sin depender del loadout MCP del controller.
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

		// Validar que el HeroType tiene specializations antes de usarlo.
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

		// Aplicar las character parts con bUseServerChoosePart=true (via
		// ServerChoosePart), EXACTAMENTE como el sistema antiguo que si muestra
		// la skin (FortServerBotManagerAthena::SpawnBotHook). La via false
		// (ApplyCharacterCosmetics) depende de que el hero se resuelva desde el
		// loadout MCP del controller; los bots no tienen AthenaProfile y el juego
		// registra "Failed to find hero ... HeroId: (empty)" y deja partes por
		// defecto. ServerChoosePart registra cada parte directamente en el pawn.
		auto Tg0 = std::chrono::steady_clock::now();
		ApplyHID(Bot.Pawn, HeroType, true);
		auto Tg1 = std::chrono::steady_clock::now();
		LOG_INFO(LogBots, "[CustomBot] ApplyHID completed (bUseServerChoosePart=true) in {}ms",
			(int)std::chrono::duration_cast<std::chrono::milliseconds>(Tg1 - Tg0).count());
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
