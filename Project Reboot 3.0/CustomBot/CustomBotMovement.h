#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"

#include "CustomBotPathfinding.h"
#include "CustomBotDoors.h"

#include <chrono>
#include <utility>

// CustomBot - Movimiento.
//
// Mueve al pawn del bot como un jugador usando el sistema de movimiento nativo
// del Character (CharacterMovement component -> Velocity). NO usa teletransporte
// como movimiento normal.
//
// Nota (research 06): no hay wrapper AddMovementInput en el repo; el pawn hereda
// el CharacterMovement del Character. En el lado servidor, manipular la Velocity
// del CharacterMovement produce movimiento real de jugador.

namespace CustomBotMovement
{
	// Constantes de movimiento (valores razonables de jugador).
	inline constexpr float WalkSpeed = 600.0f;
	inline constexpr float SprintSpeed = 900.0f;
	inline constexpr float JumpStrength = 500.0f;

	constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

	// Rota el pawn (y su control) hacia una rotacion concreta. (definida debajo;
	// LookAt la usa antes de su definicion).
	static void SetRotation(CustomBot& Bot, const FRotator& Rotation);

	// Limpia la ruta navmesh cacheada del bot (definida abajo; MoveTo la usa
	// antes de su definicion).
	static void ClearPath(CustomBot& Bot);

	// Devuelve la direccion normalizada desde From hacia To.
	// Si From==To devuelve zero. La normalizacion se hace a mano porque FVector
	// del repo no tiene .Size()/.Normalize() (solo SizeSquared y dot).
	static FVector DirectionTo(const FVector& From, const FVector& To)
	{
		FVector Delta = To - From;

		float LenSq = Delta | Delta; // dot = cuadratico de la longitud

		if (LenSq <= 0.0001f)
			return FVector{};

		float InvLen = 1.0f / FMath::Sqrt(LenSq);
		return Delta * InvLen;
	}

	// Distancia 2D (horizontal) entre dos puntos.
	static float HorizontalDistance(const FVector& A, const FVector& B)
	{
		float DX = B.X - A.X;
		float DY = B.Y - A.Y;
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	// Distancia 3D entre dos puntos.
	static float Distance(const FVector& A, const FVector& B)
	{
		FVector Delta = B - A;
		return FMath::Sqrt(Delta | Delta);
	}

	// Construye un FRotator (Yaw/Pitch) que "apunta" en la direccion Dir.
	// No existe Conv_VectorToRotator en el repo; se compone a mano.
	static FRotator RotationFromDirection(const FVector& Dir)
	{
		FRotator Rot{};

		Rot.Yaw = FMath::Atan2(Dir.Y, Dir.X) * RAD_TO_DEG;
		Rot.Pitch = FMath::Atan2(Dir.Z, FMath::Sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y)) * RAD_TO_DEG;
		Rot.Roll = 0.0f;

		return Rot;
	}

	// Accede al CharacterMovement del pawn y devuelve puntero a su Velocity.
	static UObject* GetCharacterMovement(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return nullptr;

		static auto CharacterMovementOffset = Bot.Pawn->GetOffset("CharacterMovement");
		return Bot.Pawn->Get(CharacterMovementOffset);
	}

	// Restaura el puntero PlayerState del pawn (lo borra UnPossess). Sin el
	// puntero, InitializeCharacterParts falla y el sistema nativo de cosmeticos
	// re-intenta cargar parts cada tick (leak ~100MB/s + UObjects creciendo,
	// medido 19:12 con bots=5). Se llama SOLO al spawn del bot
	// (EnableServerSimulation + primer tick de EnsureCMCActive). NO cada 30
	// ticks: la re-escritura periodica re-triggeraba OnRep_PlayerState ->
	// InitializeCharacterParts -> mesh reload.
	static void RestorePawnPlayerState(CustomBot& Bot)
	{
		if (!Bot.Pawn || !Bot.PlayerState)
			return;

		int PSOff = Bot.Pawn->GetOffset("PlayerState", false);

		if (PSOff != -1 && Bot.Pawn->Get<UObject*>(PSOff) != (UObject*)Bot.PlayerState)
			Bot.Pawn->Get<UObject*>(PSOff) = (UObject*)Bot.PlayerState;
	}

	// Habilita la simulacion CMC en servidor para el bot (research 08).
	// SetIsBot(false) + UnPossess + bRunPhysicsWithNoController=true.
	// Sin ClaimLive — el handshake no es necesario para la fisica.
	static bool EnableServerSimulation(CustomBot& Bot)
	{
		if (!Bot.PlayerState || !Bot.Controller || !Bot.Pawn)
			return false;

		Bot.PlayerState->SetIsBot(false);
		Bot.Controller->UnPossess();

		// UnPossess() limpia Pawn->PlayerState (ACharacter::UnPossessed -> null).
		// Se restaura el puntero para que el sistema de cosmeticos/tick del pawn
		// lo vea (sin el, el mesh re-intenta cargar parts cada tick).
		RestorePawnPlayerState(Bot);

		bool bBitOK = false;
		if (auto* CMR = GetCharacterMovement(Bot))
		{
			auto* Prop = CMR->GetProperty("bRunPhysicsWithNoController");
			int Off = CMR->GetOffset("bRunPhysicsWithNoController", false);
			if (Prop && Off != -1)
			{
				CMR->SetBitfieldValue(Off, GetFieldMask(Prop), true);
				bBitOK = true;
			}
		}

		LOG_WARN(LogBots, "[CustomBot] RUNPHYS-FIX: IsBot=false, UnPossess, bRunPhysicsWithNoController={}",
			bBitOK);

		return bBitOK;
	}

	// Forza el tick del CMC y re-aplica valores que el engine revierte.
	// Llamar CADA TICK desde TickAll para cada bot activo.
	// (basado en lo que hace DebugBot en MovingForward que funciona correctamente:
	//  CLAIM-LIVE + velocidades + MovementMode Walking, reaplicados cada frame)
	//
	// OPTIMIZACION: las operaciones "una sola vez" (ProcessEvent de
	// SetComponentTickEnabled/Activate, CLAIM-LIVE) se ejecutan solo en el
	// primer tick (bCMCInitialized). Antes se ejecutaban CADA tick x CADA bot
	// (15 bots x 2 ProcessEvent x 60 tps = 1800 ProcessEvent/s), lo que
	// re-triggeraba la re-evaluacion nativa de character parts en el pawn
	// -> FortCustomizationAssetLoader re-cargaba meshes/skins continuamente.
	// RestorePawnPlayerState se throttlea a cada 30 ticks (~0.5s) por la
	// misma razon: el write directo al pointer PlayerState puede triggerar
	// OnRep_PlayerState -> InitializeCharacterParts -> mesh reload.
	static void EnsureCMCActive(CustomBot& Bot, bool bPerTickWork = true, bool bDoClaim = true)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		auto* CME = GetCharacterMovement(Bot);
		if (!CME)
			return;

		__int64 CMEAddr = __int64(CME);

		// --- Tick 1: inicializacion una sola vez por bot ---
		if (!Bot.bCMCInitialized)
		{
			// Mantener el PlayerState en el pawn una vez (skin necesita el puntero).
			RestorePawnPlayerState(Bot);

			// CLAIM-LIVE una sola vez: replica ServerAcknowledgePossession.
			// Aislamiento del leak: bDoClaim=false (modo 6) omite el handshake
			// (EnableServerSimulation documenta que no es necesario para la
			// fisica) para ver si el claim es el que activa el tick nativo con
			// leak de arena (~44MB/s con 5 bots).
			if (bDoClaim && !Bot.bClaimLiveDone && Bot.Controller)
			{
				static auto AckFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ServerAcknowledgePossession");
				if (AckFn)
				{
					struct { APawn* NewPawn; } Params{};
					Params.NewPawn = Bot.Pawn;
					Bot.Controller->ProcessEvent(AckFn, &Params);
				}

				auto AckOff = Bot.Controller->GetOffset("AcknowledgedPawn", false);
				if (AckOff != -1 && Bot.Controller->Get<APawn*>(AckOff) != Bot.Pawn)
					Bot.Controller->Get<APawn*>(AckOff) = Bot.Pawn;

				Bot.bClaimLiveDone = true;
				LOG_WARN(LogBots, "[CustomBot] CLAIM-LIVE: ServerAcknowledgePossession invoked for bot");
			}

			// Forzar tick del CMC habilitado + Activate una sola vez.
			static auto FnSetTick = FindObject<UFunction>(L"/Script/Engine.ActorComponent.SetComponentTickEnabled");
			static auto FnActivate = FindObject<UFunction>(L"/Script/Engine.ActorComponent.Activate");
			if (FnSetTick) { struct { char Buf[32]; } P{}; P.Buf[0] = 1; CME->ProcessEvent(FnSetTick, &P); }
			if (FnActivate) { struct { char Buf[32]; } P{}; P.Buf[0] = 1; CME->ProcessEvent(FnActivate, &P); }

			Bot.bCMCInitialized = true;
			LOG_INFO(LogBots, "[CustomBot] CMC initialized (one-time setup done)");
		}

// --- DESHABILITADO: RestorePawnPlayerState se llama SOLO al spawn ---
		// El restore periodico cada 30 ticks re-triggeraba OnRep_PlayerState ->
		// InitializeCharacterParts -> mesh reload continuo.
		/*
		{
			Bot.RestorePSCounter++;
			if (Bot.RestorePSCounter >= 30)
			{
				RestorePawnPlayerState(Bot);
				Bot.RestorePSCounter = 0;
			}
		}
		*/

		// Aislamiento del leak (modo 5/6): con bPerTickWork=false se hace SOLO
		// la inicializacion una vez (claim + activar CMC) y no se toca nada mas
		// por tick. Si el leak (~44MB/s) sigue con los writes por tick ausentes,
		// el driver es de ESTADO (el pawn live tickea nativo y fuga), no nuestros
		// writes.
		if (!bPerTickWork)
			return;

		// --- Cada tick: re-aplicar valores que el engine revierte ---
		// LEAK RAIZ (fix): los offsets se cachean en static la primera vez.
		// Antes se llamaba GetOffset() cada tick x cada bot, y GetOffset ->
		// GetProperty escaneaba toda la cadena de propiedades llamando
		// FName::ToString() por cada propiedad. ToString() filtraba un FString
		// del engine por llamada (ver UnrealNames.cpp) -> ~45MB/s con 5 bots.
		// Ahora el bloque por tick no escanea nada.
		// Re-aplicar velocidades que el engine revierte. MaxWalkSpeed se sube a la
		// velocidad de sprint (900): el CMC usa GetMaxSpeed() como tope al integrar,
		// y sin esto las peticiones MoveTo sprint (900) quedaban capadas a 600.
		{
			static int MaxWalkSpeedOff = CME->GetOffset("MaxWalkSpeed", false);
			static int MaxWalkSpeedCrouchedOff = CME->GetOffset("MaxWalkSpeedCrouched", false);
			static int MaxFlySpeedOff = CME->GetOffset("MaxFlySpeed", false);
			static int MaxAccelerationOff = CME->GetOffset("MaxAcceleration", false);
			if (MaxWalkSpeedOff != -1) *(float*)(CMEAddr + MaxWalkSpeedOff) = SprintSpeed;
			if (MaxWalkSpeedCrouchedOff != -1) *(float*)(CMEAddr + MaxWalkSpeedCrouchedOff) = WalkSpeed;
			if (MaxFlySpeedOff != -1) *(float*)(CMEAddr + MaxFlySpeedOff) = SprintSpeed;
			if (MaxAccelerationOff != -1) *(float*)(CMEAddr + MaxAccelerationOff) = 2048.0f;
		}

		// Refuerzo de Walking si el juego relega el pawn no-live a Falling
		// (misma tecnica que el DebugBot). Se salta mientras el bot es pasajero
		// del bus (el avion lo monta en Skydive/Falling; no tocar el modo).
		bool bInAircraft = Bot.PlayerState && Bot.PlayerState->IsInAircraft();

		if (!bInAircraft)
		{
			static int ModeOff = CME->GetOffset("MovementMode", false);
			if (ModeOff != -1 && *(int*)(CMEAddr + ModeOff) != 1)
			{
				*(int*)(CMEAddr + ModeOff) = 1;
				static int GroundOff = CME->GetOffset("GroundMovementMode", false);
				if (GroundOff != -1) *(int*)(CMEAddr + GroundOff) = 1;
			}
		}

		// Desbloquear gates que Fortnite pone en pawns sin cliente
		{
			static int bSimGravityDisabledOff = Bot.Pawn->GetOffset("bSimGravityDisabled", false);
			static int bDisableMovementOff = Bot.Pawn->GetOffset("bDisableMovementAndTurnInPlace", false);
			static int bAllowMovementOff = Bot.Pawn->GetOffset("bAllowMovement", false);
			if (bSimGravityDisabledOff != -1) *(uint8_t*)(__int64(Bot.Pawn) + bSimGravityDisabledOff) = 0;
			if (bDisableMovementOff != -1) *(uint8_t*)(__int64(Bot.Pawn) + bDisableMovementOff) = 0;
			if (bAllowMovementOff != -1) *(uint8_t*)(__int64(Bot.Pawn) + bAllowMovementOff) = 1;
		}

		// Los pawns simulados en servidor se replican a los clientes segun su
		// NetUpdateFrequency. Con frecuencia baja el cliente ve a los bots
		// avanzar a saltos y "resincronizar hacia atras" (~cada 0.1s). Subirla a
		// ~50Hz hace que el paso se vea fluido (updates pequenos, sin bloqueo).
		{
			auto& NetFreq = Bot.Pawn->GetNetUpdateFrequency();
			if (NetFreq < 50.0f)
				NetFreq = 50.0f;
			auto& MinNetFreq = Bot.Pawn->GetMinNetUpdateFrequency();
			if (MinNetFreq < 50.0f)
				MinNetFreq = 50.0f;
		}

		// Probe de smoothness (diagnostico del "resync atras"): cada 60 ticks
		// (~2s) con MoveTo activo se loguea el desplazamiento horizontal por
		// ventana. Si es ~velocidad*2s el servidor genera movimiento continuo y
		// el tiron es de replicacion/cliente; si es erratico (0, 3000...), el
		// movimiento del servidor salta y el fallo es de simulacion.
		if (Bot.bMoveRequestActive)
		{
			Bot.ProbeTicks++;
			if (Bot.ProbeTicks == 1)
			{
				Bot.ProbePrevLoc = Bot.Pawn->GetActorLocation();
			}
			else if (Bot.ProbeTicks >= 60)
			{
				float ProbeDist = HorizontalDistance(Bot.ProbePrevLoc, Bot.Pawn->GetActorLocation());
				int ModeOff = CME->GetOffset("MovementMode", false);
				int Mode = ModeOff != -1 ? *(int*)(CMEAddr + ModeOff) : -1;
				static auto VelOff = CME->GetOffset("Velocity", false);
				float VZ = VelOff != -1 ? CME->Get<FVector>(VelOff).Z : 0.0f;
				LOG_INFO(LogBots, "[CustomBot] [mprobe] moved {:.0f}u / 60 ticks (mode={}, vz={:.1f})",
					ProbeDist, Mode, VZ);
				Bot.ProbePrevLoc = Bot.Pawn->GetActorLocation();
				Bot.ProbeTicks = 0;
			}
		}
	}

	// Mira hacia el punto objetivo (rota el control del pawn hacia alla).
	static void LookAt(CustomBot& Bot, const FVector& TargetLocation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FVector Dir = DirectionTo(Bot.Pawn->GetActorLocation(), TargetLocation);

		if (Dir | Dir) // distinto de zero
		{
			SetRotation(Bot, RotationFromDirection(Dir));
		}
	}

	// Aplica la visualizacion DIFERIDA de la skin (mesh rebuild + replicacion).
	// El spawn deja el bot con bSkinPending=true; el tick del servidor lo aplica
	// con un presupuesto de ~2 skins por TickAll (CustomBotSpawner::PendingSkinBudget)
	// para no saturar el async loader en rafagas de spawns. Se invoca DENTRO del
	// SEH de TickCustomBotSafe, asi un fallo de cosmetico no tira el servidor.
	static void ApplyPendingSkin(CustomBot& Bot)
	{
		if (!Bot.bSkinPending || !Bot.Pawn || !Bot.PlayerState)
			return;

		Bot.bSkinPending = false;

		auto T0 = std::chrono::steady_clock::now();

		static auto UpdateVizFn = FindObject<UFunction>(L"/Script/FortniteGame.FortKismetLibrary.UpdatePlayerCustomCharacterPartsVisualization");
		if (UpdateVizFn)
		{
			auto PS = (AFortPlayerState*)Bot.PlayerState;
			UFortKismetLibrary::StaticClass()->ProcessEvent(UpdateVizFn, &PS);
		}

		Bot.PlayerState->ForceNetUpdate();
		Bot.Pawn->ForceNetUpdate();

		auto T1 = std::chrono::steady_clock::now();
		LOG_INFO(LogBots, "[CustomBot] deferred skin applied in {}ms",
			(int)std::chrono::duration_cast<std::chrono::milliseconds>(T1 - T0).count());
	}

	// Rota el pawn (y su control) hacia la direccion de movimiento.
	// Rota la CAMARA nativamente, NUNCA con TeleportTo: TeleportTo marca
	// bJustTeleported en el CharacterMovement (parte la fisica simulada del frame)
	// y en el cliente cada replicacion se ve como un snap (saltos/botecitos).
	// La rotacion nativa es la que leen las weapon abilities (GetBaseAimRotation)
	// y la que replica el CMC en cada move.
	// Funciones verificadas en Dump/ObjectsDump.txt:
	//   /Script/Engine.Controller.SetControlRotation
	//   /Script/Engine.SceneComponent.K2_SetWorldRotation
	static void SetRotation(CustomBot& Bot, const FRotator& Rotation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		// 1. Camara del controlador (SetControlRotation) -> direccion de disparo.
		if (Bot.Controller)
		{
			static auto SetControlRotationFn = FindObject<UFunction>(L"/Script/Engine.Controller.SetControlRotation");

			if (SetControlRotationFn)
			{
				static auto NewRotationOffset = FindOffsetStruct("/Script/Engine.Controller.SetControlRotation", "NewRotation");

				auto Params = Alloc(SetControlRotationFn->GetPropertiesSize());

				*(FRotator*)(__int64(Params) + NewRotationOffset) = Rotation;

				Bot.Controller->ProcessEvent(SetControlRotationFn, Params);

				VirtualFree(Params, 0, MEM_RELEASE);
			}
		}

		// 2. Pawn (RootComponent = capsule) con rotacion nativa no-teleport.
		static auto RootComponentOffset = Bot.Pawn->GetOffset("RootComponent");
		auto Root = (UObject*)Bot.Pawn->Get(RootComponentOffset);

		if (!Root)
			return;

		static auto K2_SetWorldRotationFn = FindObject<UFunction>(L"/Script/Engine.SceneComponent.K2_SetWorldRotation");

		if (K2_SetWorldRotationFn)
		{
			static auto NewRotationOffset = FindOffsetStruct("/Script/Engine.SceneComponent.K2_SetWorldRotation", "NewRotation");
			static auto bSweepOffset = FindOffsetStruct("/Script/Engine.SceneComponent.K2_SetWorldRotation", "bSweep");
			static auto bTeleportOffset = FindOffsetStruct("/Script/Engine.SceneComponent.K2_SetWorldRotation", "bTeleport");

			auto Params = Alloc(K2_SetWorldRotationFn->GetPropertiesSize());

			*(FRotator*)(__int64(Params) + NewRotationOffset) = Rotation;
			*(bool*)(__int64(Params) + bSweepOffset) = false;
			*(bool*)(__int64(Params) + bTeleportOffset) = false; // NO teleport: la fisica sigue intacta

			Root->ProcessEvent(K2_SetWorldRotationFn, Params);

			VirtualFree(Params, 0, MEM_RELEASE);
		}
	}

	// Rota el pawn al yaw dado (mantiene pitch/roll actuales).
	static void SetYaw(CustomBot& Bot, float YawDegrees)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FRotator Current = Bot.Pawn->GetActorRotation();
		Current.Yaw = YawDegrees;
		SetRotation(Bot, Current);
	}

	// Detiene el movimiento poniendo la velocidad del CharacterMovement a cero
	// y limpiando la peticion de movimiento activa.
	static void StopMovement(CustomBot& Bot)
	{
		auto CharacterMovement = GetCharacterMovement(Bot);

		if (CharacterMovement)
		{
			static auto VelocityOffset = CharacterMovement->GetOffset("Velocity");
			CharacterMovement->Get<FVector>(VelocityOffset) = FVector{};
		}

		Bot.bMoveRequestActive = false;
		Bot.MoveState = CBT::EMovementState::Idle;
	}

	// Aplica la velocidad horizontal hacia Destination una sola vez.
	// (rota al pawn hacia la direccion de movimiento si bRotateTowardsMove).
	// REGLA: NUNCA teletransportar bots; el movimiento DEBE salir de la fisica
	// simulada del pawn (Velocity/Acceleration del CharacterMovement).
	static void ApplyMoveVelocity(CustomBot& Bot, const FVector& Destination, float Speed, bool bRotateTowardsMove)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		auto CharacterMovement = GetCharacterMovement(Bot);

		if (!CharacterMovement)
			return;

		FVector BotLocation = Bot.Pawn->GetActorLocation();
		FVector Dir = DirectionTo(BotLocation, Destination);

		if (!(Dir | Dir))
		{
			StopMovement(Bot);
			return;
		}

		if (bRotateTowardsMove)
			LookAt(Bot, Destination);

		FVector NewVelocity{ Dir.X * Speed, Dir.Y * Speed, 0.0f };

		static auto VelocityOffset = CharacterMovement->GetOffset("Velocity");
		static auto AccelerationOffset = CharacterMovement->GetOffset("Acceleration");
		FVector& CharacterVelocity = CharacterMovement->Get<FVector>(VelocityOffset);

		// Conservamos el componente vertical (gravedad/caida) del pawn.
		CharacterVelocity.X = NewVelocity.X;
		CharacterVelocity.Y = NewVelocity.Y;

		// La fisica nativa recalcula Velocity desde Acceleration cada tick
		// (CalcVelocity). Alimentamos Acceleration en la misma direccion para
		// que el movimiento se mantenga pese al frenado por friccion.
		FVector& CharacterAcceleration = CharacterMovement->Get<FVector>(AccelerationOffset);
		CharacterAcceleration.X = Dir.X * Speed;
		CharacterAcceleration.Y = Dir.Y * Speed;
	}

	// Mueve al pawn hacia Destination. bSprint usara velocidad de sprint.
	// Almacena la peticion de movimiento en el bot: mientras este activa,
	// UpdateMovement() la re-aplica cada tick (llamado desde TickAll).
	// Si llegamos a AcceptanceRadius, la peticion marca el estado Arrived.
	// TODO-PATH: si cambia el destino se invalida la ruta navmesh (la polilinea
	// cacheada solo vale para el destino para el que se consulto) y se fuerza
	// una re-consulta (PathQueryTime=-1) al moverte a un punto nuevo.
	static void MoveTo(CustomBot& Bot, const FVector& Destination, float AcceptanceRadius = 100.0f, bool bSprint = false, bool bRotateTowardsMove = true)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (Bot.MoveRequest.Destination.X != Destination.X
			|| Bot.MoveRequest.Destination.Y != Destination.Y
			|| Bot.MoveRequest.Destination.Z != Destination.Z)
		{
			ClearPath(Bot);
			Bot.PathQueryTime = -1.0f;
		}

		Bot.MoveRequest.Destination = Destination;
		Bot.MoveRequest.AcceptanceRadius = AcceptanceRadius;
		Bot.MoveRequest.MoveSpeed = bSprint ? SprintSpeed : WalkSpeed;
		Bot.MoveRequest.bStopOnArrival = true;
		Bot.bMoveRequestActive = true;
		Bot.MoveState = CBT::EMovementState::Moving;

		ApplyMoveVelocity(Bot, Destination, bSprint ? SprintSpeed : WalkSpeed, bRotateTowardsMove);
	}

	// Limpia la ruta navmesh cacheada del bot (cambio de destino / agotada).
	// NOTA: NO resetea PathQueryTime; asi la re-consulta respeta el cooldown y
	// no se pide el navmesh cada frame cuando la polilinea se agota (la recta
	// final hacia el destino no necesita re-consulta).
	static void ClearPath(CustomBot& Bot)
	{
		Bot.PathWaypoints.clear();
		Bot.PathIndex = 0;
		Bot.bPathFollowBlocked = false;
	}

	// (Re)consulta la ruta hacia FinalDest con throttle (PathQueryCooldown).
	// Primero navmesh (QueryPath) y si no hay, ruta puerta-a-puerta
	// (RouteThroughDoors, "loot -> puertas -> ... -> bot"). La polilinea se
	// guarda en Bot.PathWaypoints para que UpdateMovement la recorra.
	static void RefreshPath(CustomBot& Bot, const FVector& FinalDest)
	{
		float Now = CustomBotPerception::BotTime();

		if (Bot.PathQueryTime > 0.0f && Now - Bot.PathQueryTime < CustomBot::PathQueryCooldown)
			return;

		Bot.PathQueryTime = Now;
		Bot.PathIndex = 0;
		Bot.PathWaypoints.clear();
		Bot.bPathFollowBlocked = false;

		std::vector<FVector> Points;

		if (CustomBotPathfinding::QueryPath(Bot.Pawn->GetActorLocation(), FinalDest, Points))
		{
			Bot.PathWaypoints = std::move(Points);
			return;
		}

		// TODO-PATH (ruta puertas): fallback cuando el navmesh no encuentra
		// camino (interiores de edificios / POIs). Encadena puertas desde el
		// destino hacia el bot para abrirse camino dentro de una casa.
		if (CustomBotPathfinding::RouteThroughDoors(Bot, FinalDest, Points))
			Bot.PathWaypoints = std::move(Points);
	}

	// Tick de movimiento: consume la peticion activa cada frame del servidor.
	//   1. si llegamos al radio de aceptacion -> Arrived (y velocidad a cero)
	//   2. si hay una puerta cerrada delante bloqueando -> abrirla nativamente
	//   3. si el pathfinding esta ON y hay ruta navmesh -> seguir la polilinea
	//      (con re-consulta ciclica vía RefreshPath)
	//   4. si no -> linea recta (fallback): LOS bloqueada marca BlockedPath
	//      (la velocidad sigue aplicandose; el desatascado lo hace CustomBotBreak)
	// TODO-PATH: el bloqueo de la LOS y la ruta conviven: aunque sigas un
	// waypoint, si la LOS esta bloqueada se marca BlockedPath y el
	// CustomBotBreak (fallback) entra a abrir puertas / romper obstaculos.
	static void UpdateMovement(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (!Bot.bMoveRequestActive)
			return;

		const FVector& Destination = Bot.MoveRequest.Destination;
		FVector BotLoc = Bot.Pawn->GetActorLocation();

		if (HorizontalDistance(BotLoc, Destination) <= Bot.MoveRequest.AcceptanceRadius)
		{
			if (auto CharacterMovement = GetCharacterMovement(Bot))
			{
				static auto VelocityOffset = CharacterMovement->GetOffset("Velocity");
				CharacterMovement->Get<FVector>(VelocityOffset) = FVector{};
			}

			ClearPath(Bot);
			Bot.MoveState = CBT::EMovementState::Arrived;
			return;
		}

		// La LOS se refresca como mucho cada ~0.25s: LineTraceSingle por tick con
		// varios bots pesa (los barridos de clase ya estan cacheados en
		// CustomBotPerception). Mientras tanto se usa el ultimo estado.
		if (Bot.MoveLOSTime < 0.0f || CustomBotPerception::BotTime() - Bot.MoveLOSTime >= 0.25f)
		{
			Bot.MoveLOSTime = CustomBotPerception::BotTime();
			Bot.bMoveLOSBlocked = !CustomBotPerception::HasLineOfSight(Bot, Destination);
		}

		// TODO-PATH (puertas): si el camino directo esta bloqueado y hay una
		// puerta cerrada enfrente, abrirla nativamente y seguir avanzando.
		if (Bot.bMoveLOSBlocked)
			CustomBotDoors::TryOpenDoorInFront(Bot);

		// TODO-PATH (navmesh): sigue la polilinea de waypoints si existe. La
		// ruta se consulta (o re-consulta) con throttle en RefreshPath. Con el
		// checkbox de la UI en off (bCustomBotPathfinding=false) se salta todo
		// este bloque y se va en linea recta (lo barato para PC malos).
		if (bCustomBotPathfinding)
		{
			if (Bot.PathWaypoints.empty())
				RefreshPath(Bot, Destination);

			if (!Bot.PathWaypoints.empty() && Bot.PathIndex < (int)Bot.PathWaypoints.size())
			{
				const FVector& Waypoint = Bot.PathWaypoints[Bot.PathIndex];

				if (HorizontalDistance(BotLoc, Waypoint) <= CustomBot::PathWaypointAcceptance)
				{
					// Waypoint alcanzado: avanzar al siguiente (o acabar).
					++Bot.PathIndex;

					if (Bot.PathIndex >= (int)Bot.PathWaypoints.size())
					{
						ClearPath(Bot); // fuera de la polilinea -> recta final
					}
				}

				if (!Bot.PathWaypoints.empty() && Bot.PathIndex < (int)Bot.PathWaypoints.size())
				{
					const FVector& Target = Bot.PathWaypoints[Bot.PathIndex];

					Bot.MoveState = Bot.bMoveLOSBlocked
						? CBT::EMovementState::BlockedPath
						: CBT::EMovementState::Moving;

					ApplyMoveVelocity(Bot, Target, Bot.MoveRequest.MoveSpeed, true);
					return;
				}
			}
		}

		Bot.MoveState = Bot.bMoveLOSBlocked
			? CBT::EMovementState::BlockedPath
			: CBT::EMovementState::Moving;

		ApplyMoveVelocity(Bot, Destination, Bot.MoveRequest.MoveSpeed, true);
	}

	// Movimiento por eje tipo input jugador: mover hacia delante/atras.
	// Value en [-1, 1]; positiva = forward, negativa = backward.
	static void MoveForward(CustomBot& Bot, float Value)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		auto CharacterMovement = GetCharacterMovement(Bot);

		if (!CharacterMovement)
			return;

		FVector Dir = Bot.Pawn->GetActorForwardVector() * Value;

		static auto VelocityOffset = CharacterMovement->GetOffset("Velocity");
		FVector& CharacterVelocity = CharacterMovement->Get<FVector>(VelocityOffset);

		CharacterVelocity.X = Dir.X * WalkSpeed;
		CharacterVelocity.Y = Dir.Y * WalkSpeed;
	}

	// Movimiento por eje tipo input jugador: strafe lateral.
	// Value en [-1, 1]; positiva = right, negativa = left.
	static void MoveRight(CustomBot& Bot, float Value)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		auto CharacterMovement = GetCharacterMovement(Bot);

		if (!CharacterMovement)
			return;

		FVector Dir = Bot.Pawn->GetActorRightVector() * Value;

		static auto VelocityOffset = CharacterMovement->GetOffset("Velocity");
		FVector& CharacterVelocity = CharacterMovement->Get<FVector>(VelocityOffset);

		CharacterVelocity.X = Dir.X * WalkSpeed;
		CharacterVelocity.Y = Dir.Y * WalkSpeed;
	}

	// Salto del pawn (invoca la UFunction nativa Character.Jump).
	static void Jump(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		static auto JumpFn = FindObject<UFunction>(L"/Script/Engine.Character.Jump");
		Bot.Pawn->ProcessEvent(JumpFn);
	}

	// Lanza el pawn (impulso), util para impulsos puntuales.
	static void Launch(CustomBot& Bot, const FVector& LaunchVelocity, bool bXYOverride = false, bool bZOverride = false)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		static auto LaunchCharacterFn = FindObject<UFunction>(L"/Script/Engine.Character.LaunchCharacter");

		struct
		{
			FVector LaunchVelocity;
			bool bXYOverride;
			bool bZOverride;
		} ACharacter_LaunchCharacter_Params{ LaunchVelocity, bXYOverride, bZOverride };

		Bot.Pawn->ProcessEvent(LaunchCharacterFn, &ACharacter_LaunchCharacter_Params);
	}
}
