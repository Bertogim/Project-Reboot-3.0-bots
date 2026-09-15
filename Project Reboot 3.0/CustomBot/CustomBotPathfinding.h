#pragma once

#include "CustomBot.h"

#include "CustomBotDoors.h"

#include <vector>


namespace CustomBotPathfinding
{
	inline constexpr float kDoorsRouteRadius = 1500.0f;
	inline constexpr int kMaxDoorsInRoute = 6;
	inline constexpr float kMinDoorStep = 250.0f;

	static UObject* GetNavSystem()
	{
		int NavOffset = GetWorld()->GetOffset("NavigationSystem", false);

		if (NavOffset == -1)
			return nullptr;

		return GetWorld()->Get<UObject*>(NavOffset);
	}

	static bool QueryPath(const FVector& Start, const FVector& End, std::vector<FVector>& OutPoints)
	{
		OutPoints.clear();

		auto NavSystem = GetNavSystem();
		if (!NavSystem)
			return false;

		static auto FindPathFn = FindObject<UFunction>(L"/Script/Engine.NavigationSystem.FindPathToLocationSynchronously");
		if (!FindPathFn)
			return false;

		static int WorldContextOffset = FindOffsetStruct("/Script/Engine.NavigationSystem.FindPathToLocationSynchronously", "WorldContextObject", false);
		static int PathStartOffset = FindOffsetStruct("/Script/Engine.NavigationSystem.FindPathToLocationSynchronously", "PathStart", false);
		static int PathEndOffset = FindOffsetStruct("/Script/Engine.NavigationSystem.FindPathToLocationSynchronously", "PathEnd", false);
		static int ReturnOffset = FindOffsetStruct("/Script/Engine.NavigationSystem.FindPathToLocationSynchronously", "ReturnValue", false);

		if (WorldContextOffset == -1 || PathStartOffset == -1 || PathEndOffset == -1 || ReturnOffset == -1)
			return false;

		auto Params = Alloc(FindPathFn->GetPropertiesSize());
		if (!Params)
			return false;

		*(UObject**)(__int64(Params) + WorldContextOffset) = GetWorld();
		*(FVector*)(__int64(Params) + PathStartOffset) = Start;
		*(FVector*)(__int64(Params) + PathEndOffset) = End;

		NavSystem->ProcessEvent(FindPathFn, Params);

		UObject* Path = *(UObject**)(__int64(Params) + ReturnOffset);
		VirtualFree(Params, 0, MEM_RELEASE);

		if (!Path)
			return false;

		int PointsOffset = Path->GetOffset("PathPoints", false);
		if (PointsOffset == -1)
			return false;

		auto& Points = Path->Get<TArray<FVector>>(PointsOffset);

		for (int i = 0; i < Points.Num(); ++i)
			OutPoints.push_back(Points.at(i));

		return !OutPoints.empty();
	}

	static bool RouteThroughDoors(CustomBot& Bot, const FVector& End, std::vector<FVector>& OutPoints)
	{
		OutPoints.clear();

		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector Start = Bot.Pawn->GetActorLocation();
		std::vector<AActor*> Doors = CustomBotDoors::FindDoorsWithin(Bot, kDoorsRouteRadius);

		if (Doors.empty())
			return false;

		std::vector<bool> Used(Doors.size(), false);
		std::vector<FVector> Chain;

		FVector Cur = End;

		for (int Step = 0; Step < kMaxDoorsInRoute; ++Step)
		{
			int Best = -1;
			float BestScore = FLT_MAX;

			for (size_t i = 0; i < Doors.size(); ++i)
			{
				if (Used[i])
					continue;

				AActor* Door = Doors[i];
				if (!Door || Door->IsActorBeingDestroyed())
					continue;

				FVector DLoc = Door->GetActorLocation();
				FVector ToDoor = DLoc - Cur;
				FVector ToBot = DLoc - Start;
				float DistToDoor = FMath::Sqrt(ToDoor | ToDoor);
				float DistToBot = FMath::Sqrt(ToBot | ToBot);

				float Score = DistToDoor + DistToBot * 0.35f;

				if (Score < BestScore)
				{
					BestScore = Score;
					Best = (int)i;
				}
			}

			if (Best == -1)
				break;

			AActor* Door = Doors[Best];
			Used[Best] = true;

			FVector DLoc = Door->GetActorLocation();
			FVector StepToCur = DLoc - Cur;
			float StepDist = FMath::Sqrt(StepToCur | StepToCur);

			if (Chain.empty() || StepDist >= kMinDoorStep)
				Chain.push_back(DLoc);
			else
				Chain.back() = DLoc;

			Cur = DLoc;

			if (FMath::Sqrt((DLoc - Start) | (DLoc - Start)) <= kMinDoorStep)
				break;
		}

		if (Chain.empty())
			return false;

		OutPoints.clear();

		for (size_t i = Chain.size(); i-- > 0;)
			OutPoints.push_back(Chain[i]);

		OutPoints.push_back(End);

		return true;
	}
}