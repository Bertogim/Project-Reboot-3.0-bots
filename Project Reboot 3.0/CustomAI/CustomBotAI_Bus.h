#pragma once


#include "CustomBotAI.h"

namespace CustomBotAIBus
{
	inline constexpr float SkydiveHorizontalSpeed = 1500.0f;
	inline constexpr float SkydiveMaxDescent = -1200.0f;
	inline constexpr float GlideHorizontalSpeed = 900.0f;

	static void StartSkydive(CustomBot& Bot, BotAIContext& Ctx, const FVector& Target);

	static UObject* CMC(CustomBot& Bot)
	{
		return CustomBotMovement::GetCharacterMovement(Bot);
	}

	static void SetVelocity(CustomBot& Bot, const FVector& Vel)
	{
		auto CM = CMC(Bot);
		if (!CM)
			return;
		static auto VelocityOffset = CM->GetOffset("Velocity");
		CM->Get<FVector>(VelocityOffset) = Vel;
	}

	static FVector GetVelocity(CustomBot& Bot)
	{
		auto CM = CMC(Bot);
		if (!CM)
			return FVector{};
		static auto VelocityOffset = CM->GetOffset("Velocity");
		return CM->Get<FVector>(VelocityOffset);
	}

	static int GetMovementMode(CustomBot& Bot)
	{
		auto CM = CMC(Bot);
		if (!CM)
			return -1;
		int Off = CM->GetOffset("MovementMode", false);
		if (Off == -1)
			return -1;
		return (int)CM->Get<uint8_t>(Off);
	}

	static bool IsInAircraft(CustomBot& Bot)
	{
		if (!Bot.PlayerState)
			return false;
		return Bot.PlayerState->IsInAircraft();
	}

	static FVector LandingTarget(BotAIContext& Ctx, const CustomBot& Bot)
	{
		if (Ctx.bHasLandingPoint)
			return Ctx.LandingPoint;

		FVector P = CustomBotAI::PickLandingPoint(Ctx.Personality.Aggression, Ctx.Personality.RiskTolerance);
		FVector L = P;
		L.Z = FMath::Max(0.0f, L.Z);
		return L;
	}

	// Suelta la posesion y activa el modo de simulacion de servidor (RUNPHYS) para
	// que el CMC siga integrando Velocity/Acceleration durante la caida y el
	// midgame. Se llama al salir del avion (solo si el bot estaba poseido).
	static void ReleaseForSimulation(CustomBot& Bot)
	{
		if (!Bot.bKeepPossessed)
			return;

		Bot.bKeepPossessed = false;

		if (Bot.Controller && Bot.Pawn && Bot.Controller->GetPawn() == Bot.Pawn)
			Bot.Controller->UnPossess();
	}

	static void ReapplySkinViz(CustomBot& Bot)
	{
		if (!Bot.Pawn || !Bot.PlayerState)
			return;

		static auto UpdateVizFn = FindObject<UFunction>(L"/Script/FortniteGame.FortKismetLibrary.UpdatePlayerCustomCharacterPartsVisualization");
		if (UpdateVizFn)
		{
			auto PS = (AFortPlayerState*)Bot.PlayerState;
			UFortKismetLibrary::StaticClass()->ProcessEvent(UpdateVizFn, &PS);
		}

		Bot.PlayerState->ForceNetUpdate();
		Bot.Pawn->ForceNetUpdate();
	}

	static void ForceExitAircraft(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.Pawn)
			return;

		if (Bot.PlayerState)
			Bot.PlayerState->SetInAircraft(false);

		FVector Target = LandingTarget(Ctx, Bot);

		FVector Ground = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
			FVector{ Target.X, Target.Y, 0.0f }, 150000.0f, -30000.0f, FName(0));

		FVector OutLoc = Ground;
		if (OutLoc.Z <= -29000.0f)
			OutLoc = Target;

		Bot.Pawn->TeleportTo(OutLoc, CustomBotMovement::RotationFromDirection(
			CustomBotMovement::DirectionTo(OutLoc, Target)));

		{
			auto CM = CMC(Bot);
			if (CM)
			{
				static auto VelOff = CM->GetOffset("Velocity");
				CM->Get<FVector>(VelOff) = FVector{};
				static auto ModeOff = CM->GetOffset("MovementMode", false);
				if (ModeOff != -1 && CM->Get<uint8_t>(ModeOff) != 1)
					CM->Get<uint8_t>(ModeOff) = 1;
			}
		}

		Ctx.State = EBotState::Looting;
		Ctx.bHasLandingPoint = false;
		Bot.bInAirPhase = false;

		LOG_WARN(LogBots, "[BotBus] FALLBACK: inAircraft cleared, placed at ground ({:.0f},{:.0f},{:.0f})",
			OutLoc.X, OutLoc.Y, OutLoc.Z);
	}

	static void StartLateDrop(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.Pawn)
			return;

		if (Bot.PlayerState)
			Bot.PlayerState->SetInAircraft(false);

		ReleaseForSimulation(Bot);
		Bot.bInAirPhase = true;

		if (!Ctx.bHasLandingPoint)
		{
			Ctx.LandingPoint = CustomBotAI::PickLandingPoint(
				Ctx.Personality.Aggression, Ctx.Personality.RiskTolerance);
			Ctx.bHasLandingPoint = true;
		}

		FVector Target = Ctx.LandingPoint;
		FVector DropPos{ Target.X, Target.Y, 80936.0f };

		Bot.Pawn->TeleportTo(DropPos, Bot.Pawn->GetActorRotation());
		ReapplySkinViz(Bot);
		CustomBotAIMidgame::ResetToMatchHP(Bot);
		StartSkydive(Bot, Ctx, Target);
		Ctx.State = EBotState::Gliding;

		LOG_WARN(LogBots, "[BotBus] late spawn -> altitude drop from ({:.0f},{:.0f},{:.0f}) toward landing",
			DropPos.X, DropPos.Y, DropPos.Z);
	}

	static void StartSkydive(CustomBot& Bot, BotAIContext& Ctx, const FVector& Target)
	{
		if (!Bot.Pawn)
			return;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Target);

		FVector Vel{Dir.X * SkydiveHorizontalSpeed, Dir.Y * SkydiveHorizontalSpeed, SkydiveMaxDescent};
		SetVelocity(Bot, Vel);

		CustomBotMovement::LookAt(Bot, Target);

		Ctx.bFiredGlider = false;
		LOG_INFO(LogBots, "[BotBus] skydive started toward ({:.0f},{:.0f}) desc={:.0f}",
			Target.X, Target.Y, SkydiveMaxDescent);
	}

	static void Glide(CustomBot& Bot, BotAIContext& Ctx, const FVector& Target)
	{
		if (!Bot.Pawn)
			return;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Vel = GetVelocity(Bot);
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Target);

		FVector NewVel{Dir.X * GlideHorizontalSpeed, Dir.Y * GlideHorizontalSpeed, Vel.Z};

		if (NewVel.Z < SkydiveMaxDescent)
			NewVel.Z = SkydiveMaxDescent;

		if (NewVel.Z > -50.0f)
			NewVel.Z = -50.0f;

		CustomBotMovement::LookAt(Bot, Target);
		SetVelocity(Bot, NewVel);
		Ctx.bFiredGlider = true;
	}

	static void OnLanded(CustomBot& Bot, BotAIContext& Ctx)
	{
		Bot.bInAirPhase = false;
		CustomBotMovement::StopMovement(Bot);
		ReapplySkinViz(Bot);
		Ctx.State = EBotState::Looting;
		Ctx.bHasLandingPoint = false;
		LOG_INFO(LogBots, "[BotBus] landed -> Looting");
	}

	static float AircraftPhaseStart()
	{
		static float Start = -1.0f;

		if (CustomBotAI::IsInAircraftPhase())
		{
			if (Start < 0.0f)
				Start = CustomBotPerception::BotTime();
		}
		else
		{
			Start = -1.0f;
		}

		return Start;
	}

	static void CustomEject(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.Pawn)
			return;

		if (Bot.PlayerState)
			Bot.PlayerState->SetInAircraft(false);

		ReleaseForSimulation(Bot);
		Bot.bInAirPhase = true;

		FVector BusPos{-47557.0f, -62295.0f, 80936.0f};

		if (AActor* Aircraft = CustomBotAI::GetAircraft())
		{
			FVector A = Aircraft->GetActorLocation();
			if (A.Z > 0.0f && (A | A) != 0.0f)
				BusPos = FVector{A.X, A.Y, A.Z + 200.0f};
		}

		Bot.Pawn->TeleportTo(BusPos, Bot.Pawn->GetActorRotation());
		ReapplySkinViz(Bot);
		CustomBotAIMidgame::ResetToMatchHP(Bot);

		if (!Ctx.bHasLandingPoint)
		{
			Ctx.LandingPoint = CustomBotAI::PickLandingPoint(
				Ctx.Personality.Aggression, Ctx.Personality.RiskTolerance);
			Ctx.bHasLandingPoint = true;
		}

		StartSkydive(Bot, Ctx, LandingTarget(Ctx, Bot));
		Ctx.State = EBotState::Gliding;

		LOG_WARN(LogBots, "[BotBus] custom eject to ({:.0f},{:.0f},{:.0f}) toward landing",
			BusPos.X, BusPos.Y, BusPos.Z);
	}

	static void Update(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (!CustomBotAI::IsInAircraftPhase())
		{
			if (Ctx.State == EBotState::InBus)
			{
				if (IsInAircraft(Bot))
				{
					LOG_WARN(LogBots, "[BotBus] aircraft phase ended while on bus, forcing ground placement");
					ForceExitAircraft(Bot, Ctx);
				}
				else
				{
					AFortGameStateAthena* GS = CustomBotAI::GetGameState();
					EAthenaGamePhase Phase = GS ? GS->GetGamePhase() : EAthenaGamePhase::None;

					if (Phase == EAthenaGamePhase::Warmup)
					{
						Ctx.State = EBotState::Warmup;
						Ctx.bHasLandingPoint = false;
					}
					else
					{
						StartLateDrop(Bot, Ctx);
					}
				}

				return;
			}

			if (Ctx.State == EBotState::Ejecting)
			{
				StartLateDrop(Bot, Ctx);
			}
		}

		switch (Ctx.State)
		{
		case EBotState::InBus:
		{
			float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

			if (Ctx.InBusSince < 0.0f)
			{
				Ctx.InBusSince = Now;

				float Stagger = 15.0f + float(std::rand() % 3001) / 100.0f;

				if (CustomBotAI::IsInAircraftPhase() && Ctx.InBusSince - AircraftPhaseStart() > 10.0f)
					Stagger = FMath::Max(1.0f, 15.0f - (Ctx.InBusSince - AircraftPhaseStart()));

				Ctx.JumpDelay = Now + Stagger;
			}

			if (UGameplayStatics::GetTimeSeconds(GetWorld()) >= Ctx.JumpDelay)
				Ctx.State = EBotState::Ejecting;

			break;
		}

		case EBotState::Ejecting:
		{
			if (!Ctx.bHasLandingPoint)
			{
				Ctx.LandingPoint = CustomBotAI::PickLandingPoint(
					Ctx.Personality.Aggression, Ctx.Personality.RiskTolerance);
				Ctx.bHasLandingPoint = true;
			}

			CustomEject(Bot, Ctx);
			break;
		}

		case EBotState::Gliding:
		case EBotState::Landing:
		{
			FVector Target = LandingTarget(Ctx, Bot);
			Glide(Bot, Ctx, Target);

			if (GetMovementMode(Bot) == 1)
			{
				OnLanded(Bot, Ctx);
			}

			break;
		}

		default:
			break;
		}
	}
}