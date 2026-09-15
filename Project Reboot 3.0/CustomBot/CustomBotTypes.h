#pragma once

#include "reboot.h"

inline bool bCustomBotPathfinding = false;

inline bool bCustomBotPathfindingFallback = false;

namespace CBT
{
	enum class EResult : uint8_t
	{
		Success,
		Failure,
		NotReady,
		NotFound,
		OutOfRange,
		Blocked,
		NoResources,
		Cooldown,
	};

	enum class EMovementState : uint8_t
	{
		Idle,
		Moving,
		BlockedPath,
		Arrived,
	};

	enum class ELifeState : uint8_t
	{
		Alive,
		Downed,
		Dead,
	};

	enum class EObstacleType : uint8_t
	{
		None,
		EnemyStructure,
		OwnStructure,
		WorldObject,
		Structure,
		LargeDip,
		LargeRise,
		Actor,
	};

	struct FMoveRequest
	{
		FVector Destination{};
		FVector Start{};
		float AcceptanceRadius = 100.0f;
		float MoveSpeed = 600.0f;
		bool bStopOnArrival = true;
	};

	struct FScanResult
	{
		TArray<AActor*> Actors;
		float Radius = 0.0f;

		~FScanResult()
		{
			Actors.FreeEngine();
		}
	};
}
