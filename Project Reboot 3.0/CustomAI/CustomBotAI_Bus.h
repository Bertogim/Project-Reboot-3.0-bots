#pragma once

// CustomBot AI - Battle Bus phase (Parte 2).
//
// NO usa el salto nativo del bus (Character::Jump / montaje nativo): se quito
// porque dejaba a la mitad de los bots montados en el avion sin salir (ghosts
// en el lobby). Flujo ACTUAL — teletransporte directo, TODOS los bots:
//   1) al arrancar el avion cada bot espera entre 15 y 45s (ventana de salto:
//      pasan 15s desde que empieza el bus y luego hay 30s para tirarse), de
//      modo que las caidas se reparten a lo largo del trayecto,
//   2) ChoosingLanding elige su zona (cerca de cofres),
//   3) Jumping -> CustomEject: se teletransporta a la posicion ACTUAL del avion
//      (o al centro fijo del bus como fallback) a altura de vuelo,
//   4) ReleaseForSimulation + bInAirPhase=true (gravedad SI actua) +
//      StartSkydive controlado hacia su zona,
//   5) el motor despliega la ala delta automaticamente cerca del suelo,
//   6) cuando el CMC vuelve a Walking -> ha aterrizado -> Looting.
//
// Garantia anti-lobby: si la fase de avion ya termino y un bot sigue en
// InBus/ChoosingLanding sin haber salido, Update lo coloca DIRECTAMENTE en el
// suelo de su destino (ForceExitAircraft). Ningun bot se queda en la isla.
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

	// Sincroniza de nuevo la visualizacion de la skin (character parts) y fuerza
	// la replicacion tras un teleport/aterrizaje. Mismo mecanismo que el apply
	// diferido del spawn (UpdatePlayerCustomCharacterPartsVisualization), que
	// funciona sobre el PlayerState y re-construye el mesh del pawn. Re-ejecutarlo
	// despues del eject y del landing re-emite las partes al cliente (evita bots
	// que caen bien pero se ven sin skin).
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

	// FALLBACK: coloca al bot en el suelo de su destino ("put character here").
	// Lo saca a mano del autobus colocandolo de verdad donde tocar ("put
	// character here"): limpia bInAircraft (si no, el juego lo mantiene como
	// pasajero del avion colgado a Z~80000, sin caer y replicando los seats),
	// y le hace el trace del terreno para sentarlo en el suelo del destino con
	// velocidad 0 -> pasa a Looting directamente.
	static void ForceExitAircraft(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.Pawn)
			return;

		// 1) Salir del estado pasajero: el sistema nativo del avion (seats +
		// replicacion del bus) deja de considerarlo montado.
		if (Bot.PlayerState)
			Bot.PlayerState->SetInAircraft(false);

		// 2) Punto de despliegue: el destino elegido, con la Z del terreno real.
		FVector Target = LandingTarget(Ctx, Bot);

		FVector Ground = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
			FVector{ Target.X, Target.Y, 0.0f }, 150000.0f, -30000.0f, FName(0));

		FVector OutLoc = Ground;
		if (OutLoc.Z <= -29000.0f) // trace fallida: usar el target tal cual
			OutLoc = Target;

		// 3) Colocarlo en el suelo del destino, mirando hacia donde toca jugar.
		Bot.Pawn->TeleportTo(OutLoc, CustomBotMovement::RotationFromDirection(
			CustomBotMovement::DirectionTo(OutLoc, Target)));

		// 4) Velocidad 0 y CMC en Walking: ya no viene del cielo.
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
		Bot.bInAirPhase = false;
		CustomBotMovement::StopMovement(Bot);
		ReapplySkinViz(Bot);
		Ctx.State = EBotState::Looting;
		Ctx.bHasLandingPoint = false;
		LOG_INFO(LogBots, "[BotBus] landed -> Looting");
	}

	// Momento (tiempo de mundo) en que empezo la fase de avion actual (-1 si no
	// esta activa). Se usa para detectar spawns tardios (el bus ya lleva un rato
	// volando) y ejectarlos pronto en lugar de esperar el stagger completo.
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

	// EJECT CUSTOM ("no se monta al bus"): si el bot NO esta en el avion, lo
	// teletransporta a la posicion ACTUAL del aeroplano (o al centro fijo del bus
	// si no hay) a altura de vuelo y lo lanza en skydive hacia su zona. Dos clave
	// de que NUNCA quede congelado a ~80km: ReleaseForSimulation (la fisica pasa a
	// la simulacion de servidor; si sigue poseido el controller le pone velocidad
	// 0 y no cae) y bInAirPhase=true (EnsureCMCActive deja de forzarle Walking en
	// el aire, asi la gravedad actua).
	static void CustomEject(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.Pawn)
			return;

		// Salir del estado pasajero: aunque el juego hubiera montado al bot en un
		// seat, dejamos de ser pasajero del avion (si no, lo seguira replicando
		// como ghost del lobby).
		if (Bot.PlayerState)
			Bot.PlayerState->SetInAircraft(false);

		ReleaseForSimulation(Bot);
		Bot.bInAirPhase = true;

		// Posicion de drop: la del aeroplano actual (+200 de altura sobre el
		// casco), con fallback al centro fijo del Battle Bus a Z~80936.
		FVector BusPos{-47557.0f, -62295.0f, 80936.0f};

		if (AActor* Aircraft = CustomBotAI::GetAircraft())
		{
			FVector A = Aircraft->GetActorLocation();
			if (A.Z > 0.0f && (A | A) != 0.0f)
				BusPos = FVector{A.X, A.Y, A.Z + 200.0f};
		}

		Bot.Pawn->TeleportTo(BusPos, Bot.Pawn->GetActorRotation());
		ReapplySkinViz(Bot);

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
					// Sigue montado pero el bus no va a seguir: colocarlo en el
					// suelo de su destino ("put character here").
					LOG_WARN(LogBots, "[BotBus] aircraft phase ended while on bus, forcing ground placement");
					ForceExitAircraft(Bot, Ctx);
				}
				else
				{
					// El bus ya termino y este bot no salio a tiempo: NO lo dejamos
					// en la isla. Colocarlo directamente en el suelo de su destino
					// ("put character here") para que empiece a jugar en el mapa.
					// (Si seguimos en Warmup, que siga en el lobby: el avion lo
					// recogera al empezar la fase.)
					AFortGameStateAthena* GS = CustomBotAI::GetGameState();
					EAthenaGamePhase Phase = GS ? GS->GetGamePhase() : EAthenaGamePhase::None;

					if (Phase == EAthenaGamePhase::Warmup)
					{
						Ctx.State = EBotState::Warmup;
						Ctx.bHasLandingPoint = false;
					}
					else
					{
						Bot.bInAirPhase = false;
						ForceExitAircraft(Bot, Ctx);
					}
				}

				return;
			}

			if (Ctx.State == EBotState::Jumping)
			{
				// El bus termino justo cuando iba a saltar: no empezar la caida
				// desde la isla (se quedaria en el lobby); colocarlo en el suelo
				// de su destino directamente.
				Bot.bInAirPhase = false;
				ForceExitAircraft(Bot, Ctx);
			}
		}

		switch (Ctx.State)
		{
		case EBotState::InBus:
		{
			// Sin montaje nativo y sin salto nativo: TODOS se teletransportan
			// desde el lobby a la posicion del avion y caen. Aqui solo se
			// programa la ventana de salto: pasan 15s desde que arranca el bus
			// y luego hay una ventana de 30s para tirarse (cada bot salta en un
			// momento aleatorio entre 15s y 45s -> caidas repartidas por la ruta).
			float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

			if (Ctx.InBusSince < 0.0f)
			{
				Ctx.InBusSince = Now;

				float Stagger = 15.0f + float(std::rand() % 3001) / 100.0f; // 15-45s

				// Spawn tardio (el avion ya lleva un rato volando): no esperarse
				// la ventana entera, el bus puede estar ya acabando.
				if (CustomBotAI::IsInAircraftPhase() && Ctx.InBusSince - AircraftPhaseStart() > 10.0f)
					Stagger = FMath::Max(1.0f, 15.0f - (Ctx.InBusSince - AircraftPhaseStart()));

				Ctx.JumpDelay = Now + Stagger;
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
			// SIN salto nativo y SIN montaje: eject custom directo desde el lobby/
			// isla a la posicion ACTUAL del avion + caida controlada. No se posee ni
			// se des-posee nada (el bot ya viene en modo simulacion desde el spawn),
			// asi que nunca se dispara el re-spawn de pawn del juego (SpawnDefaultPawn
			// fallando por colision / errores MCP) que dejaba media partida rota.
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