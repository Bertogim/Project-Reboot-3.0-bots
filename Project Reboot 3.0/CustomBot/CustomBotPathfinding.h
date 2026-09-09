#pragma once

#include "CustomBot.h"

#include "CustomBotDoors.h"

#include <vector>

// CustomBot - Pathfinding (navmesh del mundo).
//
// Consulta el navmesh real de Fortnite (AthenaNavSystem/FortNavMesh) para
// calcular una ruta entre dos puntos y la devuelve como una polilinea de
// waypoints que UpdateMovement recorre en vez de ir en linea recta.
//
// Verificado en Dump/ObjectsDump.txt:
//   Function /Script/Engine.NavigationSystem:FindPathToLocationSynchronously
//     - ReturnValue / FilterClass / PathfindingContext / PathEnd / PathStart / WorldContextObject
//   ArrayProperty /Script/Engine.NavigationPath:PathPoints  (TArray<FVector>)
//   Function /Script/Engine.NavigationPath:IsPartial / IsValid
//   Class /Script/FortniteGame.AthenaNavSystem / FortNavMesh
//
// TODO-PATH: la ruta se re-consulta como mucho cada PathQueryCooldown desde
// CustomBotMovement::UpdateMovement (FindPathToLocationSynchronously es cara).
// Si la consulta navmesh falla (sin malla, destino dentro de un edificio), cae
// a la ruta puerta-a-puerta de RouteThroughDoors ("loot -> puertas -> puertas ->
// bot") y, si tampoco, el llamador sigue con linea recta + CustomBotBreak.

namespace CustomBotPathfinding
{
	// Alcance del barrido de puertas para la ruta puerta-a-puerta.
	inline constexpr float kDoorsRouteRadius = 1500.0f;
	// Tope de puertas encadenadas en una ruta (evita rutas absurdas gigantes).
	inline constexpr int kMaxDoorsInRoute = 6;
	// Distancia minima entre pasos de la ruta puerta-a-puerta (filtra puertas
	// pegadas que anadarian ruido a la polilinea).
	inline constexpr float kMinDoorStep = 250.0f;

	// Devuelve el sistema de navegacion del mundo (NavigationSystem). nullptr
	// si el mundo/partida no tiene navmesh cargado (p.ej. pre-partida).
	static UObject* GetNavSystem()
	{
		int NavOffset = GetWorld()->GetOffset("NavigationSystem", false);

		if (NavOffset == -1)
			return nullptr;

		return GetWorld()->Get<UObject*>(NavOffset);
	}

	// Calcula la ruta navmesh desde Start hasta End. Devuelve true y rellena
	// OutPoints (sin incluir Start) si el navmesh devolvio camino. Los puntos
	// se copian del UNavigationPath a un std::vector PROPIO: el TArray de
	// PathPoints vive en el objeto del engine y NO se libera aqui (doble free).
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

		// Relleno manual del buffer de params (mismo patron que SetRotation).
		*(UObject**)(__int64(Params) + WorldContextOffset) = GetWorld();
		*(FVector*)(__int64(Params) + PathStartOffset) = Start;
		*(FVector*)(__int64(Params) + PathEndOffset) = End;
		// PathfindingContext / FilterClass quedan a nullptr (malla por defecto).

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

	// Ruta puerta-a-puerta como fallback cuando el navmesh no encuentra camino
	// (interiores de edificios / POIs con malla rota): se encadena desde el
	// destino (loot) hacia el bot pasando por puertas ("loot -> puertas -> ... ->
	// puerta mas cercana al bot -> bot"). Devuelve true y rellena OutPoints con
	// el orden de marcha del bot (puerta mas cercana primero, loot al final).
	static bool RouteThroughDoors(CustomBot& Bot, const FVector& End, std::vector<FVector>& OutPoints)
	{
		OutPoints.clear();

		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector Start = Bot.Pawn->GetActorLocation();
		std::vector<AActor*> Doors = CustomBotDoors::FindDoorsWithin(Bot, kDoorsRouteRadius);

		if (Doors.empty())
			return false;

		// Cadena construida DESDE el destino (loot) hacia el bot: cada paso elige
		// la puerta sin usar que acerca mas hacia el bot (greedy).
		std::vector<bool> Used(Doors.size(), false);
		std::vector<FVector> Chain; // orden: cercana al loot ... cercana al bot

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

				// Greedy: minimiza distancia al paso actual + peso hacia el bot.
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

			// Saltar puertas aun pegadas al paso anterior (ruido).
			if (Chain.empty() || StepDist >= kMinDoorStep)
				Chain.push_back(DLoc);
			else
				Chain.back() = DLoc; // mover el ultimo paso (mas representativo)

			Cur = DLoc;

			// Fin: la puerta ya esta muy cerca del bot; no hace falta seguir.
			if (FMath::Sqrt((DLoc - Start) | (DLoc - Start)) <= kMinDoorStep)
				break;
		}

		if (Chain.empty())
			return false;

		OutPoints.clear();

		// Orden de marcha del bot: gate mas cercana primero, luego cadena, y el
		// loot al final (la puerta mas cercana al loot es la ultima de la cadena).
		for (size_t i = Chain.size(); i-- > 0;)
			OutPoints.push_back(Chain[i]);

		OutPoints.push_back(End);

		return true;
	}
}