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

	static UObject* GetCharacterMovement(APawn* InPawn)
	{
		if (!InPawn)
			return nullptr;

		static auto CharacterMovementOffset = InPawn->GetOffset("CharacterMovement");
		return InPawn->Get(CharacterMovementOffset);
	}

	static void SetActorHiddenInGame(UObject* Actor, bool bHidden)
	{
		if (!Actor)
			return;

		static auto Fn = FindObject<UFunction>(L"/Script/Engine.Actor.SetActorHiddenInGame");

		if (!Fn)
			return;

		static auto Off = FindOffsetStruct("/Script/Engine.Actor.SetActorHiddenInGame", "bNewHidden", false);

		if (Off == -1)
			return;

		auto Params = Alloc(Fn->GetPropertiesSize());

		*(bool*)(__int64(Params) + Off) = bHidden;

		Actor->ProcessEvent(Fn, Params);

		VirtualFree(Params, 0, MEM_RELEASE);
	}

	static void IgnoreEachOther(AActor* A, AActor* B)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/Engine.Actor.SetIgnoreActorWhenMoving");

		if (!Fn || !A || !B)
			return;

		static auto ActorOff = FindOffsetStruct("/Script/Engine.Actor.SetIgnoreActorWhenMoving", "Actor", false);
		static auto bShouldIgnoreOff = FindOffsetStruct("/Script/Engine.Actor.SetIgnoreActorWhenMoving", "bShouldIgnore", false);

		if (ActorOff == -1 || bShouldIgnoreOff == -1)
			return;

		{
			auto Params = Alloc(Fn->GetPropertiesSize());
			*(AActor**)(__int64(Params) + ActorOff) = B;
			*(bool*)(__int64(Params) + bShouldIgnoreOff) = true;
			A->ProcessEvent(Fn, Params);
			VirtualFree(Params, 0, MEM_RELEASE);
		}

		{
			auto Params = Alloc(Fn->GetPropertiesSize());
			*(AActor**)(__int64(Params) + ActorOff) = A;
			*(bool*)(__int64(Params) + bShouldIgnoreOff) = true;
			B->ProcessEvent(Fn, Params);
			VirtualFree(Params, 0, MEM_RELEASE);
		}
	}

	static void SetActorEnableCollision(UObject* Actor, bool bEnabled)
	{
		if (!Actor)
			return;

		static auto Fn = FindObject<UFunction>(L"/Script/Engine.Actor.SetActorEnableCollision");

		if (!Fn)
			return;

		static auto Off = FindOffsetStruct("/Script/Engine.Actor.SetActorEnableCollision", "bNewCollisionEnabled", false);

		if (Off == -1)
			return;

		auto Params = Alloc(Fn->GetPropertiesSize());

		*(bool*)(__int64(Params) + Off) = bEnabled;

		Actor->ProcessEvent(Fn, Params);

		VirtualFree(Params, 0, MEM_RELEASE);
	}

	static void DisableComponentTick(UObject* Comp)
	{
		if (!Comp)
			return;

		static auto Fn = FindObject<UFunction>(L"/Script/Engine.ActorComponent.SetComponentTickEnabled");

		if (!Fn)
			return;

		static auto Off = FindOffsetStruct("/Script/Engine.ActorComponent.SetComponentTickEnabled", "bEnabled", false);

		if (Off == -1)
			return;

		auto Params = Alloc(Fn->GetPropertiesSize());

		*(bool*)(__int64(Params) + Off) = false;

		Comp->ProcessEvent(Fn, Params);

		VirtualFree(Params, 0, MEM_RELEASE);
	}

	static void SetWorldRotation(UObject* Actor, const FRotator& Rot)
	{
		if (!Actor)
			return;

		int RootOff = Actor->GetOffset("RootComponent", false);

		if (RootOff == -1)
			return;

		auto Root = (UObject*)Actor->Get(RootOff);

		if (!Root)
			return;

		static auto K2_SetWorldRotationFn = FindObject<UFunction>(L"/Script/Engine.SceneComponent.K2_SetWorldRotation");

		if (!K2_SetWorldRotationFn)
			return;

		static auto NewRotationOffset = FindOffsetStruct("/Script/Engine.SceneComponent.K2_SetWorldRotation", "NewRotation", false);
		static auto bSweepOffset = FindOffsetStruct("/Script/Engine.SceneComponent.K2_SetWorldRotation", "bSweep", false);
		static auto bTeleportOffset = FindOffsetStruct("/Script/Engine.SceneComponent.K2_SetWorldRotation", "bTeleport", false);

		if (NewRotationOffset == -1 || bSweepOffset == -1 || bTeleportOffset == -1)
			return;

		auto Params = Alloc(K2_SetWorldRotationFn->GetPropertiesSize());

		*(FRotator*)(__int64(Params) + NewRotationOffset) = Rot;
		*(bool*)(__int64(Params) + bSweepOffset) = false;
		*(bool*)(__int64(Params) + bTeleportOffset) = false;

		Root->ProcessEvent(K2_SetWorldRotationFn, Params);

		VirtualFree(Params, 0, MEM_RELEASE);
	}

	static void SetupCosmeticFollower(CustomBot& Bot)
	{
		if (!Bot.CosmeticPawn)
			return;

		SetActorEnableCollision(Bot.CosmeticPawn, false);

		if (auto* CME = GetCharacterMovement(Bot.CosmeticPawn))
			DisableComponentTick(CME);
	}

	static bool SetupGhostSim(AFortPlayerControllerAthena* Ctrl, AFortPlayerPawnAthena* Pawn)
	{
		if (!Ctrl || !Pawn)
			return false;

		if (Ctrl->GetPawn() == Pawn)
			Ctrl->UnPossess();

		if (auto* PS = (UObject*)Ctrl->GetPlayerState())
		{
			int PSOff = Pawn->GetOffset("PlayerState", false);

			if (PSOff != -1 && Pawn->Get<UObject*>(PSOff) != PS)
				Pawn->Get<UObject*>(PSOff) = PS;
		}

		bool bBitOK = false;

		if (auto* CMR = GetCharacterMovement(Pawn))
		{
			auto* Prop = CMR->GetProperty("bRunPhysicsWithNoController");
			int Off = CMR->GetOffset("bRunPhysicsWithNoController", false);
			if (Prop && Off != -1)
			{
				CMR->SetBitfieldValue(Off, GetFieldMask(Prop), true);
				bBitOK = true;
			}
		}

		return bBitOK;
	}

	static void SetupCosmeticSim(CustomBot& Bot)
	{
		if (!Bot.Controller || !Bot.CosmeticPawn)
			return;

		if (Bot.Controller->GetPawn() == Bot.CosmeticPawn)
			Bot.Controller->UnPossess();

		if (auto* PS = (UObject*)Bot.Controller->GetPlayerState())
		{
			int PSOff = Bot.CosmeticPawn->GetOffset("PlayerState", false);

			if (PSOff != -1 && Bot.CosmeticPawn->Get<UObject*>(PSOff) != PS)
				Bot.CosmeticPawn->Get<UObject*>(PSOff) = PS;
		}
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
			if (bDoClaim && !Bot.bClaimLiveDone && Bot.Controller)
			{
				static auto AckFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ServerAcknowledgePossession");
				if (AckFn)
				{
					struct { APawn* NewPawn; } Params{};
					Params.NewPawn = Bot.CosmeticPawn ? Bot.CosmeticPawn : Bot.Pawn;
					Bot.Controller->ProcessEvent(AckFn, &Params);
				}

				auto AckOff = Bot.Controller->GetOffset("AcknowledgedPawn", false);
				auto* DesiredAckPawn = Bot.CosmeticPawn ? Bot.CosmeticPawn : Bot.Pawn;
				if (AckOff != -1 && Bot.Controller->Get<APawn*>(AckOff) != DesiredAckPawn)
					Bot.Controller->Get<APawn*>(AckOff) = DesiredAckPawn;

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

		if (Bot.CosmeticPawn)
			Bot.CosmeticPawn->ForceNetUpdate();

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

	static void SyncHealth(CustomBot& Bot)
	{
		if (!Bot.Pawn || !Bot.CosmeticPawn)
			return;

		float MoveH = Bot.Pawn->GetHealth();
		float MoveS = Bot.Pawn->GetShield();
		float CosH = Bot.CosmeticPawn->GetHealth();
		float CosS = Bot.CosmeticPawn->GetShield();

		float H = FMath::Min(MoveH, CosH);
		float S = FMath::Min(MoveS, CosS);

		if (FMath::Abs(MoveH - H) > 0.01f || FMath::Abs(MoveS - S) > 0.05f)
		{
			Bot.Pawn->SetHealth(H);
			Bot.Pawn->SetShield(S);
		}

		if (FMath::Abs(CosH - H) > 0.01f || FMath::Abs(CosS - S) > 0.05f)
		{
			Bot.CosmeticPawn->SetHealth(H);
			Bot.CosmeticPawn->SetShield(S);
		}
	}

	static void SyncCosmetic(CustomBot& Bot)
	{
		if (!Bot.CosmeticPawn)
			return;

		if (!Bot.CosmeticPawn->IsActorBeingDestroyed() && Bot.Pawn && !Bot.Pawn->IsActorBeingDestroyed())
		{
			SetActorHiddenInGame(Bot.Pawn, true);

			auto* SrcCM = GetCharacterMovement(Bot.Pawn);
			auto* DstCM = GetCharacterMovement(Bot.CosmeticPawn);

			static int SrcVelOff = SrcCM ? SrcCM->GetOffset("Velocity", false) : -1;
			static int DstVelOff = DstCM ? DstCM->GetOffset("Velocity", false) : -1;
			static int SrcAccOff = SrcCM ? SrcCM->GetOffset("Acceleration", false) : -1;
			static int DstAccOff = DstCM ? DstCM->GetOffset("Acceleration", false) : -1;
			static int SrcModeOff = SrcCM ? SrcCM->GetOffset("MovementMode", false) : -1;
			static int DstModeOff = DstCM ? DstCM->GetOffset("MovementMode", false) : -1;

			static auto ClearAccumulatedForcesFn = FindObject<UFunction>(L"/Script/Engine.MovementComponent.ClearAccumulatedForces");

			if (SrcCM && DstCM)
			{
				FVector SrcVel{};
				if (SrcVelOff != -1)
					SrcVel = SrcCM->Get<FVector>(SrcVelOff);

				FVector SrcAcc{};
				if (SrcAccOff != -1)
					SrcAcc = SrcCM->Get<FVector>(SrcAccOff);

				bool bGhostIdle = (SrcVel | SrcVel) < 4.0f && (SrcAcc | SrcAcc) < 4.0f;

				int SrcMode = SrcModeOff != -1 ? SrcCM->Get<int>(SrcModeOff) : -1;

				if (SrcMode != -1 && DstModeOff != -1)
					DstCM->Get<int>(DstModeOff) = SrcMode;

				if (DstVelOff != -1)
					DstCM->Get<FVector>(DstVelOff) = SrcVel;

				if (DstAccOff != -1)
					DstCM->Get<FVector>(DstAccOff) = SrcAcc;

				if (bGhostIdle)
				{
					if (DstAccOff != -1)
						DstCM->Get<FVector>(DstAccOff) = FVector{ 0.0f, 0.0f, 0.0f };

					if (DstVelOff != -1)
						DstCM->Get<FVector>(DstVelOff) = FVector{ 0.0f, 0.0f, 0.0f };

					if (ClearAccumulatedForcesFn)
						DstCM->ProcessEvent(ClearAccumulatedForcesFn);
				}
			}

			FRotator MoveRot = Bot.Pawn->GetActorRotation();
			FRotator CosRot = Bot.CosmeticPawn->GetActorRotation();

			float Dy = FMath::Abs(CosRot.Yaw - MoveRot.Yaw);
			if (Dy > 180.0f)
				Dy = 360.0f - Dy;

			float Dp = FMath::Abs(CosRot.Pitch - MoveRot.Pitch);
			float Dr = FMath::Abs(CosRot.Roll - MoveRot.Roll);

			if (Dp > 0.5f || Dy > 0.5f || Dr > 0.5f)
				SetWorldRotation(Bot.CosmeticPawn, MoveRot);

			FVector MoveLoc = Bot.Pawn->GetActorLocation();
			FVector CosLoc = Bot.CosmeticPawn->GetActorLocation();

			FVector Delta = MoveLoc - CosLoc;

			if ((Delta | Delta) > 0.0004f)
			{
				Bot.CosmeticPawn->TeleportTo(MoveLoc, MoveRot);

				if (DstCM && SrcVelOff != -1 && DstVelOff != -1 && SrcCM)
					DstCM->Get<FVector>(DstVelOff) = SrcCM->Get<FVector>(SrcVelOff);
			}
		}

		SyncHealth(Bot);
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
