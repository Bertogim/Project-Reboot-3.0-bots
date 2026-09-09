#pragma once

#include "CustomBot.h"

#include "GameplayStatics.h"

// CustomBot - Puertas.
//
// Detecta puertas (BuildingWall con componente de puerta de esta version) y
// las abre de forma nativa. La puerta cerrada impide tanto el paso fisico como
// el navmesh (FortNavArea_ClosedDoors): al abrirla se desactiva su collision
// (bDoorCollisionDisabled = true) y el bot puede seguir. El navmesh se
// actualiza en runtime cuando cambia el estado de la puerta (FortNavSystem).
//
// Verificado en Dump/ObjectsDump.txt:
//   Class /Script/FortniteGame.BuildingWall
//   Function /Script/FortniteGame.BuildingWall:IsDoorComponent / IsDoorOpen
//   BoolProperty BuildingWall:bDoorCollisionDisabled / bDoorOpen
//
// TODO-PATH: apertura nativa por propiedad replicada + ForceNetUpdate (mismo
// patron que SetAlreadySearched de los cofres, robusto y barato). Alternativa
// "real de jugador" (mismo efecto, mas cara): invocar
// FortPlayerController:ServerAttemptInteract con ReceivingActor=puerta -
// probar en build si anima mejor la apertura.

namespace CustomBotDoors
{
	// Clase BuildingWall (puertas de interiores) resuelta una sola vez.
	static UClass* BuildingWallClass()
	{
		static auto DoorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingWall");
		return DoorClass;
	}

	// Escribe un bool (bitfield o nativo) por nombre de propiedad. Devuelve
	// false si la propiedad no existe. Misma tecnica que SetBitfieldValue de
	// CustomBotMovement (bRunPhysicsWithNoController).
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

	// Lee un bool (bitfield o nativo) por nombre de propiedad. Devuelve false
	// si la propiedad no existe (Out no se toca en ese caso).
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

	// true si el actor es una puerta (BuildingWall con bDoorOpen/bDoorCollisionDisabled)
	// y no esta destruida.
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

	// true si la puerta esta CERRADA (bloquea el paso). Si bDoorCollisionDisabled
	// esta a true ya cuenta como abierta (la collision esta desactivada).
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

	// Todas las puertas (BuildingWall) dentro del radio, no destruidas.
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

	// Puerta cerrada mas cercana que esta ENFRENTE del bot (conos delanteros,
	// no requiere LOS: basta con que este mirando hacia ella a corta distancia).
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

			// "Enfrente": cono delantero amplio (0.55 = ~57 grados a cada lado).
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

	// Abre la puerta de forma nativa: bDoorOpen=true + bDoorCollisionDisabled=true
	// y replica el cambio a los clientes (ForceNetUpdate). Devuelve true si era
	// una puerta. TODO-PATH: en esta version el estado de puerta lo replica
	// BuildingWall (bDoorOpen / OnRep_bDoorOpen verificados en el dump).
	static bool OpenDoor(AActor* Door)
	{
		if (!IsDoor(Door))
			return false;

		SetBoolProperty(Door, "bDoorOpen", true);
		SetBoolProperty(Door, "bDoorCollisionDisabled", true);

		// Replica el cambio a los clientes (los OnRep animan la puerta).
		Door->ForceNetUpdate();

		return true;
	}

	// Conveniencia usada por UpdateMovement / CustomBotBreak: si hay una puerta
	// cerrada enfrente la abre. Devuelve true si abrio algo.
	static bool TryOpenDoorInFront(CustomBot& Bot, float MaxDistance = 320.0f)
	{
		auto Door = FindDoorInFront(Bot, MaxDistance);

		if (!Door)
			return false;

		return OpenDoor(Door);
	}
}