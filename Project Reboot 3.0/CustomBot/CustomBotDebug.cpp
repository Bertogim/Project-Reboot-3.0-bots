#include "CustomBotDebug.h"

#include "CustomBot.h"

#include "CustomBotSpawner.h"
#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotResources.h"
#include "CustomBotCombat.h"
#include "CustomBotBuilding.h"
#include "CustomBotDestruction.h"
#include "CustomBotInteraction.h"

#include "FortItem.h"
#include "BuildingSMActor.h"
#include "GameplayStatics.h"

#include <format>
#include <string>

namespace
{
	// Envia un mensaje al chat del jugador (misma tecnica que SendMessageToConsole
	// de commands.h, pero local para no arrastrar el sistema antiguo de bots).
	void SendBotMessage(AFortPlayerController* PlayerController, const wchar_t* Msg)
	{
		if (!PlayerController)
			return;

		FString FMsg = Msg;
		FName TypeName = FName();
		float MsgLifetime = 1;

		struct
		{
			FString S;
			FName Type;
			float MsgLifeTime;
		} PlayerController_ClientMessage_Params{ FMsg, TypeName, MsgLifetime };

		static auto ClientMessageFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ClientMessage");
		PlayerController->ProcessEvent(ClientMessageFn, &PlayerController_ClientMessage_Params);
	}

	bool TryParseFloat(const std::string& Text, float& Out)
	{
		try
		{
			Out = std::stof(Text);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool TryParseInt(const std::string& Text, int& Out)
	{
		try
		{
			Out = std::stoi(Text);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	// Convierte el nombre de clase (std::string, UTF-8/ASCII) a wchar para SendBotMessage.
	std::wstring ToWide(const std::string& Text)
	{
		if (Text.empty())
			return L"";

		std::wstring Out(Text.size(), L' ');

		for (size_t i = 0; i < Text.size(); ++i)
			Out[i] = (wchar_t)(unsigned char)Text[i];

		return Out;
	}

	// Bot objetivo de los comandos (el primer bot custom valido).
	CustomBot* GetFirstValidBot()
	{
		auto& Bots = CustomBotSpawner::AllCustomBots;

		for (auto& Bot : Bots)
		{
			if (Bot.IsValidActor())
				return &Bot;
		}

		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// Secuencia de prueba "debugbot" (Parte 1): maquina de estados + timers.
// Un unico debugbot activo; cada paso ocurre en el orden esperado usando los
// sistemas reales (movimiento, salto, construccion, destruccion, armas...).
// ---------------------------------------------------------------------------
namespace
{
	enum class DebugBotState : uint8_t
	{
		None,
		Spawned,
		MovingForward,
		Jumping,
		BuildingRamp,
		ClimbingRamp,
		Turning,
		EquippingPickaxe,
		DestroyingRamp,
		EquippingWeapon,
		Shooting,
		DroppingWeapon,
		WaitingToDisappear,
		Finished,
	};

	// Nombre (narrow) del estado para los logs spdlog.
	const char* DebugBotStateName(DebugBotState State)
	{
		switch (State)
		{
		case DebugBotState::Spawned:            return "Spawned";
		case DebugBotState::MovingForward:      return "MovingForward";
		case DebugBotState::Jumping:            return "Jumping";
		case DebugBotState::BuildingRamp:       return "BuildingRamp";
		case DebugBotState::ClimbingRamp:       return "ClimbingRamp";
		case DebugBotState::Turning:            return "Turning";
		case DebugBotState::EquippingPickaxe:   return "EquippingPickaxe";
		case DebugBotState::DestroyingRamp:     return "DestroyingRamp";
		case DebugBotState::EquippingWeapon:    return "EquippingWeapon";
		case DebugBotState::Shooting:           return "Shooting";
		case DebugBotState::DroppingWeapon:     return "DroppingWeapon";
		case DebugBotState::WaitingToDisappear: return "WaitingToDisappear";
		case DebugBotState::Finished:           return "Finished";
		case DebugBotState::None:
		default:                                return "None";
		}
	}

	// Estado global de la secuencia (unico debugbot activo a la vez).
	struct DebugBotContext
	{
		DebugBotState Step = DebugBotState::None;
		double NextActionTime = 0.0;      // timestamp (GetTimeSeconds) del proximo cambio
		ABuildingSMActor* RampActor = nullptr; // la rampa construida (referencia real)
		int ShotsFired = 0;
	};

	static DebugBotContext gDebugBot;

	static float DebugBotTime()
	{
		return UGameplayStatics::GetTimeSeconds(GetWorld());
	}

	// Primer jugador real valido de la partida (para colocar el bot junto a el).
	// Se excluyen los bots (nuevos custom bots y AFortAthenaAIBotController).
	static AFortPlayerControllerAthena* FindFirstValidPlayer()
	{
		static auto AIControllerClass = FindObject<UClass>(L"/Script/FortniteGame.AFortAthenaAIBotController");

		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (GameMode)
		{
			auto& Alive = GameMode->GetAlivePlayers();

			for (int i = 0; i < Alive.Num(); ++i)
			{
				auto* Candidate = Alive.At(i);

				if (!Candidate || Candidate->IsActorBeingDestroyed() || !Candidate->GetPawn())
					continue;

				if (AIControllerClass && Candidate->IsA(AIControllerClass))
					continue; // bots del sistema antiguo

				bool bIsCustomBot = false;

				for (size_t k = 0; k < CustomBotSpawner::AllCustomBots.size(); ++k)
				{
					if (CustomBotSpawner::AllCustomBots[k].Controller == Candidate)
					{
						bIsCustomBot = true;
						break;
					}
				}

				if (bIsCustomBot)
					continue; // bot custom del sistema nuevo

				return Candidate; // primer jugador real valido
			}
		}

		// Fallback: host / local player.
		auto* Local = Cast<AFortPlayerControllerAthena>(GetLocalPlayerController());

		if (Local && !Local->IsActorBeingDestroyed() && Local->GetPawn())
			return Local;

		return nullptr;
	}

	// Busca una definicion de arma valida para la prueba (best-effort).
	// Primero rutas conocidas (varian por version); si ninguna carga, escanea
	// GObjects y elige la primera "gun" de Athena (excluyendo herramientas).
	static UFortItemDefinition* FindTestWeaponDefinition()
	{
		static const wchar_t* CandidatePaths[] = {
			L"/Game/Athena/Items/Weapons/Guns/Assault/Assault_Longbarrel.SAID_Assault_Longbarrel_Auto",
			L"/Game/Athena/Items/Weapons/Guns/Assault/Assault_Short.SAID_Assault_Short",
			L"/Game/Athena/Items/Weapons/Guns/Rifle/Rifle_SemiAuto.SAID_Rifle_SemiAuto",
			L"/Game/Athena/Items/Weapons/Guns/Pistol/Pistol_SemiAuto.SAID_Pistol_SemiAuto",
		};

		for (auto* Path : CandidatePaths)
		{
			if (auto* Def = FindObject<UFortItemDefinition>(Path))
				return Def;
		}

		auto WeaponClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponItemDefinition");
		auto All = GetAllObjectsOfClass<UFortItemDefinition>(WeaponClass);

		for (auto* Def : All)
		{
			if (!Def)
				continue;

			std::string Path = Def->GetPathName();

			if (Path.find("/Athena/Items/Weapons/") == std::string::npos)
				continue;
			if (Path.find("Pickaxe") != std::string::npos)
				continue;
			if (Path.find("BuildingTools") != std::string::npos)
				continue;
			if (Path.find("Aimlaser") != std::string::npos)
				continue;
			if (Path.find("Melee") != std::string::npos)
				continue;

			return Def;
		}

		return nullptr;
	}

	// Da al bot arma, municion, materiales y escudo. Devuelve false si no hay arma.
	static bool DebugBotGrantLoadout(CustomBot& Bot)
	{
		CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, 1000);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, 1000);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, 1000);
		LOG_INFO(LogBots, "[DebugBot] Materials granted");

		UFortItemDefinition* WeaponDef = FindTestWeaponDefinition();

		if (!WeaponDef)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: no weapon item definition found for test loadout!");
			return false;
		}

		LOG_INFO(LogBots, "[DebugBot] Weapon equipped: {}", WeaponDef->GetPathName());

		// Arma con 999 de municion cargada.
		CustomBotInventory::GiveItem(Bot, WeaponDef, 1, 999);

		if (!CustomBotInventory::EquipFirstWeapon(Bot))
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: could not equip granted weapon!");
			return false;
		}

		CustomBotCombat::Reload(Bot, 999);
		LOG_INFO(LogBots, "[DebugBot] Weapon ammo: {}", CustomBotCombat::GetCurrentAmmo(Bot));

		Bot.Pawn->SetMaxShield(100);
		Bot.Pawn->SetShield(100);
		LOG_INFO(LogBots, "[DebugBot] Shield set to 100");

		return true;
	}

	// Elimina el bot de la partida limpiando estado, listas y actores.
	static void DebugBotRemove(CustomBot& Bot)
	{
		LOG_INFO(LogBots, "[DebugBot] Removing bot");

		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());

		// Quitar del listado de jugadores vivos (mirror del spawn).
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

		// Decrementar el contador de jugadores restantes (mirror del spawn).
		if (GameState)
		{
			GameState->GetPlayersLeft() = FMath::Clamp(GameState->GetPlayersLeft() - 1, 0, 9999);
			GameState->OnRep_PlayersLeft();
		}

		// Estado interno.
		Bot.DebugTick = nullptr;
		Bot.MoveRequest = CBT::FMoveRequest{};
		Bot.bMoveRequestActive = false;
		Bot.MoveState = CBT::EMovementState::Idle;
		gDebugBot = DebugBotContext{};

		// Actores: inventario, pawn y controller.
		if (Bot.WorldInventory)
			Bot.WorldInventory->K2_DestroyActor();

		Bot.Destroy(); // pawn + controller (K2_DestroyActor y null de punteros)

		LOG_INFO(LogBots, "[DebugBot] Finished");
	}

	// Aborta la secuencia: log de error y desaparicion limpia poco despues.
	static void DebugBotError(CustomBot& Bot, const char* Reason)
	{
		LOG_ERROR(LogBots, "[DebugBot] ERROR during {}: {}", DebugBotStateName(gDebugBot.Step), Reason);
		gDebugBot.NextActionTime = DebugBotTime() + 1.0;
		gDebugBot.Step = DebugBotState::WaitingToDisappear;
	}

	// Tick de la secuencia (registrado en CustomBot::DebugTick; corre cada frame
	// del servidor dentro de CustomBotSpawner::TickAll).
	static void TickDebugBot(CustomBot& Bot)
	{
		if (gDebugBot.Step == DebugBotState::None || gDebugBot.Step == DebugBotState::Finished)
			return;

		if (!Bot.IsReady() || !Bot.IsValidActor() || Bot.GetLifeState() != CBT::ELifeState::Alive)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: bot lost/died during sequence, removing");
			gDebugBot.NextActionTime = DebugBotTime() + 0.5;
			gDebugBot.Step = DebugBotState::WaitingToDisappear;
			return;
		}

		float T = DebugBotTime();

		switch (gDebugBot.Step)
		{
		case DebugBotState::Spawned:
		{
			// Configurados en el comando; arranca el avance real.
			gDebugBot.Step = DebugBotState::MovingForward;
			LOG_INFO(LogBots, "[DebugBot] Moving forward");

			FVector Fwd = Bot.Pawn->GetActorForwardVector();
			FVector Start = Bot.Pawn->GetActorLocation();
			CustomBotMovement::MoveTo(Bot, FVector{ Start.X + Fwd.X * 250.0f, Start.Y + Fwd.Y * 250.0f, Start.Z }, 120.0f);
			gDebugBot.NextActionTime = T + 8.0; // watchdog si no llega
			break;
		}

		case DebugBotState::MovingForward:
		{
			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::Jumping;
				LOG_INFO(LogBots, "[DebugBot] Jumping");
				CustomBotMovement::Jump(Bot);
				gDebugBot.NextActionTime = T + 0.45;
			}
			break;
		}

		case DebugBotState::Jumping:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::BuildingRamp;
				LOG_INFO(LogBots, "[DebugBot] Building ramp");

				if (CustomBotResources::GetTotalResourceCount(Bot) < 10)
				{
					DebugBotError(Bot, "not enough materials to build ramp");
					break;
				}

				CustomBotBuilding::SelectPiece(Bot, CustomBotBuilding::EPieceType::Ramp);

				FVector Fwd = Bot.Pawn->GetActorForwardVector();
				FVector Start = Bot.Pawn->GetActorLocation();
				FVector RampLoc{ Start.X + Fwd.X * 400.0f, Start.Y + Fwd.Y * 400.0f, Start.Z - 16.0f };

				gDebugBot.RampActor = CustomBotBuilding::BuildRamp(Bot, RampLoc, Bot.Pawn->GetActorRotation());

				if (!gDebugBot.RampActor)
				{
					DebugBotError(Bot, "ramp could not be built (invalid location)");
					break;
				}

				LOG_INFO(LogBots, "[DebugBot] Ramp built (health {:.0f})", CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor));

				gDebugBot.Step = DebugBotState::ClimbingRamp;
				LOG_INFO(LogBots, "[DebugBot] Climbing ramp");

				FVector Climb{ Start.X + Fwd.X * 600.0f, Start.Y + Fwd.Y * 600.0f, Start.Z + 100.0f };
				CustomBotMovement::MoveTo(Bot, Climb, 150.0f);
				gDebugBot.NextActionTime = T + 9.0; // watchdog
			}
			break;
		}

		case DebugBotState::ClimbingRamp:
		{
			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (T >= gDebugBot.NextActionTime)
					LOG_INFO(LogBots, "[DebugBot] ramp climb watchdog (continuing sequence)");

				gDebugBot.Step = DebugBotState::Turning;
				LOG_INFO(LogBots, "[DebugBot] Turning around");
				CustomBotMovement::SetYaw(Bot, Bot.Pawn->GetActorRotation().Yaw + 180.0f);
				gDebugBot.NextActionTime = T + 0.6;
			}
			break;
		}

		case DebugBotState::Turning:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::EquippingPickaxe;
				LOG_INFO(LogBots, "[DebugBot] Equipping pickaxe");

				if (!CustomBotInventory::EquipPickaxe(Bot))
				{
					DebugBotError(Bot, "pickaxe could not be equipped");
					break;
				}

				// Acercarse a la rampa para golpearla.
				if (gDebugBot.RampActor)
				{
					FVector RampPos = gDebugBot.RampActor->GetActorLocation();
					CustomBotMovement::MoveTo(Bot, FVector{ RampPos.X, RampPos.Y, RampPos.Z + 20.0f }, 140.0f);
				}

				gDebugBot.NextActionTime = T + 3.0; // watchdog de acercamiento
			}
			break;
		}

		case DebugBotState::EquippingPickaxe:
		{
			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::DestroyingRamp;
				LOG_INFO(LogBots, "[DebugBot] Destroying ramp with pickaxe");

				if (!gDebugBot.RampActor || CustomBotDestruction::IsStructureDestroyed(gDebugBot.RampActor))
				{
					DebugBotError(Bot, "ramp already gone or missing to destroy");
					break;
				}

				float Before = CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor);
				bool bDestroyed = CustomBotDestruction::DestroyTarget(gDebugBot.RampActor);
				float After = CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor);

				if (!bDestroyed || After > 0.0f)
				{
					LOG_INFO(LogBots, "[DebugBot] ramp damage applied: {:.0f} -> {:.0f} (melee pickaxe real pendiente en Parte 2)", Before, After);
				}
				else
				{
					LOG_INFO(LogBots, "[DebugBot] Ramp destroyed: {:.0f} -> {:.0f}", Before, After);
				}

				gDebugBot.NextActionTime = T + 0.1;
			}
			break;
		}

		case DebugBotState::DestroyingRamp:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::EquippingWeapon;
				LOG_INFO(LogBots, "[DebugBot] Equipping weapon");

				if (!CustomBotInventory::EquipFirstWeapon(Bot))
				{
					DebugBotError(Bot, "no weapon to re-equip after destroying ramp");
					break;
				}

				CustomBotCombat::Reload(Bot, 999);
				LOG_INFO(LogBots, "[DebugBot] Weapon re-equipped (ammo {})", CustomBotCombat::GetCurrentAmmo(Bot));
				gDebugBot.NextActionTime = T + 0.6;
			}
			break;
		}

		case DebugBotState::EquippingWeapon:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::Shooting;
				gDebugBot.ShotsFired = 0;

				FVector Fwd = Bot.Pawn->GetActorForwardVector();
				FVector Start = Bot.Pawn->GetActorLocation();
				CustomBotCombat::AimAt(Bot, FVector{ Start.X + Fwd.X * 1500.0f, Start.Y + Fwd.Y * 1500.0f, Start.Z });

				LOG_INFO(LogBots, "[DebugBot] Shooting");
				gDebugBot.NextActionTime = T + 0.3; // primer disparo
			}
			break;
		}

		case DebugBotState::Shooting:
		{
			if (T >= gDebugBot.NextActionTime && gDebugBot.ShotsFired <= 5)
			{
				if (gDebugBot.ShotsFired < 5)
				{
					bool bFired = CustomBotCombat::FireWeapon(Bot);
					++gDebugBot.ShotsFired;
					LOG_INFO(LogBots, "[DebugBot] Shot {}/5 fired={} ammo={}", gDebugBot.ShotsFired, bFired, CustomBotCombat::GetCurrentAmmo(Bot));
					gDebugBot.NextActionTime = T + 0.35;
				}
				else
				{
					gDebugBot.Step = DebugBotState::DroppingWeapon;
				}
			}
			break;
		}

		case DebugBotState::DroppingWeapon:
		{
			LOG_INFO(LogBots, "[DebugBot] Dropping weapon");

			UFortItem* Weapon = CustomBotInventory::FindItemByType(Bot, CustomBotPerception::EItemType::Weapon);

			if (!Weapon)
			{
				DebugBotError(Bot, "no weapon to drop");
				break;
			}

			bool bDropped = CustomBotInventory::DropItem(Bot, Weapon, Weapon->GetItemEntry()->GetCount());
			int Remaining = CustomBotInventory::GetItemCount(Bot, Weapon->GetItemEntry()->GetItemDefinition());
			LOG_INFO(LogBots, "[DebugBot] Weapon dropped={}; remaining in inventory={}", bDropped, Remaining);

			gDebugBot.Step = DebugBotState::WaitingToDisappear;
			gDebugBot.NextActionTime = DebugBotTime() + 10.0;
			LOG_INFO(LogBots, "[DebugBot] Waiting 10 seconds");
			break;
		}

		case DebugBotState::WaitingToDisappear:
		{
			if (T >= gDebugBot.NextActionTime)
				DebugBotRemove(Bot);
			break;
		}

		case DebugBotState::None:
		case DebugBotState::Finished:
		default:
			break;
		}
	}
}

// Inicia la secuencia de prueba "debugbot" (un solo debugbot activo a la vez).
void CustomBotDebug::StartDebugBot(AFortPlayerControllerAthena* ContextPlayer)
{
	if (gDebugBot.Step != DebugBotState::None && gDebugBot.Step != DebugBotState::Finished)
	{
		SendBotMessage(ContextPlayer, L"[DebugBot] A debug bot is already running!");
		LOG_INFO(LogBots, "[DebugBot] Start ignored: sequence already active");
		return;
	}

	// Buscar el primer jugador valido ANTES de crear el bot.
	AFortPlayerControllerAthena* TargetPlayer = FindFirstValidPlayer();

	if (!TargetPlayer || !TargetPlayer->GetPawn())
	{
		SendBotMessage(ContextPlayer, L"[DebugBot] No valid player found, bot not created!");
		LOG_INFO(LogBots, "[DebugBot] ERROR: no valid player in match, skipping spawn");
		return;
	}

	LOG_INFO(LogBots, "[DebugBot] Found player");

	APawn* TargetPawn = TargetPlayer->GetPawn();
	FVector PlayerLoc = TargetPawn->GetActorLocation();
	FRotator PlayerRot = TargetPawn->GetActorRotation();
	FVector Fwd = TargetPawn->GetActorForwardVector();

	// Aparicion "junto al jugador" (unico teletransporte del debugbot).
	FVector BotSpawn = PlayerLoc + Fwd * 250.0f;
	BotSpawn.Z += 50.0f; // no quedar dentro del jugador

	FTransform SpawnTransform{};
	SpawnTransform.Translation = BotSpawn;
	SpawnTransform.Rotation = PlayerRot.Quaternion();
	SpawnTransform.Scale3D = { 1, 1, 1 };

	CustomBot* DebugBot = CustomBotSpawner::SpawnCustomBot(SpawnTransform);

	if (!DebugBot)
	{
		SendBotMessage(ContextPlayer, L"[DebugBot] Failed to create custom bot!");
		return;
	}

	DebugBot->Pawn->TeleportTo(BotSpawn, PlayerRot);
	LOG_INFO(LogBots, "[DebugBot] Teleported to player ({}, {}, {})", BotSpawn.X, BotSpawn.Y, BotSpawn.Z);

	// Arma, municion, materiales y escudo.
	if (!DebugBotGrantLoadout(*DebugBot))
	{
		DebugBotRemove(*DebugBot);
		SendBotMessage(ContextPlayer, L"[DebugBot] Loadout failed, bot removed!");
		return;
	}

	LOG_INFO(LogBots, "[DebugBot] Spawned");
	SendBotMessage(ContextPlayer, L"[DebugBot] Sequence started!");

	// Registrar la secuencia en el bot (Tick la ejecutara cada frame).
	gDebugBot = DebugBotContext{};
	gDebugBot.Step = DebugBotState::Spawned;
	DebugBot->DebugTick = &TickDebugBot;
}

// Devuelve true si Arguments[0] es un comando de CustomBot y se ejecuto.
bool CustomBotDebug::HandleCommand(AFortPlayerControllerAthena* PlayerController, const std::vector<std::string>& Arguments, size_t NumArgs)
{
	if (!PlayerController || Arguments.empty())
		return false;

	const std::string& Command = Arguments[0];

	if (Command == "spawncustombot")
	{
		auto Pawn = PlayerController->GetPawn();

		if (!Pawn)
		{
			SendBotMessage(PlayerController, L"No pawn to spawn at!");
			return true;
		}

		FVector Location = Pawn->GetActorLocation();
		Location.Z += 100.0f;

		FTransform SpawnTransform{};
		SpawnTransform.Translation = Location;
		SpawnTransform.Rotation = Pawn->GetActorRotation().Quaternion();
		SpawnTransform.Scale3D = { 1, 1, 1 };

		auto NewBot = CustomBotSpawner::SpawnCustomBot(SpawnTransform);

		SendBotMessage(PlayerController, NewBot ? L"Custom bot spawned!" : L"Failed to spawn custom bot!");
		return true;
	}

	// El resto de comandos requieren un bot existente.
	CustomBot* ActiveBot = GetFirstValidBot();

	if (!ActiveBot)
	{
		SendBotMessage(PlayerController, L"No custom bots alive!");
		return true;
	}

	CustomBot& Bot = *ActiveBot;

	if (Command == "cbmove")
	{
		if (NumArgs < 3)
		{
			SendBotMessage(PlayerController, L"Usage: cbmove <x> <y> <z>");
			return true;
		}

		float X, Y, Z;

		if (!TryParseFloat(Arguments[1], X) || !TryParseFloat(Arguments[2], Y) || !TryParseFloat(Arguments[3], Z))
		{
			SendBotMessage(PlayerController, L"Invalid coordinates!");
			return true;
		}

		CustomBotMovement::MoveTo(Bot, FVector{ X, Y, Z });
		SendBotMessage(PlayerController, L"Moved!");
		return true;
	}

	if (Command == "cblookat")
	{
		if (NumArgs < 3)
		{
			SendBotMessage(PlayerController, L"Usage: cblookat <x> <y> <z>");
			return true;
		}

		float X, Y, Z;

		if (!TryParseFloat(Arguments[1], X) || !TryParseFloat(Arguments[2], Y) || !TryParseFloat(Arguments[3], Z))
		{
			SendBotMessage(PlayerController, L"Invalid coordinates!");
			return true;
		}

		CustomBotMovement::LookAt(Bot, FVector{ X, Y, Z });
		SendBotMessage(PlayerController, L"Looking!");
		return true;
	}

	if (Command == "cbscan")
	{
		float Radius = 1200.0f;

		if (NumArgs >= 1)
			TryParseFloat(Arguments[1], Radius);

		auto Pickup = CustomBotPerception::FindNearestPickup(Bot, Radius);
		auto Container = CustomBotPerception::FindNearestUnopenedContainer(Bot, Radius);
		auto Player = CustomBotPerception::FindNearestPlayer(Bot, Radius);

		SendBotMessage(PlayerController, std::format(
			L"cbscan: radius={:.0f} | pickup={:.0f}m | chest={:.0f}m | player={:.0f}m",
			Radius,
			Pickup ? CustomBotPerception::DistanceToActor(Bot, Pickup) : -1.0f,
			Container ? CustomBotPerception::DistanceToActor(Bot, Container) : -1.0f,
			Player ? CustomBotPerception::DistanceToActor(Bot, Player) : -1.0f).c_str());

		return true;
	}

	if (Command == "cbpickup")
	{
		bool bPicked = CustomBotInteraction::PickupNearest(Bot, 500.0f);
		SendBotMessage(PlayerController, bPicked ? L"Pickup collected!" : L"No pickup near!");
		return true;
	}

	if (Command == "cbequip")
	{
		if (NumArgs < 1)
		{
			SendBotMessage(PlayerController, L"Usage: cbequip <WIDPath>");
			return true;
		}

		auto WID = Cast<UFortWorldItemDefinition>(FindObject(Arguments[1], nullptr));

		if (!WID)
		{
			SendBotMessage(PlayerController, L"Invalid WID!");
			return true;
		}

		CustomBotInventory::GiveItem(Bot, WID);

		auto Item = CustomBotInventory::FindItemByDefinition(Bot, WID);

		if (!Item)
		{
			SendBotMessage(PlayerController, L"Item not found in inventory!");
			return true;
		}

		CustomBotInventory::EquipItem(Bot, Item);
		SendBotMessage(PlayerController, L"Equipped!");
		return true;
	}

	if (Command == "cbshoot")
	{
		auto Target = CustomBotPerception::FindNearestPlayer(Bot, 5000.0f);

		if (Target)
			CustomBotCombat::AimAt(Bot, Target->GetActorLocation());

		bool bFired = CustomBotCombat::FireWeapon(Bot);
		SendBotMessage(PlayerController, bFired ? L"Fired!" : L"No ability to fire!");
		return true;
	}

	if (Command == "cbbuild")
	{
		if (NumArgs < 1)
		{
			SendBotMessage(PlayerController, L"Usage: cbbuild <wall|floor|ramp|roof> [ClassPath]");
			return true;
		}

		CustomBotBuilding::EPieceType PieceType;

		if (Arguments[1] == "wall")      PieceType = CustomBotBuilding::EPieceType::Wall;
		else if (Arguments[1] == "floor") PieceType = CustomBotBuilding::EPieceType::Floor;
		else if (Arguments[1] == "ramp")  PieceType = CustomBotBuilding::EPieceType::Ramp;
		else if (Arguments[1] == "roof")  PieceType = CustomBotBuilding::EPieceType::Roof;
		else
		{
			SendBotMessage(PlayerController, L"Unknown piece type!");
			return true;
		}

		UClass* BuildingClass = nullptr;

		if (NumArgs >= 2)
			BuildingClass = LoadObject<UClass>(Arguments[2]);

		if (!BuildingClass)
			BuildingClass = CustomBotBuilding::GetPieceClass(PieceType);

		if (!BuildingClass)
		{
			SendBotMessage(PlayerController, L"No building class available! Provide a ClassPath on this version.");
			return true;
		}

		// Seleccionar la pieza (build mode, igual que un jugador).
		CustomBotBuilding::SelectPiece(Bot, PieceType);

		// Posicionar la estructura justo al frente del bot.
		FVector Forward = Bot.Pawn->GetActorForwardVector();
		FVector BotLocation = Bot.Pawn->GetActorLocation();

		FVector BuildLocation{ BotLocation.X + Forward.X * 300.0f, BotLocation.Y + Forward.Y * 300.0f, BotLocation.Z - 20.0f };

		ABuildingSMActor* NewBuilding = CustomBotBuilding::BuildPiece(Bot, BuildingClass, BuildLocation, Bot.Pawn->GetActorRotation());

		SendBotMessage(PlayerController, NewBuilding ? L"Built!" : L"Build failed!");
		return true;
	}

	if (Command == "cbdestroy")
	{
		static auto BuildingSMActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingSMActor");

		auto Target = CustomBotPerception::FindNearestActorOfClass(Bot, BuildingSMActorClass, 600.0f);
		auto Building = Cast<ABuildingSMActor>(Target);

		if (!Building)
		{
			SendBotMessage(PlayerController, L"No structure near!");
			return true;
		}

		bool bDestroyed = CustomBotDestruction::DestroyTarget(Building);
		SendBotMessage(PlayerController, bDestroyed ? L"Destroyed!" : L"Failed to destroy!");
		return true;
	}

	if (Command == "cbgetmaterials")
	{
		int Count = 500;

		if (NumArgs >= 1)
			TryParseInt(Arguments[1], Count);

		CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, Count);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, Count);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, Count);

		SendBotMessage(PlayerController, std::format(L"Granted {} of each material!", Count).c_str());
		return true;
	}

	if (Command == "cbpath")
	{
		if (NumArgs < 3)
		{
			SendBotMessage(PlayerController, L"Usage: cbpath <x> <y> <z>");
			return true;
		}

		float X, Y, Z;

		if (!TryParseFloat(Arguments[1], X) || !TryParseFloat(Arguments[2], Y) || !TryParseFloat(Arguments[3], Z))
		{
			SendBotMessage(PlayerController, L"Invalid coordinates!");
			return true;
		}

		FVector Target{ X, Y, Z };

		bool bClear = CustomBotPerception::HasLineOfSight(Bot, Target);
		float Height = CustomBotPerception::GetHeightDifference(Bot, Target);

		CBT::EObstacleType ObstacleType;
		AActor* Obstacle = CustomBotPerception::FindNearestObstacle(Bot, 600.0f, ObstacleType);

		const wchar_t* TypeStr =
			ObstacleType == CBT::EObstacleType::OwnStructure   ? L"OWN_BUILD"
			: ObstacleType == CBT::EObstacleType::EnemyStructure ? L"ENEMY_BUILD"
			: ObstacleType == CBT::EObstacleType::WorldObject   ? L"WORLD_OBJECT"
			: ObstacleType == CBT::EObstacleType::Structure     ? L"STRUCTURE"
			: ObstacleType == CBT::EObstacleType::Actor         ? L"ACTOR"
			:                                                        L"NONE";

		SendBotMessage(PlayerController, std::format(
			L"cbpath: los={} height={:+.0f} obstacle={} dist={:.0f}m",
			bClear ? L"CLEAR" : L"BLOCKED",
			Height,
			TypeStr,
			Obstacle ? CustomBotPerception::DistanceToActor(Bot, Obstacle) : -1.0f).c_str());

		if (Obstacle)
			SendBotMessage(PlayerController, ToWide(Obstacle->ClassPrivate->GetName()).c_str());

		return true;
	}

	if (Command == "cbclimb")
	{
		int Ramps = 1;

		if (NumArgs >= 1)
			TryParseInt(Arguments[1], Ramps);

		Ramps = FMath::Clamp(Ramps, 1, 20);

		UClass* RampClass = nullptr;

		if (NumArgs >= 2)
			RampClass = LoadObject<UClass>(Arguments[2]);

		if (!RampClass)
			RampClass = CustomBotBuilding::GetPieceClass(CustomBotBuilding::EPieceType::Ramp);

		if (!RampClass)
		{
			SendBotMessage(PlayerController, L"No ramp class available! Provide a ClassPath on this version.");
			return true;
		}

		// Seleccionar la pieza (build mode) y construir N rampas hacia arriba.
		CustomBotBuilding::SelectPiece(Bot, CustomBotBuilding::EPieceType::Ramp);

		const float StepHoriz = 300.0f;
		const float StepZ = 96.0f;

		FVector BotLocation = Bot.Pawn->GetActorLocation();
		FVector Forward = Bot.Pawn->GetActorForwardVector();

		int Built = 0;
		FVector LastPos = BotLocation;

		for (int i = 0; i < Ramps; ++i)
		{
			FVector Pos{ BotLocation.X + Forward.X * StepHoriz * float(i + 1), BotLocation.Y + Forward.Y * StepHoriz * float(i + 1), BotLocation.Z - 20.0f + StepZ * float(i) };

			if (CustomBotBuilding::BuildPiece(Bot, RampClass, Pos, Bot.Pawn->GetActorRotation()))
				++Built;

			LastPos = Pos;
		}

		if (Built > 0)
			CustomBotMovement::MoveTo(Bot, FVector{ LastPos.X, LastPos.Y, LastPos.Z + 20.0f }, 120.0f);

		SendBotMessage(PlayerController, std::format(L"Climb: built {} / {} ramps, moving up!", Built, Ramps).c_str());
		return true;
	}

	if (Command == "cbuse")
	{
		bool bUsed = CustomBotInteraction::UseConsumable(Bot);
		SendBotMessage(PlayerController, bUsed ? L"Consumable used!" : L"No consumable to use!");
		return true;
	}

	return false;
}