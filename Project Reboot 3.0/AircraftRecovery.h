#pragma once

#include <cmath>
#include <vector>
#include "FortGameModeAthena.h"
#include "FortPlayerControllerAthena.h"
#include "FortPlayerPawn.h"
#include "globals.h"

// Estado compartido entre unidades de compilacion. Solo se modifica en el tick
// del servidor o en sus hooks, nunca desde el hilo de la interfaz.
namespace AircraftRecovery
{
	struct FEntry
	{
		int Index = -1;
		int Serial = 0;
		bool InAircraft = false;
		bool Armed = false;
		bool MovementReleased = false;
		float Until = 0.f;
		float NextAttempt = 0.f;
		int Attempts = 0;
		FVector Destination{};
	};
	inline std::vector<FEntry> Entries;
	inline FVector LastBusLocation{};
	inline bool HasBusLocation = false;
	inline int Session = -1;

	inline bool ValidLocation(const FVector& P)
	{
		return std::isfinite(P.X) && std::isfinite(P.Y)
			&& std::isfinite(P.Z) && P.Z > 1000.f;
	}

	inline void RememberBus(const FVector& P)
	{
		if (ValidLocation(P))
		{
			LastBusLocation = P;
			HasBusLocation = true;
		}
	}

	inline FEntry* Find(AFortPlayerControllerAthena* PC)
	{
		auto Item = PC ? GetItemByIndex(PC->InternalIndex) : nullptr;
		if (!Item || Item->IsPendingKill())
			return nullptr;
		for (auto& E : Entries)
			if (E.Index == PC->InternalIndex && E.Serial == Item->SerialNumber)
				return &E;
		FEntry E{};
		E.Index = PC->InternalIndex;
		E.Serial = Item->SerialNumber;
		Entries.push_back(E);
		return &Entries.back();
	}

	inline void Arm(AFortPlayerControllerAthena* PC, float Now, const FVector& Bus)
	{
		if (!ValidLocation(Bus))
			return;
		auto E = Find(PC);
		if (!E || E->Armed)
			return; // RPC repetidas no prolongan los diez segundos.
		E->Armed = true;
		E->Until = Now + 10.f;
		E->Destination = Bus;
		E->Destination.Z -= 300.f;
		E->NextAttempt = Now;
		E->Attempts = 0;
	}

	inline void ReleaseMovement(AFortPlayerControllerAthena* PC, AFortPlayerPawn* Pawn)
	{
		if (!PC || !Pawn)
			return;
		auto SetFlag = [](UObject* Object, const char* Name, bool Value)
		{
			const auto Offset = Object->GetOffset(Name, false);
			auto Property = Offset != -1 ? Object->GetProperty(Name, false) : nullptr;
			if (Property)
				Object->SetBitfieldValue(Offset, GetFieldMask(Property), Value);
		};
		SetFlag(Pawn, "bSimGravityDisabled", false);
		SetFlag(Pawn, "bDisableMovementAndTurnInPlace", false);
		SetFlag(Pawn, "bAllowMovement", true);
		const auto Offset = Pawn->GetOffset("CharacterMovement", false);
		auto CM = Offset != -1 ? Pawn->Get<UObject*>(Offset) : nullptr;
		if (!CM)
			return;
		auto Activate = FindObject<UFunction>(L"/Script/Engine.ActorComponent.Activate");
		auto Tick = FindObject<UFunction>(L"/Script/Engine.ActorComponent.SetComponentTickEnabled");
		if (Activate)
		{
			bool Reset = true;
			if (Activate->GetPropertiesSize() == sizeof(Reset))
				CM->ProcessEvent(Activate, &Reset);
		}
		if (Tick)
		{
			bool Enabled = true;
			if (Tick->GetPropertiesSize() == sizeof(Enabled))
				CM->ProcessEvent(Tick, &Enabled);
		}
		const auto ModeOffset = CM->GetOffset("MovementMode", false);
		if (ModeOffset != -1 && CM->Get<uint8_t>(ModeOffset) == 0)
		{
			auto Fn = FindObject<UFunction>(L"/Script/Engine.CharacterMovementComponent.SetMovementMode");
			struct { uint8_t Mode; uint8_t CustomMode; } Params{3, 0};
			if (Fn && Fn->GetPropertiesSize() == sizeof(Params))
				CM->ProcessEvent(Fn, &Params);
		}
		Pawn->ForceNetUpdate();
	}

	inline void Tick(AFortGameModeAthena* Mode)
	{
		auto State = Mode ? Mode->GetGameStateAthena() : nullptr;
		if (!State)
			return;
		if (Session != Globals::AmountOfListens
			|| State->GetGamePhase() <= EAthenaGamePhase::Warmup)
		{
			Session = Globals::AmountOfListens;
			Entries.clear();
			HasBusLocation = false;
			return;
		}
		if (State->GetGamePhase() >= EAthenaGamePhase::EndGame)
		{
			Entries.clear();
			return;
		}
		if (InitialPopulationSafeZonePhase <= 0 && !Globals::bLateGame.load())
			return;

		const float Now = State->GetServerWorldTimeSeconds();
		const auto AircraftsOffset = State->GetOffset("Aircrafts", false);
		if (AircraftsOffset != -1)
		{
			const auto& Aircrafts = State->Get<TArray<AActor*>>(AircraftsOffset);
			for (int i = 0; i < Aircrafts.Num(); ++i)
				if (Aircrafts.at(i) && !Aircrafts.at(i)->IsActorBeingDestroyed())
					RememberBus(Aircrafts.at(i)->GetActorLocation());
		}
		else
		{
			const auto Offset = State->GetOffset("Aircraft", false);
			auto Bus = Offset != -1 ? State->Get<AActor*>(Offset) : nullptr;
			if (Bus && !Bus->IsActorBeingDestroyed())
				RememberBus(Bus->GetActorLocation());
		}

		auto& Alive = Mode->GetAlivePlayers();
		for (int i = 0; i < Alive.Num(); ++i)
		{
			auto PC = Cast<AFortPlayerControllerAthena>(Alive.At(i));
			auto PS = PC ? PC->GetPlayerStateAthena() : nullptr;
			if (!PS)
				continue;
			auto E = Find(PC);
			if (!E)
				continue;
			const bool InAircraft = PS->IsInAircraft();
			if (E->InAircraft && !InAircraft && HasBusLocation)
				Arm(PC, Now, LastBusLocation);
			E->InAircraft = InAircraft;
			if (!E->Armed || InAircraft || Now > E->Until
				|| Now < E->NextAttempt || E->Attempts >= 12)
				continue;
			auto Pawn = Cast<AFortPlayerPawn>(PC->GetMyFortPawn());
			// No resucitar eliminados ni crear pawns duplicados. Si el pawn llega
			// tarde, se comprueba en los ticks siguientes dentro de la ventana.
			if (!Pawn || Pawn->IsActorBeingDestroyed()
				|| Pawn->GetHealth() <= 0.f || Pawn->IsDBNO())
				continue;
			const FVector P = Pawn->GetActorLocation();
			const bool Invalid = !std::isfinite(P.X) || !std::isfinite(P.Y)
				|| !std::isfinite(P.Z) || P.Z < 0.f
				|| (std::abs(P.X) < 1.f && std::abs(P.Y) < 1.f && std::abs(P.Z) < 1.f);
			if (!E->MovementReleased)
			{
				ReleaseMovement(PC, Pawn);
				E->MovementReleased = true;
				LOG_INFO(LogDev, "[aircraft-recovery] Released movement after aircraft exit: player={}.",
					PS->GetPlayerID());
			}
			if (!Invalid)
				continue;
			E->NextAttempt = Now + 0.5f;
			++E->Attempts;
			const bool Moved = Pawn->TeleportTo(E->Destination, Pawn->GetActorRotation());
			Pawn->ForceNetUpdate();
			LOG_WARN(LogDev, "[aircraft-recovery] player={} attempt={} moved={} from=({},{},{}) to=({},{},{}).",
				PS->GetPlayerID(), E->Attempts, Moved, P.X, P.Y, P.Z,
				E->Destination.X, E->Destination.Y, E->Destination.Z);
		}
		// No guardar punteros a controladores desconectados o destruidos.
		for (auto It = Entries.begin(); It != Entries.end(); )
		{
			auto Item = GetItemByIndex(It->Index);
			if (!Item || !Item->Object || Item->IsPendingKill()
				|| Item->SerialNumber != It->Serial)
				It = Entries.erase(It);
			else
				++It;
		}
	}
}

// Copia de la ultima geometria aceptada, para reparar reinicios que no pasan
// por SetZoneToIndex. No se usa en BR normal ni con zonas inversas de lategame.
namespace PopulationStormGuard
{
	inline bool Ready = false;
	inline int Session = -1;
	inline int Phase = -1;
	inline float Start = 0.f;
	inline float Finish = 0.f;
	inline FVector LastCenter{}, NextCenter{};
	inline float LastRadius = 0.f, NextRadius = 0.f;

	inline bool Fields(AFortSafeZoneIndicator* I)
	{
		return I && I->GetOffset("LastCenter", false) != -1
			&& I->GetOffset("NextCenter", false) != -1
			&& I->GetOffset("LastRadius", false) != -1
			&& I->GetOffset("NextRadius", false) != -1;
	}

	inline void Capture(AFortGameModeAthena* M)
	{
		auto S = M ? M->GetGameStateAthena() : nullptr;
		auto I = M ? M->GetSafeZoneIndicator() : nullptr;
		if (!S || !Fields(I))
			return;
		const auto Offset = M->GetOffset("SafeZonePhase", false);
		if (Offset == -1)
			return;
		Phase = M->Get<int>(Offset);
		Start = I->GetSafeZoneStartShrinkTime();
		Finish = I->GetSafeZoneFinishShrinkTime();
		LastCenter = I->Get<FVector>(I->GetOffset("LastCenter"));
		NextCenter = I->Get<FVector>(I->GetOffset("NextCenter"));
		LastRadius = I->Get<float>(I->GetOffset("LastRadius"));
		NextRadius = I->Get<float>(I->GetOffset("NextRadius"));
		Ready = std::isfinite(Start) && std::isfinite(Finish)
			&& std::isfinite(LastRadius) && std::isfinite(NextRadius)
			&& LastRadius > 0.f && NextRadius > 0.f;
		Session = Globals::AmountOfListens;
	}

	inline void Restore(AFortGameModeAthena* M)
	{
		auto S = M ? M->GetGameStateAthena() : nullptr;
		auto I = M ? M->GetSafeZoneIndicator() : nullptr;
		if (!Ready || !S || !Fields(I))
			return;
		const auto MO = M->GetOffset("SafeZonePhase", false);
		const auto SO = S->GetOffset("SafeZonePhase", false);
		if (MO == -1 || SO == -1)
			return;
		M->Get<int>(MO) = Phase;
		S->Get<int>(SO) = Phase;
		I->GetSafeZoneStartShrinkTime() = Start;
		I->GetSafeZoneFinishShrinkTime() = Finish;
		I->Get<FVector>(I->GetOffset("LastCenter")) = LastCenter;
		I->Get<FVector>(I->GetOffset("NextCenter")) = NextCenter;
		I->Get<float>(I->GetOffset("LastRadius")) = LastRadius;
		I->Get<float>(I->GetOffset("NextRadius")) = NextRadius;
		I->ForceNetUpdate();
		S->ForceNetUpdate();
	}

	inline void Tick(AFortGameModeAthena* M)
	{
		auto S = M ? M->GetGameStateAthena() : nullptr;
		if (!S)
			return;
		if (Session != Globals::AmountOfListens
			|| S->GetGamePhase() <= EAthenaGamePhase::Warmup)
		{
			Ready = false;
			Session = Globals::AmountOfListens;
			return;
		}
		if (!Ready || InitialPopulationSafeZonePhase <= 0
			|| Globals::bLateGame.load() || S->GetGamePhase() >= EAthenaGamePhase::EndGame)
			return;
		auto I = M->GetSafeZoneIndicator();
		const auto MO = M->GetOffset("SafeZonePhase", false);
		const auto SO = S->GetOffset("SafeZonePhase", false);
		if (!Fields(I) || MO == -1 || SO == -1)
			return;
		const int Current = M->Get<int>(MO);
		// Comparar los extremos de interpolacion, no Radius/Center actuales:
		// estos ultimos cambian legitimamente mientras se cierra el circulo.
		auto SameFloat = [](float A, float B)
		{
			return std::isfinite(A) && std::isfinite(B) && std::abs(A - B) <= 0.05f;
		};
		auto SameVector = [&](const FVector& A, const FVector& B)
		{
			return SameFloat(A.X, B.X) && SameFloat(A.Y, B.Y) && SameFloat(A.Z, B.Z);
		};
		const bool SamePhaseOverwrite = Current == Phase
			&& (!SameFloat(I->GetSafeZoneStartShrinkTime(), Start)
				|| !SameFloat(I->GetSafeZoneFinishShrinkTime(), Finish)
				|| !SameFloat(I->Get<float>(I->GetOffset("LastRadius")), LastRadius)
				|| !SameFloat(I->Get<float>(I->GetOffset("NextRadius")), NextRadius)
				|| !SameVector(I->Get<FVector>(I->GetOffset("LastCenter")), LastCenter)
				|| !SameVector(I->Get<FVector>(I->GetOffset("NextCenter")), NextCenter));
        // A lagging replicated state alone must not restore all geometry
        // and timers on every tick.
        if (Current == Phase && !SamePhaseOverwrite && S->Get<int>(SO) < Phase)
        {
            S->Get<int>(SO) = Phase;
            S->ForceNetUpdate();
            return;
        }
		if (Current < Phase || SamePhaseOverwrite)
		{
			Restore(M);
			LOG_WARN(LogZone, "[population-zone] Blocked overwrite: current={}, protected={}, same-phase reset={}.",
				Current, Phase, SamePhaseOverwrite);
		}
	}
}
