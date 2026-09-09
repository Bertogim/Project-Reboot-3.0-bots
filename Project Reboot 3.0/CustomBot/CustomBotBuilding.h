#pragma once

#include "CustomBot.h"

#include "CustomBotResources.h"
#include "FortKismetLibrary.h"
#include "addresses.h"

#include <cmath>

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
	// bKeepOverlaps=true deja las estructuras solapadas intactas (la demo del DebBot
	// construye un suelo bajo el bot en la misma celda que la rampa y quiere ambas
	// para destruirlas luego con el pico).
	static ABuildingSMActor* BuildPiece(CustomBot& Bot, UClass* BuildingClass, const FVector& Location, const FRotator& Rotation, bool bMirrored = false, bool bKeepOverlaps = false);

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
	// cbbuild acepta un ClassPath). Confirmadas en ObjectsDump.txt de esta version
	// (v3.5): las piezas de jugador estan en /Game/Building/ActorBlueprints/Player/
	// Wood/L1/PBWA_W1_* (los paths SM_Stair_Base/etc. viejos dan 0 matches).
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

	// --- Grid de construccion: mismo snap que el juego ---
	// El servidor NO aplica un snap al spawn (spawna el BuildLoc/BuildRot que le
	// llegan); el que ajusta al grid es el cliente del jugador real. Para el bot
	// replicamos ese ajuste usando las funciones nativas del grid del juego
	// (UBuildingStructuralSupportSystem, presentes en ObjectsDump v3.5):
	//   K2_GetGridIndicesFromWorldLoc(WorldLoc)->(bool, OutGridIndices)
	//   K2_GetWorldLocFromGridIndices(GridIndices)->(bool, OutWorldLoc)
	//   GetGridBox(CellIndex)->FBox
	// Activo en BuildPiece (todas las builds) y usado por el debug sequence para
	// elegir la celda (la rampa se coloca en la celda inmediatamente adelante).

	// Indices de la celda que contiene un punto del mundo.
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

	// Centro + mitad del tamano de una celda (via GetGridBox; FBox = 2x FVector).
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

	// Snap X/Y de un punto al centro de la celda del grid que lo CONTAINE.
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

	// Redondea un yaw a la cardinal mas cercana (90 grados).
	static float SnapYawToCardinal(float Yaw)
	{
		return std::round(Yaw / 90.0f) * 90.0f;
	}

	// Centro de la celda Steps pasos adelante de From en la direccion cardinal.
	static bool CellCenterAhead(UObject* SSS, const FVector& From, float CardinalYaw, int Steps, FVector& OutCenter)
	{
		int BaseX, BaseY;
		if (!GridIndicesFromWorldLocation(SSS, From, BaseX, BaseY))
			return false;

		int DX = 0, DY = 0;
		switch (((((int)std::round(CardinalYaw / 90.0f)) % 4) + 4) % 4)
		{
		case 0: DX = 1; break;   // +X
		case 1: DY = 1; break;   // +Y
		case 2: DX = -1; break;  // -X
		default: DY = -1; break; // -Y
		}

		FVector Center, Half;
		return CellBoxFromIndices(SSS, BaseX + DX * Steps, BaseY + DY * Steps, OutCenter, Half);
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

	// Construye una pieza en Location/Rotation:
	//   1. valida (IsWorldLocValid / IsPlayerBuildableClass) igual que el hook
	//   2. spawna el ABuildingSMActor
	//   3. consume material (Wood/Stone/Metal segun GetResourceType)
	//   4. SetPlayerPlaced + InitializeBuildingActor + SetTeam
	// Devuelve el actor construido (o nullptr si fallo).
	static ABuildingSMActor* BuildPiece(CustomBot& Bot, UClass* BuildingClass, const FVector& Location, const FRotator& Rotation, bool bMirrored, bool bKeepOverlaps)
	{
		if (!Bot.IsReady() || !Bot.Controller || !Bot.WorldInventory)
			return nullptr;

		if (!BuildingClass)
		{
			LOG_WARN(LogBots, "[BuildPiece] gate=null class (piece type not found in this version)");
			return nullptr;
		}

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto PlayerStateAthena = Bot.PlayerState;

		if (!GameState || !PlayerStateAthena)
			return nullptr;

		// Validaciones de zona (opcionales pero iguales al hook).
		auto StructuralSupportSystem = GameState->GetStructuralSupportSystem();

		std::string ClassName = BuildingClass ? BuildingClass->GetFullName() : "null";

		// Snap de TODAS las builds al grid del juego (X/Y al centro de celda) y
		// rotacion a cardinales (90 grados), como hace el cliente real al colocar.
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
			// El hook del jugador real usa CantBuild como validacion anti-trampa de
			// colocacion legal. Para el bot NO bloqueamos su resultado (la demo necesita
			// construir aunque el sitio no sea legal para un jugador), pero SI se ejecuta
			// para obtener las estructuras solapadas que luego se destruyen y para
			// diagnosticar el motivo (idk) de un rechazo eventual.
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

		// Consumo de materiales.
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

		// Destruir estructuras que ocupen el lugar (a menos que la pieza pida
		// conservarlas, p.ej. el suelo de la demo que comparte celda con la rampa).
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