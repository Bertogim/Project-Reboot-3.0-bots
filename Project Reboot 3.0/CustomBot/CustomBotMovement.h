#pragma once

#include "CustomBot.h"

#include "CustomBotPerception.h"

#include "CustomBotPathfinding.h"
#include "CustomBotDoors.h"

#include <chrono>
#include <utility>


namespace CustomBotMovement
{
	inline constexpr float WalkSpeed = 600.0f;
	inline constexpr float SprintSpeed = 900.0f;
	inline constexpr float JumpStrength = 500.0f;

	constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

	static void SetRotation(CustomBot& Bot, const FRotator& Rotation);

	static void ClearPath(CustomBot& Bot);

	static FVector DirectionTo(const FVector& From, const FVector& To)
	{
		FVector Delta = To - From;

		float LenSq = Delta | Delta;

		if (LenSq <= 0.0001f)
			return FVector{};

		float InvLen = 1.0f / FMath::Sqrt(LenSq);
		return Delta * InvLen;
	}

	static float HorizontalDistance(const FVector& A, const FVector& B)
	{
		float DX = B.X - A.X;
		float DY = B.Y - A.Y;
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	static float Distance(const FVector& A, const FVector& B)
	{
		FVector Delta = B - A;
		return FMath::Sqrt(Delta | Delta);
	}

	static FRotator RotationFromDirection(const FVector& Dir)
	{
		FRotator Rot{};

		Rot.Yaw = FMath::Atan2(Dir.Y, Dir.X) * RAD_TO_DEG;
		Rot.Pitch = FMath::Atan2(Dir.Z, FMath::Sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y)) * RAD_TO_DEG;
		Rot.Roll = 0.0f;

		return Rot;
	}

	static UObject* GetCharacterMovement(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return nullptr;

		static auto CharacterMovementOffset = Bot.Pawn->GetOffset("CharacterMovement");
		return Bot.Pawn->Get(CharacterMovementOffset);
	}

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
	// Los bots SIEMPRE se desposeen; el servidor simula el CMC sin controller.
	static bool EnableServerSimulation(CustomBot& Bot)
	{
		if (!Bot.PlayerState || !Bot.Controller || !Bot.Pawn)
			return false;

		Bot.PlayerState->SetIsBot(false);

		if (Bot.Controller->GetPawn() == Bot.Pawn)
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

	static void EnsureCMCActive(CustomBot& Bot, bool bPerTickWork = true, bool bDoClaim = true)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		auto* CME = GetCharacterMovement(Bot);
		if (!CME)
			return;

		__int64 CMEAddr = __int64(CME);

		if (!Bot.bCMCInitialized)
		{
			RestorePawnPlayerState(Bot);

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

			static auto FnSetTick = FindObject<UFunction>(L"/Script/Engine.ActorComponent.SetComponentTickEnabled");
			static auto FnActivate = FindObject<UFunction>(L"/Script/Engine.ActorComponent.Activate");
			if (FnSetTick) { struct { char Buf[32]; } P{}; P.Buf[0] = 1; CME->ProcessEvent(FnSetTick, &P); }
			if (FnActivate) { struct { char Buf[32]; } P{}; P.Buf[0] = 1; CME->ProcessEvent(FnActivate, &P); }

			Bot.bCMCInitialized = true;
			LOG_INFO(LogBots, "[CustomBot] CMC initialized (one-time setup done)");
		}


		if (!bPerTickWork)
			return;

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

		bool bInAircraft = Bot.PlayerState && Bot.PlayerState->IsInAircraft();

		if (!bInAircraft && !Bot.bInAirPhase)
		{
			static int ModeOff = CME->GetOffset("MovementMode", false);
			if (ModeOff != -1 && *(int*)(CMEAddr + ModeOff) != 1)
			{
				*(int*)(CMEAddr + ModeOff) = 1;
				static int GroundOff = CME->GetOffset("GroundMovementMode", false);
				if (GroundOff != -1) *(int*)(CMEAddr + GroundOff) = 1;
			}

			if (Bot.GroundGravityZ != 0.0f || Bot.GroundGravityScale != 0.0f)
			{
				static int GravZOff = CME->GetOffset("GravityZ", false);
				static int GravScaleOff = CME->GetOffset("GravityScale", false);
				if (GravZOff != -1 && Bot.GroundGravityZ != 0.0f)
					*(float*)(CMEAddr + GravZOff) = Bot.GroundGravityZ;
				if (GravScaleOff != -1 && Bot.GroundGravityScale != 0.0f)
					*(float*)(CMEAddr + GravScaleOff) = Bot.GroundGravityScale;
			}
		}

		{
			static int bSimGravityDisabledOff = Bot.Pawn->GetOffset("bSimGravityDisabled", false);
			static int bDisableMovementOff = Bot.Pawn->GetOffset("bDisableMovementAndTurnInPlace", false);
			static int bAllowMovementOff = Bot.Pawn->GetOffset("bAllowMovement", false);
			if (bSimGravityDisabledOff != -1) *(uint8_t*)(__int64(Bot.Pawn) + bSimGravityDisabledOff) = 0;
			if (bDisableMovementOff != -1) *(uint8_t*)(__int64(Bot.Pawn) + bDisableMovementOff) = 0;
			if (bAllowMovementOff != -1) *(uint8_t*)(__int64(Bot.Pawn) + bAllowMovementOff) = 1;
		}

		{
			auto& NetFreq = Bot.Pawn->GetNetUpdateFrequency();
			if (NetFreq < 50.0f)
				NetFreq = 50.0f;
			auto& MinNetFreq = Bot.Pawn->GetMinNetUpdateFrequency();
			if (MinNetFreq < 50.0f)
				MinNetFreq = 50.0f;
		}

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

	static void LookAt(CustomBot& Bot, const FVector& TargetLocation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FVector Dir = DirectionTo(Bot.Pawn->GetActorLocation(), TargetLocation);

		if (Dir | Dir)
		{
			SetRotation(Bot, RotationFromDirection(Dir));
		}
	}

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

	static void SetRotation(CustomBot& Bot, const FRotator& Rotation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FRotator Final = Rotation;
		if (!Bot.bFiringWeapon)
			Final.Pitch = FMath::Clamp(Final.Pitch, -20.0f, 20.0f);

		if (Bot.Controller)
		{
			static auto SetControlRotationFn = FindObject<UFunction>(L"/Script/Engine.Controller.SetControlRotation");

			if (SetControlRotationFn)
			{
				static auto NewRotationOffset = FindOffsetStruct("/Script/Engine.Controller.SetControlRotation", "NewRotation");

				auto Params = Alloc(SetControlRotationFn->GetPropertiesSize());

				*(FRotator*)(__int64(Params) + NewRotationOffset) = Final;

				Bot.Controller->ProcessEvent(SetControlRotationFn, Params);

				VirtualFree(Params, 0, MEM_RELEASE);
			}
		}

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

			*(FRotator*)(__int64(Params) + NewRotationOffset) = Final;
			*(bool*)(__int64(Params) + bSweepOffset) = false;
			*(bool*)(__int64(Params) + bTeleportOffset) = false;

			Root->ProcessEvent(K2_SetWorldRotationFn, Params);

			VirtualFree(Params, 0, MEM_RELEASE);
		}
	}

	static void SetYaw(CustomBot& Bot, float YawDegrees)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		FRotator Current = Bot.Pawn->GetActorRotation();
		Current.Yaw = YawDegrees;
		SetRotation(Bot, Current);
	}

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

		Bot.StuckWindowTime = -1.0f;
		Bot.StuckWindowPos = FVector{};
		Bot.StuckTime = 0.0f;
	}

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

		CharacterVelocity.X = NewVelocity.X;
		CharacterVelocity.Y = NewVelocity.Y;

		FVector& CharacterAcceleration = CharacterMovement->Get<FVector>(AccelerationOffset);
		CharacterAcceleration.X = Dir.X * Speed;
		CharacterAcceleration.Y = Dir.Y * Speed;
	}

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

			Bot.StuckWindowTime = -1.0f;
			Bot.StuckWindowPos = FVector{};
			Bot.StuckTime = 0.0f;

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
			Bot.UnstickTarget = nullptr;

			Bot.StuckPersistTime = 0.0f;
			Bot.StuckPersistGoal = FVector{};
			Bot.StuckPersistBest = 1e30f;
		}

		Bot.MoveRequest.Destination = Destination;
		Bot.MoveRequest.AcceptanceRadius = AcceptanceRadius;
		Bot.MoveRequest.MoveSpeed = bSprint ? SprintSpeed : WalkSpeed;
		Bot.MoveRequest.bStopOnArrival = true;
		Bot.bMoveRequestActive = true;
		Bot.MoveState = CBT::EMovementState::Moving;

		ApplyMoveVelocity(Bot, Destination, bSprint ? SprintSpeed : WalkSpeed, bRotateTowardsMove);
	}

	static void ClearPath(CustomBot& Bot)
	{
		Bot.PathWaypoints.clear();
		Bot.PathIndex = 0;
		Bot.bPathFollowBlocked = false;
	}

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

		if (CustomBotPathfinding::RouteThroughDoors(Bot, FinalDest, Points))
			Bot.PathWaypoints = std::move(Points);
	}

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

			Bot.StuckPersistTime = 0.0f;
			Bot.StuckPersistBest = 1e30f;
			return;
		}

		if (Bot.MoveLOSTime < 0.0f || CustomBotPerception::BotTime() - Bot.MoveLOSTime >= 0.25f)
		{
			Bot.MoveLOSTime = CustomBotPerception::BotTime();
			Bot.bMoveLOSBlocked = !CustomBotPerception::HasLineOfSight(Bot, Destination);
		}

		if (Bot.bMoveLOSBlocked)
			CustomBotDoors::TryOpenDoorInFront(Bot);

		const bool bUseNavPath = bCustomBotPathfinding ||
			(bCustomBotPathfindingFallback && Bot.StuckPersistTime >= CustomBot::PathfindingFallbackStuckTime);

		if (bUseNavPath)
		{
			if (Bot.PathWaypoints.empty())
				RefreshPath(Bot, Destination);

			if (!Bot.PathWaypoints.empty() && Bot.PathIndex < (int)Bot.PathWaypoints.size())
			{
				const FVector& Waypoint = Bot.PathWaypoints[Bot.PathIndex];

				if (HorizontalDistance(BotLoc, Waypoint) <= CustomBot::PathWaypointAcceptance)
				{
					++Bot.PathIndex;

					if (Bot.PathIndex >= (int)Bot.PathWaypoints.size())
					{
						ClearPath(Bot);
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

	// Salto del pawn. Se aplica UN impulso real a la fisica (LaunchCharacter +
	// patada de Velocity.Z en el CMC) y ADEMAS el Character.Jump nativo (para la
	// animacion/estado). Los bots corren bajo simulacion de servidor (UnPossess +
	// bRunPhysicsWithNoController) y no tienen su Actor Tick del Character
	// garantizado: Character::Jump solo marca bPressedJump para que ese Tick lo
	// convierta en salto, asi que se quedaban "pegados" al suelo (observado:
	// bots atascados 10s sin saltar en la etapa 3 del desatascado). El impulso
	// del CMC (PendingLaunchVelocity / Velocity) lo consume la fisica siempre.
	static void Jump(CustomBot& Bot)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		auto CharacterMovement = GetCharacterMovement(Bot);

		static auto LaunchCharacterFn = FindObject<UFunction>(L"/Script/Engine.Character.LaunchCharacter");

		if (LaunchCharacterFn)
		{
			struct
			{
				FVector LaunchVelocity;
				bool bXYOverride;
				bool bZOverride;
			} Params{ FVector{ 0.0f, 0.0f, JumpStrength }, false, true };

			Bot.Pawn->ProcessEvent(LaunchCharacterFn, &Params);
		}

		if (CharacterMovement)
		{
			static auto VelocityOffset = CharacterMovement->GetOffset("Velocity", false);

			if (VelocityOffset != -1)
			{
				FVector& Velocity = CharacterMovement->Get<FVector>(VelocityOffset);
				Velocity.Z = FMath::Max(Velocity.Z, JumpStrength);
			}
		}

		static auto JumpFn = FindObject<UFunction>(L"/Script/Engine.Character.Jump");
		if (JumpFn)
			Bot.Pawn->ProcessEvent(JumpFn);
	}

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
