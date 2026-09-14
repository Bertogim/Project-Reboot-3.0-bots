#pragma once

#include "CustomBotTypes.h"

#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawnAthena.h"
#include "FortGameModeAthena.h"
#include "FortInventory.h"
#include "AbilitySystemComponent.h"

#include <vector>

// CustomBot - Entidad orquestadora del bot "Season 3".
//
// El bot es un JUGADOR REAL: AFortPlayerControllerAthena + AFortPlayerPawnAthena,
// por lo que hereda todo el gameplay (movimiento, inventario, armas, construccion,
// destruccion, interaccion, danio, eliminacion) de las APIs nativas.
//
// Parte 1: esta clase es el "cuerpo"/capacidades. NO contiene decisiones de IA.
// Los modulos (Movement/Perception/Inventory/Combat/Building/Destruction/
// Resources/Interaction/Spawner) operan sobre una instancia de CustomBot.

// Forward: contexto de IA de Parte 2 (definido en CustomAI/CustomBotAI.h).
// Se mantiene como puntero para no acoplar fuertemente los modulos del cuerpo con
// el motor de decisiones; CustomBotSpawner lo crea en el spawn y lo destruye.
struct BotAIContext;

class AFortPickup;
class ABuildingContainer;

class CustomBot
{
public:
	AFortPlayerControllerAthena* Controller = nullptr;
	AFortPlayerPawnAthena* Pawn = nullptr;
	AFortPlayerStateAthena* PlayerState = nullptr;
	AFortInventory* WorldInventory = nullptr;

	bool bInitialized = false;

	// Muerte ya gestionada (anti-doble-proceso). El engine NO ejecuta el flujo
	// nativo de muerte para estos bots simulados (SetIsBot(false)+UnPossess):
	// al matar a un bot su pawn se destruye y TickAll lo ve "invalid". HandleBotDeath
	// (CustomBotSpawner) completa la muerte a mano (kill feed + decremento de
	// PlayersLeft) UNA sola vez; este flag evita que ticks siguientes re-procesen
	// al bot ya con los punteros a nullptr.
	bool bDeathHandled = false;

	// --- Estado de movimiento (pipeline MoveTo + UpdateMovement por tick) ----
	// Almacena la peticion de movimiento activa; CustomBotMovement::UpdateMovement
	// (llamado desde CustomBotSpawner::TickAll en el tick del servidor) la consume.
	CBT::FMoveRequest MoveRequest;
	CBT::EMovementState MoveState = CBT::EMovementState::Idle;
	bool bMoveRequestActive = false;

	// true mientras el bot dispara un arma (o agita el pico): se resetea
	// automaticamente tras un corto periodo (auto-reset de seguridad) para que
	// no se quede pillado si StopFiring no se invoca (p.ej. melee sin kill).
	// El clamp de pitch de SetRotation SIEMPRE aplica ±45° con o sin este flag.
	bool bFiringWeapon = false;
	float bFiringWeaponTime = 0.0f;   // BotTime en que se activo
	static constexpr float kFiringWeaponTimeout = 0.5f; // duracion max del swing/disparo

	// --- Contexto de IA (Parte 2) --------------------------------------------
	// Motor de decisiones autonomas (estado, personalidad, objetivo actual, etc).
	// Es un puntero opaco (forward declaration) para que los modulos de capacidad
	// (Movement/Perception/...) no dependan del motor de IA. Lo gestiona el
	// CustomBotSpawner en el spawn/destruccion del bot.
	BotAIContext* AI = nullptr;

	// --- Posesion (Parte 2) ---------------------------------------------------
	// Cuando es true, el bot se mantiene POSEIDO durante la fase de bus para
	// poder saltar del avion. Cuando es false, CustomBotMovement::EnableServerSimulation
	// aplica el RUNPHYS: UnPossess + bRunPhysicsWithNoController (el servidor simula
	// el CMC "sin controller"). El bot en el bus desactiva este bool justo despues
	// de saltar para que la caida/planeo los gestione la simulacion real.
	bool bKeepPossessed = false;

	// Handshake CLAIM-LIVE ya enviado (ServerAcknowledgePossession). El debug bot
	// lo hace al entrar en MovingForward; aqui se fuerza UNA sola vez por bot en
	// CustomBotMovement::EnsureCMCActive (mismo efecto para todo bot normal).
	bool bClaimLiveDone = false;

	// CMC inicializado una sola vez: ProcessEvent (SetComponentTickEnabled,
	// Activate) y ServerAcknowledgePossession solo se ejecutan en el primer
	// tick de EnsureCMCActive. Antes se ejecutaban CADA tick x CADA bot (15
	// bots x 2 ProcessEvent x 60 tps = 1800 ProcessEvent/s), re-triggering
	// la re-evaluacion nativa de character parts -> mesh re-loading infinito.
	bool bCMCInitialized = false;

	// Contador para throttlear RestorePawnPlayerState (cada 30 ticks en vez
	// de cada tick). El write directo al pointer de PlayerState re-triggera
	// OnRep_PlayerState -> InitializeCharacterParts -> FortCustomizationAssetLoader.
	unsigned RestorePSCounter = 0;

	// El bot esta en el AIRE (bajando del bus / planeando): EnsureCMCActive NO
	// debe forzarle MovementMode=Walking mientras eso dure, o queda congelado
	// a ~80km flotando (modo Walking en el aire = sin gravedad). Se pone a
	// true al ejectar/saltar y a false al aterrizar (OnLanded).
	bool bInAirPhase = false;

	// Gravedad de JUGADOR capturada al spawn del pawn (valores del CMC antes de
	// que la fase de bus/skydive la reduzca). Despues de caer del avion se
	// vuelve a aplicar para que el bot tenga la misma gravedad que los jugadores.
	float GroundGravityZ = 0.0f;
	float GroundGravityScale = 0.0f;

	// --- Probe de movimiento (diagnostico) ----------------------------------
	// CustomBotMovement::EnsureCMCActive registra cada ventana de ~60 ticks el
	// desplazamiento horizontal mientras hay MoveTo activo (log [mprobe]), para
	// distinguir entre tirones del servidor y de la replicacion del cliente.
	unsigned ProbeTicks = 0;
	FVector ProbePrevLoc{};

	//--- Skin diferida ------------------------------------------------------
	// El rebuild de mesh + replicacion de la skin se aplaza del spawn al tick
	// (CustomBotMovement::ApplyPendingSkin, max 2 por TickAll) para que una
	// rafaga de spawns no sature el async loader y tumbe el servidor.
	bool bSkinPending = false;

	// --- Percepcion cacheada (bots pesados) --------------------------------
	// Los barridos UGameplayStatics:GetAllActorsOfClass sobre el mundo entero
	// son lo mas caro del bot (DoLooting hacia 4+ por frame; con 10 bots el
	// servidor perdia ticks -> movimiento lento/a saltos). El primer finder de
	// cada bucket (loot/jugadores/obstaculos) refresca su cache una vez cada
	// ScanCooldown y el resto de finders del mismo bucket lo reutilizan sin
	// volver a barrer el mundo. Lo gestiona CustomBotPerception.
	float LootScanTime = -1.0f;
	float PlayerScanTime = -1.0f;
	float ObstacleScanTime = -1.0f;

	// Radio con el que se refresco cada cache: si una llamada pide mas alcance
	// que el radio cacheado, se fuerza un refresco inmediato.
	float LootScanRadius = 0.0f;
	float PlayerScanRadius = 0.0f;
	float ObstacleScanRadius = 0.0f;

	AFortPickup* CachedNearestWeapon = nullptr;
	AFortPickup* CachedNearestConsumable = nullptr;
	AFortPickup* CachedNearestPickup = nullptr;
	ABuildingContainer* CachedNearestContainer = nullptr;
	AActor* CachedNearestEnemy = nullptr;
	AActor* CachedNearestAlly = nullptr;
	AActor* CachedNearestObstacle = nullptr;
	CBT::EObstacleType CachedObstacleType = CBT::EObstacleType::None;

	// Throttle de la LOS de UpdateMovement: la trace LineTraceSingle es mas
	// barata que un barrido de clase pero se hacía por tick con 10 bots; aqui
	// el estado BlockedPath se refresca como mucho cada ~0.25s por bot.
	float MoveLOSTime = -1.0f;
	bool bMoveLOSBlocked = false;

	// --- Pathfinding (navmesh) ----------------------------------------------
	// Ruta calculada por CustomBotPathfinding::QueryPath para el destino del
	// MoveTo activo. UpdateMovement recorre los waypoints en vez de ir en linea
	// recta. Se invalida al cambiar de destino (MoveTo) o al agotarse la
	// polilinea. TODO-PATH: el cooldown de re-consulta evita FindPath* por frame.
	std::vector<FVector> PathWaypoints;
	int PathIndex = 0;
	float PathQueryTime = -1.0f;
	bool bPathFollowBlocked = false; // LOS hacia el waypoint actual bloqueada
	static constexpr float PathQueryCooldown = 2.0f;      // re-consulta navmesh (s)
	static constexpr float PathWaypointAcceptance = 220.0f; // radio de llegada por waypoint

	// --- Desatascado fisico (fallback sin pathfinding) ---------------------
	// Maquina de estados del fallback "retrocede 2m + carrerilla 1m + salto +
	// romper con pico" cuando el bot se queda bloqueado en linea recta. La
	// maneja CustomBotBreak::TickUnstuck (tick del servidor, tras UpdateMovement).
	int UnstickStage = 0;       // 0 inactivo | 1 retroceder | 2 carrerilla | 3 saltar | 4 romper
	float UnstickTime = -1.0f;  // BotTime del ultimo cambio de etapa
	float BlockedSince = -1.0f; // BotTime en que se detecto el bloqueo persistente
	FVector UnstickGoal{};      // destino original del move (se restaura al terminar)
	FVector UnstickRefLoc{};    // posicion de referencia de la etapa
	int UnstickSwings = 0;      // golpes de pico dados en la etapa "romper"
	int UnstickFails = 0;       // ciclos de desatascado fallidos consecutivos (tope)
	FVector UnstickDetourDest{};// punto de rodeo (etapa "ir por otro lado")
	bool bUnstickDetourSet = false; // el desvio ya esta apuntado
	int UnstickDetourCount = 0; // lados de rodeo probados (0/1 -> +1, 2 -> -1)
	bool bUnstickRampBuilt = false; // rampa ya construida en la etapa de construir
	int UnstickRampCount = 0;   // rampas apiladas en la escalera de la etapa 6 (tope 3)
	int UnstickFloorCount = 0;  // suelos colocados encima de la escalera

	// --- Deteccion de atasco por PROGRESO real (no solo LOS) ----------------
	// Con un move activo, el bot se considera atascado si no se desplaza mas
	// de un umbral en una ventana de tiempo, aunque la LOS al destino este
	// despejada (empotrado contra un collider/estructura). Lo gestiona
	// CustomBotBreak con StuckWindowTime/StuckWindowPos/StuckTime.
	float StuckWindowTime = -1.0f; // BotTime del ultimo muestreo de progreso
	FVector StuckWindowPos{};      // posicion horizontal en el ultimo muestreo
	float StuckTime = 0.0f;        // segundos acumulados sin avanzar
	bool bInCombat = false;        // lo marca la IA (Fighting/Defending); evita desatascar en combate

	// --- Atasco PERSISTENTE (pathfinding fallback a los 10s) -----------------
	// Para el toggle "pathfinding solo si atascado": se acumula el tiempo en que
	// el bot NO supera su mejor avance horizontal hacia el goal (UnstickGoal si
	// hay desatascado activo, si no el destino del MoveTo actual). El desatascado
	// fisico (retroceder/saltar/rodear) tambien mueve al bot, asi que NO vale
	// medir solo "se mueve o no": se mide contra el progreso NETO hacia el goal.
	float StuckPersistTime = 0.0f;  // segundos sin superar el mejor avance
	FVector StuckPersistGoal{};     // goal para el que se mide el progreso
	float StuckPersistBest = 1e30f; // mejor distancia horizontal al goal en este episodio
	// Umbral: con el checkbox bCustomBotPathfindingFallback activo, un bot con
	// StuckPersistTime >= este umbral consulta el navmesh (ruta de waypoints).
	static constexpr float PathfindingFallbackStuckTime = 10.0f;

	// --- Desatascado etapa "romper" ------------------------------------------
	// Obstaculo/estructura detectado en TryBreakFront (etapa 4); si el swing del
	// pico no abre paso, se fuerza su destruccion (DestroyTarget) como tope.
	AActor* UnstickTarget = nullptr;

	bool HasMoveRequest() const
	{
		return bMoveRequestActive;
	}

	bool HasArrived() const
	{
		return bMoveRequestActive && MoveState == CBT::EMovementState::Arrived;
	}

	bool IsPathBlocked() const
	{
		return bMoveRequestActive && MoveState == CBT::EMovementState::BlockedPath;
	}

	// --- Hook de debug (opcional) --------------------------------------------
	// La secuencia de prueba "debugbot" (CustomBotDebug.cpp) registra aqui una
	// funcion que se invoca dentro de Tick() cada frame del servidor. Mantiene
	// el nucleo de CustomBot desacoplado de la logica de la secuencia.
	void (*DebugTick)(CustomBot& Self) = nullptr;

	// --- Estado / validez ---------------------------------------------------

	bool IsReady() const
	{
		return bInitialized && Controller && Pawn && PlayerState && WorldInventory;
	}

	bool IsValidActor() const
	{
		if (!Controller || !Pawn)
			return false;

		return !Controller->IsActorBeingDestroyed() && !Pawn->IsActorBeingDestroyed();
	}

	// --- Conveniencia de ubicacion ------------------------------------------

	FVector GetLocation() const
	{
		return Pawn ? Pawn->GetActorLocation() : FVector{};
	}

	FRotator GetRotation() const
	{
		return Pawn ? Pawn->GetActorRotation() : FRotator{};
	}

	bool HasAuthority() const
	{
		return Pawn && Pawn->HasAuthority();
	}

	// --- Estado de vida ------------------------------------------------------

	CBT::ELifeState GetLifeState() const
	{
		if (!Pawn)
			return CBT::ELifeState::Dead;

		if (Pawn->IsDBNO())
			return CBT::ELifeState::Downed;

		if (Pawn->GetHealth() <= 0.0f)
			return CBT::ELifeState::Dead;

		return CBT::ELifeState::Alive;
	}

	float GetHealth() const
	{
		return Pawn ? Pawn->GetHealth() : 0.0f;
	}

	float GetShield() const
	{
		return Pawn ? Pawn->GetShield() : 0.0f;
	}

	// --- Tick ---------------------------------------------------------------
	// Llamado periodicamente desde el loop del sistema custom bot (CustomBotSpawner).
	void Tick()
	{
		static unsigned BotTickCounter = 0;
		if ((++BotTickCounter) % 1200 == 0) // cada ~40s por bot; antes cada ~4s
			LOG_INFO(LogBots, "[CustomBot] [bot.tick] ready={} valid={} dbgTick={} life={}",
				IsReady(), IsValidActor(), DebugTick != nullptr, (int)GetLifeState());

		if (!IsReady() || !IsValidActor())
			return;

		// Auto-reset de bFiringWeapon: si el flag lleva mas de kFiringWeaponTimeout
		// segundos activo sin que StopFiring lo limpiara, se resetea automaticamente.
		// Evita que el flag se quede pillado en true cuando el bot hace swing de
		// pico sin matar al enemigo (StopFiring no se invoca en esa ruta).
		if (bFiringWeapon)
		{
			float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
			if (Now - bFiringWeaponTime > kFiringWeaponTimeout)
				bFiringWeapon = false;
		}

		// Secuencia de prueba (debugbot) si esta registrada.
		if (DebugTick)
			DebugTick(*this);

		// Parte 2: aqui se insertaran las decisiones de IA.
	}

	// --- Limpieza ------------------------------------------------------------
	void Destroy()
	{
		if (Pawn)
			Pawn->K2_DestroyActor();
		if (Controller)
			Controller->K2_DestroyActor();

		Controller = nullptr;
		Pawn = nullptr;
		PlayerState = nullptr;
		WorldInventory = nullptr;
		bInitialized = false;

		// La IA se libera aqui porque el bot deja de existir. Sera un
		// memory-leak si se crea y no se destruye; SpawnCustomBot lo crea y
		// solo se invoca Destroy() cuando el bot se elimina (TickAll).
		if (AI)
		{
			delete AI;
			AI = nullptr;
		}
	}
};
