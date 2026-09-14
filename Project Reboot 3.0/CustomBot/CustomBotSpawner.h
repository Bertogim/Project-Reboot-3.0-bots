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

// Forward de ToDeathCause (definida en FortPlayerController.cpp, linkage
// externa, scope global): traducir los tags de muerte del pawn a la causa de
// muerte del DeathInfo.
uint8 ToDeathCause(const FGameplayTagContainer& TagContainer, bool bWasDBNO, AFortPawn* Pawn);

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

	// Muerte de un bot: completa a mano el flujo que el engine NO ejecuta para
	// estos bots simulados (SetIsBot(false)+UnPossess): el pawn muere por danio
	// y el flow nativo de death/kill-feed del jugador reales no corre (sin
	// ClientOnPawnDied/RemoveFromAlivePlayers -> sin kill feed ni decremento de
	// PlayersLeft). Secuencia: 1) guard anti-doble proceso, 2) contadores y
	// COMBAT-FEED (credito al killer + DeathInfo + OnRep_DeathInfo),
	// 3) --PlayersLeft + OnRep, 4) quitar de GetAlivePlayers, 5) Destroy().

	// Contadores + kill feed de la muerte de un bot (SIN destruir). Se separa de
	// la destruccion para re-usarlo desde HandleBotDeath y desde RemoveAllBots
	// (marcar la salida de bots como muertes: decrementa los vivos). DEBE correr
	// bajo SEH: lee del pawn muerto (posible carrera con su destruccion).
	// bCountOnly: solo contadores (PlayersLeft/AlivePlayers), sin kill feed/chat
	// (usado por RemoveAllBots para no spamear eliminaciones al limpiar).
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

		// KILL FEED + CHAT DE MUERTE: rellenar el DeathInfo del muerto (espejo de
		// ClientOnPawnDiedHook) y disparar OnRep_DeathInfo para que el cliente
		// muestre la eliminacion ("ha sido eliminado/a", kill feed). Solo se lee
		// del pawn si sigue vivo (0 HP recien o en destruccion); si el pawn ya lo
		// destruyo el engine, se omite la parte de tags (el credito de kill via
		// ClientReportKill + score se da igualmente).
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

				// Copia manual por elemento (la copia de TArray dentro del Get<> es
				// superficial y se libera al salir de scope): DeathTags del DeathInfo
				// no debe colgar del temporal. Mismo esquema que ClientOnPawnDiedHook.
				FGameplayTagContainer CopyTags;
				for (int i = 0; i < ClassicTags.GameplayTags.Num(); ++i)
					CopyTags.GameplayTags.Add(ClassicTags.GameplayTags.at(i));
				for (int i = 0; i < ClassicTags.ParentTags.Num(); ++i)
					CopyTags.ParentTags.Add(ClassicTags.ParentTags.at(i));

				// La escritura del DeathInfo solo aplica a versiones con ese struct
				// (>1.8 o 1.11), igual que en ClientOnPawnDiedHook; la caida a
				// kill credit/PlayersLeft de abajo corre SIEMPRE.
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

		// Credito de kill: score + kill feed nativo del killer (ClientReportKill).
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

		// Variable propia del DLL para el launcher: si tras esta muerte solo
		// queda 1 equipo vivo, se loguea [VictoryRoyale].
		CheckVictoryRoyale();

		LOG_INFO(LogBots, "[CustomBot] [death] done playersLeft={} botsLeft={}",
			GameState ? GameState->GetPlayersLeft() : -1, (int)AllCustomBots.size());
	}

	// Muerte de un bot: contadores + kill feed + destruccion definitiva.
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

		auto T0 = std::chrono::steady_clock::now();

		if (AllCustomBots.empty())
			return;

		std::vector<size_t> ToRemove;

		for (size_t i = 0; i < AllCustomBots.size(); ++i)
		{
			CustomBot& Bot = AllCustomBots[i];

			// Muerte del bot: pawn destruido por el engine al matarlo O salud
			// reducida a 0. DBNO cuenta como vivo (puede revivir / sigue en los
			// vivos). El flow nativo de muerte no corre para estos bots, asi que
			// lo completa HandleBotDeath/ProcessBotDeathCounters.
			{
				auto Life = Bot.GetLifeState();
				bool bDead = !Bot.IsValidActor() || Life == CBT::ELifeState::Dead;

				if (bDead)
				{
					if (!Bot.bDeathHandled)
					{
						// Toda la logica (contadores + kill feed) bajo SEH: el
						// pawn puede destruirse a mitad de proceso por el propio
						// engine (UAF del Instigator/tags = el crash historico de
						// muertes por tormenta). Si el SEH captura algo, el bot
						// se reintenta el siguiente tick sin tumbar el server.
						struct DeathCtx { CustomBot* Bot; } Ctx{ &Bot };
						TickBotSafeSEH([](void* P) {
							CustomBot& C = *((DeathCtx*)P)->Bot;
							if (C.IsValidActor() && C.GetLifeState() == CBT::ELifeState::Dead)
							{
								// Pawn vivo a 0 HP: el engine esta en el deathflow
								// (animacion/cadaver). Marcar + feed, y NO destruir:
								// el motor termina la muerte y el siguiente tick
								// (pawn invalido) hace la limpieza final.
								ProcessBotDeathCounters(C);
							}
							else
							{
								// Pawn ya reclamado por el engine: limpieza total
								// inmediata (HandleBotDeath es idempotente por el
								// bDeathHandled). Si el SEH fallo sin marcar, el
								// bot se reintenta el siguiente tick.
								HandleBotDeath(C);
							}
						}, &Ctx);
					}

					// Limpieza: solo cuando el pawn ya no existe. Si sigue vivo a
					// 0 HP tras el feed (bDeathHandled) se dejan un par de ticks
					// para que la animacion de muerte se vea; BotTickCallbackImpl
					// no mueve cadaveres (ver guard en CustomBotDebug.cpp).
					if (!Bot.IsValidActor())
					{
						Bot.Destroy();
						ToRemove.push_back(i);
						continue;
					}
				}
			}

			// SEH protection per-bot: si un bot crashea, lo saltamos sin matar el juego
			TickCustomBotSafe(&Bot);
		}

		for (size_t i = ToRemove.size(); i-- > 0;)
			AllCustomBots.erase(AllCustomBots.begin() + ToRemove[i]);

		// Diagnostico de CPU del tick de bots: cuantos ms de game-thread se
		// comen los bots por frame (suma de EnsureCMCActive+Tick+Movimiento+IA
		// de TODOS los bots). Se loguea cada ~300 ticks con la media; si la media
		// ronda 10-15ms por frame el server nota lageo con pocos bots.
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

		// Conteo TOTAL de UObjects sin GetName() (solo int++, cero heap): dice
		// si la explosion con bots es de objetos del engine (armas/proyectiles/
		// fx acumulandose) o solo de arena/buffers. El barrido por clase con
		// GetName() estaba aqui; era EL leak del idle (std::string por objeto).
		// OJO PERF: recorrer TODOS los slots (~618k) con GetObjectByIndex en el
		// game thread era el hitch periodico de +1.1s (STAT_FrameTime +1148ms en
		// el launcher.log). Con el leak ya resuelto el total se espacia a cada
		// ~3000 ticks (~50s); la RAM del proceso se sigue viendo cada 300 ticks.
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
		}

		// Diagnostico extra (hipotesis leak): sublevels cargados en el UWorld.
		// Si crece => streaming leak (el A/B de replicacion manual se retiro;
		// la replicacion la hace SOLO el engine).
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
		Bot.Pawn->SetShield(0);
		Bot.Pawn->SetMaxShield(100);
		LOG_INFO(LogBots, "[CustomBot] Health/Shield set to 100/100 y 0/100");

		// Capturar la gravedad de JUGADOR del CMC en este momento (recien
		// spawnado, antes de que la fase de bus/skydive la reduzca). Se usa
		// despues para restaurar al bot caigravedad/normal al tocar suelo.
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

		// 50 de madera al spawnear: los bots necesitan materiales para
		// construir (rampas/paneles cuando estan atascados) y para que el
		// sistema de building no les rechace las piezas por falta de mats.
		static auto WoodItemData = FindObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
		if (WoodItemData)
			Bot.WorldInventory->AddItem(WoodItemData, nullptr, 50);

		Bot.WorldInventory->Update();

		return true;
	}
};
