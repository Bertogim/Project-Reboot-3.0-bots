#pragma once


#include "CustomBotAI.h"

#include "CustomBot/CustomBotBreak.h"

namespace CustomBotAIMidgame
{
	static FVector PickWanderTarget(CustomBot& Bot, const FVector& Bias);
	static void BuildBarricade(CustomBot& Bot, BotAIContext& Ctx, const FVector& EnemyLoc);
	static void FleeWithWalls(CustomBot& Bot, BotAIContext& Ctx, const FVector& EnemyLoc, bool bBuildWalls);
	static void DoEndGame(CustomBot& Bot, BotAIContext& Ctx);
	static bool TryResolveBlockedPath(FVector& MoveTarget, CustomBot& Bot, BotAIContext& Ctx);
	static void RefillLobbyHP(CustomBot& Bot);

	static constexpr float kMeleeRange = 260.0f;

	static float Elapsed;
	static float LastTime = -1.0f;

	static void TickTimers(CustomBot& Bot, BotAIContext& Ctx)
	{
		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

		if (LastTime < 0.0f)
			LastTime = Now;

		Elapsed = Now - LastTime;
		LastTime = Now;

		if (Elapsed < 0.0f || Elapsed > 1.0f)
			Elapsed = 0.016f;

		Ctx.DecisionTimer -= Elapsed;
		Ctx.ScanTimer -= Elapsed;
		Ctx.ReactTimer -= Elapsed;
		Ctx.ActionTimer -= Elapsed;
		Ctx.EquipRetryTimer -= Elapsed;
	}

	static bool NeedsHealing(CustomBot& Bot)
	{
		if (!Bot.IsReady())
			return false;

		float Health = Bot.GetHealth();
		float Shield = Bot.GetShield();

		return Health < 75.0f || Shield < 50.0f;
	}

	static bool IsBeingAttacked(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		float Health = Bot.GetHealth();
		float Now = CustomBotPerception::BotTime();

		if (Ctx.CheckedHealth < 0.0f)
			Ctx.CheckedHealth = Health;

		if (Health < Ctx.CheckedHealth - 0.5f)
			Ctx.LastDamageTime = Now;

		Ctx.CheckedHealth = Health;

		return Now - Ctx.LastDamageTime < 2.0f;
	}

	static float PerceptionRadius(BotAIContext& Ctx)
	{
		float Base = 8000.0f;
		return Base * (0.5f + Ctx.Personality.Awareness * 0.9f);
	}

	static AActor* ScanForEnemy(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return nullptr;

		float Radius = PerceptionRadius(Ctx);
		return CustomBotPerception::FindNearestEnemy(Bot, Radius);
	}

	static void Decide(CustomBot& Bot, BotAIContext& Ctx);


	static constexpr int kQuickbarLootSlots = 5;

	static int ItemLootScore(FFortItemEntry* Entry)
	{
		if (!Entry || !Entry->GetItemDefinition())
			return 0;

		auto Def = Entry->GetItemDefinition();
		int Score = (Entry->GetLevel() > 0 ? Entry->GetLevel() : 1) * 100;

		const std::string& Path = CustomBotPerception::CachedPathForDef(Def);

		static const char* CatDirs[] = { "/Sniper/", "/Launchers/", "/Shotgun/", "/Rifle/", "/SMG/", "/Pistol/" };
		static const int CatBonus[]  = { 600, 550, 500, 400, 300, 150 };

		for (int i = 0; i < 6; ++i)
		{
			if (Path.find(CatDirs[i]) != std::string::npos)
			{
				Score += CatBonus[i];
				break;
			}
		}

		return Score;
	}

	static int QuickbarLootCount(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return 0;

		int Count = 0;
		auto& ItemInstances = Bot.WorldInventory->GetItemList().GetItemInstances();

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);
			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();
			if (!Entry || !Entry->GetItemDefinition())
				continue;

			auto T = CustomBotPerception::ClassifyItemDefinition(Entry->GetItemDefinition());
			if (T == CustomBotPerception::EItemType::Weapon || T == CustomBotPerception::EItemType::Consumable)
				++Count;
		}

		return Count;
	}

	static UFortItem* FindWorstWeapon(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return nullptr;

		UFortItem* Worst = nullptr;
		int WorstScore = 0;
		auto& ItemInstances = Bot.WorldInventory->GetItemList().GetItemInstances();

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);
			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();
			if (!Entry || !Entry->GetItemDefinition())
				continue;

			if (CustomBotPerception::ClassifyItemDefinition(Entry->GetItemDefinition())
				!= CustomBotPerception::EItemType::Weapon)
				continue;

			int S = ItemLootScore(Entry);
			if (!Worst || S < WorstScore)
			{
				WorstScore = S;
				Worst = Item;
			}
		}

		return Worst;
	}

	static bool EquipBestWeapon(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return false;

		static auto FortWeaponMeleeItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponMeleeItemDefinition");

		UFortItem* Best = nullptr;
		int BestScore = 0;
		auto& ItemInstances = Bot.WorldInventory->GetItemList().GetItemInstances();

		for (int i = 0; i < ItemInstances.size(); ++i)
		{
			UFortItem* Item = ItemInstances.at(i);
			if (!Item)
				continue;

			auto Entry = Item->GetItemEntry();
			auto ItemDef = Entry ? Entry->GetItemDefinition() : nullptr;

			if (!ItemDef)
				continue;

			if (CustomBotPerception::ClassifyItemDefinition(ItemDef)
				!= CustomBotPerception::EItemType::Weapon)
				continue;

			if (FortWeaponMeleeItemDefinitionClass && ItemDef->IsA(FortWeaponMeleeItemDefinitionClass))
				continue;

			int S = ItemLootScore(Entry);
			if (!Best || S > BestScore)
			{
				BestScore = S;
				Best = Item;
			}
		}

		return Best ? CustomBotInventory::EquipItem(Bot, Best) : false;
	}

	static void TrySwapForBetterWeapon(CustomBot& Bot, AFortPickup* Pickup)
	{
		if (!Bot.IsReady() || !Pickup)
			return;

		auto Entry = Pickup->GetPrimaryPickupItemEntry();
		if (!Entry || !Entry->GetItemDefinition())
			return;

		if (CustomBotPerception::ClassifyItemDefinition(Entry->GetItemDefinition())
			!= CustomBotPerception::EItemType::Weapon)
			return;

		int NewScore = ItemLootScore(Entry);

		if (QuickbarLootCount(Bot) >= kQuickbarLootSlots)
		{
			UFortItem* Worst = FindWorstWeapon(Bot);

			if (Worst && NewScore > ItemLootScore(Worst->GetItemEntry()))
			{
				LOG_INFO(LogBots, "[BotAI] loot: swapping worst weapon ({}) for better pick ({})",
					ItemLootScore(Worst->GetItemEntry()), NewScore);
				CustomBotInventory::DropItem(Bot, Worst, 1);
			}
		}
	}

	static void DoLooting(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		const float LootRange = 1500.0f * (0.6f + Ctx.Personality.LootSkill * 0.8f);

		AFortPickup* Pickup = nullptr;

		if (Ctx.Personality.LootSkill >= 0.3f)
			Pickup = CustomBotPerception::FindNearestWeapon(Bot, LootRange);

		if (!Pickup && Ctx.Personality.LootSkill < 0.7f)
			Pickup = CustomBotPerception::FindNearestConsumable(Bot, LootRange);

		if (!Pickup)
			Pickup = CustomBotPerception::FindNearestPickup(Bot, LootRange);

		ABuildingContainer* Container = CustomBotPerception::FindNearestUnopenedContainer(Bot, LootRange);

		if (Container && (!Pickup ||
			Bot.Pawn->GetDistanceTo(Container) < Bot.Pawn->GetDistanceTo(Pickup) * 0.9f))
		{
			Ctx.LootTarget = Container;

			if (CustomBotInteraction::CanInteract(Bot, Container))
			{
				if (CustomBotInteraction::Interact(Bot, Container))
				{
					++Ctx.LootedItems;

					if (Ctx.ActionTimer <= 0.0f)
					{
						EquipBestWeapon(Bot);
						Ctx.ActionTimer = 0.8f;
					}
				}
			}
			else
			{
				if (Bot.IsPathBlocked())
				{
					FVector ResolveDest = Container->GetActorLocation();
					if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
						return;
				}
				else
				{
					float VertDiff = Container->GetActorLocation().Z - Bot.Pawn->GetActorLocation().Z;

					if (VertDiff > 260.0f && Ctx.ActionTimer <= 0.0f)
					{
						if (CustomBotBreak::BuildRampUp(Bot))
						{
							Ctx.ActionTimer = 1.5f;
							LOG_INFO(LogBots, "[BotAI] loot: building ramp toward elevated container (+{:.0f}u)", VertDiff);
							return;
						}
					}
				}

				CustomBotMovement::MoveTo(Bot, Container->GetActorLocation(), 120.0f, true);
			}

			return;
		}

		if (Pickup)
		{
			Ctx.LootTarget = Pickup;

			if (Bot.Pawn->GetDistanceTo(Pickup) <= CustomBotInteraction::InteractionRadius)
			{
				TrySwapForBetterWeapon(Bot, Pickup);

				if (CustomBotInteraction::PickupItem(Bot, Pickup))
				{
					++Ctx.LootedItems;

					if (Ctx.ActionTimer <= 0.0f)
					{
						EquipBestWeapon(Bot);
						Ctx.ActionTimer = 0.8f;
					}
				}
			}
			else
			{
				CustomBotMovement::MoveTo(Bot, Pickup->GetActorLocation(), 100.0f, true);

				if (Bot.IsPathBlocked())
				{
					FVector ResolveDest = Pickup->GetActorLocation();
					if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
						return;
				}
			}

			return;
		}

		if (!Bot.HasMoveRequest() || Bot.HasArrived() || Bot.IsPathBlocked())
		{
			if (Bot.IsPathBlocked())
			{
				FVector ResolveDest = Bot.MoveRequest.Destination;
				if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
					return;
			}

			const auto& Chests = CustomBotPerception::CachedChests();
			FVector BotLoc = Bot.Pawn->GetActorLocation();
			FVector BestChest = FVector{};
			float BestDist = FLT_MAX;

			for (const auto& Chest : Chests)
			{
				if (Chest.bSearched)
					continue;

				float DX = Chest.Location.X - BotLoc.X;
				float DY = Chest.Location.Y - BotLoc.Y;
				float D2 = DX * DX + DY * DY;

				if (D2 < BestDist)
				{
					BestDist = D2;
					BestChest = Chest.Location;
				}
			}

			if (BestDist < FLT_MAX)
			{
				LOG_INFO(LogBots, "[BotAI] looting: heading to distant unopened chest ({:.0f}u)",
					FMath::Sqrt(BestDist));
				CustomBotMovement::MoveTo(Bot, BestChest, 120.0f, true);
			}
			else
			{
				FVector Goal = BotLoc + CustomBotMovement::DirectionTo(
					BotLoc,
					CustomBotAI::GetSafeZoneCenter(Bot)) * 800.0f;

				FVector Wander = CustomBotAIMidgame::PickWanderTarget(Bot, Goal);
				CustomBotMovement::MoveTo(Bot, Wander, 150.0f, true);
			}
		}
	}

	static FVector PickWanderTarget(CustomBot& Bot, const FVector& Bias);

	static void DoFarming(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		CBT::EObstacleType OutType = CBT::EObstacleType::None;
		AActor* Obstacle = CustomBotPerception::FindNearestObstacle(Bot, 900.0f, OutType);

		if (Obstacle)
		{
			Ctx.FarmTarget = Obstacle;

			auto Binding = Cast<ABuildingActor>(Obstacle);

			if (Binding)
			{
				float D = Bot.Pawn->GetDistanceTo(Obstacle);

				if (D <= 220.0f)
				{
					if (Ctx.ActionTimer <= 0.0f)
					{
						CustomBotInventory::EquipPickaxe(Bot);
						CustomBotMovement::LookAt(Bot, Obstacle->GetActorLocation());
						CustomBotDestruction::DestroyTarget(Binding, true);
						++Ctx.FarmedResources;
						Ctx.ActionTimer = 0.5f;
					}
				}
				else
				{
					CustomBotMovement::MoveTo(Bot, Obstacle->GetActorLocation(), 150.0f, true);
				}

				return;
			}
		}

		if (!Bot.HasMoveRequest() || Bot.HasArrived())
		{
			FVector Goal = Bot.Pawn->GetActorLocation() + CustomBotMovement::DirectionTo(
				Bot.Pawn->GetActorLocation(),
				CustomBotAI::GetSafeZoneCenter(Bot)) * 1000.0f;

			CustomBotMovement::MoveTo(Bot, PickWanderTarget(Bot, Goal), 150.0f, true);
		}
	}

	static void DoExploring(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (Bot.IsPathBlocked())
		{
			FVector Target = Ctx.bHasExploreTarget
				? Ctx.ExploreTarget
				: CustomBotAI::GetSafeZoneCenter(Bot);

			if (!Ctx.bHasExploreTarget)
			{
				Ctx.ExploreTarget = Target;
				Ctx.bHasExploreTarget = true;
			}

			if (CustomBotAIMidgame::TryResolveBlockedPath(Target, Bot, Ctx))
			{
				Ctx.ExploreTarget = Target;
				CustomBotMovement::MoveTo(Bot, Ctx.ExploreTarget, 200.0f, true);
				return;
			}
		}

		if (!Ctx.bHasExploreTarget || Bot.HasArrived() || Bot.IsPathBlocked())
		{
			FVector Bias = CustomBotAI::GetSafeZoneCenter(Bot);
			Ctx.ExploreTarget = PickWanderTarget(Bot, Bias);
			Ctx.bHasExploreTarget = true;
		}

		CustomBotMovement::MoveTo(Bot, Ctx.ExploreTarget, 200.0f, true);
	}

	static bool TryResolveBlockedPath(FVector& MoveTarget, CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector BotLoc = Bot.GetLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, MoveTarget);

		if (Ctx.ActionTimer <= 0.0f)
		{
			FVector ProbeLoc = BotLoc + Dir * 500.0f;
			ProbeLoc.Z = BotLoc.Z;

			FVector ProbeGround = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
				FVector{ ProbeLoc.X, ProbeLoc.Y, 0.0f }, BotLoc.Z + 3000.0f, BotLoc.Z - 8000.0f, FName(0));

			float Rise = ProbeGround.Z - BotLoc.Z;

			if (Rise > 180.0f)
			{
				int Materials = CustomBotResources::GetTotalResourceCount(Bot);

				if (Materials >= 10)
				{
					auto GS = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
					auto SSS = GS ? GS->GetStructuralSupportSystem() : nullptr;

					float Facing = CustomBotBuilding::SnapYawToCardinal(Bot.GetRotation().Yaw);
					FRotator RampRot = Bot.GetRotation();
					RampRot.Yaw = CustomBotBuilding::SnapYawToCardinal(Facing + 90.0f);

					FVector RampLoc = BotLoc;
					if (SSS && CustomBotBuilding::CellCenterAhead(SSS, BotLoc, Facing, 1, RampLoc))
					{
						FVector RampGround = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
							FVector{ RampLoc.X, RampLoc.Y, 0.0f }, BotLoc.Z + 3000.0f, BotLoc.Z - 8000.0f, FName(0));
						RampLoc.Z = RampGround.Z;

						CustomBotBuilding::BuildRamp(Bot, RampLoc, RampRot);
						Ctx.ActionTimer = 1.5f;
						LOG_INFO(LogBots, "[BotAI] blocked path: built ramp (rise {:.0f}) at ({:.0f},{:.0f},{:.0f})",
							Rise, RampLoc.X, RampLoc.Y, RampLoc.Z);
						return true;
					}
				}

				return false;
			}
		}

		if (Ctx.ActionTimer <= 0.0f)
		{
			CBT::EObstacleType BlockType = CBT::EObstacleType::None;
			AActor* Nearest = CustomBotPerception::FindNearestObstacle(Bot, 600.0f, BlockType);

			if (Nearest)
			{
				float D = Bot.Pawn->GetDistanceTo(Nearest);
				auto Binding = Cast<ABuildingActor>(Nearest);

				if (Binding && D <= 320.0f && CustomBotDestruction::CanDestroy(Binding))
				{
					FVector ToObj = CustomBotMovement::DirectionTo(BotLoc, Nearest->GetActorLocation());
					float Dot = (Dir | ToObj);

					if (Dot > 0.4f)
					{
						CustomBotMovement::LookAt(Bot, Nearest->GetActorLocation());
						CustomBotDestruction::DestroyTarget(Binding, true);
						Ctx.ActionTimer = 1.0f;
						LOG_INFO(LogBots, "[BotAI] destroyed obstacle blocking path (dist {:.0f})", D);
						return true;
					}
				}
			}
		}

		if (Ctx.ActionTimer <= 0.0f)
		{
			CustomBotMovement::Jump(Bot);
			Ctx.ActionTimer = 0.8f;
			return true;
		}

		return false;
	}

	static void DoRotating(CustomBot& Bot, BotAIContext& Ctx, bool bAggressive)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FVector Center = CustomBotAI::GetSafeZoneCenter(Bot);

		FVector Dest = Center;
		Dest.X += (float)(std::rand() % 400) - 200.0f;
		Dest.Y += (float)(std::rand() % 400) - 200.0f;

		if (Bot.IsPathBlocked())
		{
			FVector ResolveDest = Dest;
			if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
			{
				CustomBotMovement::MoveTo(Bot, Dest, 250.0f, true);
				return;
			}
		}

		CustomBotMovement::MoveTo(Bot, Dest, 250.0f, true);
	}

	static void DoHealing(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		if (CustomBotInteraction::UseConsumable(Bot))
		{
			CustomBotMovement::StopMovement(Bot);
			return;
		}

		DoRotating(Bot, Ctx, false);
	}


	static void AimWithSkill(CustomBot& Bot, BotAIContext& Ctx, const FVector& TargetLoc)
	{
		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, TargetLoc);

		float Error = (1.0f - Ctx.Personality.AimSkill) * 0.9f;

		float ErrAng = Error * 0.12f;
		Dir.X += (float)(std::rand() % 1000) / 1000.0f * ErrAng - ErrAng * 0.5f;
		Dir.Y += (float)(std::rand() % 1000) / 1000.0f * ErrAng - ErrAng * 0.5f;
		Dir.Z += (float)(std::rand() % 1000) / 1000.0f * ErrAng - ErrAng * 0.5f;

		FVector Aim = FVector{TargetLoc.X + Dir.X * 100.0f, TargetLoc.Y + Dir.Y * 100.0f,
			TargetLoc.Z + Dir.Z * 100.0f};

		CustomBotMovement::LookAt(Bot, Aim);
	}

	static float PawnTotalHealth(AActor* Actor)
	{
		if (!Actor)
			return 0.0f;

		if (auto Pawn = Cast<AFortPawn>(Actor))
			return Pawn->GetHealth() + Pawn->GetShield();

		return 100.0f;
	}

	static void DoFighting(CustomBot& Bot, BotAIContext& Ctx, AActor* Enemy)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (!Enemy || Enemy->IsActorBeingDestroyed())
		{
			Ctx.EnemyTarget = nullptr;
			Ctx.State = EBotState::Exploring;
			Bot.bInCombat = false;
			CustomBotCombat::StopFiring(Bot);
			Ctx.bIsFiring = false;
			return;
		}

		Bot.bInCombat = true;

		FVector EnemyLoc = Enemy->GetActorLocation();
		float Dist = Bot.Pawn->GetDistanceTo(Enemy);
		bool bAttacked = IsBeingAttacked(Bot, Ctx);

		bool bHasWeapon = CustomBotCombat::IsWeaponEquipped(Bot) && !CustomBotCombat::IsPickaxeEquipped(Bot);

		if (!bHasWeapon)
		{
			if (Ctx.EquipRetryTimer <= 0.0f)
			{
				EquipBestWeapon(Bot);
				Ctx.EquipRetryTimer = 1.0f;
			}
			bHasWeapon = CustomBotCombat::IsWeaponEquipped(Bot) && !CustomBotCombat::IsPickaxeEquipped(Bot);
		}

		if (!bHasWeapon)
		{
			if (CustomBotCombat::EnemyHasRealWeapon(Enemy))
			{
				FleeWithWalls(Bot, Ctx, EnemyLoc, bAttacked);
				return;
			}

			AimWithSkill(Bot, Ctx, EnemyLoc);

			if (Dist <= kMeleeRange)
			{
				CustomBotMovement::StopMovement(Bot);
				CustomBotMovement::LookAt(Bot, EnemyLoc);

				if (Ctx.ActionTimer <= 0.0f)
				{
					CustomBotInventory::EquipPickaxe(Bot);
					CustomBotCombat::FireWeapon(Bot);
					Ctx.ActionTimer = 0.35f;
				}
			}
			else if (!Bot.HasMoveRequest() || Bot.HasArrived())
			{
				if (Bot.IsPathBlocked())
				{
					FVector ResolveDest = EnemyLoc;
					if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
						return;
				}

				CustomBotMovement::MoveTo(Bot, EnemyLoc, kMeleeRange * 0.5f, true, false);
			}

			return;
		}

		float MyHP = Bot.GetHealth() + Bot.GetShield();
		float EnemyHP = PawnTotalHealth(Enemy);

		bool bFlee = bAttacked &&
			(EnemyHP > MyHP + 20.0f || Ctx.Personality.RiskTolerance < 0.4f);

		if (bFlee)
		{
			FleeWithWalls(Bot, Ctx, EnemyLoc, true);
			return;
		}

		AimWithSkill(Bot, Ctx, EnemyLoc);

		bool bLOS = CustomBotPerception::HasLineOfSight(Bot, EnemyLoc, Enemy);
		bool bInRange = Dist <= 6000.0f;

		if (!bLOS || !bInRange)
		{
			if (Bot.IsPathBlocked())
			{
				FVector ResolveDest = Bot.MoveRequest.Destination;
				if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
					return;
			}

			if (!Bot.HasMoveRequest() || Bot.HasArrived())
				CustomBotMovement::MoveTo(Bot, EnemyLoc, 550.0f, true, false);
		}

		int Ammo = CustomBotCombat::GetCurrentAmmo(Bot);

		if (Ctx.ReactTimer <= 0.0f && bLOS && bInRange)
		{
			if (Ammo > 0)
			{
				if (Ctx.ActionTimer <= 0.0f)
				{
					if ((float)(std::rand() % 1000) / 1000.0f <= Ctx.Personality.Aggression * 0.9f)
					{
						CustomBotCombat::FireWeapon(Bot);
						Ctx.bIsFiring = true;
					}
					Ctx.ActionTimer = 0.12f;
				}
			}
			else
			{
				CustomBotCombat::Reload(Bot, 30);
			}
		}
		else
		{
			if (Ctx.bIsFiring)
			{
				CustomBotCombat::StopFiring(Bot);
				Ctx.bIsFiring = false;
			}
		}

		if (Ctx.Personality.BuildSkill > 0.35f && Ctx.ActionTimer <= 0.0f)
		{
			if ((float)(std::rand() % 1000) / 1000.0f <= Ctx.Personality.BuildSkill * 0.4f)
			{
				BuildBarricade(Bot, Ctx, EnemyLoc);
				Ctx.ActionTimer = 1.2f;
			}
		}
	}

	static void BuildBarricade(CustomBot& Bot, BotAIContext& Ctx, const FVector& EnemyLoc)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, EnemyLoc);

		if (!(Dir | Dir))
			return;

		float Yaw = CustomBotMovement::RotationFromDirection(Dir).Yaw;
		float Facing = CustomBotBuilding::SnapYawToCardinal(Yaw);

		auto GS = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto SSS = GS ? GS->GetStructuralSupportSystem() : nullptr;

		if (!SSS)
		{
			LOG_WARN(LogBots, "[BotAI] barricade: no StructuralSupportSystem available");
			return;
		}

		FVector WallLoc{};
		if (!CustomBotBuilding::CellCenterAhead(SSS, BotLoc, Facing, 1, WallLoc))
		{
			LOG_WARN(LogBots, "[BotAI] barricade: could not resolve adjacent cell");
			return;
		}

		FVector Ground = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
			FVector{ WallLoc.X, WallLoc.Y, 0.0f }, BotLoc.Z + 3000.0f, BotLoc.Z - 8000.0f, FName(0));
		WallLoc.Z = Ground.Z;

		auto WallClass = CustomBotBuilding::GetPieceClass(CustomBotBuilding::EPieceType::Wall);

		if (WallClass)
		{
			CustomBotBuilding::BuildWall(Bot, WallLoc, FRotator{0.0f, Facing, 0.0f});
			LOG_INFO(LogBots, "[BotAI] barricade wall at cell ({:.0f},{:.0f},{:.0f}) facing {:.0f}",
				WallLoc.X, WallLoc.Y, WallLoc.Z, Facing);
		}
	}

	static void FleeWithWalls(CustomBot& Bot, BotAIContext& Ctx, const FVector& EnemyLoc, bool bBuildWalls)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		CustomBotCombat::StopFiring(Bot);
		Ctx.bIsFiring = false;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector AwayDir = CustomBotMovement::DirectionTo(EnemyLoc, BotLoc);

		if (!(AwayDir | AwayDir))
			return;

		FVector Away = BotLoc + AwayDir * 1500.0f;

		CustomBotMovement::LookAt(Bot, BotLoc + AwayDir * 100.0f);
		CustomBotMovement::MoveTo(Bot, Away, 100.0f, true, false);

		if (bBuildWalls && Ctx.ActionTimer <= 0.0f)
		{
			BuildBarricade(Bot, Ctx, EnemyLoc);
			Ctx.ActionTimer = 0.9f;
		}
	}

	static void DoDefending(CustomBot& Bot, BotAIContext& Ctx, AActor* Enemy)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (Ctx.ActionTimer <= 0.0f && Enemy)
		{
			BuildBarricade(Bot, Ctx, Enemy->GetActorLocation());
			Ctx.ActionTimer = 1.5f;
		}

		if (Enemy)
			DoFighting(Bot, Ctx, Enemy);
	}

	static bool HasNearbyLoot(CustomBot& Bot, BotAIContext& Ctx, float Radius)
	{
		return CustomBotPerception::FindNearestPickup(Bot, Radius) != nullptr
			|| CustomBotPerception::FindNearestUnopenedContainer(Bot, Radius) != nullptr;
	}

	static bool HasNearbyObstacle(CustomBot& Bot, float Radius)
	{
		CBT::EObstacleType T;
		return CustomBotPerception::FindNearestObstacle(Bot, Radius, T) != nullptr;
	}

	static void Decide(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		if (Bot.GetLifeState() == CBT::ELifeState::Dead)
		{
			Ctx.State = EBotState::Dead;
			CustomBotCombat::StopFiring(Bot);
			Bot.bInCombat = false;
			return;
		}

		Bot.bInCombat = false;

		bool bHeal = NeedsHealing(Bot);
		bool bAttacked = IsBeingAttacked(Bot, Ctx);

		AActor* Enemy = ScanForEnemy(Bot, Ctx);
		Ctx.EnemyTarget = Enemy;

		bool bRotate = CustomBotAI::IsOutsideSafeZone(Bot, Ctx, 2000.0f);

		if (bRotate)
		{
			Ctx.State = EBotState::Rotating;
			return;
		}

		int Alive = CustomBotAI::GetAliveCount();

		if (Alive <= 10 && Alive > 0)
		{
			Ctx.State = EBotState::EndGame;
			return;
		}

		if (Enemy)
		{
			float MyHP = Bot.GetHealth() + Bot.GetShield();
			float EnemyHP = PawnTotalHealth(Enemy);

			if (EnemyHP <= MyHP + 20.0f || Ctx.Personality.RiskTolerance > 0.6f)
			{
				Ctx.State = EBotState::Fighting;
				Bot.bInCombat = true;
			}
			else
			{
				Ctx.State = EBotState::Defending;
				Bot.bInCombat = true;
			}

			return;
		}

		if (bHeal)
		{
			Ctx.State = EBotState::Healing;
			return;
		}

		int Materials = CustomBotResources::GetTotalResourceCount(Bot);

		if (Materials < 120 && HasNearbyObstacle(Bot, 900.0f))
		{
			Ctx.State = EBotState::Farming;
			return;
		}

		if (Ctx.Personality.Aggression > 0.6f)
			Ctx.State = EBotState::SearchingEnemy;
		else if (!bAttacked && HasNearbyLoot(Bot, Ctx, 1200.0f))
			Ctx.State = EBotState::Looting;
		else if (Materials < 200 && (float)(std::rand() % 100) < 35)
			Ctx.State = EBotState::Farming;
		else
			Ctx.State = EBotState::Exploring;
	}

	static void Update(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		TickTimers(Bot, Ctx);

		bool bWarmup = CustomBotAI::IsWarmupPhase();

		if (bWarmup)
			RefillLobbyHP(Bot);

		if (Ctx.DecisionTimer <= 0.0f || Ctx.State == EBotState::Dead)
		{
			Decide(Bot, Ctx);
			Ctx.DecisionTimer = 0.5f;
		}

		switch (Ctx.State)
		{
		case EBotState::Looting:         DoLooting(Bot, Ctx); break;
		case EBotState::Farming:         DoFarming(Bot, Ctx); break;
		case EBotState::Exploring:       DoExploring(Bot, Ctx); break;
		case EBotState::SearchingEnemy:  DoExploring(Bot, Ctx); break;
		case EBotState::Fighting:        DoFighting(Bot, Ctx, Ctx.EnemyTarget); break;
		case EBotState::Defending:       DoDefending(Bot, Ctx, Ctx.EnemyTarget); break;
		case EBotState::Healing:         DoHealing(Bot, Ctx); break;
		case EBotState::Rotating:        DoRotating(Bot, Ctx, false); break;
		case EBotState::EndGame:         DoEndGame(Bot, Ctx); break;
		case EBotState::Dead: break;
		default:
			Ctx.State = EBotState::Exploring;
			DoExploring(Bot, Ctx);
			break;
		}

		if (bWarmup)
			Ctx.State = EBotState::Warmup;
	}

	static FVector PickWanderTarget(CustomBot& Bot, const FVector& Bias)
	{
		FVector BotLoc = Bot.Pawn->GetActorLocation();

		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Bias);

		if (!(Dir | Dir))
			Dir = Bot.Pawn->GetActorForwardVector();

		if (!(Dir | Dir))
			Dir = FVector{ 1.0f, 0.0f, 0.0f };

		constexpr float SpreadRad = 60.0f * 3.14159265358979323846f / 180.0f;
		float Base = FMath::Atan2(Dir.Y, Dir.X);
		float Angle = Base + (float(std::rand() % 1000) / 1000.0f * 2.0f - 1.0f) * SpreadRad;

		float Dist = 400.0f + float(std::rand() % 900);

		FVector Target{ BotLoc.X + FMath::Cos(Angle) * Dist, BotLoc.Y + FMath::Sin(Angle) * Dist, BotLoc.Z };

		return CustomBotPerception::FindReachablePoint(Bot, Target);
	}

	static void DoEndGame(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		int Alive = CustomBotAI::GetAliveCount();

		if (Alive <= 5)
		{
			AActor* Enemy = Ctx.EnemyTarget;

			if (Enemy)
				DoFighting(Bot, Ctx, Enemy);
			else
				DoRotating(Bot, Ctx, Ctx.Personality.Aggression > 0.5f);

			return;
		}

		if (AActor* Enemy = Ctx.EnemyTarget)
		{
			DoDefending(Bot, Ctx, Enemy);
			return;
		}

		DoRotating(Bot, Ctx, Ctx.Personality.Aggression > 0.5f);
	}

	static constexpr float kWarmupLobbyHP = 2000.0f;

	static void RefillLobbyHP(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		Bot.Pawn->SetMaxHealth(kWarmupLobbyHP);

		if (Bot.Pawn->GetHealth() < kWarmupLobbyHP)
			Bot.Pawn->SetHealth(kWarmupLobbyHP);

		if (Bot.Pawn->GetShield() != 0.0f)
			Bot.Pawn->SetShield(0.0f);
	}

	static void ResetToMatchHP(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		Bot.Pawn->SetMaxHealth(100.0f);
		Bot.Pawn->SetHealth(100.0f);
		Bot.Pawn->SetMaxShield(100.0f);
		Bot.Pawn->SetShield(0.0f);
	}
}
