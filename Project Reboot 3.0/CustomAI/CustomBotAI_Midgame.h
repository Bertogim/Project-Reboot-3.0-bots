#pragma once

// CustomBot AI - Midgame (despues del aterrizaje).
//
// Maquina de decisiones para todo lo que ocurre una vez el bot esta en el suelo:
// loot, farm, explorar, buscar enemigos, combatir, defenderse, curarse, rotar y
// endgame. Prioridades (Section 24):
//   1 peligro inmediato  2 defenderse  3 storm  4 curarse
//   5 combatir           6 rotar       7 loot   8 farmear  9 explorar
//
// Performance (Section 35): las decisiones y escaneos pesados corren en
// intervalos (timer), no cada frame. El movimiento se delega en el pipeline
// MoveTo/UpdateMovement del CustomBot (Parte 1).

#include "CustomBotAI.h"

namespace CustomBotAIMidgame
{
	// Forward declarations (usadas antes de su definicion mas abajo; C3861).
	static FVector PickWanderTarget(CustomBot& Bot, const FVector& Bias);
	static void BuildBarricade(CustomBot& Bot, BotAIContext& Ctx, const FVector& EnemyLoc);
	static void DoEndGame(CustomBot& Bot, BotAIContext& Ctx);
	static bool TryResolveBlockedPath(FVector& MoveTarget, CustomBot& Bot, BotAIContext& Ctx);
	static void DoWarmup(CustomBot& Bot, BotAIContext& Ctx);

	// Rango cuerpo a cuerpo del pico (alcance de swing). Lo usan tanto el
	// combate normal sin armas como los duelos de warmup.
	static constexpr float kMeleeRange = 260.0f;

	// --- Timers ---------------------------------------------------------------
	static float Elapsed;
	static float LastTime = -1.0f;

	// Acumula el tiempo real transcurrido y avanza los timers del contexto.
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
		Ctx.StrafeTimer -= Elapsed;
	}

	// --- Evaluacion de vida / escudo ------------------------------------------
	static bool NeedsHealing(CustomBot& Bot)
	{
		if (!Bot.IsReady())
			return false;

		float Health = Bot.GetHealth();
		float Shield = Bot.GetShield();

		// Curarse si vida baja o escudo bajo.
		return Health < 75.0f || Shield < 50.0f;
	}

	static bool IsBeingAttacked(CustomBot& Bot)
	{
		// Best-effort: considera que esta "siendo atacado" si la vida no esta
		// llena y hay un enemigo visible muy cerca.
		return Bot.IsReady() && Bot.GetHealth() < 100.0f;
	}

	// --- Percepcion de enemigos (con limite propio, no omnisciente) ------------
	static float PerceptionRadius(BotAIContext& Ctx)
	{
		// El alcance de deteccion depende de la atencion (demora/limita la
		// deteccion de enemigos lejanos -> imperfeccion humana).
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

	// --- Decision de estado (se ejecuta en intervalos) -------------------------
	static void Decide(CustomBot& Bot, BotAIContext& Ctx);

	// --- Acciones por estado ---------------------------------------------------

	// --- Mejora de armas (Seccion 4) ------------------------------------------
	// Slots de la quickbar de armas/consumibles (parecido a un jugador real).
	static constexpr int kQuickbarLootSlots = 5;

	// Puntuacion heuristica de un item para comparar calidad: nivel (rareza/tier)
	// como base + bonus por categoria de arma (sniper/launcher mejor que pistola).
	static int ItemLootScore(FFortItemEntry* Entry)
	{
		if (!Entry || !Entry->GetItemDefinition())
			return 0;

		auto Def = Entry->GetItemDefinition();
		int Score = (Entry->GetLevel() > 0 ? Entry->GetLevel() : 1) * 100;

		std::string Path = Def->GetPathName();

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

	// Cuenta los items que ocupan slots de "quickbar" (armas + consumibles).
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

	// Devuelve el arma (Weapon) con menor puntuacion del inventario, o nullptr.
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

	// Equipa el arma con mayor puntuacion del inventario (fortalece el swap de
	// armas: tras recoger una mejor, cambia a ella en vez de a la primera).
	static bool EquipBestWeapon(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return false;

		UFortItem* Best = nullptr;
		int BestScore = 0;
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
			if (!Best || S > BestScore)
			{
				BestScore = S;
				Best = Item;
			}
		}

		return Best ? CustomBotInventory::EquipItem(Bot, Best) : false;
	}

	// Si la quickbar esta llena y el pickup es un arma MEJOR que la peor que
	// llevamos, soltamos la peor para hacer hueco antes de recogerlo.
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

	// LOOT: moverse al loot / cofre mas cercano y recoger.
	static void DoLooting(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		const float LootRange = 1500.0f * (0.6f + Ctx.Personality.LootSkill * 0.8f);

		// Preferencia de loot segun personalidad: primero armas, luego consumibles.
		AFortPickup* Pickup = nullptr;

		if (Ctx.Personality.LootSkill >= 0.3f)
			Pickup = CustomBotPerception::FindNearestWeapon(Bot, LootRange);

		if (!Pickup && Ctx.Personality.LootSkill < 0.7f)
			Pickup = CustomBotPerception::FindNearestConsumable(Bot, LootRange);

		if (!Pickup)
			Pickup = CustomBotPerception::FindNearestPickup(Bot, LootRange);

		ABuildingContainer* Container = CustomBotPerception::FindNearestUnopenedContainer(Bot, LootRange);

		// Preferir un cofre sin abrir si hay uno cerca (buen loot).
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
				CustomBotMovement::MoveTo(Bot, Container->GetActorLocation(), 120.0f, true);
			}

			return;
		}

		if (Pickup)
		{
			Ctx.LootTarget = Pickup;

			if (Bot.Pawn->GetDistanceTo(Pickup) <= CustomBotInteraction::InteractionRadius)
			{
				// Quickbar llena: soltar la peor arma si el pickup es mejor (Sec 4).
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
			}

			return;
		}

		// Sin loot cerca: buscar un nuevo punto o explorar.
		if (!Bot.HasMoveRequest() || Bot.HasArrived() || Bot.IsPathBlocked())
		{
			// Si el camino hacia el destino actual esta bloqueado, intentar
			// saltar/construir/destruir antes de reapuntar (Sec. 6/7/14).
			if (Bot.IsPathBlocked())
			{
				FVector ResolveDest = Bot.MoveRequest.Destination;
				if (CustomBotAIMidgame::TryResolveBlockedPath(ResolveDest, Bot, Ctx))
					return;
			}

			FVector Goal = Bot.Pawn->GetActorLocation() + CustomBotMovement::DirectionTo(
				Bot.Pawn->GetActorLocation(),
				CustomBotAI::GetSafeZoneCenter(Bot)) * 800.0f;

			FVector Wander = CustomBotAIMidgame::PickWanderTarget(Bot, Goal);
			CustomBotMovement::MoveTo(Bot, Wander, 150.0f, true);
		}
	}

	// Devuelve un punto de exploracion (ofuscado con algo de aleatoriedad).
	static FVector PickWanderTarget(CustomBot& Bot, const FVector& Bias);

	// FARM: encontrar arbol/roca, equipar pico y destruir.
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
					// Equipar el pico y golpear el objeto (melee) para ganar
					// materiales. La destruccion usa el pipeline real de
					// estructura (DestroyTarget); el recurso lo otorga el juego
					// via OnDamageServer al golpear con arma melee.
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

		// Sin objetos cerca, explorar.
		if (!Bot.HasMoveRequest() || Bot.HasArrived())
		{
			FVector Goal = Bot.Pawn->GetActorLocation() + CustomBotMovement::DirectionTo(
				Bot.Pawn->GetActorLocation(),
				CustomBotAI::GetSafeZoneCenter(Bot)) * 1000.0f;

			CustomBotMovement::MoveTo(Bot, PickWanderTarget(Bot, Goal), 150.0f, true);
		}
	}

	// EXPLORAR / BUSCAR ENEMIGO: moverse a un destino y mirar alrededor.
	static void DoExploring(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		// Camino bloqueado: intentar saltar/construir/destruir antes de reapuntar
		// (Secciones 6/7/14). Si resuelve, seguimos hacia el mismo destino.
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

	// Camino hacia MoveTarget bloqueado (Secciones 6/7/14):
	//   - desnivel alto -> construir rampa si hay materiales (Seccion 7),
	//   - estructura/objeto delante -> destruirlo (Seccion 14),
	//   - obstaculo bajo -> saltar (Seccion 6).
	// Devuelve true si "hizo algo" este tick (construyo/destruyo/salto); el
	// llamador entonces no reapunta el movimiento. Si devuelve false, el llamador
	// re-elige un destino alternativo (rodear / buscar otra ruta).
	static bool TryResolveBlockedPath(FVector& MoveTarget, CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector BotLoc = Bot.GetLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, MoveTarget);

		// 1) Desnivel REAL del terreno por delante (a ~500u) vs la altura del bot:
		//    el Z del destino suele ser el del suelo, asi que medimos el terreno.
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
					// Rampa SIEMPRE mirando hacia el bot (los StairW suben hacia +X
					// local): la DERECHA de la rampa queda enfrente nuestra. Mismo
					// patron que la secuencia debug (CustomBotDebug.cpp).
					auto GS = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
					auto SSS = GS ? GS->GetStructuralSupportSystem() : nullptr;

					float Facing = CustomBotBuilding::SnapYawToCardinal(Bot.GetRotation().Yaw);
					FRotator RampRot = Bot.GetRotation();
					RampRot.Yaw = CustomBotBuilding::SnapYawToCardinal(Facing + 90.0f);

					// Posicion: la celda del grid inmediatamente adelante, con el Z
					// del TERRENO (el Z de la celda es una referencia, no el suelo).
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

				// Sin materiales (o sin grid): rodear. El llamador elige otro destino.
				return false;
			}
		}

		// 2) Estructura/objeto delante que impide el paso -> destruir.
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
					// Solo si esta delante (misma direccion que el destino).
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

		// 3) Obstaculo bajo en el frente -> saltar encima.
		if (Ctx.ActionTimer <= 0.0f)
		{
			CustomBotMovement::Jump(Bot);
			Ctx.ActionTimer = 0.8f;
			return true;
		}

		return false;
	}

	// ROTAR: ir hacia el centro de la zona segura.
	static void DoRotating(CustomBot& Bot, BotAIContext& Ctx, bool bAggressive)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FVector Center = CustomBotAI::GetSafeZoneCenter(Bot);

		// Destino dentro de la zona, no exactamente en el centro (con margen).
		FVector Dest = Center;
		Dest.X += (float)(std::rand() % 400) - 200.0f;
		Dest.Y += (float)(std::rand() % 400) - 200.0f;

		// Camino bloqueado: intentar resolver antes de seguir (Sec. 6/7/14).
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

	// HEAL: usar consumible si lo tenemos; si no, refugiarse/rotar.
	static void DoHealing(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		if (CustomBotInteraction::UseConsumable(Bot))
		{
			// Dejar de moverse mientras se cura.
			CustomBotMovement::StopMovement(Bot);
			return;
		}

		// Sin consumibles: rotar a zona segura y dejar que se regenere un poco.
		DoRotating(Bot, Ctx, false);
	}

	// --- COMBATE --------------------------------------------------------------

	// Apunta al objetivo con un error proporcional a la punteria (imperfeccion
	// humana). El error se recalcula en cada tick de disparo.
	static void AimWithSkill(CustomBot& Bot, BotAIContext& Ctx, const FVector& TargetLoc)
	{
		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, TargetLoc);

		float Error = (1.0f - Ctx.Personality.AimSkill) * 0.9f; // ~0.09..0.9

		// Error angular en radianes -> desvia la direccion.
		float ErrAng = Error * 0.12f;
		Dir.X += (float)(std::rand() % 1000) / 1000.0f * ErrAng - ErrAng * 0.5f;
		Dir.Y += (float)(std::rand() % 1000) / 1000.0f * ErrAng - ErrAng * 0.5f;
		Dir.Z += (float)(std::rand() % 1000) / 1000.0f * ErrAng - ErrAng * 0.5f;

		FVector Aim = FVector{TargetLoc.X + Dir.X * 100.0f, TargetLoc.Y + Dir.Y * 100.0f,
			TargetLoc.Z + Dir.Z * 100.0f};

		CustomBotMovement::LookAt(Bot, Aim);
	}

	static void DoFighting(CustomBot& Bot, BotAIContext& Ctx, AActor* Enemy)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		// Confirmar que el enemigo sigue vivo/visible.
		if (!Enemy || Enemy->IsActorBeingDestroyed())
		{
			Ctx.EnemyTarget = nullptr;
			Ctx.State = EBotState::Exploring;
			CustomBotCombat::StopFiring(Bot);
			Ctx.bIsFiring = false;
			return;
		}

		FVector EnemyLoc = Enemy->GetActorLocation();

		// Rotar/apuntar (si el bot era agresivo intenta apuntar mejor).
		AimWithSkill(Bot, Ctx, EnemyLoc);

		// Impulsos: strafe LATERAL alternando direccion (no orbitar). El bot
		// siempre mira al enemigo (AimWithSkill), asi que un strafe fijo a la
		// derecha lo hace girar en circulo alrededor del objetivo.
		if (!Bot.HasMoveRequest() || Bot.HasArrived())
		{
			if (Ctx.StrafeTimer <= 0.0f || Ctx.StrafeDir == 0)
			{
				Ctx.StrafeDir = (std::rand() % 2) ? -1 : 1;
				Ctx.StrafeTimer = 1.0f + float(std::rand() % 1500) / 1000.0f;
			}

			CustomBotMovement::MoveRight(Bot, 0.5f * (float)Ctx.StrafeDir);
		}

		// Arma real equipada? (el pico no cuenta como arma de fuego).
		bool bHasWeapon = CustomBotCombat::IsWeaponEquipped(Bot) && !CustomBotCombat::IsPickaxeEquipped(Bot);

		if (!bHasWeapon)
		{
			EquipBestWeapon(Bot);
			bHasWeapon = CustomBotCombat::IsWeaponEquipped(Bot) && !CustomBotCombat::IsPickaxeEquipped(Bot);
		}

		if (bHasWeapon)
		{
			int Ammo = CustomBotCombat::GetCurrentAmmo(Bot);

			bool bLOS = CustomBotPerception::HasLineOfSight(Bot, EnemyLoc, Enemy);
			bool bInRange = Bot.Pawn->GetDistanceTo(Enemy) <= 30000.0f;

			// React timer introduce retraso humano antes de disparar.
			if (Ctx.ReactTimer <= 0.0f && bLOS && bInRange)
			{
				if (Ammo > 0)
				{
					if (Ctx.ActionTimer <= 0.0f)
					{
						// Racha de disparos determinada por la agresividad.
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
					// Sin municion: recargar de forma cosmetica.
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
		}
		else
		{
			// SIN ARMAS: cuerpo a cuerpo con el pico. Acercarse y golpear
			// (nada de orbitar al enemigo: se planta en rango de melee).
			CustomBotCombat::StopFiring(Bot);
			Ctx.bIsFiring = false;

			float Dist = Bot.Pawn->GetDistanceTo(Enemy);

			if (Dist <= kMeleeRange)
			{
				CustomBotMovement::StopMovement(Bot);
				CustomBotMovement::LookAt(Bot, EnemyLoc);

				if (Ctx.ActionTimer <= 0.0f)
				{
					CustomBotInventory::EquipPickaxe(Bot);
					CustomBotCombat::FireWeapon(Bot); // swing del pico
					Ctx.ActionTimer = 0.35f;
				}
			}
			else
			{
				if (!Bot.HasMoveRequest() || Bot.HasArrived())
					CustomBotMovement::MoveTo(Bot, EnemyLoc, kMeleeRange * 0.5f, true, false);
			}
		}

		// Pequena probabilidad de construir cobertura (defensiva / BuildSkill).
		if (Ctx.Personality.BuildSkill > 0.35f && Ctx.ActionTimer <= 0.0f)
		{
			if ((float)(std::rand() % 1000) / 1000.0f <= Ctx.Personality.BuildSkill * 0.4f)
			{
				BuildBarricade(Bot, Ctx, EnemyLoc);
				Ctx.ActionTimer = 1.2f;
			}
		}
	}

	// Construye una pared rapida de cobertura frente al enemigo.
	// La pared se coloca en la celda del grid ADYACENTE hacia el enemigo (1 paso),
	// con el Z del TERRENO de esa celda. Si se coloca en una posicion arbitraria
	// (BotLoc+Dir*150) no queda en el grid y el juego la elimina al instante.
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

		// Celda adyacente (1 paso) hacia el enemigo en la cardinal mas cercana.
		FVector WallLoc{};
		if (!CustomBotBuilding::CellCenterAhead(SSS, BotLoc, Facing, 1, WallLoc))
		{
			LOG_WARN(LogBots, "[BotAI] barricade: could not resolve adjacent cell");
			return;
		}

		// Z del terreno de esa celda (la celda es una referencia vertical; el Z
		// real del suelo lo da el trace, igual que la rampa de la secuencia debug).
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

	// --- DEFENDERA / reaccionar al ser superior -------------------------------
	static void DoDefending(CustomBot& Bot, BotAIContext& Ctx, AActor* Enemy)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		// Construir cobertura y devolver fuego con cautela.
		if (Ctx.ActionTimer <= 0.0f && Enemy)
		{
			BuildBarricade(Bot, Ctx, Enemy->GetActorLocation());
			Ctx.ActionTimer = 1.5f;
		}

		if (Enemy)
			DoFighting(Bot, Ctx, Enemy);
	}

	// --- Escaneo de loot / recursos (para decidir estado) ---------------------
	static bool HasNearbyLoot(CustomBot& Bot, BotAIContext& Ctx, float Radius)
	{
		return CustomBotPerception::FindNearestPickup(Bot, Radius) != nullptr;
	}

	static bool HasNearbyObstacle(CustomBot& Bot, float Radius)
	{
		CBT::EObstacleType T;
		return CustomBotPerception::FindNearestObstacle(Bot, Radius, T) != nullptr;
	}

	// --- Master decision del midgame ------------------------------------------
	static void Decide(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		// 1. Estado de vida.
		if (Bot.GetLifeState() == CBT::ELifeState::Dead)
		{
			Ctx.State = EBotState::Dead;
			CustomBotCombat::StopFiring(Bot);
			return;
		}

		// Vida/escudo y situacion.
		bool bHeal = NeedsHealing(Bot);
		bool bAttacked = IsBeingAttacked(Bot);

		// Enemigo visible?
		AActor* Enemy = ScanForEnemy(Bot, Ctx);
		Ctx.EnemyTarget = Enemy;

		// Fuera de la zona segura / lejos -> rotar (prioridad alta, no esperar).
		bool bRotate = CustomBotAI::IsOutsideSafeZone(Bot, Ctx, 2000.0f);

		if (bRotate)
		{
			Ctx.State = EBotState::Rotating;
			return;
		}

		// Endgame (pocos jugadores): Top 20 -> mas atencion a la Storm (ya se rota
		// antes por prioridad alta); en Top 10/5/2 el comportamiento se afina en
		// DoEndGame (Seccion 21).
		int Alive = CustomBotAI::GetAliveCount();

		if (Alive <= 10 && Alive > 0)
		{
			Ctx.State = EBotState::EndGame;
			return;
		}

		// Prioridad de combate / defensa.
		if (Enemy)
		{
			// Evaluar ventaja -> pelear o defenderse.
			float MyHP = Bot.GetHealth() + Bot.GetShield();
			float EnemyHP = 100.0f;

			if (EnemyHP <= MyHP + 20.0f || Ctx.Personality.RiskTolerance > 0.6f)
				Ctx.State = EBotState::Fighting;
			else
				Ctx.State = EBotState::Defending;

			return;
		}

		// Curación.
		if (bHeal)
		{
			Ctx.State = EBotState::Healing;
			return;
		}

		// Comportamiento del midgame segun personalidad.
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

	// --- Master tick del midgame ----------------------------------------------
	static void Update(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		// Timers y escaneos a intervalos (performance).
		TickTimers(Bot, Ctx);

		// Warmup: el estado se decide por la fase de juego (no por Decide). Aunque
		// DoWarmup delegue en DoFighting/DoLooting (que cambian Ctx.State), la
		// fase Warmup mantiene el lobby hasta que arranque el avion.
		if (CustomBotAI::IsWarmupPhase())
		{
			Ctx.State = EBotState::Warmup;
			DoWarmup(Bot, Ctx);
			return;
		}

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
	}

	// --- Wander helper ---------------------------------------------------------
	static FVector PickWanderTarget(CustomBot& Bot, const FVector& Bias)
	{
		FVector BotLoc = Bot.Pawn->GetActorLocation();

		// Direccion general hacia el centro +/- dispersion.
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Bias);

		float Angle = float(std::rand() % 360);
		float Rad = Angle * 3.14159265358979323846f / 180.0f;

		// Desplazamiento aleatorio (300..1200 unidades).
		float Dist = 300.0f + float(std::rand() % 900);

		FVector Offset{ FMath::Cos(Rad) * Dist, FMath::Sin(Rad) * Dist, 0.0f };

		FVector Target = BotLoc + Offset;

		// Si hay LOS obstruido, buscar un punto alcanzable.
		return CustomBotPerception::FindReachablePoint(Bot, Target);
	}

	// --- ENDGAME ---------------------------------------------------------------
	static void DoEndGame(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		int Alive = CustomBotAI::GetAliveCount();

		// Top 5 / Top 2 (1v1): intentar ganar activamente. Si hay enemigo visible,
		// plantarle cara (disparar/construir); si no, mantenerse en zona segura.
		if (Alive <= 5)
		{
			AActor* Enemy = Ctx.EnemyTarget;

			if (Enemy)
				DoFighting(Bot, Ctx, Enemy);
			else
				DoRotating(Bot, Ctx, Ctx.Personality.Aggression > 0.5f);

			return;
		}

		// Top 10: mas cobertura y atencion a la zona segura.
		if (AActor* Enemy = Ctx.EnemyTarget)
		{
			DoDefending(Bot, Ctx, Enemy);
			return;
		}

		DoRotating(Bot, Ctx, Ctx.Personality.Aggression > 0.5f);
	}

	// --- WARMUP / PRE-PARTIDA --------------------------------------------------
	// Simula un lobby activo mientras el bus no ha arrancado: el bot pasea por la
	// isla, recoge lo que encuentra y dispara a otros bots/jugadores que ve cerca
	// (como los jugadores reales matando el tiempo antes de que despegue el avion).
	//
	// INVULNERABILIDAD DEL LOBBY: el bot tiene vida de tanque en la pre-partida.
	// El daño se sigue aplicando de forma real (el atacante ve sus numeros y no
	// se rompen los marcadores de daño), pero con 2000 de vida ni un snipe lo
	// tumba de un tiro; entre frames se rellena la vida, asi que nunca muere en
	// el lobby. Al saltar del bus (o empezar la partida) se resetea a 100/0.
	static constexpr float kWarmupLobbyHP = 2000.0f;

	// COMBATE DE WARMUP: los bots NO orbitan al jugador (el DoFighting hace
	// strafe lateral en circulo constante). Aqui:
	//   - defensivo (Aggression < 0.4): HUYE del enemigo cercano,
	//   - agresivo: se acerca y ataca cuerpo a cuerpo con el pico (o contra
	//     otros bots), manteniendo la distancia de swing en vez de rodear.
	static void DoWarmupFight(CustomBot& Bot, BotAIContext& Ctx, AActor* Enemy)
	{
		if (!Enemy || Enemy->IsActorBeingDestroyed())
		{
			Ctx.EnemyTarget = nullptr;
			CustomBotCombat::StopFiring(Bot);
			Ctx.bIsFiring = false;
			Ctx.State = EBotState::Warmup;
			return;
		}

		FVector EnemyLoc = Enemy->GetActorLocation();
		float Dist = Bot.Pawn->GetDistanceTo(Enemy);
		FVector BotLoc = Bot.Pawn->GetActorLocation();

		// Personalidad defensiva: no se enfrenta, se aleja del enemigo.
		if (Ctx.Personality.Aggression < 0.4f)
		{
			FVector Away = BotLoc + CustomBotMovement::DirectionTo(EnemyLoc, BotLoc) * 1500.0f;

			CustomBotMovement::LookAt(Bot, EnemyLoc);
			CustomBotMovement::MoveTo(Bot, Away, 100.0f, true, false);
			return;
		}

		// Agresivo: mirar siempre al enemigo.
		CustomBotMovement::LookAt(Bot, EnemyLoc);

		if (Dist <= kMeleeRange)
		{
			// Cuerpo a cuerpo: plantarse y golpear con el pico.
			CustomBotMovement::StopMovement(Bot);

			if (Ctx.ActionTimer <= 0.0f)
			{
				CustomBotInventory::EquipPickaxe(Bot);
				CustomBotCombat::FireWeapon(Bot); // swing del pico
				Ctx.ActionTimer = 0.35f;
			}

			Ctx.State = EBotState::Warmup;
			return;
		}

		// Lejos: acercarse hasta quedar en rango de melee (nunca orbitar).
		if (!Bot.HasMoveRequest() || Bot.HasArrived())
			CustomBotMovement::MoveTo(Bot, EnemyLoc, kMeleeRange * 0.4f, true, false);
	}

	// Refuerzo del lobby que se aplica CADA tick: asegura que el ejemplar tenga
	// el tope de tanque (2000), rellena la vida a 2000 si recibio daño y mantiene
	// el escudo a 0 (en el lobby no hay escudo). El daño se aplica de forma real
	// (los marcadores del atacante funcionan), pero la vida nunca llega a cero.
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

	// Vuelve a la vida normal de partida (100 de vida, 0 de escudo) al salir del
	// lobby: se llama cuando el bus arranca o la partida pasa a zonas seguras.
	static void ResetToMatchHP(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		Bot.Pawn->SetMaxHealth(100.0f);
		Bot.Pawn->SetHealth(100.0f);
		Bot.Pawn->SetMaxShield(100.0f);
		Bot.Pawn->SetShield(0.0f);
	}

	static void DoWarmup(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		// Cada tick: vida de tanque + escudo a 0 (invulnerabilidad del lobby).
		RefillLobbyHP(Bot);

		// Escaneo de enemigos y loot a intervalos (ScanTimer), no cada frame
		// (el GetAllActorsOfClass de cada barrido aloca + filtra arrays).
		if (Ctx.ScanTimer <= 0.0f)
		{
			Ctx.EnemyTarget = ScanForEnemy(Bot, Ctx);
			Ctx.LootTarget = CustomBotPerception::FindNearestPickup(Bot, 2500.0f);
			Ctx.ScanTimer = 0.25f;
		}

		AActor* Enemy = Ctx.EnemyTarget;

		if (Enemy && !Enemy->IsActorBeingDestroyed())
		{
			// Warmup: pelear cuerpo a cuerpo con el pico (o huir si es
			// defensivo), nunca orbitar al jugador.
			DoWarmupFight(Bot, Ctx, Enemy);
			return;
		}

		if (Enemy)
			Ctx.EnemyTarget = nullptr;

		// Sin enemigos: lootear lo que encuentre o pasear por la isla.
		AFortPickup* Pickup = static_cast<AFortPickup*>(Ctx.LootTarget);

		if (Pickup && Pickup->IsActorBeingDestroyed())
		{
			Ctx.LootTarget = nullptr;
			Pickup = nullptr;
		}

		if (Pickup)
		{
			if (Bot.Pawn->GetDistanceTo(Pickup) <= CustomBotInteraction::InteractionRadius)
			{
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
			}

			return;
		}

		// Si llegamos al destino y no hay loot, elegir otro punto de paseo.
		if (!Bot.HasMoveRequest() || Bot.HasArrived())
		{
			FVector Wander = PickWanderTarget(Bot, Bot.Pawn->GetActorLocation());
			CustomBotMovement::MoveTo(Bot, Wander, 150.0f, true);
		}
	}
}
