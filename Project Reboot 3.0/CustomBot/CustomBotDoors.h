#pragma once

#include "CustomBot.h"

#include "GameplayStatics.h"


namespace CustomBotDoors
{
	static UClass* BuildingWallClass()
	{
		static auto DoorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingWall");
		return DoorClass;
	}

	static bool SetBoolProperty(UObject* Obj, const std::string& Name, bool Value)
	{
		if (!Obj)
			return false;

		auto Prop = Obj->GetProperty(Name, false);
		int Off = Obj->GetOffset(Name, false);

		if (!Prop || Off == -1)
			return false;

		Obj->SetBitfieldValue(Off, GetFieldMask(Prop), Value);
		return true;
	}

	static bool ReadBoolProperty(UObject* Obj, const std::string& Name, bool& Out)
	{
		if (!Obj)
			return false;

		auto Prop = Obj->GetProperty(Name, false);
		int Off = Obj->GetOffset(Name, false);

		if (!Prop || Off == -1)
			return false;

		Out = Obj->ReadBitfieldValue(Off, GetFieldMask(Prop));
		return true;
	}

	static bool IsDoor(AActor* Actor)
	{
		if (!Actor || Actor->IsActorBeingDestroyed())
			return false;

		auto DoorClass = BuildingWallClass();
		if (!DoorClass || !Actor->IsA(DoorClass))
			return false;

		return Actor->GetOffset("bDoorOpen", false) != -1
			|| Actor->GetOffset("bDoorCollisionDisabled", false) != -1;
	}

	static bool IsDoorClosed(AActor* Door)
	{
		if (!IsDoor(Door))
			return false;

		bool bCollisionDisabled = false;
		if (ReadBoolProperty(Door, "bDoorCollisionDisabled", bCollisionDisabled) && bCollisionDisabled)
			return false;

		bool bOpen = false;
		if (ReadBoolProperty(Door, "bDoorOpen", bOpen))
			return !bOpen;

		return true;
	}

	static std::vector<AActor*> FindDoorsWithin(CustomBot& Bot, float Radius)
	{
		std::vector<AActor*> Doors;

		if (!Bot.IsReady() || !Bot.Pawn)
			return Doors;

		auto DoorClass = BuildingWallClass();
		if (!DoorClass)
			return Doors;

		TArray<AActor*> All = UGameplayStatics::GetAllActorsOfClass(GetWorld(), DoorClass);
		FVector BotLoc = Bot.Pawn->GetActorLocation();

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor || Actor->IsActorBeingDestroyed())
				continue;

			if (!IsDoor(Actor))
				continue;

			FVector Delta = Actor->GetActorLocation() - BotLoc;
			if ((Delta | Delta) <= Radius * Radius)
				Doors.push_back(Actor);
		}

		All.FreeEngine();
		return Doors;
	}

	static AActor* FindDoorInFront(CustomBot& Bot, float MaxDistance = 320.0f)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return nullptr;

		FVector Loc = Bot.Pawn->GetActorLocation();
		FVector Fwd = Bot.Pawn->GetActorForwardVector();

		float FwdLen = FMath::Sqrt(Fwd.X * Fwd.X + Fwd.Y * Fwd.Y);
		if (FwdLen < 0.0001f)
			return nullptr;

		auto Doors = FindDoorsWithin(Bot, MaxDistance);

		AActor* Best = nullptr;
		float BestScore = FLT_MAX;

		for (auto Door : Doors)
		{
			if (!IsDoorClosed(Door))
				continue;

			FVector To = Door->GetActorLocation() - Loc;
			float Dist = FMath::Sqrt(To | To);

			if (Dist > MaxDistance || Dist < 0.0001f)
				continue;

			float Dot = (Fwd.X * To.X + Fwd.Y * To.Y) / (FwdLen * Dist);

			if (Dot < 0.55f)
				continue;

			if (Dist < BestScore)
			{
				BestScore = Dist;
				Best = Door;
			}
		}

		return Best;
	}

	static bool OpenDoor(AActor* Door)
	{
		if (!IsDoor(Door))
			return false;

		SetBoolProperty(Door, "bDoorOpen", true);
		SetBoolProperty(Door, "bDoorCollisionDisabled", true);

		Door->ForceNetUpdate();

		return true;
	}

	static bool TryOpenDoorInFront(CustomBot& Bot, float MaxDistance = 320.0f)
	{
		auto Door = FindDoorInFront(Bot, MaxDistance);

		if (!Door)
			return false;

		return OpenDoor(Door);
	}
}