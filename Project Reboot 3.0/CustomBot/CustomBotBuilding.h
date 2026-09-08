#pragma once

#include "CustomBot.h"

#include "CustomBotResources.h"
#include "FortKismetLibrary.h"
#include "addresses.h"

// CustomBot - Construccion.
//
// Selecciona la pieza como un jugador (equipa el BuildingItemData correspondiente)
// y construye el actor replicando el pipeline de ServerCreateBuildingActorHook
// (spawn del ABuildingSMActor + consumo de materiales + inicializacion + team).
//
// NOTA: el BuildingClass del actor construible (wall/floor/ramp/roof) varia por
// version y no esta hardcodeado en el repo. BuildPiece() acepta la clase del actor;
// GetPieceClass() devuelve la clase por convencion (rutas comunes). El debug command
// cbbuild acepta un ClassPath opcional para esta version.

namespace CustomBotBuilding
{
	// BuildPiece (definida mas abajo; BuildWall/BuildFloor/BuildRamp/BuildRoof la usan).
	static ABuildingSMActor* BuildPiece(CustomBot& Bot, UClass* BuildingClass, const FVector& Location, const FRotator& Rotation, bool bMirrored = false);

	// Tipo de pieza construible.
	enum class EPieceType : uint8_t
	{
		Wall,
		Floor,
		Ramp,   // escalera/rampa
		Roof,
	};

	// Item definition de la pieza (lo que "selecciona" el jugador en el build mode).
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

	// Metadata de edicion asociada a la pieza (usada por el building tool).
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

	// Devuelve la clase del ABuildingSMActor para la pieza (best-effort).
	// Las rutas de las clases de pieza cambian por version y no estan garantizadas;
	// si devuelve nullptr el llamador debe proveer la clase (p.ej. el debug command
	// cbbuild acepta un ClassPath). Tambien se puede obtener de la seleccion del
	// jugador via BroadcastRemoteClientInfo->RemoteBuildableClass.
	static UClass* GetPieceClass(EPieceType Type)
	{
		switch (Type)
		{
		case EPieceType::Wall:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/BuildingAssets/Wall/SM_Wall_Base.SM_Wall_Base_C");
		case EPieceType::Floor:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/BuildingAssets/Floor/SM_Floor_Base.SM_Floor_Base_C");
		case EPieceType::Ramp:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/BuildingAssets/Stair/SM_Stair_Base.SM_Stair_Base_C");
		case EPieceType::Roof:
		default:
			return FindObject<UClass>(L"/Game/Building/ActorBlueprints/Player/BuildingAssets/Roof/SM_Roof_Base.SM_Roof_Base_C");
		}
	}

	// Selecciona la pieza: da el item al inventario y lo equipa. Esto, junto con
	// el building tool equipado, cambia la piece seleccionada (ServerExecuteInventoryItemHook).
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

	// Construye una rampa (`BuildRamp`) para superar terreno (Section 15).
	static ABuildingSMActor* BuildWall(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Wall), Location, Rotation, bMirrored);
	}

	static ABuildingSMActor* BuildFloor(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Floor), Location, Rotation, bMirrored);
	}

	static ABuildingSMActor* BuildRamp(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Ramp), Location, Rotation, bMirrored);
	}

	static ABuildingSMActor* BuildRoof(CustomBot& Bot, const FVector& Location, const FRotator& Rotation, bool bMirrored = false)
	{
		return BuildPiece(Bot, GetPieceClass(EPieceType::Roof), Location, Rotation, bMirrored);
	}

	// Construye una pieza en Location/Rotation:
	//   1. valida (IsWorldLocValid / IsPlayerBuildableClass) igual que el hook
	//   2. spawna el ABuildingSMActor
	//   3. consume material (Wood/Stone/Metal segun GetResourceType)
	//   4. SetPlayerPlaced + InitializeBuildingActor + SetTeam
	// Devuelve el actor construido (o nullptr si fallo).
	static ABuildingSMActor* BuildPiece(CustomBot& Bot, UClass* BuildingClass, const FVector& Location, const FRotator& Rotation, bool bMirrored)
	{
		if (!Bot.IsReady() || !Bot.Controller || !Bot.WorldInventory)
			return nullptr;

		if (!BuildingClass)
			return nullptr;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto PlayerStateAthena = Bot.PlayerState;

		if (!GameState || !PlayerStateAthena)
			return nullptr;

		// Validaciones de zona (opcionales pero iguales al hook).
		auto StructuralSupportSystem = GameState->GetStructuralSupportSystem();

		if (StructuralSupportSystem && !StructuralSupportSystem->IsWorldLocValid(Location))
			return nullptr;

		if (!GameState->IsPlayerBuildableClass(BuildingClass))
			return nullptr;

		TArray<ABuildingSMActor*> ExistingBuildings;

		if (Addresses::CantBuild)
		{
			char idk;
			static __int64 (*CantBuild)(UObject*, UObject*, FVector, FRotator, char, TArray<ABuildingSMActor*>*, char*) = decltype(CantBuild)(Addresses::CantBuild);
			bool bCanBuild = !CantBuild(GetWorld(), BuildingClass, Location, Rotation, bMirrored, &ExistingBuildings, &idk);

			if (!bCanBuild)
			{
				ExistingBuildings.Free();
				return nullptr;
			}
		}

		FTransform Transform{};
		Transform.Translation = Location;
		Transform.Rotation = Rotation.Quaternion();
		Transform.Scale3D = { 1, 1, 1 };

		auto BuildingActor = GetWorld()->SpawnActor<ABuildingSMActor>(BuildingClass, Transform);

		if (!BuildingActor)
		{
			ExistingBuildings.Free();
			return nullptr;
		}

		// Consumo de materiales.
		bool bBuildFree = Bot.Controller->DoesBuildFree();

		if (!bBuildFree)
		{
			int MaterialCost = 10;

			auto MatDefinition = UFortKismetLibrary::K2_GetResourceItemDefinition(BuildingActor->GetResourceType());

			UFortItem* MatInstance = MatDefinition ? Bot.WorldInventory->FindItemInstance(MatDefinition) : nullptr;

			if (!MatInstance || MatInstance->GetItemEntry()->GetCount() < MaterialCost)
			{
				ExistingBuildings.Free();
				BuildingActor->SilentDie();
				return nullptr;
			}

			bool bShouldUpdate = false;
			Bot.WorldInventory->RemoveItem(MatInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, MaterialCost);

			if (bShouldUpdate)
				Bot.WorldInventory->Update();
		}

		// Destruir estructuras que ocupen el lugar.
		for (int i = 0; i < ExistingBuildings.Num(); ++i)
		{
			auto ExistingBuilding = ExistingBuildings.At(i);
			ExistingBuilding->K2_DestroyActor();
		}

		ExistingBuildings.Free();

		BuildingActor->SetPlayerPlaced(true);
		BuildingActor->InitializeBuildingActor(Bot.Controller, BuildingActor, true);
		BuildingActor->SetTeam(PlayerStateAthena->GetTeamIndex());

		return BuildingActor;
	}
}