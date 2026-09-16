#pragma once


#include "reboot.h"
#include "CustomBot/CustomBot.h"
#include "CustomBot/CustomBotMovement.h"
#include "CustomBot/CustomBotPerception.h"
#include "CustomBot/CustomBotInventory.h"
#include "CustomBot/CustomBotCombat.h"
#include "CustomBot/CustomBotBuilding.h"
#include "CustomBot/CustomBotDestruction.h"
#include "CustomBot/CustomBotResources.h"
#include "CustomBot/CustomBotInteraction.h"

#include "FortGameModeAthena.h"
#include "FortGameStateAthena.h"
#include "FortSafeZoneIndicator.h"
#include "GameplayStatics.h"
#include "FortAthenaMapInfo.h"

enum class EBotState : uint8_t
{
	InBus,
	Ejecting,
	Gliding,
	Landing,
	Looting,
	Farming,
	Exploring,
	SearchingEnemy,
	Fighting,
	Defending,
	Healing,
	Rotating,
	EndGame,
	Warmup,
	Dead,
};

inline const TCHAR* CustomBotStateName(EBotState State)
{
	switch (State)
	{
	case EBotState::InBus:            return L"In Bus";
	case EBotState::Ejecting:         return L"Ejecting";
	case EBotState::Gliding:          return L"Gliding";
	case EBotState::Landing:          return L"Landing";
	case EBotState::Looting:          return L"Looting";
	case EBotState::Farming:          return L"Farming";
	case EBotState::Exploring:        return L"Exploring";
	case EBotState::SearchingEnemy:   return L"Searching Enemy";
	case EBotState::Fighting:         return L"Fighting";
	case EBotState::Defending:        return L"Defending";
	case EBotState::Healing:          return L"Healing";
	case EBotState::Rotating:         return L"Rotating";
	case EBotState::EndGame:          return L"End Game";
	case EBotState::Warmup:           return L"Warmup Lobby";
	case EBotState::Dead:             return L"Dead";
	}
	return L"Unknown";
}

struct BotPersonality
{
	float Aggression = 0.5f;
	float AimSkill = 0.5f;
	float BuildSkill = 0.5f;
	float LootSkill = 0.5f;
	float Awareness = 0.5f;
	float RiskTolerance = 0.5f;
};

enum class EBotPersonalityType : uint8_t
{
	Novato,
	Casual,
	Agresivo,
	Defensivo,
	Pro,
	Random,
};

enum class EBotDifficulty : uint8_t
{
	Easy,
	Normal,
	Hard,
};

namespace CustomBotAI
{
	static void BuildPersonality(BotPersonality& P, EBotPersonalityType Type, EBotDifficulty Difficulty);
	static EBotPersonalityType ResolveType(EBotPersonalityType Type);

	static FVector PickLandingPoint(float Aggression = 0.5f, float RiskTolerance = 0.5f);

	static void Tick(CustomBot& Bot, BotAIContext& Ctx);
	static void TickBus(CustomBot& Bot, BotAIContext& Ctx);
	static void TickMidgame(CustomBot& Bot, BotAIContext& Ctx);
}

struct BotAIContext
{
	EBotState State = EBotState::InBus;
	EBotPersonalityType PersonalityType = EBotPersonalityType::Casual;
	EBotDifficulty Difficulty = EBotDifficulty::Normal;
	BotPersonality Personality;

	FVector LandingPoint{};
	bool bHasLandingPoint = false;
	FVector ExploreTarget{};
	bool bHasExploreTarget = false;
	AActor* EnemyTarget = nullptr;
	AActor* LootTarget = nullptr;
	AActor* FarmTarget = nullptr;
	AActor* HealTarget = nullptr;

	float DecisionTimer = 0.0f;
	float ScanTimer = 0.0f;
	float ReactTimer = 0.0f;
	float ActionTimer = 0.0f;
	float EquipRetryTimer = 0.0f;

	float LastDamageTime = -999.0f;
	float CheckedHealth = -1.0f;

	float JumpDelay = 0.0f;
	bool bFiredGlider = false;
	float InBusSince = -1.0f;

	bool bIsFiring = false;
	bool bBuildingBarricade = false;
	float LastAimYaw = 0.0f;

	FVector SafeZoneCenter{};
	float SafeZoneRadius = 0.0f;

	int Kills = 0;
	int LootedItems = 0;
	int FarmedResources = 0;
};

#include "CustomBotAI_SafeZone.h"
#include "CustomBotAI_Midgame.h"
#include "CustomBotAI_Bus.h"


namespace CustomBotAI
{
	static EBotPersonalityType ResolveType(EBotPersonalityType Type)
	{
		if (Type == EBotPersonalityType::Random)
		{
			int R = std::rand() % 5;
			return (EBotPersonalityType)R;
		}

		return Type;
	}

	static void BuildPersonality(BotPersonality& P, EBotPersonalityType Type, EBotDifficulty Difficulty)
	{
		Type = ResolveType(Type);

		switch (Type)
		{
		case EBotPersonalityType::Novato:
			P.Aggression = 0.30f; P.AimSkill = 0.15f; P.BuildSkill = 0.10f;
			P.LootSkill = 0.30f; P.Awareness = 0.30f; P.RiskTolerance = 0.40f;
			break;
		case EBotPersonalityType::Casual:
			P.Aggression = 0.50f; P.AimSkill = 0.45f; P.BuildSkill = 0.40f;
			P.LootSkill = 0.50f; P.Awareness = 0.50f; P.RiskTolerance = 0.50f;
			break;
		case EBotPersonalityType::Agresivo:
			P.Aggression = 0.85f; P.AimSkill = 0.55f; P.BuildSkill = 0.45f;
			P.LootSkill = 0.40f; P.Awareness = 0.60f; P.RiskTolerance = 0.80f;
			break;
		case EBotPersonalityType::Defensivo:
			P.Aggression = 0.30f; P.AimSkill = 0.50f; P.BuildSkill = 0.60f;
			P.LootSkill = 0.60f; P.Awareness = 0.60f; P.RiskTolerance = 0.30f;
			break;
		case EBotPersonalityType::Pro:
			P.Aggression = 0.60f; P.AimSkill = 0.85f; P.BuildSkill = 0.85f;
			P.LootSkill = 0.80f; P.Awareness = 0.85f; P.RiskTolerance = 0.65f;
			break;
		case EBotPersonalityType::Random:
		default:
			P.Aggression = float(std::rand() % 1000) / 1000.0f;
			P.AimSkill = float(std::rand() % 1000) / 1000.0f;
			P.BuildSkill = float(std::rand() % 1000) / 1000.0f;
			P.LootSkill = float(std::rand() % 1000) / 1000.0f;
			P.Awareness = float(std::rand() % 1000) / 1000.0f;
			P.RiskTolerance = float(std::rand() % 1000) / 1000.0f;
			break;
		}

		switch (Difficulty)
		{
		case EBotDifficulty::Easy:
			P.AimSkill *= 0.65f; P.BuildSkill *= 0.65f;
			P.Awareness *= 0.75f; P.Aggression *= 0.85f;
			break;
		case EBotDifficulty::Hard:
			P.AimSkill = FMath::Clamp(P.AimSkill * 1.35f, 0.0f, 1.0f);
			P.BuildSkill = FMath::Clamp(P.BuildSkill * 1.30f, 0.0f, 1.0f);
			P.Awareness = FMath::Clamp(P.Awareness * 1.25f, 0.0f, 1.0f);
			P.RiskTolerance = FMath::Clamp(P.RiskTolerance * 1.15f, 0.0f, 1.0f);
			break;
		case EBotDifficulty::Normal:
		default:
			break;
		}
	}

	static FVector PickLandingPoint(float Aggression, float RiskTolerance)
	{
		FVector Center = GetSafeZoneCenter();

		if ((Center | Center) == 0.0f)
			Center = FVector{};

		if (CustomBotPerception::BotTime() > 0.0f)
		{
			const auto& Chests = CustomBotPerception::CachedChests();
			std::vector<FVector> Candidates;
			std::vector<FVector> HotCandidates;

			float SelectRadius = 5000.0f + RiskTolerance * 3000.0f;

			for (const auto& Chest : Chests)
			{
				if (Chest.bSearched)
					continue;

				float DX = Chest.Location.X - Center.X;
				float DY = Chest.Location.Y - Center.Y;
				float D = FMath::Sqrt(DX * DX + DY * DY);

				if (D <= SelectRadius)
				{
					Candidates.push_back(Chest.Location);
					if (D <= 3000.0f)
						HotCandidates.push_back(Chest.Location);
				}
			}

			std::vector<FVector>& Pool = (Aggression >= 0.6f && !HotCandidates.empty()) ? HotCandidates : Candidates;

			if (!Pool.empty())
			{
				FVector Landing = Pool[std::rand() % Pool.size()];

				if (Landing.Z <= 0.0f)
					Landing.Z = 0.0f;

				return Landing;
			}
		}

		float BaseRadius = 3000.0f;
		float RadiusScale = 1.0f - (Aggression * 0.4f) + ((1.0f - RiskTolerance) * 0.4f);
		RadiusScale = FMath::Clamp(RadiusScale, 0.35f, 1.6f);
		BaseRadius *= RadiusScale;

		float Radius = float(std::rand() % 1000) / 1000.0f * BaseRadius;
		float Angle = float(std::rand() % 360) * 3.14159265358979323846f / 180.0f;

		FVector Landing{Center.X + FMath::Cos(Angle) * Radius,
						Center.Y + FMath::Sin(Angle) * Radius,
						0.0f};

		Landing.Z = Center.Z;

		if (Landing.Z <= 0.0f)
			Landing.Z = 0.0f;

		return Landing;
	}

	static void TickBus(CustomBot& Bot, BotAIContext& Ctx)
	{
		CustomBotAIBus::Update(Bot, Ctx);
	}

	static void TickMidgame(CustomBot& Bot, BotAIContext& Ctx)
	{
		CustomBotAIMidgame::Update(Bot, Ctx);
	}

	static void Tick(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		CustomBotInventory::TickPendingEquips(Bot);

		if (Ctx.State == EBotState::Dead)
			return;

		if (Ctx.State == EBotState::Warmup && !CustomBotAI::IsWarmupPhase())
		{
			CustomBotAIMidgame::ResetToMatchHP(Bot);

			Ctx.State = CustomBotAI::IsInAircraftPhase() ? EBotState::InBus : EBotState::Looting;
			Ctx.bHasLandingPoint = false;
			Ctx.InBusSince = -1.0f;
			Bot.bInAirPhase = false;

			if (Ctx.State == EBotState::InBus)
			{
				TickBus(Bot, Ctx);
				return;
			}
		}

		switch (Ctx.State)
		{
		case EBotState::InBus:
		case EBotState::Ejecting:
		case EBotState::Gliding:
		case EBotState::Landing:
			TickBus(Bot, Ctx);
			break;

		case EBotState::Warmup:
			TickMidgame(Bot, Ctx);
			break;

		default:
			TickMidgame(Bot, Ctx);
			break;
		}
	}
}
