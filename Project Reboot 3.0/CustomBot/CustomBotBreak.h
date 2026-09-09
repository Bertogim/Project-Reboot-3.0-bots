#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotCombat.h"
#include "CustomBotDestruction.h"
#include "CustomBotDoors.h"

// CustomBot - Desatascado fisico (fallback cuando NO hay ruta).
//
// Si pathfinding esta OFF (checkbox en la UI: bCustomBotPathfinding=false) o la
// ruta fallo/vacio, el bot va en linea recta; si se queda bloqueado (BlockedPath
// persistente) se ejecuta la secuencia pedida:
//   1) retrocede 2m
//   2) corre 1m de carrerilla (sprint corto)
//   3) salta
//   4) si sigue sin poder pasar: abre la puerta si hay una delante, o rompe lo
//      que tenga delante con el pico (swing melee real via CustomBotCombat).
//
// REGLA: NUNCA teletransporta. Todo sale de la fisica simulada del pawn
// (MoveTo/Velocity/Jump/Launch). Lo invoca BotTickCallbackImpl justo DESPUES de
// UpdateMovement, una vez por bot, bajo el SEH del servidor.
//
// TODO-PATH: maquina de estados simple con timers; mejorar con etapas medidas en
// unidades de movimiento o anadiendo rodear el obstaculo por los lados.

namespace CustomBotBreak
{
	static constexpr float kBackupDistance = 200.0f;  // "retrocede 2m" (unreal units)
	static constexpr float kSprintDistance = 100.0f;  // "1m de carrerilla"
	static constexpr float kBlockThreshold = 0.75f;   // segundos bloqueado antes de desatascarse
	static constexpr float kStageTimeout = 1.2f;      // limite de tiempo por etapa
	static constexpr float kMaxSwings = 8;            // golpes de pico antes de rendirse
	static constexpr float kBreakRange = 280.0f;      // alcance del swing del pico
	static constexpr float kBreakCooldown = 0.4f;     // segundos entre swings
	static constexpr int kMaxUnstickFails = 3;        // ciclos fallidos: ceder a la IA

	// Reinicia por completo la maquina de desatascado.
	static void Reset(CustomBot& Bot)
	{
		Bot.UnstickStage = 0;
		Bot.UnstickTime = -1.0f;
		Bot.BlockedSince = -1.0f;
		Bot.UnstickSwings = 0;
	}

	// Etapa 4 (preparacion): abrir la puerta si la hay delante, si no equipar el
	// pico y acercarse/apuntar al obstaculo mas cercano. El swing (FireWeapon)
	// lo aplica el propio FSM cada kBreakCooldown. Devuelve true si "hizo algo".
	static bool PrepareBreak(CustomBot& Bot)
	{
		// 1) Puerta cerrada enfrente: abrirla (mas barato que romper).
		if (CustomBotDoors::TryOpenDoorInFront(Bot))
			return true;

		// 2) Obstaculo de estructuras: apuntarlo con el pico.
		CBT::EObstacleType Type = CBT::EObstacleType::None;
		AActor* Obstacle = CustomBotPerception::FindNearestObstacle(Bot, kBreakRange, Type);

		if (!Obstacle)
			return false;

		float Dist = Bot.Pawn->GetDistanceTo(Obstacle);

		if (Dist > 900.0f)
			return false;

		CustomBotMovement::StopMovement(Bot);
		CustomBotMovement::LookAt(Bot, Obstacle->GetActorLocation());

		if (Dist > kBreakRange)
			CustomBotMovement::MoveTo(Bot, Obstacle->GetActorLocation(), kBreakRange * 0.5f, true);

		return true;
	}

	// Tick del desatascado. Se llama UNA vez por bot y tick (despues de
	// UpdateMovement). Solo actua con un move activo y bloqueado de forma
	// persistente; si el forward avanza, no hace nada.
	static void TickUnstuck(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Bot.bMoveRequestActive)
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

		// No interferir mientras se sigue una ruta navmesh con waypoints: la
		// ruta ya sabe abrir puertas / rodear; el bloqueo de un solo waypoint
		// es transitorio.
		if (bCustomBotPathfinding && !Bot.PathWaypoints.empty() && Bot.PathIndex < (int)Bot.PathWaypoints.size())
		{
			Reset(Bot);
			return;
		}

		const bool bBlocked = Bot.IsPathBlocked();
		const bool bUnstucking = Bot.UnstickStage > 0;
		const float Now = CustomBotPerception::BotTime();

		// Al no estar desatascando, esperar al umbral de bloqueo antes de actuar
		// (deja margen a TryResolveBlockedPath de la IA y a la apertura de puertas).
		if (!bUnstucking)
		{
			if (!bBlocked || Bot.HasArrived())
			{
				Reset(Bot);
				return;
			}

			if (Bot.BlockedSince < 0.0f)
				Bot.BlockedSince = Now;

			if (Now - Bot.BlockedSince < kBlockThreshold)
				return;
		}

		// Si ya se supero el tope de ciclos fallidos, ceder el move a la IA
		// (que re-elegira otro destino) en vez de quedarse dando vueltas.
		if (Bot.UnstickFails >= kMaxUnstickFails)
		{
			CustomBotMovement::StopMovement(Bot);
			Bot.UnstickFails = 0;
			Reset(Bot);
			return;
		}

		switch (Bot.UnstickStage)
		{
		case 0:
		{
			// --- Etapa 1: retroceder 2m. ---
			Bot.UnstickGoal = Bot.MoveRequest.Destination;
			Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
			Bot.UnstickSwings = 0;
			Bot.UnstickStage = 1;
			Bot.UnstickTime = Now;

			FVector Away = Bot.Pawn->GetActorLocation()
				- Bot.Pawn->GetActorForwardVector() * kBackupDistance;

			CustomBotMovement::MoveTo(Bot, Away, 40.0f, false);
			break;
		}

		case 1:
			// Retrocediendo a Away: al llegar (o por timeout) -> carrera.
			if (Bot.HasArrived() || Now - Bot.UnstickTime > kStageTimeout)
			{
				Bot.UnstickStage = 2;
				Bot.UnstickTime = Now;

				FVector Fwd = CustomBotMovement::DirectionTo(Bot.Pawn->GetActorLocation(), Bot.UnstickGoal);
				Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
				CustomBotMovement::MoveTo(Bot, Bot.UnstickRefLoc + Fwd * kSprintDistance, 30.0f, true);
			}
			break;

		case 2:
			// Carrera corta de 1m; al llegar (o por timeout) -> salto.
			if (Bot.HasArrived() || Now - Bot.UnstickTime > kStageTimeout)
			{
				Bot.UnstickStage = 3;
				Bot.UnstickTime = Now;
			}
			break;

		case 3:
			// --- Salto: una vez, y reanudar el destino original. ---
			CustomBotMovement::Jump(Bot);
			Bot.UnstickStage = 4;
			Bot.UnstickTime = Now;
			CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 150.0f, true);
			break;

		case 4:
			// Tras el salto, dar un respiro; si sigue bloqueado -> romper/abrir.
			if (Now - Bot.UnstickTime < 0.8f)
				break;

			if (!bBlocked || Bot.HasArrived())
			{
				// Desatascado con exito: se limpia el contador de fallos.
				Bot.UnstickFails = 0;
				Reset(Bot);
				break;
			}

			if (!PrepareBreak(Bot))
			{
				// No hay nada que abrir/romper cerca: devolver el control a la IA.
				++Bot.UnstickFails;
				CustomBotMovement::StopMovement(Bot);
				Reset(Bot);
				break;
			}

			// TODO-PATH (romper con pico): swing melee real cada kBreakCooldown.
			// El danio/recursos los otorga el juego via OnDamageServer al golpear
			// con arma melee; si en esta build el swing no destruye, usar
			// CustomBotDestruction::DestroyTarget como respaldo (SilentDie).
			if (Now - Bot.UnstickTime >= kBreakCooldown)
			{
				Bot.UnstickTime = Now;
				++Bot.UnstickSwings;
				CustomBotInventory::EquipPickaxe(Bot);
				CustomBotCombat::FireWeapon(Bot);
			}

			if (Bot.UnstickSwings >= kMaxSwings)
			{
				// No se pudo romper/abrir: ciclos fallidos y devolver el control
				// a la IA (StopMovement para que re-elija destino).
				++Bot.UnstickFails;
				CustomBotMovement::StopMovement(Bot);
				Reset(Bot);
			}
			break;

		default:
			Reset(Bot);
		}
	}
}