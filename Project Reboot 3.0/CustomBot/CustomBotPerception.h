#pragma once

#include <unordered_map>

#include "CustomBot.h"

#include "GameplayStatics.h"
#include "FortPickup.h"
#include "BuildingContainer.h"
#include "BuildingFoundation.h"
#include "BuildingSMActor.h"
#include "FortItemDefinition.h"
#include "FortWeaponItemDefinition.h"
#include "KismetSystemLibrary.h"


namespace CustomBotPerception
{
	static bool IsAlly(CustomBot& Bot, AFortPlayerStateAthena* Other);
	static bool IsEnemy(CustomBot& Bot, AFortPlayerStateAthena* Other);
	static AFortPlayerStateAthena* GetPlayerStateOf(AActor* Actor);

	static const std::string& CachedPathForDef(UFortItemDefinition* Def)
	{
		static std::unordered_map<UFortItemDefinition*, std::string> Cache;
		static const std::string Empty;

		if (!Def)
			return Empty;

		auto It = Cache.find(Def);
		if (It != Cache.end())
			return It->second;

		Cache.emplace(Def, Def->GetPathName());
		return Cache.find(Def)->second;
	}

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

	static EItemType GetPickupItemType(AFortPickup* Pickup)
	{
		if (!Pickup)
			return EItemType::Other;

		auto Entry = Pickup->GetPrimaryPickupItemEntry();
		return Entry ? ClassifyItemDefinition(Entry->GetItemDefinition()) : EItemType::Other;
	}

	static float DistanceToActor(CustomBot& Bot, AActor* Actor)
	{
		if (!Bot.IsReady() || !Actor)
			return FLT_MAX;

		return Bot.Pawn->GetDistanceTo(Actor);
	}


	inline constexpr float SharedCooldown = 0.5f;

	struct SharedBucket
	{
		UClass* Cls = nullptr;
		float Time = -1.0f;
		TArray<AActor*> Actors;
		std::vector<FVector> Locations;
	};

	static std::vector<SharedBucket>& SharedBuckets()
	{
		static std::vector<SharedBucket> Buckets;
		return Buckets;
	}

	static SharedBucket& SharedBucketByClass(UClass* Cls)
	{
		static SharedBucket Empty;

		if (!Cls)
			return Empty;

		auto& Buckets = SharedBuckets();
		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

		for (auto& Bucket : Buckets)
		{
			if (Bucket.Cls == Cls)
			{
				if (Bucket.Time < 0.0f || Now - Bucket.Time >= SharedCooldown)
				{
					if (Bucket.Actors.Num() > 0)
						Bucket.Actors.FreeEngine();
					Bucket.Actors = UGameplayStatics::GetAllActorsOfClass(GetWorld(), Cls);

					Bucket.Locations.clear();
					Bucket.Locations.reserve(Bucket.Actors.Num());
					for (int i = 0; i < Bucket.Actors.Num(); ++i)
						Bucket.Locations.push_back(Bucket.Actors.at(i)->GetActorLocation());

					Bucket.Time = Now;
				}
				return Bucket;
			}
		}

		SharedBuckets().push_back({ Cls, Now, UGameplayStatics::GetAllActorsOfClass(GetWorld(), Cls) });
		auto& NewBucket = SharedBuckets().back();
		NewBucket.Locations.reserve(NewBucket.Actors.Num());
		for (int i = 0; i < NewBucket.Actors.Num(); ++i)
			NewBucket.Locations.push_back(NewBucket.Actors.at(i)->GetActorLocation());
		return NewBucket;
	}

	struct CachedChest
	{
		FVector Location;
		bool bSearched;
	};

	static const std::vector<CachedChest>& CachedChests()
	{
		static std::vector<CachedChest> Chests;
		static float LastBuild = -1.0f;

		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

		if (LastBuild < 0.0f || Now - LastBuild >= SharedCooldown)
		{
			LastBuild = Now;
			Chests.clear();

			static auto BuildingContainerClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingContainer");
			auto& Bucket = SharedBucketByClass(BuildingContainerClass);

			for (int i = 0; i < Bucket.Actors.Num(); ++i)
			{
				auto Container = Cast<ABuildingContainer>(Bucket.Actors.at(i));

				if (!Container || Container->IsActorBeingDestroyed())
					continue;

				CachedChest Chest;
				Chest.Location = Bucket.Locations[i];
				Chest.bSearched = Container->IsAlreadySearched();
				Chests.push_back(Chest);
			}
		}

		return Chests;
	}

	static int LootValueScore(AFortPickup* Pickup)
	{
		if (!Pickup)
			return 0;

		auto Entry = Pickup->GetPrimaryPickupItemEntry();

		if (!Entry)
			return 0;

		auto Def = Entry->GetItemDefinition();

		if (!Def)
			return 0;

		int Score = (Entry->GetLevel() > 0 ? Entry->GetLevel() : 1) * 100;

		const std::string& Path = CachedPathForDef(Def);

		static const char* CatDirs[] = { "/Sniper/", "/Launchers/", "/Shotgun/", "/Rifle/", "/SMG/", "/Pistol/" };
		static const int CatBonus[] = { 600, 550, 500, 400, 300, 150 };

		for (int i = 0; i < 6; ++i)
		{
			if (Path.find(CatDirs[i]) != std::string::npos)
			{
				Score += CatBonus[i];
				break;
			}
		}

		if (Path.find("ShieldPotion") != std::string::npos || Path.find("Shield") != std::string::npos)
			Score += 450;
		else if (Path.find("Medkit") != std::string::npos || Path.find("MedKit") != std::string::npos)
			Score += 350;
		else if (Path.find("Bandage") != std::string::npos)
			Score += 200;

		return Score;
	}

	static bool IsBetterPickup(CustomBot& Bot, AFortPickup* Cand, AFortPickup* Cur)
	{
		int CandScore = LootValueScore(Cand);
		int CurScore = Cur ? LootValueScore(Cur) : 0;

		if (CandScore != CurScore)
			return CandScore > CurScore;

		return !Cur || DistanceToActor(Bot, Cand) < DistanceToActor(Bot, Cur);
	}

	static TArray<AActor*> GetAllActorsOfClassWithin(CustomBot& Bot, UClass* ActorClass, float Radius)
	{
		TArray<AActor*> Result;

		if (!Bot.IsReady() || !ActorClass || !Bot.Pawn)
			return Result;

		auto& Bucket = SharedBucketByClass(ActorClass);
		FVector BotLocation = Bot.Pawn->GetActorLocation();
		float RadiusSq = Radius * Radius;

		int N = Bucket.Actors.Num();
		for (int i = 0; i < N; ++i)
		{
			AActor* Actor = Bucket.Actors.at(i);

			if (!Actor || Actor->IsActorBeingDestroyed())
				continue;

			const FVector& L = Bucket.Locations[i];
			float DX = L.X - BotLocation.X;
			float DY = L.Y - BotLocation.Y;
			float DZ = L.Z - BotLocation.Z;

			if (DX * DX + DY * DY + DZ * DZ <= RadiusSq)
				Result.Add(Actor);
		}

		return Result;
	}

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


	inline constexpr float ScanCooldown = 0.35f;

	static float BotTime()
	{
		return UGameplayStatics::GetTimeSeconds(GetWorld());
	}

	static AFortPickup* UsablePickup(AFortPickup* Pickup)
	{
		return (Pickup && !Pickup->IsActorBeingDestroyed()) ? Pickup : nullptr;
	}

	static void RefreshLootCache(CustomBot& Bot, float Radius)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return;

		if (Bot.LootScanTime > 0.0f && Radius <= Bot.LootScanRadius && BotTime() - Bot.LootScanTime < ScanCooldown)
			return;

		Bot.LootScanTime = BotTime();
		Bot.LootScanRadius = Radius;

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

			if (Type == EItemType::Weapon && D <= Radius && IsBetterPickup(Bot, Pickup, Bot.CachedNearestWeapon))
				Bot.CachedNearestWeapon = Pickup;

			if (Type == EItemType::Consumable && D <= Radius && IsBetterPickup(Bot, Pickup, Bot.CachedNearestConsumable))
				Bot.CachedNearestConsumable = Pickup;
		}

		All.FreeEngine();

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

	static AFortPickup* FindNearestPickup(CustomBot& Bot, float Radius, EItemType ItemTypeFilter = EItemType::Other, bool bAnyType = true)
	{
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

	static ABuildingContainer* FindNearestUnopenedContainer(CustomBot& Bot, float Radius)
	{
		RefreshLootCache(Bot, Radius);

		if (Bot.CachedNearestContainer &&
			(Bot.CachedNearestContainer->IsActorBeingDestroyed() || Bot.CachedNearestContainer->IsAlreadySearched()))
			Bot.CachedNearestContainer = nullptr;

		return Bot.CachedNearestContainer;
	}

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

	static bool HasLineOfSight(CustomBot& Bot, const FVector& TargetLocation, AActor* ActorToIgnore = nullptr)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return false;

		FVector Start = Bot.Pawn->GetActorLocation();
		Start.Z += 60.0f;

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

	static bool HasBlockingObstacle(CustomBot& Bot, const FVector& TargetLocation, float TraceRadius = 40.0f)
	{
		return !HasLineOfSight(Bot, TargetLocation);
	}


	static CBT::FScanResult ScanNearbyObjects(CustomBot& Bot, float Radius)
	{
		CBT::FScanResult Result;
		Result.Radius = Radius;

		if (!Bot.IsReady() || !Bot.Pawn)
			return Result;

		Result.Actors = GetAllActorsOfClassWithin(Bot, AActor::StaticClass(), Radius);
		return Result;
	}


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

	static int GetActorTeam(AActor* Actor)
	{
		if (!Actor)
			return -1;

		if (auto PlayerState = Cast<AFortPlayerStateAthena>(Actor))
			return (int)PlayerState->GetTeamIndex();

		if (auto Pawn = Cast<AFortPlayerPawn>(Actor))
		{
			if (auto PS = GetPlayerStateOf(Pawn))
				return (int)PS->GetTeamIndex();

			return -1;
		}

		if (auto Building = Cast<ABuildingActor>(Actor))
			return GetBuildingTeam(Building);

		return -1;
	}

	static CBT::EObstacleType ClassifyObstacle(CustomBot& Bot, AActor* Actor)
	{
		if (!Actor)
			return CBT::EObstacleType::None;

		auto Building = Cast<ABuildingActor>(Actor);

		if (!Building)
			return CBT::EObstacleType::Actor;

		auto SMActor = Cast<ABuildingSMActor>(Actor);

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

		return CBT::EObstacleType::WorldObject;
	}

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

	static AActor* FindNearestObstacle(CustomBot& Bot, float Radius, CBT::EObstacleType& OutType)
	{
		RefreshObstacleCache(Bot, Radius);

		if (Bot.CachedNearestObstacle && Bot.CachedNearestObstacle->IsActorBeingDestroyed())
			Bot.CachedNearestObstacle = nullptr;

		OutType = Bot.CachedObstacleType;
		return Bot.CachedNearestObstacle;
	}

	static AActor* FindFrontObstacle(CustomBot& Bot, float Radius, const FVector& FacingDir, float MinDot, CBT::EObstacleType& OutType, bool (*Filter)(AActor*) = nullptr)
	{
		OutType = CBT::EObstacleType::None;

		if (!Bot.IsReady() || !Bot.Pawn)
			return nullptr;

		FVector BotLoc = Bot.Pawn->GetActorLocation();

		FVector Fwd{ FacingDir.X, FacingDir.Y, 0.0f };
		float FwdLenSq = Fwd | Fwd;

		if (FwdLenSq <= 0.0001f)
			return nullptr;

		float InvFwdLen = 1.0f / FMath::Sqrt(FwdLenSq);
		Fwd.X *= InvFwdLen;
		Fwd.Y *= InvFwdLen;

		AActor* Best = nullptr;
		float BestDist = 1e30f;

		static auto BuildingSMActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingSMActor");
		TArray<AActor*> All = GetAllActorsOfClassWithin(Bot, BuildingSMActorClass, Radius);

		for (int i = 0; i < All.Num(); ++i)
		{
			AActor* Actor = All.at(i);

			if (!Actor || Actor == Bot.Pawn || Actor->IsActorBeingDestroyed())
				continue;

			if (Filter && !Filter(Actor))
				continue;

			FVector ToObj = Actor->GetActorLocation() - BotLoc;
			ToObj.Z = 0.0f;
			float LenSq = ToObj | ToObj;

			if (LenSq <= 0.0001f)
				continue;

			float InvLen = 1.0f / FMath::Sqrt(LenSq);
			ToObj.X *= InvLen;
			ToObj.Y *= InvLen;

			if ((Fwd | ToObj) < MinDot)
				continue;

			float D = DistanceToActor(Bot, Actor);

			if (D <= Radius && D < BestDist)
			{
				Best = Actor;
				BestDist = D;
			}
		}

		All.FreeEngine();

		OutType = Best ? ClassifyObstacle(Bot, Best) : CBT::EObstacleType::None;
		return Best;
	}

	static bool IsPathBlocked(CustomBot& Bot, const FVector& TargetLocation)
	{
		return !HasLineOfSight(Bot, TargetLocation);
	}

	static float GetHeightDifference(CustomBot& Bot, const FVector& TargetLocation)
	{
		if (!Bot.IsReady() || !Bot.Pawn)
			return 0.0f;

		return TargetLocation.Z - Bot.Pawn->GetActorLocation().Z;
	}

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
			{
				if (auto PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState()))
					return PlayerState;
			}

			static int PSOffset = -1;
			if (PSOffset == -1)
				PSOffset = Pawn->GetOffset("PlayerState", false);

			if (PSOffset != -1)
			{
				auto* PS = Cast<AFortPlayerStateAthena>(Pawn->Get<UObject*>(PSOffset));
				if (PS)
					return PS;
			}
		}

		return nullptr;
	}
}
