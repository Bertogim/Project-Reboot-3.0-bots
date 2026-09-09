#pragma once

// CustomBot AI - Parte 2.
//
// Motor de decisiones del Custom Bot para convertirlo en un jugador autonomo de
// Battle Royale (Season 3). Se apoya EXCLUSIVAMENTE en las capacidades del
// CustomBot de la Parte 1 (CustomBot/CustomBot*.h). NO usa el sistema antiguo
// (PlayerBot / AFortAthenaAIBotController).
//
// Este archivo define los TIPOS compartidos (estado, personalidad, dificultad) y
// el contexto de IA por bot (BotAIContext). Los modulos de fase (Bus / Midgame /
// Combat / SafeZone) se incluyen al final y exponen sus updates; el master tick
// CustomBotAI::Tick() enruta segun el estado actual.

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

// Estado de juego en el que se encuentra el bot (maquina de estados).
enum class EBotState : uint8_t
{
	InBus,            // dentro del Battle Bus, esperando
	ChoosingLanding,  // eligiendo destino mientras sigue en el bus
	Jumping,          // inicio del salto del bus
	Gliding,          // planeando con el glider hacia el destino
	Landing,          // aterrizando (frenando cerca del suelo)
	Looting,          // buscando/recogiendo loot
	Farming,          // recogiendo materiales
	Exploring,        // explorando el mapa / buscando objetivos
	SearchingEnemy,   // buscando combate activamente
	Fighting,         // combatiendo
	Defending,        // defendiendose / buscando cobertura
	Healing,          // curandose
	Rotating,         // rotando hacia la zona segura
	EndGame,          // endgame (pocos jugadores)
	Warmup,           // pre-partida (lobby): pasea, lootea y dispara por diversion
	Dead,             // eliminado
};

inline const TCHAR* CustomBotStateName(EBotState State)
{
	switch (State)
	{
	case EBotState::InBus:            return L"In Bus";
	case EBotState::ChoosingLanding:  return L"Choosing Landing";
	case EBotState::Jumping:          return L"Jumping";
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

// Parámetros de personalidad (Section 18). Valores en [0, 1].
struct BotPersonality
{
	float Aggression = 0.5f;     // busca combate vs evita
	float AimSkill = 0.5f;       // precision al disparar
	float BuildSkill = 0.5f;     // velocidad/calidad de construccion
	float LootSkill = 0.5f;      // evaluacion y preferencia de loot
	float Awareness = 0.5f;      // rango/tiempo de deteccion
	float RiskTolerance = 0.5f;  // tolerancia a situaciones de riesgo
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
	// Rellena la personalidad segun el tipo y la dificultad.
	static void BuildPersonality(BotPersonality& P, EBotPersonalityType Type, EBotDifficulty Difficulty);
	// Elige un tipo de personalidad (Random escoge uno al azar).
	static EBotPersonalityType ResolveType(EBotPersonalityType Type);

	// --- Objetivo de seleccion de destino/exploracion ------------------------
	// Devuelve una posicion de destino/aterrizaje razonable. Usa el centro de la
	// zona segura final (SafeZoneLocations) con una desviacion aleatoria, para
	// que los bots aterricen repartidos y cerca de donde habra que rotar.
	// El radio de la zona se sesga por la personalidad (Seccion 3): un bot
	// agresivo tiende a aterrizar cerca del centro (zona caliente), mientras que
	// uno que no asume riesgo se dispersa mas (zonas tranquilas).
	static FVector PickLandingPoint(float Aggression = 0.5f, float RiskTolerance = 0.5f);

	// Master tick: enruta al bot al update de su estado actual.
	static void Tick(CustomBot& Bot, BotAIContext& Ctx);
	static void TickBus(CustomBot& Bot, BotAIContext& Ctx);
	static void TickMidgame(CustomBot& Bot, BotAIContext& Ctx);
}

// Contexto de IA por bot (definicion completa aqui; CustomBot.h solo tiene el fwd).
struct BotAIContext
{
	// Estado y personalidad
	EBotState State = EBotState::InBus;
	EBotPersonalityType PersonalityType = EBotPersonalityType::Casual;
	EBotDifficulty Difficulty = EBotDifficulty::Normal;
	BotPersonality Personality;

	// Objetivos seleccionados
	FVector LandingPoint{};
	bool bHasLandingPoint = false;
	FVector ExploreTarget{};
	bool bHasExploreTarget = false;
	AActor* EnemyTarget = nullptr;
	AActor* LootTarget = nullptr;
	AActor* FarmTarget = nullptr;
	AActor* HealTarget = nullptr;

	// Temporizacion (intervalos para no correr IA pesada cada frame)
	float DecisionTimer = 0.0f;
	float ScanTimer = 0.0f;
	float ReactTimer = 0.0f;   // retraso de reaccion humano
	float ActionTimer = 0.0f;  // cooldown entre acciones de combate

	// Battle bus / glider
	float JumpDelay = 0.0f;    // momento (tiempo) en que decide saltar
	bool bFiredGlider = false; // ya desplego el glider
	bool bJumpAttempted = false; // ya envio la senal nativa de salto (Character::Jump)
	float JumpAttemptTime = 0.0f;  // momento del intento (para el fallback de 5s)

	// Combate
	bool bIsFiring = false;
	bool bBuildingBarricade = false;
	float LastAimYaw = 0.0f;

	// Rotacion / zona segura
	FVector SafeZoneCenter{};
	float SafeZoneRadius = 0.0f;

	// Contadores
	int Kills = 0;
	int LootedItems = 0;
	int FarmedResources = 0;
};

#include "CustomBotAI_SafeZone.h"
#include "CustomBotAI_Bus.h"
#include "CustomBotAI_Midgame.h"

// --- Implementaciones de CustomBotAI (declaradas arriba) ----------------------

namespace CustomBotAI
{
	// Elige el tipo concreto: si el tipo es Random, escoge uno al azar.
	static EBotPersonalityType ResolveType(EBotPersonalityType Type)
	{
		if (Type == EBotPersonalityType::Random)
		{
			int R = std::rand() % 5;
			return (EBotPersonalityType)R; // Novato..Pro (0..4)
		}

		return Type;
	}

	// Rellena la personalidad segun tipo + dificultad. Los valores base suben o
	// bajan con la dificultad (mejor/bastante, pero NUNCA perfecto).
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
			// Aleatorio: cada parametro independiente.
			P.Aggression = float(std::rand() % 1000) / 1000.0f;
			P.AimSkill = float(std::rand() % 1000) / 1000.0f;
			P.BuildSkill = float(std::rand() % 1000) / 1000.0f;
			P.LootSkill = float(std::rand() % 1000) / 1000.0f;
			P.Awareness = float(std::rand() % 1000) / 1000.0f;
			P.RiskTolerance = float(std::rand() % 1000) / 1000.0f;
			break;
		}

		// Ajuste por dificultad (sin dar informacion ilegal).
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

	// Punto de aterrizaje / destino: repartido alrededor del centro de la zona
	// final, con la Z del centro de la zona (los jugadores eligen "donde va la
	// zona"). La variacion aleatoria reparte a los bots (no saltan todos al mismo
	// sitio) y el sesgo de personalidad acerca/aleja del centro (agresivo vs
	// defensivo, Seccion 3).
	static FVector PickLandingPoint(float Aggression, float RiskTolerance)
	{
		FVector Center = GetSafeZoneCenter();

		if ((Center | Center) == 0.0f)
			Center = FVector{};

		// Radio de desviacion: un bot agresivo (Aggression ~0.85) aterriza cerca
		// del centro; uno cauto (RiskTolerance ~0.3) se dispersa hacia zonas
		// tranquilas. Rango resultante: ~850..5000 unidades.
		float BaseRadius = 3000.0f;
		float RadiusScale = 1.0f - (Aggression * 0.4f) + ((1.0f - RiskTolerance) * 0.4f);
		RadiusScale = FMath::Clamp(RadiusScale, 0.35f, 1.6f);
		BaseRadius *= RadiusScale;

		float Radius = float(std::rand() % 1000) / 1000.0f * BaseRadius;
		float Angle = float(std::rand() % 360) * 3.14159265358979323846f / 180.0f;

		FVector Landing{Center.X + FMath::Cos(Angle) * Radius,
						Center.Y + FMath::Sin(Angle) * Radius,
						0.0f};

		// Z: la del centro del mapa como referencia (el CMC detecta el suelo).
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

	// Master tick: enruta segun el estado (bus vs midgame).
	static void Tick(CustomBot& Bot, BotAIContext& Ctx)
	{
		if (!Bot.IsReady())
			return;

		if (Ctx.State == EBotState::Dead)
			return;

		// El warmup termino: volver al flujo normal (bus si arranco el avion, o
		// directamente al suelo si la partida paso a zonas seguras sin avion).
		if (Ctx.State == EBotState::Warmup && !CustomBotAI::IsWarmupPhase())
		{
			Ctx.State = CustomBotAI::IsInAircraftPhase() ? EBotState::InBus : EBotState::Looting;
			Ctx.bHasLandingPoint = false;

			if (Ctx.State == EBotState::InBus)
			{
				TickBus(Bot, Ctx);
				return;
			}
		}

		switch (Ctx.State)
		{
		case EBotState::InBus:
		case EBotState::ChoosingLanding:
		case EBotState::Jumping:
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
