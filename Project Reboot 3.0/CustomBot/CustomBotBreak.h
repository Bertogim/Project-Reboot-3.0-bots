#pragma once

#include "CustomBot.h"

#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotCombat.h"
#include "CustomBotDestruction.h"
#include "CustomBotBuilding.h"
#include "CustomBotDoors.h"


namespace CustomBotBreak
{
	static constexpr float kProgressWindow = 0.9f;
	static constexpr float kMinProgress = 40.0f;
	static constexpr float kStuckAccumulated = 1.0f;

	static constexpr float kBackupDistance = 220.0f;
	static constexpr float kRunUpDistance = 260.0f;
	static constexpr float kStageTimeout = 1.2f;
	static constexpr float kPostJumpRespite = 0.9f;
	static constexpr float kMaxSwings = 8;
	static constexpr float kBreakRange = 320.0f;
	static constexpr float kBreakCooldown = 0.45f;
	static constexpr float kBuildBackup = 350.0f;
	static constexpr float kBuildRespite = 1.8f;
	static constexpr float kDetourRadius = 550.0f;
	static constexpr float kDetourTimeout = 2.5f;
	static constexpr int kMaxUnstickFails = 3;

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

		Bot.UnstickTarget = nullptr;
	}

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

		{
			FVector Goal = (Bot.UnstickStage > 0) ? Bot.UnstickGoal : Bot.MoveRequest.Destination;

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

	static bool IsStuck(const CustomBot& Bot)
	{
		return Bot.bMoveRequestActive && Bot.StuckTime >= kStuckAccumulated;
	}

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

	static bool TryBreakFront(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		if (CustomBotDoors::TryOpenDoorInFront(Bot))
			return true;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector GoalDir = CustomBotMovement::DirectionTo(BotLoc, Bot.UnstickGoal);

		CBT::EObstacleType Type = CBT::EObstacleType::None;
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

		Bot.UnstickTarget = Obstacle;
		CustomBotMovement::LookAt(Bot, Obstacle->GetActorLocation());
		CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 60.0f, false);
		return true;
	}

	static bool BuildRampUp(CustomBot& Bot)
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

		FRotator RampRot = Bot.GetRotation();
		RampRot.Yaw = CustomBotBuilding::SnapYawToCardinal(Facing + 90.0f);

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

		FloorLoc.Z = BotLoc.Z;

		auto Floor = CustomBotBuilding::BuildFloor(Bot, FloorLoc, FRotator{ 0.0f, Facing, 0.0f });

		if (!Floor)
			return false;

		LOG_INFO(LogBots, "[CustomBot] unstuck: built floor on top of ramp stairs at ({:.0f},{:.0f},{:.0f})",
			FloorLoc.X, FloorLoc.Y, FloorLoc.Z);
		return true;
	}

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

	static void TickUnstuck(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
		{
			Reset(Bot);
			return;
		}

		if (Bot.PlayerState && Bot.PlayerState->IsInAircraft())
		{
			Reset(Bot);
			return;
		}

		if (Bot.bInAirPhase)
		{
			Reset(Bot);
			return;
		}

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

		if (Bot.UnstickFails >= kMaxUnstickFails)
		{
			CustomBotMovement::StopMovement(Bot);
			Bot.UnstickFails = 0;
			Reset(Bot);
			return;
		}

		if (bCustomBotPathfindingFallback &&
			Bot.StuckPersistTime >= CustomBot::PathfindingFallbackStuckTime &&
			!Bot.PathWaypoints.empty())
		{
			return;
		}

		float Now = CustomBotPerception::BotTime();
		const bool bUnstucking = Bot.UnstickStage > 0;

		AdvanceStuckDetector(Bot);

		if (!bUnstucking)
		{
			if (Bot.HasArrived())
			{
				Reset(Bot);
				return;
			}

			if (!IsStuck(Bot))
				return;

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
			if (Bot.HasArrived() || Now - Bot.UnstickTime > kStageTimeout)
			{
				Bot.UnstickStage = 3;
				Bot.UnstickTime = Now;
			}
			break;

		case 3:
			CustomBotMovement::Jump(Bot);
			Bot.UnstickStage = 4;
			Bot.UnstickTime = Now;
			Bot.UnstickSwings = 0;
			Bot.UnstickRefLoc = Bot.Pawn->GetActorLocation();
			CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 150.0f, true);
			break;

		case 4:
			if (Now - Bot.UnstickTime < kPostJumpRespite)
				break;

			if (MadeRealProgress(Bot))
			{
				Bot.UnstickFails = 0;
				Reset(Bot);
				break;
			}

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

			if (Now - Bot.UnstickTime >= kBreakCooldown)
			{
				Bot.UnstickTime = Now;
				++Bot.UnstickSwings;
				CustomBotInventory::EquipPickaxe(Bot);
				CustomBotCombat::FireWeapon(Bot);
			}

			if (Bot.UnstickSwings >= kMaxSwings)
			{
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

				Bot.UnstickStage = 5;
				Bot.UnstickTime = Now;
				Bot.UnstickSwings = 0;

				FVector Away2 = Bot.Pawn->GetActorLocation()
					- Bot.Pawn->GetActorForwardVector() * kBuildBackup;
				CustomBotMovement::MoveTo(Bot, Away2, 40.0f, false);
			}
			break;

		case 5:
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
					Bot.UnstickStage = 7;
					Bot.UnstickTime = Now;
					Bot.UnstickDetourCount = 0;
					Bot.bUnstickDetourSet = false;
					break;
				}
			}

			CustomBotMovement::MoveTo(Bot, Bot.UnstickGoal, 150.0f, true);

			if (MadeRealProgress(Bot))
			{
				Bot.UnstickFails = 0;
				Reset(Bot);
				break;
			}

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
			if (Bot.UnstickDetourCount >= 2)
			{
				++Bot.UnstickFails;
				CustomBotMovement::StopMovement(Bot);
				Reset(Bot);
				break;
			}

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
					++Bot.UnstickDetourCount;
					Bot.UnstickTime = Now;
				}
				break;
			}

			if (Now - Bot.UnstickTime >= kDetourTimeout)
			{
				float Moved = CustomBotMovement::HorizontalDistance(Bot.UnstickRefLoc, Bot.Pawn->GetActorLocation());

				if (Moved >= kMinProgress * 3.0f)
				{
					Bot.UnstickFails = 0;
					Reset(Bot);
					break;
				}

				Bot.bUnstickDetourSet = false;
				++Bot.UnstickDetourCount;
				Bot.UnstickTime = Now;
				break;
			}

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