#include "CustomBotDebug.h"

#include "CustomBot.h"
#include "globals.h"

#include "CustomBotSpawner.h"
#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotResources.h"
#include "CustomBotCombat.h"
#include "CustomBotBuilding.h"
#include "CustomBotDestruction.h"
#include "CustomBotInteraction.h"
#include "CustomBotBreak.h"

#include "FortItem.h"
#include "BuildingSMActor.h"
#include "GameplayStatics.h"

#include <format>
#include <string>
#include <cmath>

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
		BuildingRamp,
		WalkToRampStart,
		WalkToRampMiddle,
		Jumping,
		BuildingFloor,
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
		case DebugBotState::BuildingRamp:       return "BuildingRamp";
		case DebugBotState::WalkToRampStart:    return "WalkToRampStart";
		case DebugBotState::WalkToRampMiddle:   return "WalkToRampMiddle";
		case DebugBotState::Jumping:            return "Jumping";
		case DebugBotState::BuildingFloor:      return "BuildingFloor";
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
		ABuildingSMActor* FloorActor = nullptr; // el suelo construido debajo del bot
		int ShotsFired = 0;
		// --- Experimento de fisica: hacer el CM del bot identico al de un jugador
		// real (tick habilitado + Walking + velocidades maximas) a cada frame. ----
		bool bMoveExperimentDone = false;
		int ModeForceCount = 0;           // veces que el watchdog relego el modo a Falling
		double LastModeReLog = -1.0;      // throtling del log de revert
		double PauseUntil = -1.0;         // pausa de pacing (3s) entre fases de la demo
		bool bClimbStarted = false;       // si ya se arranco a caminar hacia la rampa

		bool bRampFacingLogged = false;   // guarda la orientacion real de la rampa
		int RampAttempts = 0;             // intentos de construir la rampa (anti-spam)
		int MaxRampAttempts = 5;          // tope para evitar loop infinito de BuildingRamp
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
		LOG_INFO(LogBots, "[DebugBot] Searching for weapon definition...");

		static const wchar_t* CandidatePaths[] = {
			L"/Game/Athena/Items/Weapons/Guns/Assault/Assault_Longbarrel.SAID_Assault_Longbarrel_Auto",
			L"/Game/Athena/Items/Weapons/Guns/Assault/Assault_Short.SAID_Assault_Short",
			L"/Game/Athena/Items/Weapons/Guns/Rifle/Rifle_SemiAuto.SAID_Rifle_SemiAuto",
			L"/Game/Athena/Items/Weapons/Guns/Pistol/Pistol_SemiAuto.SAID_Pistol_SemiAuto",
		};

		for (auto* Path : CandidatePaths)
		{
			if (auto* Def = FindObject<UFortItemDefinition>(Path))
			{
				LOG_INFO(LogBots, "[DebugBot] Weapon found by path: {}", Def->GetPathName());
				return Def;
			}
		}

		LOG_INFO(LogBots, "[DebugBot] Hardcoded paths failed, scanning GObjects...");

		auto WeaponClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponItemDefinition");
		auto All = GetAllObjectsOfClass<UFortItemDefinition>(WeaponClass);

		LOG_INFO(LogBots, "[DebugBot] Found {} WeaponItemDefinitions total", All.size());

		// Categorias de armas reales (preferidas) y nombres a excluir (herramientas/depuradores).
		static const char* GunDirs[] = { "/Guns/", "/Rifle/", "/Pistol/", "/SMG/", "/Shotgun/", "/Launchers/", "/Sniper/" };
		static const char* Excludes[] = {
			"Pickaxe", "BuildingTools", "EditTool", "Aimlaser", "Melee", "StatDebugger",
			"WID_StatDebugger", "Harvest_Tool", "ConsumesBuildingResource", "Trap", "Grenade",
		};

		auto IsExcluded = [](const std::string& P) {
			for (auto* E : Excludes)
				if (P.find(E) != std::string::npos)
					return true;
			return false;
		};

		auto IsGun = [](const std::string& P) {
			for (auto* G : GunDirs)
				if (P.find(G) != std::string::npos)
					return true;
			return false;
		};

		// Primera pasada: preferir paths en carpetas de armas reales (/Guns/, /Rifle/,...).
		for (auto* Def : All)
		{
			if (!Def)
				continue;

			std::string Path = Def->GetPathName();

			if (Path.find("/Athena/Items/Weapons/") == std::string::npos)
				continue;
			if (IsExcluded(Path))
				continue;
			if (!IsGun(Path))
				continue;

			LOG_INFO(LogBots, "[DebugBot] Gun weapon found by scan: {}", Path);
			return Def;
		}

		// Segunda pasada: cualquier arma de Athena que no sea herramienta/depurador.
		for (auto* Def : All)
		{
			if (!Def)
				continue;

			std::string Path = Def->GetPathName();

			if (Path.find("/Athena/Items/Weapons/") == std::string::npos)
				continue;
			if (IsExcluded(Path))
				continue;

			LOG_INFO(LogBots, "[DebugBot] Fallback weapon found by scan: {}", Path);
			return Def;
		}

		LOG_WARN(LogBots, "[DebugBot] No weapon definition found!");
		return nullptr;
	}

	// Da al bot arma, municion, materiales y escudo. Devuelve false si no hay arma.
	static bool DebugBotGrantLoadout(CustomBot& Bot)
	{
		LOG_INFO(LogBots, "[DebugBot] === Granting loadout ===");

		// 1) Equipar pico PRIMERO para salir del build mode (EditTool de starting items).
		LOG_INFO(LogBots, "[DebugBot] Step 1: Equipping pickaxe to exit build mode...");
		bool bPickaxeEquipped = CustomBotInventory::EquipPickaxe(Bot);
		LOG_INFO(LogBots, "[DebugBot] Pickaxe equipped: {}", bPickaxeEquipped ? "YES" : "NO");

		// 2) Materiales.
		LOG_INFO(LogBots, "[DebugBot] Step 2: Granting materials...");
		CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, 1000);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, 1000);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, 1000);
		LOG_INFO(LogBots, "[DebugBot] Materials: Wood={} Stone={} Metal={}",
			CustomBotResources::GetResourceCount(Bot, EFortResourceType::Wood),
			CustomBotResources::GetResourceCount(Bot, EFortResourceType::Stone),
			CustomBotResources::GetResourceCount(Bot, EFortResourceType::Metal));

		// 3) Arma real.
		LOG_INFO(LogBots, "[DebugBot] Step 3: Finding weapon...");
		UFortItemDefinition* WeaponDef = FindTestWeaponDefinition();

		if (!WeaponDef)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: no weapon item definition found for test loadout!");
			return false;
		}

		LOG_INFO(LogBots, "[DebugBot] Giving weapon: {}", WeaponDef->GetPathName());
		CustomBotInventory::GiveItem(Bot, WeaponDef, 1, 999);

		// Equipar EXACTAMENTE la arma otorgada, NO "la primera arma": el pickaxe
		// (FortWeaponItemDefinition) se clasifica como Weapon y al ser el primer item
		// del inventario, EquipFirstWeapon equipaba el pickaxe (current=pickaxe,
		// "OK" contra el pickaxe) y la sniper jamas se equipaba.
		UFortItem* GrantedWeapon = CustomBotInventory::FindItemByDefinition(Bot, WeaponDef);

		if (!GrantedWeapon)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: granted weapon NOT in inventory after GiveItem!");
			return false;
		}

		LOG_INFO(LogBots, "[DebugBot] Equipping weapon...");
		if (!CustomBotInventory::EquipItem(Bot, GrantedWeapon))
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: could not equip granted weapon!");
			return false;
		}

		// Verificar que el arma equipada es la correcta (no el EditTool).
		auto* CurrentWeapon = CustomBotInventory::GetCurrentWeapon(Bot);
		if (CurrentWeapon)
		{
			auto* WeaponData = CurrentWeapon->GetWeaponData();
			LOG_INFO(LogBots, "[DebugBot] Current weapon after equip: {}",
				WeaponData ? WeaponData->GetPathName() : "UNKNOWN");
		}
		else
		{
			LOG_WARN(LogBots, "[DebugBot] No weapon equipped after EquipFirstWeapon!");
		}

		// 4) Municion.
		LOG_INFO(LogBots, "[DebugBot] Step 4: Reloading weapon...");
		CustomBotCombat::Reload(Bot, 999);
		LOG_INFO(LogBots, "[DebugBot] Weapon ammo: {}", CustomBotCombat::GetCurrentAmmo(Bot));

		// 5) Escudo.
		LOG_INFO(LogBots, "[DebugBot] Step 5: Setting shield...");
		Bot.Pawn->SetMaxShield(100);
		Bot.Pawn->SetShield(100);
		LOG_INFO(LogBots, "[DebugBot] Shield set to 100 (current={})", Bot.Pawn->GetShield());

		LOG_INFO(LogBots, "[DebugBot] === Loadout complete ===");
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

	// -----------------------------------------------------------------------
	// Diagnostico: JUGADOR REAL vs BOT CUSTOM (comparativa de estado completo).
	// Esto permite ver, lado a lado, que tiene el pawn del jugador real (que se
	// mueve) y que le falta al pawn del bot (que a veces se queda congelado).
	// -----------------------------------------------------------------------

	// Volcado plano del estado de un pawn (jugador real o bot).
	struct ProbeSnapshot
	{
		FVector Loc{}, Rot{};
		FVector Vel{}, Acc{};
		int Mode = -1;
		float GravScale = -1.f;
		float MaxWalk = -1.f;
		int Crouch = -1;      // bIsCrouching
		int WantsCrouch = -1; // bWantsToCrouch
		int NetDorm = -1;
		int Rep = -1;
		int TearOff = -1;
		int OnlyOwner = -1;
		int AlwaysRel = -1;
		int UsesOwnerRel = -1;
		int NetFreq = -1;
		int MinNetFreq = -1;
		int GravGate = -1;   // bSimGravityDisabled
		int MoveGate = -1;   // bDisableMovementAndTurnInPlace
		int AllowGate = -1;  // bAllowMovement
		int CMStartTick = -1;
		int CMIsTicking = -1;  // IsComponentTickEnabled() AHORA (real), no el bit de arranque
		int CMIsActive = -1;   // IsActive() AHORA
		int RunPhys = -1;      // bRunPhysicsWithNoController
		UObject* CMObj = nullptr;   // el CM real (para ptrs)
		int CharOwner = 0;     // CM->CharacterOwner != 0 ?
		int UpdComp = 0;       // CM->UpdatedComponent != 0 ?
		int AckPawn = -1;      // el controller reconoce este pawn (AcknowledgedPawn == Pawn)?
		bool NetConn = false;
		int Health = -1;
	};

	// Llama a la UFUNCTION /Script/Engine.ActorComponent.IsComponentTickEnabled sobre
	// el componente (movement) y devuelve 1=habilitado, 0=deshabilitado, -1=sin fn.
	// Distingue el caso clave "CM sin tick = fisica muerta pese a escribirle estado".
	static int DebugBotCMIsTickEnabled(UObject* CM)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/Engine.ActorComponent.IsComponentTickEnabled");
		if (!Fn || !CM) return -1;
		struct { char Buf[32]; } Params{};
		CM->ProcessEvent(Fn, &Params);
		return Params.Buf[0] ? 1 : 0;
	}

	// Fuerza (idempotente) el tick del componente: SetComponentTickEnabled(true) +
	// Activate(true) (Activate registra el componente si no lo esta). Devuelve el
	// estado de IsComponentTickEnabled tras el refuerzo.
	static int DebugBotCMForceTick(UObject* CM)
	{
		static auto FnSet = FindObject<UFunction>(L"/Script/Engine.ActorComponent.SetComponentTickEnabled");
		static auto FnActivate = FindObject<UFunction>(L"/Script/Engine.ActorComponent.Activate");

		if (FnSet && CM)
		{
			struct { char Buf[32]; } Params{};
			Params.Buf[0] = 1;
			CM->ProcessEvent(FnSet, &Params);
		}
		if (FnActivate && CM)
		{
			struct { char Buf[32]; } Params{};
			Params.Buf[0] = 1; // Activate(bool bReset)
			CM->ProcessEvent(FnActivate, &Params);
		}
		return DebugBotCMIsTickEnabled(CM);
	}

	// Como DebugBotCMIsTickEnabled pero para /Script/Engine.ActorComponent.IsActive.
	static int DebugBotCMIsActive(UObject* CM)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/Engine.ActorComponent.IsActive");
		if (!Fn || !CM) return -1;
		struct { char Buf[32]; } Params{};
		CM->ProcessEvent(Fn, &Params);
		return Params.Buf[0] ? 1 : 0;
	}

	// "CLAIM-LIVE": un pawn de jugador real se marca "vivo" cuando su controller hace
	// ServerAcknowledgePossession (replica la confirmacion del cliente). Un bot de
	// servidor exclusivo nunca la recibe -> Fortnite lo mantiene congelado (mismas
	// señales: Falling re-aplicado + fisica inmovil). Aqui se fuerza ese handshake
	// exactamente igual que lo haria un cliente real.
	static void DebugBotClaimLive(AFortPlayerControllerAthena* Controller, APawn* Pawn, bool bCallUFunction)
	{
		if (!Controller || !Pawn || !bCallUFunction)
			return;

		static auto AckFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ServerAcknowledgePossession");
		if (AckFn)
		{
			struct { APawn* NewPawn; } Params{ Pawn };
			Controller->ProcessEvent(AckFn, &Params);
			LOG_WARN(LogBots, "[DebugBot] CLAIM-LIVE: ServerAcknowledgePossession invoked on bot controller");
		}

		// Blindaje por si el UFunction no actualizo el campo (siempre que el offset exista).
		auto AckOff = Controller->GetOffset("AcknowledgedPawn", false);
		if (AckOff != -1 && Controller->Get<APawn*>(AckOff) != Pawn)
		{
			Controller->Get<APawn*>(AckOff) = Pawn;
			LOG_WARN(LogBots, "[DebugBot] CLAIM-LIVE: AcknowledgedPawn forced to bot pawn");
		}
	}

	static ProbeSnapshot DebugBotProbePawn(AFortPlayerPawnAthena* Pawn)
	{
		ProbeSnapshot S;

		if (!Pawn)
			return S;

		auto Loc = Pawn->GetActorLocation();
		auto R = Pawn->GetActorRotation();
		S.Loc = Loc;
		S.Rot = { R.Pitch, R.Yaw, R.Roll };

		auto ReadByte = [&](const char* Name) -> int {
			int o = Pawn->GetOffset(std::string(Name), false);
			return o != -1 ? *(uint8_t*)(__int64(Pawn) + o) : -1;
		};

		S.NetDorm = (int)Pawn->GetNetDormancy();
		S.Rep = Pawn->DoesReplicate() ? 1 : 0;
		S.TearOff = Pawn->IsTearOff() ? 1 : 0;
		S.OnlyOwner = Pawn->IsOnlyRelevantToOwner() ? 1 : 0;
		S.AlwaysRel = Pawn->IsAlwaysRelevant() ? 1 : 0;
		S.UsesOwnerRel = Pawn->UsesOwnerRelevancy() ? 1 : 0;
		S.NetFreq = (int)Pawn->GetNetUpdateFrequency();
		S.MinNetFreq = (int)Pawn->GetMinNetUpdateFrequency();

		S.GravGate = ReadByte("bSimGravityDisabled");
		S.MoveGate = ReadByte("bDisableMovementAndTurnInPlace");
		S.AllowGate = ReadByte("bAllowMovement");

		// CharacterMovement del pawn.
		int CMOff = Pawn->GetOffset(std::string("CharacterMovement"), false);

		if (CMOff != -1)
		{
			auto* CM = Pawn->Get(CMOff);

			if (CM)
			{
				int o = CM->GetOffset(std::string("Velocity"), false);
				if (o != -1) S.Vel = CM->Get<FVector>(o);

				o = CM->GetOffset(std::string("Acceleration"), false);
				if (o != -1) S.Acc = CM->Get<FVector>(o);

				o = CM->GetOffset(std::string("MovementMode"), false);
				if (o != -1) S.Mode = *(int*)(__int64(CM) + o);

				o = CM->GetOffset(std::string("GravityScale"), false);
				if (o != -1) S.GravScale = CM->Get<float>(o);

				o = CM->GetOffset(std::string("MaxWalkSpeed"), false);
				if (o != -1) S.MaxWalk = CM->Get<float>(o);

				o = CM->GetOffset(std::string("bIsCrouching"), false);
				if (o != -1) S.Crouch = *(uint8_t*)(__int64(CM) + o);

				o = CM->GetOffset(std::string("bWantsToCrouch"), false);
				if (o != -1) S.WantsCrouch = *(uint8_t*)(__int64(CM) + o);

				int PCT = CM->GetOffset(std::string("PrimaryComponentTick"), false);
				int ST = CM->GetOffset(std::string("bStartWithTickEnabled"), false);
				if (ST == -1) ST = PCT;
				if (ST != -1) S.CMStartTick = *(uint8_t*)(__int64(CM) + ST);

				S.CMIsTicking = DebugBotCMIsTickEnabled(CM);
				S.CMIsActive = DebugBotCMIsActive(CM);
				S.CMObj = CM;

				// Punteros que el CM necesita para simular (si fallan -> tick no-op).
				auto OffChar = CM->GetOffset(std::string("CharacterOwner"), false);
				auto OffUpd = CM->GetOffset(std::string("UpdatedComponent"), false);
				auto OffPhys = CM->GetOffset(std::string("bRunPhysicsWithNoController"), false);
				S.CharOwner = OffChar != -1 ? (CM->Get<UObject*>(OffChar) != 0 ? 1 : 0) : -1;
				S.UpdComp = OffUpd != -1 ? (CM->Get<UObject*>(OffUpd) != 0 ? 1 : 0) : -1;
				S.RunPhys = OffPhys != -1 ? *(uint8_t*)(__int64(CM) + OffPhys) : -1;
			}
		}

		// Reconocimiento de posesion del controller (el juego lo marca al recibir
		// ServerAcknowledgePossession; los pawns "no-live" lo tienen sin setear).
		auto* Ctrl = Pawn->GetController();
		int AckOff = Ctrl ? Ctrl->GetOffset(std::string("AcknowledgedPawn"), false) : -1;
		if (AckOff != -1)
			S.AckPawn = (Ctrl->Get<APawn*>(AckOff) == Pawn) ? 1 : 0;

		S.Health = (int)Pawn->GetHealth();

		return S;
	}

	// Tick de la secuencia (registrado en CustomBot::DebugTick; corre cada frame
	// del servidor dentro de CustomBotSpawner::TickAll).
	static void TickDebugBot(CustomBot& Bot)
	{
		// Diagnostico: confirma que la secuencia se invoca (contador, ~1 vez cada
		// 120 frames, en cualquier estado). Si esto no aparece, el problema es que
		// DebugTick no se esta llamando (TickAll / Bot.Tick), no la maquina de estados.
		static unsigned DebugTickCounter = 0;
		if ((++DebugTickCounter) % 120 == 0)
			LOG_INFO(LogBots, "[DebugBot] [seq.tick] step={} ready={} valid={} life={}",
				DebugBotStateName(gDebugBot.Step), Bot.IsReady(), Bot.IsValidActor(), (int)Bot.GetLifeState());

		if (gDebugBot.Step == DebugBotState::None || gDebugBot.Step == DebugBotState::Finished)
			return;

		if (!Bot.IsReady() || !Bot.IsValidActor() || Bot.GetLifeState() != CBT::ELifeState::Alive)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: bot lost/died during sequence (step={}, life={}), removing",
				DebugBotStateName(gDebugBot.Step), (int)Bot.GetLifeState());
			gDebugBot.NextActionTime = DebugBotTime() + 0.5;
			gDebugBot.Step = DebugBotState::WaitingToDisappear;
			return;
		}

		float T = DebugBotTime();
		FVector Pos = Bot.Pawn->GetActorLocation();
		auto* CurrentWeapon = CustomBotInventory::GetCurrentWeapon(Bot);

		switch (gDebugBot.Step)
		{
		case DebugBotState::Spawned:
		{
			auto* GameStateDBG = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
			LOG_INFO(LogBots, "[DebugBot] Match: phase={} step={}",
				GameStateDBG ? (int)GameStateDBG->GetGamePhase() : -1,
				GameStateDBG ? (int)GameStateDBG->GetGamePhaseStep() : -1);

			auto* WeaponData = CurrentWeapon ? CurrentWeapon->GetWeaponData() : nullptr;
			LOG_INFO(LogBots, "[DebugBot] [Spawned] pos=({:.0f},{:.0f},{:.0f}) weapon={} moveActive={}",
				Pos.X, Pos.Y, Pos.Z,
				WeaponData ? WeaponData->GetPathName().c_str() : (CurrentWeapon ? "UNKNOWN" : "NONE"),
				Bot.bMoveRequestActive);

			// Diagnostico: estado real del sistema de movimiento para ver por que
			// la fisica no se simula (gravedad/velocity no mueven el pawn).
			if (auto* CM = CustomBotMovement::GetCharacterMovement(Bot))
			{
				static auto VelocityOffset = CM->GetOffset("Velocity");
				static auto AccelerationOffset = CM->GetOffset("Acceleration");
				static auto MovementModeOffset = CM->GetOffset("MovementMode");
				FVector V = CM->Get<FVector>(VelocityOffset);
				FVector A = CM->Get<FVector>(AccelerationOffset);
				int Mode = 255;
				auto TryMode = CM->GetOffset("MovementMode");
				if (TryMode != -1) Mode = *(int*)(__int64(CM) + TryMode);
				LOG_INFO(LogBots, "[DebugBot] CM: vel=({:.0f},{:.0f},{:.0f}) acc=({:.0f},{:.0f},{:.0f}) mode={}",
					V.X, V.Y, V.Z, A.X, A.Y, A.Z, Mode);

				// Estado de tick del componente (por que no se simula):
				auto CMAddr = __int64(CM);
				LOG_INFO(LogBots, "[DebugBot] CM state: addr=0x{:x}",
					CMAddr);
				auto ProbeByte = [&](const char* Field, int Def = -1) -> int {
					int off = CM->GetOffset(Field, false);
					if (off == -1) return Def;
					return *(uint8_t*)(CMAddr + off);
				};
				auto ProbeTickFlags = [&](const char* StructField, const char* SubField) -> int {
					// algunos campos de FTickFunction estan logicamente antes; probe por nombre
					int off = CM->GetOffset(StructField, false);
					if (off == -1) off = CM->GetOffset(SubField, false);
					if (off == -1) return -1;
					// bCanEverTick/bStartWithTickEnabled son bits consecutivos en FTickFunction
					return *(uint8_t*)(CMAddr + off);
				};
				LOG_INFO(LogBots, "[DebugBot] CM flags: bMovementEnabled={} bComponentShouldTick={} bRegistered={} bNeedUROUpdate={} GravityScaleField={}",
					ProbeByte("bMovementEnabled"), ProbeByte("bComponentShouldTick"),
					ProbeByte("bRegistered"), ProbeByte("bNeedUROUpdate"),
					ProbeTickFlags("PrimaryComponentTick", "bStartWithTickEnabled"));

				// Estado del pawn (actor tick / control).
				LOG_INFO(LogBots, "[DebugBot] Pawn: dormancy={} bTearOff={} bReplicates={} controllerPawnMatch={} netConnection={}",
					(int)Bot.Pawn->GetNetDormancy(),
					(int)Bot.Pawn->IsTearOff(),
					Bot.Pawn->DoesReplicate(),
					Bot.Controller && Bot.Controller->GetPawn() == Bot.Pawn,
					Bot.Controller ? (bool)Bot.Controller->GetNetConnection() : false);

				// Gates de gravedad/movimiento de Fortnite AFortPawn: si el pawn se
				// materializo fuera del flujo de jugador (bus/skydive), el juego deja
				// la gravedad desactivada (bSimGravityDisabled) y no se mueve. Se
				// sondearon y, si estan puestos, se desactivan.
				int GravOff = Bot.Pawn->GetOffset("bSimGravityDisabled", false);
				int StopOff = Bot.Pawn->GetOffset("bDisableMovementAndTurnInPlace", false);
				int AllowOff = Bot.Pawn->GetOffset("bAllowMovement", false);

				LOG_INFO(LogBots, "[DebugBot] Pawn gates: bSimGravityDisabled={} bDisableMovementAndTurnInPlace={} bAllowMovement={} (offs={},{},{})",
					GravOff != -1 ? *(uint8_t*)(__int64(Bot.Pawn) + GravOff) : -1,
					StopOff != -1 ? *(uint8_t*)(__int64(Bot.Pawn) + StopOff) : -1,
					AllowOff != -1 ? *(uint8_t*)(__int64(Bot.Pawn) + AllowOff) : -1,
					GravOff, StopOff, AllowOff);

				if (GravOff != -1 && *(uint8_t*)(__int64(Bot.Pawn) + GravOff))
				{
					*(uint8_t*)(__int64(Bot.Pawn) + GravOff) = 0;
					LOG_INFO(LogBots, "[DebugBot] FORCED bSimGravityDisabled=false");
				}
				if (StopOff != -1 && *(uint8_t*)(__int64(Bot.Pawn) + StopOff))
				{
					*(uint8_t*)(__int64(Bot.Pawn) + StopOff) = 0;
					LOG_INFO(LogBots, "[DebugBot] FORCED bDisableMovementAndTurnInPlace=false");
				}
				if (AllowOff != -1 && *(uint8_t*)(__int64(Bot.Pawn) + AllowOff) == 0)
				{
					*(uint8_t*)(__int64(Bot.Pawn) + AllowOff) = 1;
					LOG_INFO(LogBots, "[DebugBot] FORCED bAllowMovement=true");
				}

				// Gravedad configurada en el movement component.
				auto GetCmFloat = [&](const char* Field) -> float {
					int Off = CM->GetOffset(Field, false);
					if (Off == -1) return -99999.0f;
					return *(float*)(CMAddr + Off);
				};
				LOG_INFO(LogBots, "[DebugBot] CM gravity: GravityZ={:.1f} GravityScale={:.2f}",
					GetCmFloat("GravityZ"), GetCmFloat("GravityScale"));
			}
			else
			{
				LOG_ERROR(LogBots, "[DebugBot] ERROR: CharacterMovement is NULL on bot pawn!");
			}

			// Configurados en el comando; arranca el avance real.
			gDebugBot.Step = DebugBotState::MovingForward;

			LOG_INFO(LogBots, "[DebugBot] -> MovingForward");

			// EXPERIMENTO: impulso nativo (LaunchCharacter Z puro) para ver si la
			// fisica del pawn responde. Si sube y cae: la fisica funciona.
			LOG_INFO(LogBots, "[DebugBot] PHYSICS-PROBE: LaunchCharacter impulse Z=300");
			CustomBotMovement::Launch(Bot, FVector(0, 0, 300), false, false);

			FVector Fwd = Bot.Pawn->GetActorForwardVector();
			FVector Start = Bot.Pawn->GetActorLocation();
			FVector Dest{ Start.X + Fwd.X * 250.0f, Start.Y + Fwd.Y * 250.0f, Start.Z };
			LOG_INFO(LogBots, "[DebugBot] MoveTo dest=({:.0f},{:.0f},{:.0f}) radius=120", Dest.X, Dest.Y, Dest.Z);
			CustomBotMovement::MoveTo(Bot, Dest, 120.0f);
			gDebugBot.NextActionTime = T + 8.0; // watchdog si no llega
			break;
		}

		case DebugBotState::MovingForward:
		{
			// EXPERIMENTO "BOT = JUGADOR REAL": clonar el arranque de un pawn vivo.
			// A) Handshake de live: al entrar, forzar el ServerAcknowledgePossession
			//    que envía un cliente real (el servidor exclusivo del bot nunca lo hace).
			// B) Mantener el CM del bot idéntico al de un pawn vivo: tick habilitado,
			//    MovementMode Walking y velocidades máximas, reaplicado cada frame si
			//    el watchdog del juego lo revierte (los reverts se loguean).
			auto* CME = CustomBotMovement::GetCharacterMovement(Bot);

			if (!gDebugBot.bMoveExperimentDone)
			{
				gDebugBot.bMoveExperimentDone = true;
				DebugBotClaimLive(Bot.Controller, Bot.Pawn, true);
			}

			// EXPERIMENTO "RUNPHYS": probar la rama del CMC que simula fisica en servidor
			// para pawns sin controller. Requisito: bIsABot=false + soltar el pose +
			// bit bRunPhysicsWithNoController=true (la prueba de UnPossess previa no
			// lo activo -> la rama quedo inerte). Si fisica integra la Velocity/
			// Acceleration escritas por MoveTo, el CMC simula para el bot.
			// RESUELTO en research 08: ahora se aplica SIEMPRE en SpawnCustomBot
			// (CustomBotMovement::EnableServerSimulation), ya en el spawn de todo bot.

			if (CME)
			{
				__int64 CMEAddr = __int64(CME);

				int TickNow = DebugBotCMForceTick(CME);

				auto SetFloatIfPresent = [&](const char* Name, float Value) {
					int Off = CME->GetOffset(Name, false);
					if (Off != -1) *(float*)(CMEAddr + Off) = Value;
				};
				auto MoveIntIfPresent = [&](const char* Name, int Value) -> bool {
					int Off = CME->GetOffset(Name, false);
					if (Off == -1) return false;
					*(int*)(CMEAddr + Off) = Value;
					return true;
				};

				SetFloatIfPresent("MaxWalkSpeed", CustomBotMovement::WalkSpeed);
				SetFloatIfPresent("MaxWalkSpeedCrouched", CustomBotMovement::WalkSpeed);
				SetFloatIfPresent("MaxFlySpeed", CustomBotMovement::WalkSpeed);
				SetFloatIfPresent("MaxAcceleration", 2048.0f);

				// Refuerzo de Walking si el juego relega el pawn no-live a Falling.
				int Mode = -1;
				int ModeOff = CME->GetOffset("MovementMode", false);
				if (ModeOff != -1) Mode = *(int*)(CMEAddr + ModeOff);
				if (Mode != 1)
				{
					MoveIntIfPresent("MovementMode", 1);
					MoveIntIfPresent("GroundMovementMode", 1);
					++gDebugBot.ModeForceCount;
					if (T - gDebugBot.LastModeReLog >= 1.0)
					{
						LOG_WARN(LogBots, "[DebugBot] CLONE-EXPERIMENT: watchdog reverted mode->{} (re-forced Walking #{}) cmTickEnabled={}",
							Mode, gDebugBot.ModeForceCount, TickNow);
						gDebugBot.LastModeReLog = T;
					}
				}
			}

			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (gDebugBot.PauseUntil < 0)
				{
					gDebugBot.PauseUntil = T + 3.0;
					LOG_INFO(LogBots, "[DebugBot] [MovingForward] arrived, holding 3s before jumping (pos=({:.0f},{:.0f},{:.0f}))",
						Pos.X, Pos.Y, Pos.Z);
					break;
				}

if (T >= gDebugBot.PauseUntil)
			{
				gDebugBot.Step = DebugBotState::BuildingRamp;
				LOG_INFO(LogBots, "[DebugBot] -> BuildingRamp");
				gDebugBot.NextActionTime = T + 3.0; // pausa 3s antes de construir
				break;
			}
				break;
			}
			else
			{
				// Log de progreso cada 2 segundos, incluyendo Velocity real y
				// MovementMode para seguir el probe de fisica.
				static double LastMoveLog = 0;
				auto* GameStateDBG = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
				if (T - LastMoveLog >= 2.0)
				{
					FVector V{}, A{};
					int Mode = -1;
					if (auto* CM = CustomBotMovement::GetCharacterMovement(Bot))
					{
						static const auto OffV = CM->GetOffset("Velocity");
						static const auto OffA = CM->GetOffset("Acceleration");
						static const auto OffM = CM->GetOffset("MovementMode");
						V = CM->Get<FVector>(OffV);
						A = CM->Get<FVector>(OffA);
						if (OffM != -1) Mode = *(int*)(__int64(CM) + OffM);
					}
					LOG_INFO(LogBots, "[DebugBot] [MovingForward] pos=({:.0f},{:.0f},{:.0f}) moveState={} vel=({:.0f},{:.0f},{:.0f}) acc=({:.0f},{:.0f},{:.0f}) mode={} phase={}",
						Pos.X, Pos.Y, Pos.Z, (int)Bot.MoveState, V.X, V.Y, V.Z, A.X, A.Y, A.Z, Mode,
						GameStateDBG ? (int)GameStateDBG->GetGamePhase() : -1);

if (GameStateDBG)
				{
					LOG_INFO(LogBots, "[DebugBot] [MovingForward] phaseStep={}",
						(int)GameStateDBG->GetGamePhaseStep());
				}

				LastMoveLog = T;
				}
			}
			break;
		}

		case DebugBotState::BuildingRamp:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				++gDebugBot.RampAttempts;
				if (gDebugBot.RampAttempts > gDebugBot.MaxRampAttempts)
				{
					LOG_ERROR(LogBots, "[DebugBot] BuildingRamp: {} attempts failed, skipping to Finished", gDebugBot.RampAttempts - 1);
					DebugBotError(Bot, "ramp build failed after max retries");
					gDebugBot.Step = DebugBotState::Finished;
					break;
				}

				LOG_INFO(LogBots, "[DebugBot] [BuildingRamp] pos=({:.0f},{:.0f},{:.0f}) attempt={}/{}",
					Pos.X, Pos.Y, Pos.Z, gDebugBot.RampAttempts, gDebugBot.MaxRampAttempts);

				int TotalMat = CustomBotResources::GetTotalResourceCount(Bot);
				LOG_INFO(LogBots, "[DebugBot] Total materials: {}", TotalMat);

				if (TotalMat < 10)
				{
					// Los materiales otorgados al spawn pueden volatilizarse si la
					// sesion hace una transicion de fase/reset del inventario; se
					// re-otorgan justo antes de construir para lidiar con ello.
					LOG_WARN(LogBots, "[DebugBot] Refreshing materials (was {})...", TotalMat);
					CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, 1000);
					CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, 1000);
					CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, 1000);
					TotalMat = CustomBotResources::GetTotalResourceCount(Bot);
					LOG_INFO(LogBots, "[DebugBot] Materials after refresh: {}", TotalMat);
				}

				if (TotalMat < 10)
				{
					DebugBotError(Bot, "not enough materials to build ramp");
					break;
				}

				FVector Start = Bot.Pawn->GetActorLocation();

				auto GS_Build = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
				auto SSS_Build = GS_Build ? GS_Build->GetStructuralSupportSystem() : nullptr;

				// Rampa SIEMPRE mirando hacia el bot (los StairW suben hacia +X local):
				// la DERECHA de la rampa queda enfrente nuestra (perpendicular al
				// avance del bot, NO 180 grados).
				float FacingCardinal = CustomBotBuilding::SnapYawToCardinal(Bot.Pawn->GetActorRotation().Yaw);
				FRotator RampRot = Bot.Pawn->GetActorRotation();
				RampRot.Yaw = CustomBotBuilding::SnapYawToCardinal(FacingCardinal + 90.0f);

				// Posicion: la celda del grid inmediatamente adelante del bot, con
				// el Z del TERRENO (el Z de la celda del grid es una referencia, no
				// la superficie donde el bot camina).
				FVector RampLoc = Start;
				if (SSS_Build && !CustomBotBuilding::CellCenterAhead(SSS_Build, Start, FacingCardinal, 1, RampLoc))
				{
					DebugBotError(Bot, "could not find grid cell for ramp");
					break;
				}
				FVector RampGround = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
					FVector{ RampLoc.X, RampLoc.Y, 0.0f }, Pos.Z + 2000.0f, Pos.Z - 5000.0f, FName(0));
				RampLoc.Z = RampGround.Z;
				LOG_INFO(LogBots, "[DebugBot] BuildRamp (cell ahead, yaw {:.1f}) at ({:.0f},{:.0f},{:.0f}) groundZ={:.0f}",
					RampRot.Yaw, RampLoc.X, RampLoc.Y, RampLoc.Z, RampGround.Z);

				gDebugBot.RampActor = CustomBotBuilding::BuildRamp(Bot, RampLoc, RampRot);

				if (!gDebugBot.RampActor)
				{
					LOG_ERROR(LogBots, "[DebugBot] BuildRamp returned nullptr!");
					DebugBotError(Bot, "ramp could not be built (invalid location)");
					break;
				}

				LOG_INFO(LogBots, "[DebugBot] Ramp built OK (health {:.0f})", CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor));

				if (gDebugBot.RampActor && !gDebugBot.bRampFacingLogged)
				{
					gDebugBot.bRampFacingLogged = true;
					FRotator RA = gDebugBot.RampActor->GetActorRotation();
					FRotator RP = Bot.Pawn->GetActorRotation();
					LOG_INFO(LogBots, "[DebugBot] RAMP-ORIENT rampaYaw={:.1f} botYaw={:.1f} rampLoc=({:.0f},{:.0f},{:.0f}) botLoc=({:.0f},{:.0f},{:.0f})",
						RA.Yaw, RP.Yaw, gDebugBot.RampActor->GetActorLocation().X, gDebugBot.RampActor->GetActorLocation().Y, gDebugBot.RampActor->GetActorLocation().Z,
						Bot.Pawn->GetActorLocation().X, Bot.Pawn->GetActorLocation().Y, Bot.Pawn->GetActorLocation().Z);
				}

				// Pausa de 3s antes de empezar a caminar hacia la rampa (pacing de la demo).
				gDebugBot.RampAttempts = 0; // reset counter on success
				gDebugBot.Step = DebugBotState::WalkToRampStart;
				gDebugBot.PauseUntil = -1; // WalkToRampStart dispara la pausa
				gDebugBot.bClimbStarted = false;
				LOG_INFO(LogBots, "[DebugBot] -> WalkToRampStart (3s pause)");
			}
			break;
		}

		case DebugBotState::WalkToRampStart:
		{
			// Pausa de 3s tras construir la rampa antes de caminar a su inicio.
			if (gDebugBot.PauseUntil < 0)
			{
				gDebugBot.PauseUntil = T + 3.0;
				LOG_INFO(LogBots, "[DebugBot] Ramp built; holding 3s before walking to its start");
				break;
			}

			if (!gDebugBot.bClimbStarted && T >= gDebugBot.PauseUntil)
			{
				gDebugBot.bClimbStarted = true;

				// Andar hacia el INICIO de la rampa: la ENTRADA (borde bajo) esta en
				// el punto medio entre el bot y el centro de la celda de la rampa.
				FVector StartPos = Bot.Pawn->GetActorLocation();
				FVector RampPos = gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation() : Bot.Pawn->GetActorLocation();
				FVector Entrance{ (StartPos.X + RampPos.X) * 0.5f, (StartPos.Y + RampPos.Y) * 0.5f, StartPos.Z };
				CustomBotMovement::MoveTo(Bot, Entrance, 150.0f);
				gDebugBot.NextActionTime = T + 9.0; // watchdog
				LOG_INFO(LogBots, "[DebugBot] walking to ramp start/entrance ({:.0f},{:.0f},{:.0f})", Entrance.X, Entrance.Y, Entrance.Z);
				break;
			}

			if (gDebugBot.bClimbStarted && (Bot.HasArrived() || T >= gDebugBot.NextActionTime))
			{
				if (T >= gDebugBot.NextActionTime)
					LOG_INFO(LogBots, "[DebugBot] walk to ramp start watchdog (continuing sequence)");

				gDebugBot.Step = DebugBotState::WalkToRampMiddle;
				gDebugBot.bClimbStarted = false;
				LOG_INFO(LogBots, "[DebugBot] -> WalkToRampMiddle");
			}
			break;
		}

		case DebugBotState::WalkToRampMiddle:
		{
			// Ir al MEDIO de la rampa: andar hacia delante hacia el centro de la
			// celda y el juego sube al bot por la pendiente (sin precomputar Z).
			if (!gDebugBot.bClimbStarted)
			{
				gDebugBot.bClimbStarted = true;
				FVector RampPos = gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation() : Bot.Pawn->GetActorLocation();
				FVector Middle{ RampPos.X, RampPos.Y, Bot.Pawn->GetActorLocation().Z };
				CustomBotMovement::MoveTo(Bot, Middle, 150.0f);
				gDebugBot.NextActionTime = T + 9.0; // watchdog
				LOG_INFO(LogBots, "[DebugBot] walking to ramp middle ({:.0f},{:.0f},{:.0f})", Middle.X, Middle.Y, Middle.Z);
				break;
			}

			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (T >= gDebugBot.NextActionTime)
					LOG_INFO(LogBots, "[DebugBot] ramp mid watchdog (continuing sequence)");

				gDebugBot.Step = DebugBotState::Jumping;
				gDebugBot.bClimbStarted = false;
				LOG_INFO(LogBots, "[DebugBot] -> Jumping (on ramp middle)");
				CustomBotMovement::Jump(Bot);
				gDebugBot.NextActionTime = T + 1.0; // 1s para poner el suelo debajo
			}
			break;
		}

		case DebugBotState::Jumping:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::BuildingFloor;
				LOG_INFO(LogBots, "[DebugBot] -> BuildingFloor (suelo justo debajo del bot)");
			}
			break;
		}

		case DebugBotState::BuildingFloor:
		{
			LOG_INFO(LogBots, "[DebugBot] [BuildingFloor] pos=({:.0f},{:.0f},{:.0f})", Pos.X, Pos.Y, Pos.Z);

			// Poner un SUELO justo debajo del bot (misma celda X/Y del grid, a ras de
			// suelo = Z de la base de la rampa), para luego destruirlo con el pico.
			FVector FloorLoc = FVector{ Pos.X, Pos.Y, (gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation().Z : Pos.Z - 16.0f) };
			auto GS_Floor = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
			auto SSS_Floor = GS_Floor ? GS_Floor->GetStructuralSupportSystem() : nullptr;
			if (SSS_Floor && CustomBotBuilding::CellCenterAhead(SSS_Floor, Pos,
				CustomBotBuilding::SnapYawToCardinal(Bot.Pawn->GetActorRotation().Yaw), 0, FloorLoc))
			{
				FloorLoc.Z = gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation().Z : Pos.Z - 16.0f;
			}
			LOG_INFO(LogBots, "[DebugBot] BuildingFloor at ({:.0f},{:.0f},{:.0f})", FloorLoc.X, FloorLoc.Y, FloorLoc.Z);

			gDebugBot.FloorActor = CustomBotBuilding::BuildFloor(Bot, FloorLoc, FRotator{ 0.0f, 0.0f, 0.0f }, false, true);

			if (!gDebugBot.FloorActor)
			{
				DebugBotError(Bot, "floor could not be built below the bot");
				break;
			}

			LOG_INFO(LogBots, "[DebugBot] Floor built OK (health {:.0f})", CustomBotDestruction::GetStructureHealth(gDebugBot.FloorActor));

			gDebugBot.Step = DebugBotState::EquippingPickaxe;
			gDebugBot.NextActionTime = T + 0.6;
			LOG_INFO(LogBots, "[DebugBot] -> EquippingPickaxe");
			break;
		}

		case DebugBotState::EquippingPickaxe:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::DestroyingRamp;
				LOG_INFO(LogBots, "[DebugBot] Destroying ramp + floor with pickaxe");

				if (!CustomBotInventory::EquipPickaxe(Bot))
				{
					DebugBotError(Bot, "pickaxe could not be equipped");
					break;
				}

				// Acercarse a la rampa/floor para golpearlas.
				if (gDebugBot.RampActor)
				{
					FVector RampPos = gDebugBot.RampActor->GetActorLocation();
					CustomBotMovement::MoveTo(Bot, FVector{ RampPos.X, RampPos.Y, RampPos.Z + 20.0f }, 140.0f);
				}

				gDebugBot.NextActionTime = T + 3.0; // watchdog de acercamiento
			}
			break;
		}

		case DebugBotState::DestroyingRamp:
		{
			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (!gDebugBot.RampActor || CustomBotDestruction::IsStructureDestroyed(gDebugBot.RampActor))
				{
					DebugBotError(Bot, "ramp already gone or missing to destroy");
					break;
				}

				// Destruir la RAMPA con el pico.
				float BeforeR = CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor);
				bool bDestroyedR = CustomBotDestruction::DestroyTarget(gDebugBot.RampActor);
				float AfterR = CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor);

				if (!bDestroyedR || AfterR > 0.0f)
				{
					LOG_INFO(LogBots, "[DebugBot] ramp damage applied: {:.0f} -> {:.0f} (melee pickaxe real pendiente en Parte 2)", BeforeR, AfterR);
				}
				else
				{
					LOG_INFO(LogBots, "[DebugBot] Ramp destroyed: {:.0f} -> {:.0f}", BeforeR, AfterR);
				}

				// Destruir el SUELO con el pico.
				if (gDebugBot.FloorActor && !CustomBotDestruction::IsStructureDestroyed(gDebugBot.FloorActor))
				{
					float BeforeF = CustomBotDestruction::GetStructureHealth(gDebugBot.FloorActor);
					bool bDestroyedF = CustomBotDestruction::DestroyTarget(gDebugBot.FloorActor);
					float AfterF = CustomBotDestruction::GetStructureHealth(gDebugBot.FloorActor);

					if (!bDestroyedF || AfterF > 0.0f)
					{
						LOG_INFO(LogBots, "[DebugBot] floor damage applied: {:.0f} -> {:.0f} (melee pickaxe real pendiente en Parte 2)", BeforeF, AfterF);
					}
					else
					{
						LOG_INFO(LogBots, "[DebugBot] Floor destroyed: {:.0f} -> {:.0f}", BeforeF, AfterF);
					}

					gDebugBot.NextActionTime = T + 0.1;
				}
				else
				{
					gDebugBot.NextActionTime = T + 0.1;
				}

				gDebugBot.Step = DebugBotState::EquippingWeapon;
				gDebugBot.PauseUntil = -1; // EquippingWeapon arranca el re-equip
				LOG_INFO(LogBots, "[DebugBot] -> EquippingWeapon");
			}
			break;
		}

		case DebugBotState::EquippingWeapon:
		{
			// Re-equipar el arma tras destruir las estructuras con el pico y
			// pausar 3s antes de empezar a disparar.
			if (gDebugBot.PauseUntil < 0)
			{
				gDebugBot.PauseUntil = T + 3.0;
				LOG_INFO(LogBots, "[DebugBot] Equipping weapon");

				if (!CustomBotInventory::EquipFirstWeapon(Bot))
				{
					DebugBotError(Bot, "no weapon to re-equip after destroying ramp");
					break;
				}

				CustomBotCombat::Reload(Bot, 999);
				LOG_INFO(LogBots, "[DebugBot] Weapon re-equipped (ammo {}); holding 3s before shooting", CustomBotCombat::GetCurrentAmmo(Bot));
				break;
			}

			if (T >= gDebugBot.PauseUntil)
			{
				gDebugBot.PauseUntil = -1;
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

	// Aparicion "como un jugador real": mismo suelo (Z) que el jugador, separado
	// solo en horizontal. Antes se sumaba +50 en Z y el bot quedaba flotando 50cm
	// (modo Falling congelado). Este es el unico teletransporte del debugbot.
	FVector BotSpawn = PlayerLoc + Fwd * 250.0f;

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

		// Construir N rampas hacia arriba.
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

// SEH-safe bot tick: TickBotSafeSEH lives in CustomBotSEH.cpp (pure C TU).
// BotTickCallbackImpl is the C++ callback; TickCustomBotSafe is the public API.

extern "C" void TickBotSafeSEH(void (*cb)(void*), void* data);

static void BotTickCallbackImpl(void* data)
{
	CustomBot* Bot = (CustomBot*)data;
	if (!Bot || !Bot->IsValidActor() || !Bot->IsReady())
		return;

	// Aislamiento del leak (A/B en vivo): el slider de la GUI cambia
	// gBotTickMode (0=full | 1=sin IA | 2=sin IA/mov | 3=sin IA/mov/CMC |
	// 4=existencia | 5=CMC init-only sin writes | 6=idem sin CLAIM-LIVE).
	// Cada cambio se loguea para correlar la pendiente de committed/WS de
	// memdiag con la fase.
	static int LastMode = -1;
	int Mode = gBotTickMode;
	if (Mode != LastMode)
	{
		LOG_INFO(LogBots, "[leak] [tick-mode] -> {} (0=full 1=noAI 2=noAI/move 3=noAI/move/CMC 4=existence 5=initNoWrites 6=initNoClaim)", Mode);
		LastMode = Mode;
	}

	if (Mode < 4)
	{
		if (Mode <= 2)
			CustomBotMovement::EnsureCMCActive(*Bot, true, true);

		Bot->Tick();

		if (Mode <= 1)
		{
			CustomBotMovement::UpdateMovement(*Bot);

			// Desatascado fisico (retroceder 2m + carrerilla 1m + salto + romper
			// con pico) cuando el bot lleva >umbral bloqueado en linea recta.
			// Corre DESPUES de UpdateMovement: solo re-apunta el move si lo necesita.
			CustomBotBreak::TickUnstuck(*Bot);

			// Skin diferida: aplicar como maximo PendingSkinBudget skins por TickAll
			// (se reinicia en CustomBotSpawner::TickAll). Todo esto corre bajo SEH.
			if (Bot->bSkinPending && CustomBotSpawner::PendingSkinBudget > 0)
			{
				--CustomBotSpawner::PendingSkinBudget;
				CustomBotMovement::ApplyPendingSkin(*Bot);
			}
		}

		if (Mode <= 0 && Bot->AI)
			CustomBotAI::Tick(*Bot, *Bot->AI);

		return;
	}

	if (Mode == 4)
		return; // existencia pura: el pawn sigue su tick nativo, nosotros no hacemos nada

	// Modos 5/6: SOLO la inicializacion una vez (claim + activar CMC) y ningun
	// trabajo por tick despues. Aisla "el estado de pawn live" de "nuestros
	// writes por tick" (5) y el CLAIM-LIVE del CMC (6).
	bool bClaim = (Mode == 5);
	CustomBotMovement::EnsureCMCActive(*Bot, false, bClaim);
	Bot->Tick();
}

void TickCustomBotSafe(CustomBot* Bot)
{
	TickBotSafeSEH(BotTickCallbackImpl, Bot);
}