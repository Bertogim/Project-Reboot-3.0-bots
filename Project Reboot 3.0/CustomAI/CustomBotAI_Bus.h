#pragma once

// CustomBot AI - Battle Bus phase (Parte 2).
//
// Convierte al bot en un pasajero del Battle Bus y luego en un jugador que salta,
// planea y aterriza. Ya NO teletransporta constantemente hacia el bus: el juego
// monta al bot automaticamente (bInAircraft). El bot solo:
//   1) espera su momento aleatorio para saltar (JumpDelay),
//   2) envia la senal NATIVA de salto (Character::Jump, igual que un jugador real
//      pulsando Espacio / igual que el sistema antiguo bots.h:440),
//   3) si a los 5s sigue inAircraft -> FALLBACK: lo saca a mano del autobus y loguea,
//   4) controla la caida como un SKYDIVE controlado hacia la zona elegida
//      (velocidad horizontal + descenso limitado; NO es caer desde un edificio),
//   5) el motor despliega la ala delta automaticamente cerca del suelo,
//   6) cuando el CMC vuelve a Walking -> ha aterrizado -> Looting.
//
// Nota: el motor de fisicas SOBREESCRIBE el movement mode del bot constantemente.
// No se setea MovementMode aqui: si esta en el aire estara Falling automaticamente.

#include "CustomBotAI.h"

namespace CustomBotAIBus
{
	// Velocidades de referencia para el skydive controlado / planeo.
	inline constexpr float SkydiveHorizontalSpeed = 1500.0f; // avance rapido hacia el destino
	inline constexpr float SkydiveMaxDescent = -1200.0f;     // descenso de skydive (NO caida libre)
	inline constexpr float GlideHorizontalSpeed = 900.0f;    // planeo (el glider ya desplegado)

	// Lee el CharacterMovement del bot (UObject*).
	static UObject* CMC(CustomBot& Bot)
	{
		return CustomBotMovement::GetCharacterMovement(Bot);
	}

	// Escribe la velocidad del CM (el servidor integra Velocity/Acceleration cada frame).
	static void SetVelocity(CustomBot& Bot, const FVector& Vel)
	{
		auto CM = CMC(Bot);
		if (!CM)
			return;
		static auto VelocityOffset = CM->GetOffset("Velocity");
		CM->Get<FVector>(VelocityOffset) = Vel;
	}

	// Lee la velocidad actual del CM.
	static FVector GetVelocity(CustomBot& Bot)
	{
		auto CM = CMC(Bot);
		if (!CM)
			return FVector{};
		static auto VelocityOffset = CM->GetOffset("Velocity");
		return CM->Get<FVector>(VelocityOffset);
	}

	// Lee el movement mode del CMC (1=Walking, 3=Falling) o -1 si no disponible.
	// El motor de fisicas lo sobreescribe en cada frame: SOLO se lee, nunca se setea.
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

	// El bot sigue dentro del avion? (monta al bus).
	static bool IsInAircraft(CustomBot& Bot)
	{
		if (!Bot.PlayerState)
			return false;
		return Bot.PlayerState->IsInAircraft();
	}

	// El punto de aterrizaje elegido.
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

	// Senal NATIVA de salto del bus: Character::Jump (igual que bots.h usa para
	// hacer saltar a los robots). El juego detecta que el pawn esta inAircraft y lo
	// ejecta del bus en modo skydive.
	static void SendNativeJump(CustomBot& Bot)
	{
		if (!Bot.Pawn)
			return;

		static auto JumpFn = FindObject<UFunction>(L"/Script/Engine.Character.Jump");

		if (JumpFn)
		{
			Bot.Pawn->ProcessEvent(JumpFn);
			LOG_INFO(LogBots, "[BotBus] native Character::Jump sent");
		}
		else
		{
			LOG_WARN(LogBots, "[BotBus] Character::Jump UFunction not found!");
		}
	}

	// FALLBACK: el salto nativo no funciono (el bot sigue inAircraft a los 5s).
	// Lo saca a mano del autobus: lo coloca justo fuera de la puerta del avion.
	static void ForceExitAircraft(CustomBot& Bot)
	{
		if (!Bot.Pawn)
			return;

		AActor* Aircraft = CustomBotAI::GetAircraft();

		FVector OutLoc = Bot.Pawn->GetActorLocation();

		if (Aircraft)
		{
			FVector AircraftLoc = Aircraft->GetActorLocation();
			FVector Fwd = Aircraft->GetActorForwardVector();

			// Justo al lado (adelantado) y un poco mas abajo que el avion
			// (fuera de su colision).
			OutLoc = AircraftLoc + Fwd * 1200.0f;
			OutLoc.Z -= 600.0f;
		}

		Bot.Pawn->TeleportTo(OutLoc, Bot.Pawn->GetActorRotation());

		LOG_WARN(LogBots, "[BotBus] FALLBACK: still inAircraft after 5s, force-exited to ({:.0f},{:.0f},{:.0f})",
			OutLoc.X, OutLoc.Y, OutLoc.Z);
	}

	// Inicia la caida como un SKYDIVE CONTROLADO hacia el destino (no como caerse
	// de un edificio): velocidad horizontal hacia la zona + descenso limitado.
	static void StartSkydive(CustomBot& Bot, BotAIContext& Ctx, const FVector& Target)
	{
		if (!Bot.Pawn)
			return;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Target);

		FVector Vel{Dir.X * SkydiveHorizontalSpeed, Dir.Y * SkydiveHorizontalSpeed, SkydiveMaxDescent};
		SetVelocity(Bot, Vel);

		// Girar hacia el destino durante la caida.
		CustomBotMovement::LookAt(Bot, Target);

		Ctx.bFiredGlider = false;
		LOG_INFO(LogBots, "[BotBus] skydive started toward ({:.0f},{:.0f}) desc={:.0f}",
			Target.X, Target.Y, SkydiveMaxDescent);
	}

	// Planeo/gliding: mantiene el avance horizontal hacia el destino y limita el
	// descenso. El motor despliega la ala delta automaticamente cerca del suelo.
	static void Glide(CustomBot& Bot, BotAIContext& Ctx, const FVector& Target)
	{
		if (!Bot.Pawn)
			return;

		FVector BotLoc = Bot.Pawn->GetActorLocation();
		FVector Vel = GetVelocity(Bot);
		FVector Dir = CustomBotMovement::DirectionTo(BotLoc, Target);

		FVector NewVel{Dir.X * GlideHorizontalSpeed, Dir.Y * GlideHorizontalSpeed, Vel.Z};

		// Limitar el descenso: si la caida es mas brusca que un skydive/planeo,
		// caparla (evita caida libre como desde un edificio).
		if (NewVel.Z < SkydiveMaxDescent)
			NewVel.Z = SkydiveMaxDescent;

		if (NewVel.Z > -50.0f)
			NewVel.Z = -50.0f;

		CustomBotMovement::LookAt(Bot, Target);
		SetVelocity(Bot, NewVel);
		Ctx.bFiredGlider = true;
	}

	// Aterrizo: el CMC ya esta en Walking (1). Pasar a la fase de juego.
	static void OnLanded(CustomBot& Bot, BotAIContext& Ctx)
	{
		CustomBotMovement::StopMovement(Bot);
		Ctx.State = EBotState::Looting;
		Ctx.bHasLandingPoint = false;
		LOG_INFO(LogBots, "[BotBus] landed -> Looting");
	}

	// Update principal de la fase de bus / salto / aterrizaje.
	static void Update(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		// Ya no estamos en fase de avion (el bus termino el recorrido).
		if (!CustomBotAI::IsInAircraftPhase())
		{
			if (Ctx.State == EBotState::InBus || Ctx.State == EBotState::ChoosingLanding)
			{
				if (IsInAircraft(Bot))
				{
					// Sigue montado pero el bus no va a seguir: forzarlo a saltar.
					LOG_WARN(LogBots, "[BotBus] aircraft phase ended while on bus, forcing exit");
					ForceExitAircraft(Bot);
					StartSkydive(Bot, Ctx, LandingTarget(Ctx, Bot));
					Ctx.State = EBotState::Gliding;
				}
				else
				{
					// Aun no está montado. Si el bus ya paso (SafeZones+) no
					// vendra a recogerlo: empezar a jugar en el suelo. Si aun
					// estamos en Warmup, esperar a que el avion lo recoja.
					AFortGameStateAthena* GS = CustomBotAI::GetGameState();
					EAthenaGamePhase Phase = GS ? GS->GetGamePhase() : EAthenaGamePhase::None;

					if (Phase == EAthenaGamePhase::SafeZones || Phase == EAthenaGamePhase::EndGame)
					{
						Ctx.State = EBotState::Looting;
						Ctx.bHasLandingPoint = false;
					}
				}

				return;
			}

			if (Ctx.State == EBotState::Jumping)
			{
				Ctx.State = EBotState::Gliding;
			}
		}

		switch (Ctx.State)
		{
		case EBotState::InBus:
		{
			// Esperar a estar realmente montado en el bus. El juego lo monta solo
			// cuando empieza la fase de avion; mientras tanto no hacemos nada.
			if (!IsInAircraft(Bot))
			{
				Ctx.JumpDelay = 0.0f;
				break;
			}

			if (Ctx.JumpDelay <= 0.0f)
			{
				// Entre 3 y 12 segundos tras empezar a volar.
				float Delta = 3.0f + float(std::rand() % 90) / 10.0f;
				Ctx.JumpDelay = UGameplayStatics::GetTimeSeconds(GetWorld()) + Delta;
			}

			if (UGameplayStatics::GetTimeSeconds(GetWorld()) >= Ctx.JumpDelay)
				Ctx.State = EBotState::ChoosingLanding;

			break;
		}

		case EBotState::ChoosingLanding:
		{
			if (!Ctx.bHasLandingPoint)
			{
				Ctx.LandingPoint = CustomBotAI::PickLandingPoint(Ctx.Personality.Aggression, Ctx.Personality.RiskTolerance);
				Ctx.bHasLandingPoint = true;
			}

			// Pequena variacion de tiempo y saltar.
			if (UGameplayStatics::GetTimeSeconds(GetWorld()) >= Ctx.JumpDelay + 0.5f)
				Ctx.State = EBotState::Jumping;

			break;
		}

		case EBotState::Jumping:
		{
			// 1) Asegurar poseido mientras se intenta saltar (el bool
			//    bKeepPossessed evita que la simulacion de servidor lo despida).
			Bot.bKeepPossessed = true;

			if (Bot.Controller && Bot.Pawn && Bot.Controller->GetPawn() != Bot.Pawn)
				Bot.Controller->Possess(Bot.Pawn);

			// 2) Senal NATIVA de salto (una sola vez).
			if (!Ctx.bJumpAttempted)
			{
				SendNativeJump(Bot);
				Ctx.bJumpAttempted = true;
				Ctx.JumpAttemptTime = UGameplayStatics::GetTimeSeconds(GetWorld());
			}

			// 3) Si ya no esta inAircraft -> salto exitoso: controlar el skydive.
			if (!IsInAircraft(Bot))
			{
				ReleaseForSimulation(Bot); // la simulacion vuelve a controlar el pawn
				StartSkydive(Bot, Ctx, LandingTarget(Ctx, Bot));
				Ctx.State = EBotState::Gliding;
				break;
			}

			// 4) FALLBACK: 5s despues sigue en el avion -> sacarlo a mano.
			float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

			if (Now >= Ctx.JumpAttemptTime + 5.0f)
			{
				ForceExitAircraft(Bot);
				ReleaseForSimulation(Bot);
				StartSkydive(Bot, Ctx, LandingTarget(Ctx, Bot));
				Ctx.State = EBotState::Gliding;
			}

			break;
		}

		case EBotState::Gliding:
		case EBotState::Landing:
		{
			FVector Target = LandingTarget(Ctx, Bot);
			Glide(Bot, Ctx, Target);

			// Aterrizado: el CMC pasa a Walking (1) -> empezar a jugar.
			if (GetMovementMode(Bot) == 1)
			{
				OnLanded(Bot, Ctx);
			}

			break;
		}

		default:
			// No deberia llamarse aqui; el master tick enruta a midgame.
			break;
		}
	}
}