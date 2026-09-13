#pragma once

// CustomBot AI - Safe Zone / Storm helpers.
//
// Proporciona acceso best-effort a la zona segura (lo que un jugador real puede
// ver en el mapa): el centro de la zona final (SafeZoneLocations del GameMode) y
// la fase de juego. Se usa para la rotacion (Section 16) y el endgame.
//
// IMPORTANTE: NO omnisciencia. Solo usamos datos que el jugador ve (el circulo en
// el mapa) y la posicion del propio bot, nunca posiciones de enemigos ocultas.

#include "CustomBotAI.h"

namespace CustomBotAI
{
	static AFortGameModeAthena* GetGameMode()
	{
		return Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
	}

	static AFortGameStateAthena* GetGameState()
	{
		return Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
	}

	// Centro de la zona segura actual/objetivo (best-effort).
	// Prefiere la POSICION del indicador de zona actual (el circulo real que ve
	// el jugador en el mapa, usado ya por GetSafeZoneRadius para el radio); si
	// no, el centro de la zona FINAL (SafeZoneLocations[last]). El indicador es
	// la fuente mas fiable: cuando SafeZoneLocations llega vacio (algunas
	// versiones) GetSafeZoneCenter devolvia (0,0,0) y con el fallback
	// GetSafeZoneCenter(bot) la distancia al "centro" era siempre 0, asi que
	// IsOutsideSafeZone nunca daba true y los bots NO rotaban aunque la storm
	// les estuviera encima (observado: bots yendo al loot en vez de a la zona).
	static FVector GetSafeZoneCenter()
	{
		auto GameMode = GetGameMode();

		if (GameMode)
		{
			auto SafeZoneIndicator = GameMode->GetSafeZoneIndicator();

			if (SafeZoneIndicator && !SafeZoneIndicator->IsActorBeingDestroyed())
			{
				FVector IndicatorLoc = SafeZoneIndicator->GetActorLocation();

				if ((IndicatorLoc | IndicatorLoc) > 0.0f)
					return IndicatorLoc;
			}

			static auto SafeZoneLocationsOffset = GameMode->GetOffset("SafeZoneLocations", false);

			if (SafeZoneLocationsOffset != -1)
			{
				const TArray<FVector>& Locs = GameMode->Get<TArray<FVector>>(SafeZoneLocationsOffset);

				if (Locs.Num() > 0)
					return Locs.at(Locs.Num() - 1);
			}
		}

		return FVector{};
	}

	// Centro de la zona segura actual/objetivo (best-effort).
	// Prefiere el centro de la zona final (SafeZoneLocations[last]); si no hay
	// datos, usa la posicion del bot como fallback.
	static FVector GetSafeZoneCenter(const CustomBot& Bot)
	{
		FVector Center = GetSafeZoneCenter();

		if ((Center | Center) == 0.0f)
			return Bot.GetLocation();

		return Center;
	}

	// Radio de la zona segura ACTUAL (el indicador del mapa, "SafeZoneIndicator"
// del GameMode). La heuristica vieja devolvia 90000 fijo: con el circulo ya
// encogido los bots estaban "dentro" del falso radio y nadie rotaba, asi que
// la tormenta los mataba sin intentar huir. Ahora se lee el Radius real; si no
// esta disponible (phase temprana / sin indicador) se cae al default grande.
static float GetSafeZoneRadius()
{
	auto GameMode = GetGameMode();

	if (GameMode)
	{
		auto SafeZoneIndicator = GameMode->GetSafeZoneIndicator();

		if (SafeZoneIndicator && !SafeZoneIndicator->IsActorBeingDestroyed())
		{
			static auto RadiusOffset = SafeZoneIndicator->GetOffset("Radius", false);

			if (RadiusOffset != -1)
			{
				float R = SafeZoneIndicator->Get<float>(RadiusOffset);

				if (R > 0.0f)
					return R;
			}
		}
	}

	constexpr float DefaultRadius = 90000.0f;
	return DefaultRadius;
}

	// Devuelve true si el bot esta fuera de la zona segura actual (frente a la
	// tormenta). Se asume un radio heuristico; el efecto neto es que rota hacia
	// el centro cuando esta lejos de el.
	static bool IsOutsideSafeZone(CustomBot& Bot, BotAIContext& Ctx, float Margin = 0.0f)
	{
		if (!Bot.IsReady())
			return false;

		FVector Center = GetSafeZoneCenter(Bot);
		Ctx.SafeZoneCenter = Center;

		FVector Delta = Bot.GetLocation() - Center;
		float DistSq = (Delta | Delta);
		float Radius = GetSafeZoneRadius() + Margin;

		return DistSq > Radius * Radius;
	}

	// Distancia horizontal al centro de la zona segura.
	static float DistanceToSafeZoneCenter(CustomBot& Bot)
	{
		FVector Center = GetSafeZoneCenter(Bot);
		FVector Delta = Bot.GetLocation() - Center;
		return FMath::Sqrt((Delta | Delta));
	}

	// Devuelve true si la partida esta en fase de avion (bus volando).
	static bool IsInAircraftPhase()
	{
		auto GameState = GetGameState();
		return GameState && GameState->GetGamePhase() == EAthenaGamePhase::Aircraft;
	}

	// Devuelve true si la partida esta en pre-partida (Warmup): los bots simulan
	// un lobby activo paseando, looteando y disparandose hasta que despegue el bus.
	static bool IsWarmupPhase()
	{
		auto GameState = GetGameState();
		return GameState && GameState->GetGamePhase() == EAthenaGamePhase::Warmup;
	}

	// Devuelve true si la partida esta en fase de zonas seguras (midgame).
	static bool IsInSafeZonesPhase()
	{
		auto GameState = GetGameState();
		return GameState && GameState->GetGamePhase() == EAthenaGamePhase::SafeZones;
	}

	// Devuelve el numero de jugadores (y bots) vivos segun el proyecto.
	static int GetPlayersAlive()
	{
		auto GameState = GetGameState();
		return GameState ? GameState->GetPlayersLeft() : 0;
	}

	// Devuelve la cantidad de jugadores vivos (humanos + bots) que quedan, usando
	// el contador real del proyecto (PlayersLeft).
	static int GetAliveCount()
	{
		return GetPlayersAlive();
	}

	// El aeroplano actual (Battle Bus), best-effort. Devuelve nullptr si no hay.
	static AActor* GetAircraft()
	{
		auto GameState = GetGameState();

		if (!GameState)
			return nullptr;

		static auto AircraftsOffset = GameState->GetOffset("Aircrafts", false);

		if (AircraftsOffset != -1)
		{
			auto Aircrafts = GameState->GetPtr<TArray<AActor*>>(AircraftsOffset);

			if (Aircrafts && Aircrafts->Num() > 0)
			{
				for (int i = 0; i < Aircrafts->Num(); ++i)
				{
					AActor* A = Aircrafts->at(i);
					if (A && !A->IsActorBeingDestroyed())
						return A;
				}

				return nullptr;
			}
		}

		static auto AircraftOffset = GameState->GetOffset("Aircraft", false);

		if (AircraftOffset != -1)
			return GameState->Get<AActor*>(AircraftOffset);

		return nullptr;
	}
}
