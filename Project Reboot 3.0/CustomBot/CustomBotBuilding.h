#pragma once

#include "CustomBot.h"

#include "CustomBotResources.h"
#include "FortKismetLibrary.h"
#include "addresses.h"

#include <cmath>


namespace CustomBotBuilding
{
	static ABuildingSMActor* BuildPiece(CustomBot& Bot, UClass* BuildingClass, const FVector& Location, const FRotator& Rotation, bool bMirrored = false, bool bKeepOverlaps = false);

	enum class EPieceType : uint8_t
	{
		Wall,
		Floor,
		Ramp,
		Roof,
	};

	static UObject* GetPieceItemDefinition(EPieceType Type)
	{
		switch (Type)
		{
		case EPieceType::Roof:
			return FindObject<UObject>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS");
		case EPieceType::Floor:
			return FindObject<UObject>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor");
		case EPieceType::Ramp:
			return FindObject<UObject>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W");
		case EPieceType::Wall:
		default:
			return FindObject<UObject>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall");
		}
	}

	static UObject* GetPieceMetadata(EPieceType Type)
	{
		switch (Type)
		{
		case EPieceType::Roof:
			return FindObject<UObject>(L"/Game/Building/EditModePatterns/Roof/EMP_Roof_RoofC.EMP_Roof_RoofC");
		case EPieceType::Floor:
			return FindObject<UObject>(L"/Game/Building/EditModePatterns/Floor/EMP_Floor_Floor.EMP_Floor_Floor");
		case EPieceType::Ramp:
			return FindObject<UObject>(L"/Game/Building/EditModePatterns/Stair/EMP_Stair_StairW.EMP_Stair_StairW");
		case EPieceType::Wall:
		default:
			return FindObject<UObject>(L"/Game/Building/EditModePatterns/Wall/EMP_Wall_Solid.EMP_Wall_Solid");
		}
	}

	static UClass* GetPieceClass(EPieceType Type)
	{
		switch (Type)
		{
		case EPieceType::Wall:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_Solid.PBWA_W1_Solid_C");
		case EPieceType::Floor:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_Floor.PBWA_W1_Floor_C");
		case EPieceType::Ramp:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_StairW.PBWA_W1_StairW_C");
		case EPieceType::Roof:
		default:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_RoofS.PBWA_W1_RoofS_C");
		}
	}


	static bool GridIndicesFromWorldLocation(UObject* SSS, const FVector& WorldLoc, int& OutX, int& OutY)
	{
		if (!SSS)
			return false;
		static auto Fn = FindObject<UFunction>(L"/Script/FortniteGame.BuildingStructuralSupportSystem.K2_GetGridIndicesFromWorldLoc");
		static int WOff = FindOffsetStruct("/Script/FortniteGame.BuildingStructuralSupportSystem.K2_GetGridIndicesFromWorldLoc", "WorldLoc", false);
		static int GOff = FindOffsetStruct("/Script/FortniteGame.BuildingStructuralSupportSystem.K2_GetGridIndicesFromWorldLoc", "OutGridIndices", false);
		static int ROff = FindOffsetStruct("/Script/FortniteGame.BuildingStructuralSupportSystem.K2_GetGridIndicesFromWorldLoc", "ReturnValue", false);
		if (!Fn || GOff == -1 || ROff == -1)
			return false;
		alignas(16) char Buf[0x100] = {};
		if (WOff != -1)
			*(FVector*)(Buf + WOff) = WorldLoc;
		SSS->ProcessEvent(Fn, Buf);
		if (!*(bool*)(Buf + ROff))
			return false;
		OutX = *(int*)(Buf + GOff);
		OutY = *(int*)(Buf + GOff + 4);
		return true;
	}

	static bool CellBoxFromIndices(UObject* SSS, int X, int Y, FVector& OutCenter, FVector& OutHalfExtent)
	{
		if (!SSS)
			return false;
		static auto Fn = FindObject<UFunction>(L"/Script/FortniteGame.BuildingStructuralSupportSystem.GetGridBox");
		static int COff = FindOffsetStruct("/Script/FortniteGame.BuildingStructuralSupportSystem.GetGridBox", "CellIndex", false);
		static int ROff = FindOffsetStruct("/Script/FortniteGame.BuildingStructuralSupportSystem.GetGridBox", "ReturnValue", false);
		if (!Fn || ROff == -1)
			return false;
		alignas(16) char Buf[0x100] = {};
		if (COff != -1)
		{
			*(int*)(Buf + COff) = X;
			*(int*)(Buf + COff + 4) = Y;
		}
		SSS->ProcessEvent(Fn, Buf);
		FVector Min = *(FVector*)(Buf + ROff);
		FVector Max = *(FVector*)(Buf + ROff + 12);
		OutCenter = (Min + Max) * 0.5f;
		OutHalfExtent = (Max - Min) * 0.5f;
		return true;
	}

	static bool SnapLocationToGrid(UObject* SSS, FVector& Loc)
	{
		int X, Y;
		if (!GridIndicesFromWorldLocation(SSS, Loc, X, Y))
			return false;
		FVector Center, Half;
		if (!CellBoxFromIndices(SSS, X, Y, Center, Half))
			return false;
		Loc.X = Center.X;
		Loc.Y = Center.Y;
		return true;
	}

	static float SnapYawToCardinal(float Yaw)
	{
		return std::round(Yaw / 90.0f) * 90.0f;
	}

	static bool CellCenterAhead(UObject* SSS, const FVector& From, float CardinalYaw, int Steps, FVector& OutCenter)
	{
		int BaseX, BaseY;
		if (!GridIndicesFromWorldLocation(SSS, From, BaseX, BaseY))
			return false;

		int DX = 0, DY = 0;
		switch (((((int)std::round(CardinalYaw / 90.0f)) % 4) + 4) % 4)
		{
		case 0: DX = 1; break;
		case 1: DY = 1; break;
		case 2: DX = -1; break;
		default: DY = -1; break;
		}

		FVector Center, Half;
		return CellBoxFromIndices(SSS, BaseX + DX * Steps, BaseY + DY * Steps, OutCenter, Half);
	}

	static void SelectPiece(CustomBot& Bot, EPieceType Type)
	{
		if (!Bot.IsReady() || !Bot.WorldInventory)
			return;

		auto PieceItemDef = GetPieceItemDefinition(Type);

		if (!PieceItemDef)
			return;

		bool bShouldUpdate = false;
		Bot.WorldInventory->AddItem((UFortItemDefinition*)PieceItemDef, &bShouldUpdate, 1);

		if (bShouldUpdate)
			Bot.WorldInventory->Update();
	}

	static ABuildingSMActor* BuildWall(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Wall), Location, Rotation, bMirrored);
	}

	static ABuildingSMActor* BuildFloor(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false, bool bKeepOverlaps = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Floor), Location, Rotation, bMirrored, bKeepOverlaps);
	}

	static ABuildingSMActor* BuildRamp(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Ramp), Location, Rotation, bMirrored);
	}

	static ABuildingSMActor* BuildRoof(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Roof), Location, Rotation, bMirrored);
	}

	static EPieceType PieceTypeOf(UClass* BuildingClass)
	{
		if (BuildingClass == GetPieceClass(EPieceType::Wall))
			return EPieceType::Wall;

		if (BuildingClass == GetPieceClass(EPieceType::Floor))
			return EPieceType::Floor;

		if (BuildingClass == GetPieceClass(EPieceType::Ramp))
			return EPieceType::Ramp;

		return EPieceType::Roof;
	}

	static ABuildingSMActor* BuildPiece(CustomBot& Bot, UClass* BuildingClass, const FVector& Location, const FRotator& Rotation, bool bMirrored, bool bKeepOverlaps)
	{
		if (!Bot.IsReady() || !Bot.Controller || !Bot.WorldInventory)
			return nullptr;

		if (!BuildingClass)
		{
			LOG_WARN(LogBots, "[BuildPiece] gate=null class (piece type not found in this version)");
			return nullptr;
		}

		SelectPiece(Bot, PieceTypeOf(BuildingClass));

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto PlayerStateAthena = Bot.PlayerState;

		if (!GameState || !PlayerStateAthena)
			return nullptr;

		auto StructuralSupportSystem = GameState->GetStructuralSupportSystem();

		std::string ClassName = BuildingClass ? BuildingClass->GetFullName() : "null";

		FVector BuildLoc = Location;
		FRotator BuildRot = Rotation;
		if (StructuralSupportSystem && SnapLocationToGrid(StructuralSupportSystem, BuildLoc))
		{
			if (std::abs(BuildLoc.X - Location.X) > 1.0f || std::abs(BuildLoc.Y - Location.Y) > 1.0f)
			{
				LOG_INFO(LogBots, "[BuildPiece] snapped loc ({:.0f},{:.0f},{:.0f}) -> cell ({:.0f},{:.0f},{:.0f})",
					Location.X, Location.Y, Location.Z, BuildLoc.X, BuildLoc.Y, BuildLoc.Z);
			}
		}
		float CardinalYaw = SnapYawToCardinal(BuildRot.Yaw);
		if (std::abs(CardinalYaw - BuildRot.Yaw) > 0.5f)
			LOG_INFO(LogBots, "[BuildPiece] snapped yaw {:.1f} -> {:.0f}", BuildRot.Yaw, CardinalYaw);
		BuildRot.Yaw = CardinalYaw;

		if (StructuralSupportSystem && !StructuralSupportSystem->IsWorldLocValid(BuildLoc))
		{
			LOG_INFO(LogBots, "[BuildPiece] gate=worldloc invalid loc=({:.0f},{:.0f},{:.0f}) class={}", BuildLoc.X, BuildLoc.Y, BuildLoc.Z, ClassName);
			return nullptr;
		}

		if (!GameState->IsPlayerBuildableClass(BuildingClass))
		{
			LOG_INFO(LogBots, "[BuildPiece] gate=unbuildable class={}", ClassName);
			return nullptr;
		}

		TArray<ABuildingSMActor*> ExistingBuildings;

		if (Addresses::CantBuild)
		{
			char idk;
			static __int64 (*CantBuild)(UObject*, UObject*, FVector, FRotator, char, TArray<ABuildingSMActor*>*, char*) = decltype(CantBuild)(Addresses::CantBuild);
			bool bCanBuild = !CantBuild(GetWorld(), BuildingClass, BuildLoc, BuildRot, bMirrored, &ExistingBuildings, &idk);

			if (!bCanBuild)
			{
				LOG_INFO(LogBots, "[BuildPiece] CantBuild rejected (bypassed for bot) reason={} loc=({:.0f},{:.0f},{:.0f}) class={}",
					(int)(uint8_t)idk, BuildLoc.X, BuildLoc.Y, BuildLoc.Z, ClassName);
			}
		}

		FTransform Transform{};
		Transform.Translation = BuildLoc;
		Transform.Rotation = BuildRot.Quaternion();
		Transform.Scale3D = { 1, 1, 1 };

		auto BuildingActor = GetWorld()->SpawnActor<ABuildingSMActor>(BuildingClass, Transform);

		if (!BuildingActor)
		{
			LOG_INFO(LogBots, "[BuildPiece] gate=spawn failed loc=({:.0f},{:.0f},{:.0f}) class={}", BuildLoc.X, BuildLoc.Y, BuildLoc.Z, ClassName);
			ExistingBuildings.FreeEngine();
			return nullptr;
		}

		bool bBuildFree = Bot.Controller->DoesBuildFree();

		if (!bBuildFree)
		{
			int MaterialCost = 10;

			auto MatDefinition = UFortKismetLibrary::K2_GetResourceItemDefinition(BuildingActor->GetResourceType());

			UFortItem* MatInstance = MatDefinition ? Bot.WorldInventory->FindItemInstance(MatDefinition) : nullptr;

			if (!MatInstance || MatInstance->GetItemEntry()->GetCount() < MaterialCost)
			{
				ExistingBuildings.FreeEngine();
				BuildingActor->SilentDie();
				return nullptr;
			}

			bool bShouldUpdate = false;
			Bot.WorldInventory->RemoveItem(MatInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, MaterialCost);

			if (bShouldUpdate)
				Bot.WorldInventory->Update();
		}

		for (int i = 0; i < ExistingBuildings.Num(); ++i)
		{
			auto ExistingBuilding = ExistingBuildings.At(i);
			if (ExistingBuilding == BuildingActor)
				continue;
			if (bKeepOverlaps)
				continue;
			ExistingBuilding->K2_DestroyActor();
		}

		ExistingBuildings.FreeEngine();

		BuildingActor->SetPlayerPlaced(true);
		BuildingActor->InitializeBuildingActor(Bot.Controller, BuildingActor, true);
		BuildingActor->SetTeam(PlayerStateAthena->GetTeamIndex());

		return BuildingActor;
	}
}