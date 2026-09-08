#pragma once

#include "CustomBotTypes.h"

#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawnAthena.h"
#include "FortGameModeAthena.h"
#include "FortInventory.h"
#include "AbilitySystemComponent.h"

// CustomBot - Entidad orquestadora del bot "Season 3".
//
// El bot es un JUGADOR REAL: AFortPlayerControllerAthena + AFortPlayerPawnAthena,
// por lo que hereda todo el gameplay (movimiento, inventario, armas, construccion,
// destruccion, interaccion, danio, eliminacion) de las APIs nativas.
//
// Parte 1: esta clase es el "cuerpo"/capacidades. NO contiene decisiones de IA.
// Los modulos (Movement/Perception/Inventory/Combat/Building/Destruction/
// Resources/Interaction/Spawner) operan sobre una instancia de CustomBot.

class CustomBot
{
public:
	AFortPlayerControllerAthena* Controller = nullptr;
	AFortPlayerPawnAthena* Pawn = nullptr;
	AFortPlayerStateAthena* PlayerState = nullptr;
	AFortInventory* WorldInventory = nullptr;

	bool bInitialized = false;

	// --- Estado de movimiento (pipeline MoveTo + UpdateMovement por tick) ----
	// Almacena la peticion de movimiento activa; CustomBotMovement::UpdateMovement
	// (llamado desde CustomBotSpawner::TickAll en el tick del servidor) la consume.
	CBT::FMoveRequest MoveRequest;
	CBT::EMovementState MoveState = CBT::EMovementState::Idle;
	bool bMoveRequestActive = false;

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
		if ((++BotTickCounter) % 120 == 0)
			LOG_INFO(LogBots, "[CustomBot] [bot.tick] ready={} valid={} dbgTick={} life={}",
				IsReady(), IsValidActor(), DebugTick != nullptr, (int)GetLifeState());

		if (!IsReady() || !IsValidActor())
			return;

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
	}
};
