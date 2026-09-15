#pragma once


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

	static FVector GetSafeZoneCenter(const CustomBot& Bot)
	{
		FVector Center = GetSafeZoneCenter();

		if ((Center | Center) == 0.0f)
			return Bot.GetLocation();

		return Center;
	}

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

	static float DistanceToSafeZoneCenter(CustomBot& Bot)
	{
		FVector Center = GetSafeZoneCenter(Bot);
		FVector Delta = Bot.GetLocation() - Center;
		return FMath::Sqrt((Delta | Delta));
	}

	static bool IsInAircraftPhase()
	{
		auto GameState = GetGameState();
		return GameState && GameState->GetGamePhase() == EAthenaGamePhase::Aircraft;
	}

	static bool IsWarmupPhase()
	{
		auto GameState = GetGameState();
		return GameState && GameState->GetGamePhase() == EAthenaGamePhase::Warmup;
	}

	static bool IsInSafeZonesPhase()
	{
		auto GameState = GetGameState();
		return GameState && GameState->GetGamePhase() == EAthenaGamePhase::SafeZones;
	}

	static int GetPlayersAlive()
	{
		auto GameState = GetGameState();
		return GameState ? GameState->GetPlayersLeft() : 0;
	}

	static int GetAliveCount()
	{
		return GetPlayersAlive();
	}

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
