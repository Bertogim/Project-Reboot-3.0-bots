#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"

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

	// Aplica el FIX RUNPHYS (research 08): convierte al bot en un pawn sin controller
	// con bRunPhysicsWithNoController=true para que el servidor SIMULE su CMC (la rama
	// "sin controller" integra Velocity/Acceleration cada frame). Sin esto el pawn
	// poseido por un PlayerController sin cliente conectado NUNCA se simula.
	// Debe aplicarse en el spawn de TODO bot (CustomBotSpawner::SpawnCustomBot).
	static bool EnableServerSimulation(CustomBot& Bot)
	{
		if (!Bot.PlayerState || !Bot.Controller || !Bot.Pawn)
			return false;

		Bot.PlayerState->SetIsBot(false);
		Bot.Controller->UnPossess();

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

		LOG_WARN(LogBots, "[CustomBot] RUNPHYS-FIX: IsBot=false, pose released, bRunPhysicsWithNoController set={} (possessor={})",
			bBitOK, Bot.Pawn->GetController() != nullptr);

		return bBitOK;
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

	// Rota el pawn (y su control) hacia una rotacion concreta.
	static void SetRotation(CustomBot& Bot, const FRotator& Rotation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		Bot.Pawn->TeleportTo(Bot.Pawn->GetActorLocation(), Rotation);
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

		// Sincronizar velocidades maximas del CM: algunos pawns spawnean con
		// MaxWalkSpeed=0 (visto en RealVsBot: real=550 bot=0) y el CharacterMovement
		// no mueve nada. Se reafirma una vez (estatico) y es barato.
		static bool bSpeedSynced = false;
		if (!bSpeedSynced)
		{
			auto SetFloatIfPresent = [&](const char* Name, float Value) {
				int Off = CharacterMovement->GetOffset(Name, false);
				if (Off != -1) *(float*)(__int64(CharacterMovement) + Off) = Value;
			};
			SetFloatIfPresent("MaxWalkSpeed", WalkSpeed);
			SetFloatIfPresent("MaxWalkSpeedCrouched", WalkSpeed);
			SetFloatIfPresent("MaxFlySpeed", WalkSpeed);
			SetFloatIfPresent("MaxAcceleration", 2048.0f);
			bSpeedSynced = true;
		}

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
	static void MoveTo(CustomBot& Bot, const FVector& Destination, float AcceptanceRadius = 100.0f, bool bSprint = false, bool bRotateTowardsMove = true)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		Bot.MoveRequest.Destination = Destination;
		Bot.MoveRequest.AcceptanceRadius = AcceptanceRadius;
		Bot.MoveRequest.bStopOnArrival = true;
		Bot.bMoveRequestActive = true;
		Bot.MoveState = CBT::EMovementState::Moving;

		ApplyMoveVelocity(Bot, Destination, bSprint ? SprintSpeed : WalkSpeed, bRotateTowardsMove);
	}

	// Tick de movimiento: consume la peticion activa cada frame del servidor.
	//   1. si llegamos al radio de aceptacion -> Arrived (y velocidad a cero)
	//   2. si la LOS hacia el destino esta bloqueada -> BlockedPath (informativo;
	//      la velocidad sigue aplicandose para no detener el avance)
	//   3. si no -> Moving y re-aplica la velocidad hacia el destino
	static void UpdateMovement(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (!Bot.bMoveRequestActive)
			return;

		const FVector& Destination = Bot.MoveRequest.Destination;

		if (HorizontalDistance(Bot.Pawn->GetActorLocation(), Destination) <= Bot.MoveRequest.AcceptanceRadius)
		{
			if (auto CharacterMovement = GetCharacterMovement(Bot))
			{
				static auto VelocityOffset = CharacterMovement->GetOffset("Velocity");
				CharacterMovement->Get<FVector>(VelocityOffset) = FVector{};
			}

			Bot.MoveState = CBT::EMovementState::Arrived;
			return;
		}

		// Informacion para la futura IA: si el camino directo esta bloqueado
		// (estructuras, montanas...), se marca BlockedPath sin detener el avance.
		Bot.MoveState = CustomBotPerception::HasLineOfSight(Bot, Destination)
			? CBT::EMovementState::Moving
			: CBT::EMovementState::BlockedPath;

		ApplyMoveVelocity(Bot, Destination, WalkSpeed, true);
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
