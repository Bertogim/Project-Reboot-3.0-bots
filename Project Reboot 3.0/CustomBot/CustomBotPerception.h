#pragma once

#include "CustomBot.h"

#include "GameplayStatics.h"
#include "FortPickup.h"
#include "BuildingContainer.h"
#include "BuildingFoundation.h"
#include "BuildingSMActor.h"
#include "FortItemDefinition.h"
#include "FortWeaponItemDefinition.h"
#include "KismetSystemLibrary.h"

// CustomBot - Percepcion.
//
// Escanea el mundo alrededor del bot y clasifica lo que encuentra (loot, cofres,
// estructuras, jugadores). Son capacidades puras (sin decisiones de IA).

namespace CustomBotPerception
{
	// Forward declarations (usadas antes de su definicion al final del modulo).
	static bool IsAlly(CustomBot& Bot, AFortPlayerStateAthena* Other);
	static bool IsEnemy(CustomBot& Bot, AFortPlayerStateAthena* Other);
	static AFortPlayerStateAthena* GetPlayerStateOf(AActor* Actor);

	// Tipos de item que puede contener un pickup / inventory.
	enum class EItemType : uint8_t
	{
		Weapon,
		Ammo,
		Consumable,
		Resource,
		BuildingPiece,
		Trap,
		Gadget,
		ConsumableDeco,
		Other,
	};

	// Clasifica un UFortItemDefinition en EItemType (sin asumir tipos C++ concretos).
	static EItemType ClassifyItemDefinition(UFortItemDefinition* ItemDefinition)
	{
		if (!ItemDefinition)
			return EItemType::Other;

		static auto FortWeaponItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponItemDefinition");
		static auto FortAmmoItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortAmmoItemDefinition");
		static auto FortConsumableItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortConsumableItemDefinition");
		static auto FortResourceItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortResourceItemDefinition");
		static auto FortBuildingItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortBuildingItemDefinition");
		static auto FortTrapItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortTrapItemDefinition");
		static auto FortGadgetItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortGadgetItemDefinition");
		static auto FortDecoItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortDecoItemDefinition");
		static auto FortEditToolItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortEditToolItemDefinition");

	#define IS(Class) (Class && ItemDefinition->IsA(Class))

		// OJO: orden importa. En esta version building pieces (p.ej.
		// BuildingItemData_Wall) heredan de FortWeaponItemDefinition, asi que
		// hay que comprobar los tipos de construccion/edicion ANTES que Weapon,
		// si no EquipFirstWeapon agarra el EditTool/Wall como "arma".
		if (IS(FortBuildingItemDefinitionClass))   return EItemType::BuildingPiece;
		if (IS(FortEditToolItemDefinitionClass))   return EItemType::BuildingPiece;
		if (IS(FortWeaponItemDefinitionClass))     return EItemType::Weapon;
		if (IS(FortAmmoItemDefinitionClass))       return EItemType::Ammo;
		if (IS(FortConsumableItemDefinitionClass)) return EItemType::Consumable;
		if (IS(FortResourceItemDefinitionClass))   return EItemType::Resource;
		if (IS(FortTrapItemDefinitionClass))       return EItemType::Trap;
		if (IS(FortGadgetItemDefinitionClass))     return EItemType::Gadget;
		if (IS(FortDecoItemDefinitionClass))       return EItemType::ConsumableDeco;

	#undef IS

		return EItemType::Other;
	}

	// Devuelve el tipo de item de un pickup (AFortPickup) en el suelo.
	static EItemType GetPickupItemType(AFortPickup* Pickup)
	{
		if (!Pickup)
			return EItemType::Other;

		auto Entry = Pickup->GetPrimaryPickupItemEntry();
		return Entry ? ClassifyItemDefinition(Entry->GetItemDefinition()) : EItemType::Other;
	}

	// Distancia (3D) desde el bot hasta un actor.
	static float DistanceToActor(CustomBot& Bot, AActor* Actor)
	{
		if (!Bot.IsReady() || !Actor)
			return FLT_MAX;

		return Bot.Pawn->GetDistanceTo(Actor);
	}

	// Barrido: obtiene una clase de actor dentro del radio alrededor del bot.
	// Devuelve la TArray de actores de esa clase (sin filtrar por distancia).
	static TArray<AActor*> GetAllActorsOfClassWithin(CustomBot& Bot, UClass* ActorClass, float Radius)
	{
		TArray<AActor*> Result;

		if (!Bot.IsReady() || !ActorClass || !Bot.Pawn)
			return Result;

		static auto World = GetWorld();

		TArray<AActor*> All = UGameplayStatics::GetAllActorsOfClass(World, ActorClass);

		FVector BotLocation = Bot.Pawn->GetActorLocation();

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor || Actor->IsActorBeingDestroyed())
				continue;

			if (DistanceToActor(Bot, Actor) <= Radius)
				Result.Add(Actor);
		}

		All.FreeEngine();
		return Result;
	}

	// Encuentra el actor de la clase dada mas cercano al bot dentro de Radius.
	static AActor* FindNearestActorOfClass(CustomBot& Bot, UClass* ActorClass, float Radius)
	{
		if (!Bot.IsReady() || !ActorClass)
			return nullptr;

		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, ActorClass, Radius);

		AActor* Nearest = nullptr;
		float NearestDist = Radius;

		FVector BotLocation = Bot.Pawn->GetActorLocation();

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor)
				continue;

			float D = DistanceToActor(Bot, Actor);

			if (D <= NearestDist)
			{
				NearestDist = D;
				Nearest = Actor;
			}
		}

		All.FreeEngine();
		return Nearest;
	}

	// --- Cache de barridos (bots pesados) -----------------------------------
	// Los barridos GetActorsOfClass sobre el mundo entero son lo mas caro del
	// bot (DoLooting hacia 4+ por frame; con 10 bots el servidor perdia ticks
	// -> movimiento lento/a saltos). Cada bucket (loot/jugadores/obstaculos)
	// refresca su cache UNA vez por ScanCooldown y el resto de finders del mismo
	// bucket (que miran los MISMOS actores del mundo) lo reutilizan sin volver a
	// barrer. Los refreshes viven en CustomBot (LootScanTime/...).

	inline constexpr float ScanCooldown = 0.35f;

	static float BotTime()
	{
		return UGameplayStatics::GetTimeSeconds(GetWorld());
	}

	// Pickup en cache utilizable (no destruido entre refrescos).
	static AFortPickup* UsablePickup(AFortPickup* Pickup)
	{
		return (Pickup && !Pickup->IsActorBeingDestroyed()) ? Pickup : nullptr;
	}

	// Refresca el cache de loot (pickups + cofres) si expiro. Con UN barrido de
	// FortPickup se clasifican todas las tipos de pickup en la misma pasada, y
	// con otro los cofres sin abrir. Antes eran 4+ barridos por frame.
	static void RefreshLootCache(CustomBot& Bot, float Radius)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (Bot.LootScanTime > 0.0f && Radius <= Bot.LootScanRadius && BotTime() - Bot.LootScanTime < ScanCooldown)
			return;

		Bot.LootScanTime = BotTime();
		Bot.LootScanRadius = Radius;

		// Pickups: un solo barrido, clasificando todos en una pasada.
		Bot.CachedNearestWeapon = nullptr;
		Bot.CachedNearestConsumable = nullptr;
		Bot.CachedNearestPickup = nullptr;

		static auto FortPickupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPickup");
		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, FortPickupClass, Radius);

		for (int i = 0; i < All.Num(); ++i)
		{
			auto Pickup = Cast<AFortPickup>(All.at(i));

			if (!Pickup || Pickup->IsActorBeingDestroyed())
				continue;

			float D = DistanceToActor(Bot, Pickup);

			if (D <= Radius && (!Bot.CachedNearestPickup || D < DistanceToActor(Bot, Bot.CachedNearestPickup)))
				Bot.CachedNearestPickup = Pickup;

			EItemType Type = GetPickupItemType(Pickup);

			if (Type == EItemType::Weapon && D <= Radius && (!Bot.CachedNearestWeapon || D < DistanceToActor(Bot, Bot.CachedNearestWeapon)))
				Bot.CachedNearestWeapon = Pickup;

			if (Type == EItemType::Consumable && D <= Radius && (!Bot.CachedNearestConsumable || D < DistanceToActor(Bot, Bot.CachedNearestConsumable)))
				Bot.CachedNearestConsumable = Pickup;
		}

		All.FreeEngine();

		// Cofres: un solo barrido.
		Bot.CachedNearestContainer = nullptr;

		static auto BuildingContainerClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingContainer");
		TArray<AActor*> AllContainers = GetAllActorsOfClassWithin(Bot, BuildingContainerClass, Radius);

		for (int i = 0; i < AllContainers.Num(); ++i)
		{
			auto Container = Cast<ABuildingContainer>(AllContainers.at(i));

			if (!Container || Container->IsActorBeingDestroyed() || Container->IsAlreadySearched())
				continue;

			float D = DistanceToActor(Bot, Container);

			if (D <= Radius && (!Bot.CachedNearestContainer || D < DistanceToActor(Bot, Bot.CachedNearestContainer)))
				Bot.CachedNearestContainer = Container;
		}

		AllContainers.FreeEngine();
	}

	// Pickup (loot en el suelo) mas cercano, opcionalmente filtrando por tipo de item.
	static AFortPickup* FindNearestPickup(CustomBot& Bot, float Radius, EItemType ItemTypeFilter = EItemType::Other, bool bAnyType = true)
	{
		// Tipos usados por la IA: leen el cache de loot (un barrido cada cooldown).
		if (bAnyType)
		{
			RefreshLootCache(Bot, Radius);
			return UsablePickup(Bot.CachedNearestPickup);
		}

		if (ItemTypeFilter == EItemType::Weapon)
		{
			RefreshLootCache(Bot, Radius);
			return UsablePickup(Bot.CachedNearestWeapon);
		}

		if (ItemTypeFilter == EItemType::Consumable)
		{
			RefreshLootCache(Bot, Radius);
			return UsablePickup(Bot.CachedNearestConsumable);
		}

		// Filtros poco usados (Ammo/Resource/...): barrido directo sin cache.
		static auto FortPickupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPickup");
		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, FortPickupClass, Radius);

		AFortPickup* Nearest = nullptr;
		float NearestDist = Radius;
		FVector BotLocation = Bot.Pawn->GetActorLocation();

		for (int i = 0; i < All.Num(); ++i)
		{
			auto Pickup = Cast<AFortPickup>(All.at(i));

			if (!Pickup || Pickup->IsActorBeingDestroyed())
				continue;

			if (!bAnyType && GetPickupItemType(Pickup) != ItemTypeFilter)
				continue;

			float D = DistanceToActor(Bot, Pickup);

			if (D <= NearestDist)
			{
				NearestDist = D;
				Nearest = Pickup;
			}
		}

		All.FreeEngine();
		return Nearest;
	}

	// Cofre (BuildingContainer) sin abrir mas cercano dentro del radio.
	static ABuildingContainer* FindNearestUnopenedContainer(CustomBot& Bot, float Radius)
	{
		RefreshLootCache(Bot, Radius);

		if (Bot.CachedNearestContainer &&
			(Bot.CachedNearestContainer->IsActorBeingDestroyed() || Bot.CachedNearestContainer->IsAlreadySearched()))
			Bot.CachedNearestContainer = nullptr;

		return Bot.CachedNearestContainer;
	}

	// Jugador (AFortPlayerPawn / AFortPlayerPawnAthena) mas cercano dentro del radio.
	// Excluye a otros bots custom y al propio bot.
	static AActor* FindNearestPlayer(CustomBot& Bot, float Radius)
	{
		static auto FortPlayerPawnClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerPawn");
		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, FortPlayerPawnClass, Radius);

		AActor* Nearest = nullptr;
		float NearestDist = Radius;
		FVector BotLocation = Bot.Pawn->GetActorLocation();

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor || Actor == Bot.Pawn || Actor->IsActorBeingDestroyed())
				continue;

			float D = DistanceToActor(Bot, Actor);

			if (D <= NearestDist)
			{
				NearestDist = D;
				Nearest = Actor;
			}
		}

		All.FreeEngine();
		return Nearest;
	}

	// Comprueba linea de vision (LOS) entre el bot y TargetLocation usando line trace.
	// Devuelve true si NO hay obstaculo (visibilidad clara).
	static bool HasLineOfSight(CustomBot& Bot, const FVector& TargetLocation, AActor* ActorToIgnore = nullptr)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector Start = Bot.Pawn->GetActorLocation();
		Start.Z += 60.0f; // altura de la cabeza

		FVector End = TargetLocation;
		End.Z += 60.0f;

		TArray<AActor*> ActorsToIgnore;

		if (ActorToIgnore)
			ActorsToIgnore.Add(ActorToIgnore);

		FHitResult* OutHit = nullptr;

		bool bHit = UKismetSystemLibrary::LineTraceSingle(
			GetWorld(),
			Start,
			End,
			ETraceTypeQuery::TraceTypeQuery1,
			false,
			ActorsToIgnore,
			EDrawDebugTrace::None,
			false,
			FLinearColor(1, 0, 0, 1),
			FLinearColor(0, 1, 0, 1),
			0.0f,
			&OutHit);

		ActorsToIgnore.FreeEngine();
		return bHit;
	}

	// Devuelve true si hay un obstaculo bloqueante directo hacia TargetLocation.
	static bool HasBlockingObstacle(CustomBot& Bot, const FVector& TargetLocation, float TraceRadius = 40.0f)
	{
		return !HasLineOfSight(Bot, TargetLocation);
	}

	// --- Escaneo generico ----------------------------------------------------

	// Barrido generico: todos los actores dentro del radio (FScanResult).
	// La IA (Parte 2) puede clasificar cada actor con ClassifyObstacle /
	// ClassifyItemDefinition segun lo que encuentre.
	static CBT::FScanResult ScanNearbyObjects(CustomBot& Bot, float Radius)
	{
		CBT::FScanResult Result;
		Result.Radius = Radius;

		if (!Bot.IsReady() || !Bot.Pawn)
			return Result;

		Result.Actors = GetAllActorsOfClassWithin(Bot, AActor::StaticClass(), Radius);
		return Result;
	}

	// --- Finders de loot (Section 11) -----------------------------------------

	// Cualquier pickup (loot en el suelo) mas cercano.
	static AFortPickup* FindNearbyLoot(CustomBot& Bot, float Radius)
	{
		return FindNearestPickup(Bot, Radius);
	}

	static AFortPickup* FindNearestWeapon(CustomBot& Bot, float Radius)
	{
		return FindNearestPickup(Bot, Radius, EItemType::Weapon, false);
	}

	static AFortPickup* FindNearestAmmo(CustomBot& Bot, float Radius)
	{
		return FindNearestPickup(Bot, Radius, EItemType::Ammo, false);
	}

	static AFortPickup* FindNearestConsumable(CustomBot& Bot, float Radius)
	{
		return FindNearestPickup(Bot, Radius, EItemType::Consumable, false);
	}

	static AFortPickup* FindNearestResource(CustomBot& Bot, float Radius)
	{
		return FindNearestPickup(Bot, Radius, EItemType::Resource, false);
	}

	// --- Equipos / relacion aliado-enemigo ------------------------------------

	static int GetBuildingTeam(ABuildingActor* Building)
	{
		if (!Building)
			return -1;

		static auto TeamIndexOffset = Building->GetOffset("TeamIndex", false);

		if (TeamIndexOffset != -1)
			return (int)Building->Get<uint8_t>(TeamIndexOffset);

		static auto TeamOffset = Building->GetOffset("Team", false);

		if (TeamOffset == -1)
			return -1;

		return (int)Building->Get<uint8_t>(TeamOffset);
	}

	// Team de cualquier actor: playerstate/pawn/controller o BuildingActor.
	static int GetActorTeam(AActor* Actor)
	{
		if (!Actor)
			return -1;

		if (auto PlayerState = Cast<AFortPlayerStateAthena>(Actor))
			return (int)PlayerState->GetTeamIndex();

		if (auto Pawn = Cast<AFortPlayerPawn>(Actor))
		{
			if (auto Controller = Pawn->GetController())
			{
				if (auto PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState()))
					return (int)PlayerState->GetTeamIndex();
			}

			return -1;
		}

		if (auto Building = Cast<ABuildingActor>(Actor))
			return GetBuildingTeam(Building);

		return -1;
	}

	// Clasifica un actor como obstaculo (EObstacleType) para la futura IA.
	static CBT::EObstacleType ClassifyObstacle(CustomBot& Bot, AActor* Actor)
	{
		if (!Actor)
			return CBT::EObstacleType::None;

		auto Building = Cast<ABuildingActor>(Actor);

		if (!Building)
			return CBT::EObstacleType::Actor;

		auto SMActor = Cast<ABuildingSMActor>(Actor);

		// Estructura construida por un jugador/bot.
		if (SMActor && SMActor->IsPlayerPlaced())
		{
			int BotTeam = Bot.PlayerState ? (int)Bot.PlayerState->GetTeamIndex() : -1;
			int BuildingTeam = GetBuildingTeam(Building);

			if (BotTeam != -1 && BuildingTeam == BotTeam)
				return CBT::EObstacleType::OwnStructure;

			if (BuildingTeam != -1)
				return CBT::EObstacleType::EnemyStructure;

			return CBT::EObstacleType::Structure;
		}

		// Objeto del mundo no colocado por un jugador (arboles, rocas, etc.).
		return CBT::EObstacleType::WorldObject;
	}

	// Refresca el cache de obstaculos (BuildingSMActor) si expiro.
	static void RefreshObstacleCache(CustomBot& Bot, float Radius)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (Bot.ObstacleScanTime > 0.0f && Radius <= Bot.ObstacleScanRadius && BotTime() - Bot.ObstacleScanTime < ScanCooldown)
			return;

		Bot.ObstacleScanTime = BotTime();
		Bot.ObstacleScanRadius = Radius;

		Bot.CachedNearestObstacle = nullptr;
		Bot.CachedObstacleType = CBT::EObstacleType::None;

		static auto BuildingSMActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingSMActor");
		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, BuildingSMActorClass, Radius);

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor || Actor == Bot.Pawn || Actor->IsActorBeingDestroyed())
				continue;

			float D = DistanceToActor(Bot, Actor);

			if (D <= Radius && (!Bot.CachedNearestObstacle || D < DistanceToActor(Bot, Bot.CachedNearestObstacle)))
			{
				Bot.CachedNearestObstacle = Actor;
				Bot.CachedObstacleType = ClassifyObstacle(Bot, Actor);
			}
		}

		All.FreeEngine();
	}

	// Estructura/obstaculo (ABuildingSMActor o ABuildingFoundation) mas cercano
	// dentro del radio, con su tipo clasificado.
	static AActor* FindNearestObstacle(CustomBot& Bot, float Radius, CBT::EObstacleType& OutType)
	{
		RefreshObstacleCache(Bot, Radius);

		if (Bot.CachedNearestObstacle && Bot.CachedNearestObstacle->IsActorBeingDestroyed())
			Bot.CachedNearestObstacle = nullptr;

		OutType = Bot.CachedObstacleType;
		return Bot.CachedNearestObstacle;
	}

	// Devuelve true si el camino directo hacia TargetLocation esta bloqueado.
	static bool IsPathBlocked(CustomBot& Bot, const FVector& TargetLocation)
	{
		return !HasLineOfSight(Bot, TargetLocation);
	}

	// Diferencia de altura (Z del objetivo - Z del bot). Positivo = subir,
	// negativo = bajar. Util para detectar montanas/pendientes (Section 9).
	static float GetHeightDifference(CustomBot& Bot, const FVector& TargetLocation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return 0.0f;

		return TargetLocation.Z - Bot.Pawn->GetActorLocation().Z;
	}

	// Busca un punto alternativo alcanzable por la IA para rodear un obstaculo.
	// Si hay LOS directo devuelve TargetLocation; si no, prueba un anillo de
	// puntos alrededor y devuelve el primero con LOS (o el objetivo si ninguno).
	static FVector FindReachablePoint(CustomBot& Bot, const FVector& TargetLocation, float ProbeRadius = 250.0f)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return TargetLocation;

		if (HasLineOfSight(Bot, TargetLocation))
			return TargetLocation;

		const float TwoPi = 3.14159265358979323846f * 2.0f;

		for (int i = 0; i < 8; ++i)
		{
			float Angle = TwoPi * float(i) / 8.0f;
			FVector Probe{ TargetLocation.X + FMath::Cos(Angle) * ProbeRadius, TargetLocation.Y + FMath::Sin(Angle) * ProbeRadius, TargetLocation.Z };

			if (HasLineOfSight(Bot, Probe))
				return Probe;
		}

		return TargetLocation;
	}

	// --- Jugadores / bots cercanos (con relacion de equipo) --------------------

	// Refresca el cache de jugadores (FortPlayerPawn) si expiro. UN barrido
	// clasifica en la misma pasada aliados y enemigos.
	static void RefreshPlayerCache(CustomBot& Bot, float Radius)
	{
		if (!Bot.IsReady() || !Bot.Pawn || !Bot.PlayerState)
			return;

		if (Bot.PlayerScanTime > 0.0f && Radius <= Bot.PlayerScanRadius && BotTime() - Bot.PlayerScanTime < ScanCooldown)
			return;

		Bot.PlayerScanTime = BotTime();
		Bot.PlayerScanRadius = Radius;

		Bot.CachedNearestEnemy = nullptr;
		Bot.CachedNearestAlly = nullptr;

		static auto FortPlayerPawnClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerPawn");
		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, FortPlayerPawnClass, Radius);

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor || Actor == Bot.Pawn || Actor->IsActorBeingDestroyed())
				continue;

			auto PlayerState = GetPlayerStateOf(Actor);

			if (!PlayerState || PlayerState == Bot.PlayerState)
				continue;

			float D = DistanceToActor(Bot, Actor);

			if (D > Radius)
				continue;

			if (IsEnemy(Bot, PlayerState) && (!Bot.CachedNearestEnemy || D < DistanceToActor(Bot, Bot.CachedNearestEnemy)))
				Bot.CachedNearestEnemy = Actor;

			if (IsAlly(Bot, PlayerState) && (!Bot.CachedNearestAlly || D < DistanceToActor(Bot, Bot.CachedNearestAlly)))
				Bot.CachedNearestAlly = Actor;
		}

		All.FreeEngine();
	}

	static AActor* FindNearestEnemy(CustomBot& Bot, float Radius)
	{
		RefreshPlayerCache(Bot, Radius);

		if (Bot.CachedNearestEnemy && Bot.CachedNearestEnemy->IsActorBeingDestroyed())
			Bot.CachedNearestEnemy = nullptr;

		return Bot.CachedNearestEnemy;
	}

	static AActor* FindNearestAlly(CustomBot& Bot, float Radius)
	{
		RefreshPlayerCache(Bot, Radius);

		if (Bot.CachedNearestAlly && Bot.CachedNearestAlly->IsActorBeingDestroyed())
			Bot.CachedNearestAlly = nullptr;

		return Bot.CachedNearestAlly;
	}

	static bool IsAlly(CustomBot& Bot, AFortPlayerStateAthena* Other)
	{
		return Bot.PlayerState && Other && Bot.PlayerState->GetTeamIndex() == Other->GetTeamIndex();
	}

	static bool IsEnemy(CustomBot& Bot, AFortPlayerStateAthena* Other)
	{
		return Bot.PlayerState && Other && Bot.PlayerState->GetTeamIndex() != Other->GetTeamIndex();
	}

	static AFortPlayerStateAthena* GetPlayerStateOf(AActor* Actor)
	{
		if (!Actor)
			return nullptr;

		if (auto PlayerState = Cast<AFortPlayerStateAthena>(Actor))
			return PlayerState;

		if (auto Pawn = Cast<AFortPlayerPawn>(Actor))
		{
			if (auto Controller = Pawn->GetController())
				return Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		}

		return nullptr;
	}
}
