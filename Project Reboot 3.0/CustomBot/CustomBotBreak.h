#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotCombat.h"
#include "CustomBotDestruction.h"
#include "CustomBotBuilding.h"
#include "CustomBotDoors.h"

// CustomBot - Desatascado fisico (fallback cuando NO hay ruta).
//
// Si pathfinding esta OFF (checkbox en la UI: bCustomBotPathfinding=false) o la
// ruta fallo/vacio, el bot va en linea recta. La deteccion de atasco NO se basa
// solo en la LOS (empotrado contra un collider con la LOS despejada camina para
// siempre): se mide el PROGRESO REAL del pawn. Con un move activo, si el bot no
// se desplaza mas de kMinProgress en una ventana de kProgressWindow, se acumula
// StuckTime; al llegar a kStuckAccumulated se considera atascado y se ejecuta la
// secuencia pedida:
//   1) retrocede 2m
//   2) corre unos metros de carrerilla (sprint corto)
//   3) salta
//   4) respiro post-salto; si sigue sin pasar: abre la puerta delante o rompe
//      lo que tenga delante con el pico (swing melee real via CustomBotCombat)
//   5) si no se puede romper/abrir: retrocede AUN MAS
//   6) construye una rampa en la celda delante y sube por ella
//   7) si nada funciona: rodea el obstaculo por los lados;
//      agotados los ciclos, devuelve el move a la IA para que re-elija destino.
//
// REGLA: NUNCA teletransporta. Todo sale de la fisica simulada del pawn
// (MoveTo/Velocity/Jump/Launch). Lo invoca BotTickCallbackImpl justo DESPUES de
// UpdateMovement, una vez por bot, bajo el SEH del servidor.

namespace CustomBotBreak
{
	// --- Deteccion de atasco por progreso ------------------------------------
	static constexpr float kProgressWindow = 0.9f;   // ventana de muestreo de progreso (s)
	static constexpr float kMinProgress = 40.0f;     // desplazamiento minimo por ventana (u)
	static constexpr float kStuckAccumulated = 1.0f; // segundos sin avanzar que disparan el desatasco

	// --- Secuencia de desatascado --------------------------------------------
	static constexpr float kBackupDistance = 220.0f; // "retrocede 2m" (unreal units)
	static constexpr float kRunUpDistance = 260.0f;  // carrerilla corta antes del salto
	static constexpr float kStageTimeout = 1.2f;     // limite de tiempo por etapa
	static constexpr float kPostJumpRespite = 0.9f;  // respiro despues del salto
	static constexpr float kMaxSwings = 8;           // golpes de pico antes de rendirse
	static constexpr float kBreakRange = 320.0f;     // alcance del swing del pico
	static constexpr float kBreakCooldown = 0.45f;   // segundos entre swings
	static constexpr float kBuildBackup = 350.0f;    // cuanta distancia se retrocede para construir
	static constexpr float kBuildRespite = 1.8f;     // margen para subir por la rampa
	static constexpr float kDetourRadius = 550.0f;   // distancia lateral del rodeo
	static constexpr float kDetourTimeout = 2.5f;    // limite de tiempo en cada lado del rodeo
	static constexpr int kMaxUnstickFails = 3;       // ciclos fallidos: ceder a la IA

	// Reinicia por completo la maquina de desatascado y el detector de progreso.
	static void Reset(CustomBot& Bot)
	{
		Bot.UnstickStage = 0;
		Bot.UnstickTime = -1.0f;
		Bot.BlockedSince = -1.0f;
		Bot.UnstickSwings = 0;
		Bot.UnstickGoal = FVector{};
		Bot.UnstickRefLoc = FVector{};
		Bot.bUnstickDetourSet = false;
		Bot.UnstickDetourDest = FVector{};
		Bot.UnstickDetourCount = 0;
		Bot.bUnstickRampBuilt = false;
		Bot.UnstickRampCount = 0;
		Bot.UnstickFloorCount = 0;

		Bot.StuckWindowTime = -1.0f;
		Bot.StuckWindowPos = FVector{};
		Bot.StuckTime = 0.0f;

		// Obstaculo detectado en la etapa "romper" (solo relevante durante esa
		// etapa; se limpia en el siguiente Reset si no se destruyo).
		Bot.UnstickTarget = nullptr;
	}

	// Avanza el detector de atasco por progreso: muestrea la posicion horizontal
	// cada kProgressWindow y acumula/decae StuckTime segun si hubo desplazamiento.
	// Se llama UNA vez por tick con un move activo (desde TickUnstuck).
	static void AdvanceStuckDetector(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		float Now = CustomBotPerception::BotTime();

		if (!Bot.bMoveRequestActive)
		{
			Bot.StuckWindowTime = -1.0f;
			Bot.StuckTime = 0.0f;
			return;
		}

		if (Bot.StuckWindowTime < 0.0f)
		{
			Bot.StuckWindowTime = Now;
			Bot.StuckWindowPos = Bot.Pawn->GetActorLocation();
			return;
		}

		if (Now - Bot.StuckWindowTime < kProgressWindow)
			return;

		float Moved = CustomBotMovement::HorizontalDistance(Bot.StuckWindowPos, Bot.Pawn->GetActorLocation());

		if (Moved >= kMinProgress)
			Bot.StuckTime = FMath::Max(0.0f, Bot.StuckTime - kProgressWindow);
		else
			Bot.StuckTime += kProgressWindow;

		Bot.StuckWindowTime = Now;
		Bot.StuckWindowPos = Bot.Pawn->GetActorLocation();

		// --- Atasco PERSISTENTE (fallback navmesh de los 10s) ------------------
		// Se mide contra el GOAL real (UnstickGoal mientras haya desatascado
		// activo; si no, el destino del MoveTo). El desatascado fisico tambien
		// mueve al bot (retrocede/salta/rodea), asi que NO vale medir "se mueve o
		// no": solo se considera progreso si el bot SUPERA su mejor distancia
		// horizontal al goal en este episodio. Paredes/edificios no superables
		// hacen crecer el contador hasta el umbral (CustomBot::StuckPersistTime
		// >= CustomBot::PathfindingFallbackStuckTime) y entonces el navmesh entra
		// de escape (UpdateMovement + bCustomBotPathfindingFallback).
		{
			FVector Goal = (Bot.UnstickStage > 0) ? Bot.UnstickGoal : Bot.MoveRequest.Destination;

			// Goal nuevo (destino redirigido por la IA): re-anclar la medicion y
			// empezar un episodio de atasco limpio para el nuevo objetivo. Durante
			// el desatascado el goal no cambia (UnstickGoal constante), asi que no
			// resetea mientras la secuencia de desatascado este en marcha.
			if (FMath::Abs(Goal.X - Bot.StuckPersistGoal.X) > 50.0f ||
				FMath::Abs(Goal.Y - Bot.StuckPersistGoal.Y) > 50.0f)
			{
				Bot.StuckPersistGoal = Goal;
				Bot.StuckPersistBest = CustomBotMovement::HorizontalDistance(Bot.Pawn->GetActorLocation(), Goal);
				Bot.StuckPersistTime = 0.0f;
			}

			float GoalDist = CustomBotMovement::HorizontalDistance(Bot.Pawn->GetActorLocation(), Goal);

			if (GoalDist < Bot.StuckPersistBest - kMinProgress)
			{
				// Progreso NETO hacia el goal: reiniciar el episodio de atasco.
				Bot.StuckPersistBest = GoalDist;
				Bot.StuckPersistTime = 0.0f;
			}
			else
			{
				Bot.StuckPersistBest = FMath::Min(Bot.StuckPersistBest, GoalDist);
				Bot.StuckPersistTime = FMath::Min(Bot.StuckPersistTime + kProgressWindow, 60.0f);
			}
		}
	}

	// true si el bot lleva acumulado el umbral de tiempo sin avanzar.
	static bool IsStuck(const CustomBot& Bot)
	{
		return Bot.bMoveRequestActive && Bot.StuckTime >= kStuckAccumulated;
	}

	// true si el bot avanza de verdad hacia el goal (desplazamiento real desde
	// el punto de referencia de la etapa + LOS al goal despejada) o ya llego.
	static bool MadeRealProgress(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		if (Bot.HasArrived())
			return true;

		if (CustomBotMovement::HorizontalDistance(Bot.UnstickRefLoc, Bot.Pawn->GetActorLocation()) < kMinProgress)
			return false;

		return !Bot.IsPathBlocked();
	}

	// Etapa 4 (romper): abrir la puerta si la hay delante, si no apuntar el pico
	// al obstaculo/estructura destructible mas cercano que este DELANTE del bot.
	// Mientras tanto se mantiene el empuje hacia el goal: al destruirse el
	// obstaculo el bot atraviesa de golpe (MadeRealProgress lo detecta). El
	// swing (FireWeapon) lo aplica el propio FSM cada kBreakCooldown.
	// Devuelve true si "hizo algo" (hay puerta que abrir o algo que romper).
	static bool TryBreakFront(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		// 1) Puerta cerrada enfrente: abrirla (mas barato que romper).
		if (CustomBotDoors::TryOpenDoorInFront(Bot))
			return true;

		// 2) Obstaculo de estructuras: solo si esta DELANTE y se puede destruir.
		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector GoalDir = CustomBotMovement::DirectionTo(BotLoc, Bot.UnstickGoal);

		CBT::EObstacleType Type = CBT::EObstacleType::None;
		// Buscar el obstaculo mas cercano DENTRO del cono frontal (FindFrontObstacle),
		// no el mas cercano global: antes, si un arbol/roca a un lado estaba mas
		// cerca que la pared de delante, la pared se ignoraba y el bot seguia sin
		// picar (FindNearestObstacle devuelve UN unico candidato). El filtro
		// descarta las piezas NO destructibles (el suelo del terreno a los pies
		// suele ser el "mas cercano" del cono y no es lo que bloquea el paso).
		AActor* Obstacle = CustomBotPerception::FindFrontObstacle(Bot, kBreakRange, GoalDir, 0.3f, Type,
			[](AActor* Actor) -> bool
			{
				auto Building = Cast<ABuildingActor>(Actor);
				return Building && CustomBotDestruction::CanDestroy(Building);
			});

		if (!Obstacle)
			return false;

		auto Building = Cast<ABuildingActor>(Obstacle);

		if (!Building || !CustomBotDestruction::CanDestroy(Building))
			return false;

		if (Bot.Pawn->GetDistanceTo(Obstacle) > kBreakRange)
			return false;

		// Mirar al obstaculo (apunta el swing) pero seguir empujando hacia el goal.
		Bot.UnstickTarget = Obstacle;
		CustomBotMovement::LookAt(Bot, Obstacle->GetActorLocation());
		CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 60.0f, false);
		return true;
	}

	// Etapa 6 (construir): levanta una rampa en la celda del grid inmediatamente
	// delante (hacia el goal), con el Z del TERRENO de esa celda. Misma tecnica
	// que TryResolveBlockedPath y la secuencia debug. Devuelve true si se coloco.
	static bool BuildRampUp(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		// Sin materiales (cuesta 10) no hay nada que construir.
		if (CustomBotResources::GetTotalResourceCount(Bot) < 10)
			return false;

		FVector BotLoc = Bot.Pawn->GetActorLocation();

		auto GS = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto SSS = GS ? GS->GetStructuralSupportSystem() : nullptr;

		if (!SSS)
			return false;

		// Cardinal hacia el goal (el bot ya esta mirando hacia alli en esta etapa).
		float Facing = CustomBotBuilding::SnapYawToCardinal(Bot.GetRotation().Yaw);

		FRotator RampRot = Bot.GetRotation();
		RampRot.Yaw = CustomBotBuilding::SnapYawToCardinal(Facing + 90.0f);

		// Posicion: la celda del grid inmediatamente adelante, con el Z del terreno.
		FVector RampLoc = BotLoc;
		if (!CustomBotBuilding::CellCenterAhead(SSS, BotLoc, Facing, 1, RampLoc))
			return false;

		FVector RampGround = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
			FVector{ RampLoc.X, RampLoc.Y, 0.0f }, BotLoc.Z + 3000.0f, BotLoc.Z - 8000.0f, FName(0));
		RampLoc.Z = RampGround.Z;

		auto Ramp = CustomBotBuilding::BuildRamp(Bot, RampLoc, RampRot);

		if (!Ramp)
			return false;

		LOG_INFO(LogBots, "[CustomBot] unstuck: built ramp at ({:.0f},{:.0f},{:.0f}) facing {:.0f}",
			RampLoc.X, RampLoc.Y, RampLoc.Z, Facing);
		return true;
	}

	// Etapa 6 (construir - remate): coloca un SUELO en la celda delante a la
	// ALTURA ACTUAL del bot, para asentar la cima de la escalera de rampas y
	// poder pararse/cruzar tras subir 3 rampas. Misma tecnica de grid que
	// BuildRampUp. Devuelve true si se coloco.
	static bool BuildFlatOnTop(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		if (CustomBotResources::GetTotalResourceCount(Bot) < 10)
			return false;

		FVector BotLoc = Bot.Pawn->GetActorLocation();

		auto GS = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto SSS = GS ? GS->GetStructuralSupportSystem() : nullptr;

		if (!SSS)
			return false;

		float Facing = CustomBotBuilding::SnapYawToCardinal(Bot.GetRotation().Yaw);

		FVector FloorLoc = BotLoc;
		if (!CustomBotBuilding::CellCenterAhead(SSS, BotLoc, Facing, 1, FloorLoc))
			return false;

		// Z a la altura ACTUAL del bot (sobre la ultima rampa), no al suelo.
		FloorLoc.Z = BotLoc.Z;

		auto Floor = CustomBotBuilding::BuildFloor(Bot, FloorLoc, FRotator{ 0.0f, Facing, 0.0f });

		if (!Floor)
			return false;

		LOG_INFO(LogBots, "[CustomBot] unstuck: built floor on top of ramp stairs at ({:.0f},{:.0f},{:.0f})",
			FloorLoc.X, FloorLoc.Y, FloorLoc.Z);
		return true;
	}

	// Etapa 7 (rodear): busca un punto lateral alcanzable para rodear el
	// obstaculo. Prueba en abanico desde casi de frente hacia el lado elegido
	// (0 = izquierda, 1 = derecha) y devuelve el primer punto con LOS despejada.
	static bool FindDetourPoint(CustomBot& Bot, int Side, FVector& OutPoint)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Bot.UnstickGoal);

		if (!(Dir | Dir))
			return false;

		float Base = FMath::Atan2(Dir.Y, Dir.X) * CustomBotMovement::RAD_TO_DEG;
		float Sign = (Side == 0) ? 90.0f : -90.0f;

		for (int i = 0; i < 5; ++i)
		{
			float Ang = (Base + Sign * (20.0f + i * 20.0f)) * 3.14159265358979323846f / 180.0f;
			FVector Probe{ BotLoc.X + FMath::Cos(Ang) * kDetourRadius,
				BotLoc.Y + FMath::Sin(Ang) * kDetourRadius,
				BotLoc.Z };

			if (CustomBotPerception::HasLineOfSight(Bot, Probe))
			{
				OutPoint = Probe;
				return true;
			}
		}

		return false;
	}

	// Tick del desatascado. Se llama UNA vez por bot y tick (despues de
	// UpdateMovement). Solo actua con un move activo: con la LOS despejada pero
	// SIN progreso real (empotrado contra un collider), o con la LOS bloqueada.
	static void TickUnstuck(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
		{
			Reset(Bot);
			return;
		}

		// Nunca desatascarse dentro del bus/avion (el pawn va de pasajero).
		if (Bot.PlayerState && Bot.PlayerState->IsInAircraft())
		{
			Reset(Bot);
			return;
		}

		// Tampoco mientras caiga/planeando tras saltar del bus.
		if (Bot.bInAirPhase)
		{
			Reset(Bot);
			return;
		}

		// No interferir en combate: la IA (DoFighting/TryResolveBlockedPath)
		// gestiona saltar/destruir/construir mientras persigue o huye.
		if (Bot.bInCombat)
		{
			Reset(Bot);
			return;
		}

		if (!Bot.bMoveRequestActive)
		{
			Reset(Bot);
			return;
		}

		// Si ya se supero el tope de ciclos fallidos, devolver el move a la IA
		// (que re-elegira otro destino) en vez de seguir dando vueltas.
		if (Bot.UnstickFails >= kMaxUnstickFails)
		{
			CustomBotMovement::StopMovement(Bot);
			Bot.UnstickFails = 0;
			Reset(Bot);
			return;
		}

		// Pathfinding fallback activo: si el bot lleva >=10s sin avanzar y ya hay
		// ruta navmesh en marcha, dejamos que la siga. El desatascado fisico
		// (retroceder/saltar/romper) interferiria con los waypoints de la ruta.
		if (bCustomBotPathfindingFallback &&
			Bot.StuckPersistTime >= CustomBot::PathfindingFallbackStuckTime &&
			!Bot.PathWaypoints.empty())
		{
			return;
		}

		float Now = CustomBotPerception::BotTime();
		const bool bUnstucking = Bot.UnstickStage > 0;

		// Detector de progreso SIEMPRE en marcha con un move activo (la maquina
		// y el resto del sistema lo leen para saber si se avanza de verdad).
		AdvanceStuckDetector(Bot);

		if (!bUnstucking)
		{
			// Al llegar al destino no hay nada que desatascar.
			if (Bot.HasArrived())
			{
				Reset(Bot);
				return;
			}

			// Entrar SOLO si el detector de progreso confirma el atasco: la LOS
			// despejada sin desplazamiento tambien cuenta (collider invisible).
			if (!IsStuck(Bot))
				return;

			// --- Arranque: guardar el goal y retroceder (etapa 1). ---
			Bot.UnstickGoal = Bot.MoveRequest.Destination;
			++Bot.UnstickFails;

			Bot.UnstickStage = 1;
			Bot.UnstickTime = Now;

			FVector Away = Bot.Pawn->GetActorLocation()
				- Bot.Pawn->GetActorForwardVector() * kBackupDistance;

			CustomBotMovement::MoveTo(Bot, Away, 40.0f, false);
			return;
		}

		switch (Bot.UnstickStage)
		{
		case 1:
			// Retrocediendo a Away: al llegar (o por timeout) -> carrera.
			if (Bot.HasArrived() || Now - Bot.UnstickTime > kStageTimeout)
			{
				Bot.UnstickStage = 2;
				Bot.UnstickTime = Now;

				FVector BotLoc = Bot.Pawn->GetActorLocation();
				FVector Fwd = CustomBotMovement::DirectionTo(BotLoc, Bot.UnstickGoal);
				Bot.UnstickRefLoc = BotLoc;
				CustomBotMovement::MoveTo(Bot, BotLoc + Fwd * kRunUpDistance, 30.0f, true);
			}
			break;

		case 2:
			// Carrera corta; al llegar (o por timeout) -> salto.
			if (Bot.HasArrived() || Now - Bot.UnstickTime > kStageTimeout)
			{
				Bot.UnstickStage = 3;
				Bot.UnstickTime = Now;
			}
			break;

		case 3:
			// --- Salto: una vez, y reanudar hacia el destino original. ---
			CustomBotMovement::Jump(Bot);
			Bot.UnstickStage = 4;
			Bot.UnstickTime = Now;
			Bot.UnstickSwings = 0;
			Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
			CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 150.0f, true);
			break;

		case 4:
			// Respiro post-salto: dejar que el intento de salto remedie el bloqueo.
			if (Now - Bot.UnstickTime < kPostJumpRespite)
				break;

			// Progreso real / llegada: desatascado con exito.
			if (MadeRealProgress(Bot))
			{
				Bot.UnstickFails = 0;
				Reset(Bot);
				break;
			}

			// Nada que abrir/romper delante: retroceder mas para construir (5).
			if (!TryBreakFront(Bot))
			{
				Bot.UnstickStage = 5;
				Bot.UnstickTime = Now;
				Bot.UnstickSwings = 0;

				FVector Away2 = Bot.Pawn->GetActorLocation()
					- Bot.Pawn->GetActorForwardVector() * kBuildBackup;
				CustomBotMovement::MoveTo(Bot, Away2, 40.0f, false);
				break;
			}

			// Swing melee real cada kBreakCooldown. El danio/recursos los otorga
			// el juego via OnDamageServer al golpear con arma melee; si en esta
			// build el swing no destruye, agotados los swings se pasa a construir.
			if (Now - Bot.UnstickTime >= kBreakCooldown)
			{
				Bot.UnstickTime = Now;
				++Bot.UnstickSwings;
				CustomBotInventory::EquipPickaxe(Bot);
				CustomBotCombat::FireWeapon(Bot);
			}

			if (Bot.UnstickSwings >= kMaxSwings)
			{
				// El pico no ha abierto paso: forzar la destruccion del obstaculo
				// que bloquea (pipeline real DestroyTarget) y seguir empujando; el
				// destrozado lo detecta MadeRealProgress en el siguiente tick.
				if (Bot.UnstickTarget)
				{
					auto Binding = Cast<ABuildingActor>(Bot.UnstickTarget);

					if (Binding && !Binding->IsActorBeingDestroyed() &&
						CustomBotDestruction::DestroyTarget(Binding, true))
					{
						CustomBotInventory::EquipPickaxe(Bot);
						Bot.UnstickTime = Now;
						Bot.UnstickSwings = 0;
						Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
						CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 150.0f, true);
						LOG_INFO(LogBots, "[CustomBot] unstuck: pickaxe failed, force-destroyed obstacle blocking path");
						break;
					}
				}

				// No se pudo abrir/romper: retroceder mas y construir (etapa 5).
				Bot.UnstickStage = 5;
				Bot.UnstickTime = Now;
				Bot.UnstickSwings = 0;

				FVector Away2 = Bot.Pawn->GetActorLocation()
					- Bot.Pawn->GetActorForwardVector() * kBuildBackup;
				CustomBotMovement::MoveTo(Bot, Away2, 40.0f, false);
			}
			break;

		case 5:
			// Retrocediendo para dejar hueco a la rampa; al llegar -> construir.
			if (Bot.HasArrived() || Now - Bot.UnstickTime > kStageTimeout)
			{
				Bot.UnstickStage = 6;
				Bot.UnstickTime = Now;
				Bot.UnstickSwings = 0;
				Bot.bUnstickRampBuilt = false;
				Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
				CustomBotMovement::LookAt(Bot, Bot.UnstickGoal);
			}
			break;

		case 6:
		{
			// ESCALERA de rampas: cada intento coloca la rampa en la celda delante
			// con el Z del terreno; como el bot ya esta SUBIDO en la rampa anterior
			// al construirse la siguiente, las rampas se apilan en vertical (el Z
			// real del suelo de la nueva celda sube un peldano). Tras 3 rampas se
			// asienta la cima con un suelo (BuildFlatOnTop) para poder cruzar.
			// Solo si NINGUNA opcion avanza se pasa a rodear (etapa 7).
			if (!Bot.bUnstickRampBuilt)
			{
				bool bBuilt = false;

				if (Bot.UnstickRampCount < 3)
					bBuilt = BuildRampUp(Bot);
				else if (Bot.UnstickFloorCount < 1)
				{
					bBuilt = BuildFlatOnTop(Bot);
					if (bBuilt)
						++Bot.UnstickFloorCount;
				}

				if (bBuilt)
				{
					Bot.bUnstickRampBuilt = true;
					Bot.UnstickTime = Now;
				}
				else
				{
					// Sin materiales (o sin grid): no se puede construir -> rodear.
					Bot.UnstickStage = 7;
					Bot.UnstickTime = Now;
					Bot.UnstickDetourCount = 0;
					Bot.bUnstickDetourSet = false;
					break;
				}
			}

			// Subir por la escalera: reanudar el move hacia el goal.
			CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 150.0f, true);

			// Si se avanza de verdad tras construir, desatascado.
			if (MadeRealProgress(Bot))
			{
				Bot.UnstickFails = 0;
				Reset(Bot);
				break;
			}

			// Agotado el margen de la rampa actual sin progreso: subir el siguiente
			// peldano (otra rampa) o, si las 3 rampas + suelo no abrieron paso,
			// rodear por los lados.
			if (Now - Bot.UnstickTime >= kBuildRespite)
			{
				if (Bot.UnstickRampCount < 3)
				{
					++Bot.UnstickRampCount;
					Bot.bUnstickRampBuilt = false;
					Bot.UnstickTime = Now;
				}
				else
				{
					Bot.UnstickStage = 7;
					Bot.UnstickTime = Now;
					Bot.UnstickDetourCount = 0;
					Bot.bUnstickDetourSet = false;
				}
			}
			break;
		}

		case 7:
		{
			// Ambos lados probados sin exito: ceder el move a la IA.
			if (Bot.UnstickDetourCount >= 2)
			{
				++Bot.UnstickFails;
				CustomBotMovement::StopMovement(Bot);
				Reset(Bot);
				break;
			}

			// Elegir el punto de rodeo del lado actual (0 = izquierda, 1 = derecha).
			if (!Bot.bUnstickDetourSet)
			{
				FVector Detour;
				if (FindDetourPoint(Bot, Bot.UnstickDetourCount, Detour))
				{
					Bot.UnstickDetourDest = Detour;
					Bot.bUnstickDetourSet = true;
					Bot.UnstickTime = Now;
					Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
					CustomBotMovement::MoveTo(Bot, Detour, 100.0f, true);
				}
				else
				{
					// Sin salida por este lado: pasar al otro.
					++Bot.UnstickDetourCount;
					Bot.UnstickTime = Now;
				}
				break;
			}

			// Timeout del desvio sin avance: probar el otro lado.
			if (Now - Bot.UnstickTime >= kDetourTimeout)
			{
				float Moved = CustomBotMovement::HorizontalDistance(Bot.UnstickRefLoc, Bot.Pawn->GetActorLocation());

				if (Moved >= kMinProgress * 3.0f)
				{
					// El desvio avanzo: intento valido; volver hacia el goal.
					Bot.UnstickFails = 0;
					Reset(Bot);
					break;
				}

				Bot.bUnstickDetourSet = false;
				++Bot.UnstickDetourCount;
				Bot.UnstickTime = Now;
				break;
			}

			// Llegar al punto de rodeo tambien cuenta como desatascado.
			if (Bot.HasArrived())
			{
				Bot.UnstickFails = 0;
				Reset(Bot);
			}
			break;
		}

		default:
			Reset(Bot);
		}
	}
}