#pragma once

#include "CustomBotTypes.h"

#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawnAthena.h"
#include "FortGameModeAthena.h"
#include "FortInventory.h"
#include "AbilitySystemComponent.h"

#include <vector>


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

	AFortPlayerControllerAthena* MoveController = nullptr;
	AFortPlayerStateAthena* MovePlayerState = nullptr;
	AFortPlayerPawnAthena* CosmeticPawn = nullptr;

	bool bInitialized = false;

	bool bDeathHandled = false;

	CBT::FMoveRequest MoveRequest;
	CBT::EMovementState MoveState = CBT::EMovementState::Idle;
	bool bMoveRequestActive = false;

	bool bFiringWeapon = false;
	float bFiringWeaponTime = 0.0f;
	static constexpr float kFiringWeaponTimeout = 0.5f;

	BotAIContext* AI = nullptr;

	bool bClaimLiveDone = false;

	bool bCMCInitialized = false;

	bool bGhostHibernated = false;

	unsigned RestorePSCounter = 0;

	bool bInAirPhase = false;

	float GroundGravityZ = 0.0f;
	float GroundGravityScale = 0.0f;

	unsigned ProbeTicks = 0;
	FVector ProbePrevLoc{};

	bool bSkinPending = false;

	float LootScanTime = -1.0f;
	float PlayerScanTime = -1.0f;
	float ObstacleScanTime = -1.0f;

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

	float MoveLOSTime = -1.0f;
	bool bMoveLOSBlocked = false;

	std::vector<FVector> PathWaypoints;
	int PathIndex = 0;
	float PathQueryTime = -1.0f;
	bool bPathFollowBlocked = false;
	static constexpr float PathQueryCooldown = 2.0f;
	static constexpr float PathWaypointAcceptance = 220.0f;

	int UnstickStage = 0;
	float UnstickTime = -1.0f;
	float BlockedSince = -1.0f;
	FVector UnstickGoal{};
	FVector UnstickRefLoc{};
	int UnstickSwings = 0;
	int UnstickFails = 0;
	FVector UnstickDetourDest{};
	bool bUnstickDetourSet = false;
	int UnstickDetourCount = 0;
	bool bUnstickRampBuilt = false;
	int UnstickRampCount = 0;
	int UnstickFloorCount = 0;

	float StuckWindowTime = -1.0f;
	FVector StuckWindowPos{};
	float StuckTime = 0.0f;
	bool bInCombat = false;

	float StuckPersistTime = 0.0f;
	FVector StuckPersistGoal{};
	float StuckPersistBest = 1e30f;
	static constexpr float PathfindingFallbackStuckTime = 10.0f;

	AActor* UnstickTarget = nullptr;

	bool bPendingEquip = false;
	FGuid PendingEquipGuid{};
	float PendingEquipTime = 0.0f;
	int PendingEquipAttempts = 0;

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

	void (*DebugTick)(CustomBot& Self) = nullptr;


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

	void Tick()
	{
		static unsigned BotTickCounter = 0;
		if ((++BotTickCounter) % 1200 == 0)
			LOG_INFO(LogBots, "[CustomBot] [bot.tick] ready={} valid={} dbgTick={} life={}",
				IsReady(), IsValidActor(), DebugTick != nullptr, (int)GetLifeState());

		if (!IsReady() || !IsValidActor())
			return;

		if (bFiringWeapon)
		{
			float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
			if (Now - bFiringWeaponTime > kFiringWeaponTimeout)
				bFiringWeapon = false;
		}

		if (DebugTick)
			DebugTick(*this);

	}

	void Destroy()
	{
		if (Pawn)
			Pawn->K2_DestroyActor();
		if (CosmeticPawn)
			CosmeticPawn->K2_DestroyActor();
		if (Controller)
			Controller->K2_DestroyActor();
		if (MoveController)
			MoveController->K2_DestroyActor();

		Controller = nullptr;
		Pawn = nullptr;
		PlayerState = nullptr;
		WorldInventory = nullptr;
		MoveController = nullptr;
		MovePlayerState = nullptr;
		CosmeticPawn = nullptr;
		bInitialized = false;

		if (AI)
		{
			delete AI;
			AI = nullptr;
		}
	}
};
