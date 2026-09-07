#pragma once

#include <algorithm>
#include <cctype>

#include "FortGameModeAthena.h"
#include "OnlineReplStructs.h"
#include "FortAthenaAIBotController.h"
#include "BuildingContainer.h"
#include "FortPickup.h"
#include "FortWorldItemDefinition.h"
#include "FortWeaponItemDefinition.h"
#include "FortSafeZoneIndicator.h"
#include "KismetSystemLibrary.h"
#include "EngineTypes.h"
#include "GameplayTagContainer.h"
#include "botnames.h"

// Windows.h defines min/max macros that collide with std::min/std::max.
// Bot AI code uses std:: algorithms extensively; disable the macros here.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// Shared battlefield audio: when any bot fires, it records the shot so nearby bots can
// "hear" it and investigate (see PlayerBot::Tick hearing path).
static inline float LastGlobalGunshotTime = -100.0f;
static inline FVector LastGlobalGunshotLocation{};

class BotPOI
{
	FVector CenterLocation;
	FVector Range; // this just has to be FVector2D
};

class BotPOIEncounter
{
public:
	int NumChestsSearched;
	int NumAmmoBoxesSearched;
	int NumPlayersEncountered;
};

enum class EBotPersonalityType : uint8_t
{
	Novato = 0,
	Casual = 1,
	Agresivo = 2,
	Defensivo = 3,
	Pro = 4,
	Random = 5
};

struct BotPersonality
{
	float Aggression;    // 0..1 how willing to engage / push
	float AimSkill;      // 0..1 accuracy
	float BuildSkill;    // 0..1 building/cover usage
	float LootSkill;     // 0..1 loot efficiency / prioritization
	float Awareness;     // 0..1 perception radius & reaction
	float RiskTolerance; // 0..1 tolerance to fights when disadvantageous
};

enum class EBotState : uint8_t
{
	InBus = 0,
	ChoosingLanding = 1,
	Jumping = 2,
	Gliding = 3,
	Landing = 4,
	Looting = 5,
	Exploring = 6,
	Farming = 7,
	SearchingEnemy = 8,
	Fighting = 9,
	Defending = 10,
	Healing = 11,
	Rotating = 12,
	EndGame = 13
};

namespace BotMath // tiny self-contained math helpers so we don't depend on the (missing) FVector/FMath helpers
{
	static inline float RandomRange(float Min, float Max)
	{
		return Min + std::rand() * (1.0f / float(RAND_MAX)) * (Max - Min);
	}

	static inline int RandomInt(int Min, int Max) // inclusive
	{
		if (Max <= Min) return Min;
		return Min + std::rand() % (Max - Min + 1);
	}

	static inline float Dist3D(const FVector& A, const FVector& B)
	{
		auto DX = A.X - B.X, DY = A.Y - B.Y, DZ = A.Z - B.Z;
		return std::sqrt(DX * DX + DY * DY + DZ * DZ);
	}

	static inline float Dist2D(const FVector& A, const FVector& B)
	{
		auto DX = A.X - B.X, DY = A.Y - B.Y;
		return std::sqrt(DX * DX + DY * DY);
	}

	static inline FVector Normalize(FVector V)
	{
		auto Length = std::sqrt(V.SizeSquared());
		if (Length < 0.0001f)
			return FVector(0, 0, 0);
		return V * (1.0f / Length);
	}

	static inline float Dot(const FVector& A, const FVector& B)
	{
		return A | B;
	}

	static inline FRotator LookAtRotation(FVector From, FVector To)
	{
		FVector Dir = To - From;
		auto HorizontalLength = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y);
		FRotator Out{};
		Out.Yaw = std::atan2(double(Dir.Y), double(Dir.X)) * 180.0 / 3.14159265358979323846;
		Out.Pitch = -std::atan2(double(Dir.Z), double(HorizontalLength)) * 180.0 / 3.14159265358979323846;
		Out.Roll = 0;
		return Out;
	}

	static inline FVector DirectionFromYaw(float YawDegrees)
	{
		float Rad = YawDegrees * 3.14159265358979323846f / 180.0f;
		return FVector(std::cos(Rad), std::sin(Rad), 0);
	}
}

namespace BotWeapons // rough weapon classification from asset names (version-safe approximation)
{
	static inline std::string Lower(std::string S)
	{
		for (auto& C : S)
			C = char(std::tolower((unsigned char)C));
		return S;
	}

	static inline float GetWeaponScore(UFortWeaponItemDefinition* Def)
	{
		if (!Def) return 0.0f;
		std::string N = Lower(Def->GetPathName());
		if (N.find("harvest") != std::string::npos || N.find("pickaxe") != std::string::npos || N.find("pickax") != std::string::npos)
			return 0.0f;
		if (N.find("launcher") != std::string::npos || N.find("rocket") != std::string::npos)
			return 115.0f;
		if (N.find("sniper") != std::string::npos)
			return 100.0f;
		if (N.find("shotgun") != std::string::npos)
			return 85.0f;
		if (N.find("rifle") != std::string::npos || N.find("assault") != std::string::npos)
			return 70.0f;
		if (N.find("smg") != std::string::npos || N.find("submachine") != std::string::npos)
			return 55.0f;
		if (N.find("pistol") != std::string::npos)
			return 35.0f;
		return 40.0f;
	}

	static inline bool IsFirearm(UFortWeaponItemDefinition* Def)
	{
		return GetWeaponScore(Def) >= 25.0f;
	}

	static inline void GetFireStats(UFortWeaponItemDefinition* Def, float& OutInterval, float& OutDamage)
	{
		if (!Def)
		{
			OutInterval = 0.3f;
			OutDamage = 20.0f;
			return;
		}
		std::string N = Lower(Def->GetPathName());
		if (N.find("sniper") != std::string::npos) { OutInterval = 1.7f; OutDamage = 105.0f; }
		else if (N.find("launcher") != std::string::npos || N.find("rocket") != std::string::npos) { OutInterval = 4.0f; OutDamage = 130.0f; }
		else if (N.find("shotgun") != std::string::npos) { OutInterval = 0.95f; OutDamage = 70.0f; }
		else if (N.find("rifle") != std::string::npos || N.find("assault") != std::string::npos) { OutInterval = 0.13f; OutDamage = 28.0f; }
		else if (N.find("smg") != std::string::npos || N.find("submachine") != std::string::npos) { OutInterval = 0.08f; OutDamage = 16.0f; }
		else if (N.find("pistol") != std::string::npos) { OutInterval = 0.24f; OutDamage = 20.0f; }
		else { OutInterval = 0.3f; OutDamage = 22.0f; }
	}
}

class PlayerBot
{
public:
	static inline UClass* PawnClass = nullptr;
	static inline UClass* ControllerClass = nullptr;

	AController* Controller = nullptr; // This can be 1. AFortAthenaAIBotController OR AFortPlayerControllerAthena
	bool bIsAthenaController = false;
	AFortPlayerPawnAthena* Pawn = nullptr;
	AFortPlayerStateAthena* PlayerState = nullptr;
	BotPOIEncounter currentBotEncounter;
	int TotalPlayersEncountered;
	std::vector<BotPOI> POIsTraveled;
	float NextJumpTime = 1.0f;

	// ---------- Bot AI ----------
	EBotState BotState = EBotState::InBus;
	EBotPersonalityType PersonalityType = EBotPersonalityType::Random;
	BotPersonality Personality{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

	FVector LandingTarget{};
	bool bHasLandingTarget = false;
	FVector MoveTarget{};
	bool bHasMoveTarget = false;
	FVector LastKnownEnemyLocation{};
	bool bHasLastKnownEnemyLocation = false;

	AFortPlayerPawnAthena* CurrentTarget = nullptr;
	AFortPlayerPawnAthena* CurrentTargetNext = nullptr;

	float LastPerceptTime = -100.0f;
	float LastDecisionTime = -100.0f;
	float LastContainerScanTime = -100.0f;
	float LastPickupScanTime = -100.0f;
	float LastEnemyScanTime = -100.0f;
	float LastMoveTargetChange = -100.0f;
	float LastShotTime = -100.0f;
	float LastDamageTakenTime = -100.0f;
	float LastHealTime = -100.0f;
	float LastBuildTime = -100.0f;
	float HealingStartedTime = -100.0f;
	float AirborneStartTime = -100.0f;
	float LandedTime = -100.0f;
	float StrafeSeed = 0.0f;
	float AimErrorOffset = 0.0f;
	float TrackedHealth = 100.0f;
	float TrackedShield = 100.0f;
	float LastEnemySeenTime = -100.0f;
	float ContainerSearchStartTime = -100.0f;
	float DefendingStartTime = -100.0f;
	int CurrentStrafeDir = 1;
	int TotalContainersSearched = 0;
	bool bWasAirborne = false;
	bool bHasEnemy = false;
	EBotState PrevState = EBotState::Exploring;
	ABuildingContainer* CurrentContainer = nullptr;
	AFortPawn* LastPawnChecked = nullptr;
	FVector EndGameHoldPoint{};
	bool bHasEndGameHoldPoint = false;
	float LastLookAroundTime = -100.0f;
	float LookAroundUntil = -100.0f;
	float LastShotHeardTime = -100.0f;
	FVector LastShotHeardLocation{};
	float CurrentThreatScore = 0.0f; // 0 = weak, increasing = stronger relative to us
	bool bFightIsUnfavorable = false;

	// NOTE: all bot AI methods (SetupBotAI, Tick, AssignRandomPersonality and the
	// helpers below) are implemented inline further down in this class body.

	void OnPlayerEncountered()
	{
		currentBotEncounter.NumPlayersEncountered++;
		TotalPlayersEncountered++;
	}

	void MoveToNewPOI()
	{
		if (!Pawn)
			return;

		auto Container = FindNearestContainer(6000.0f);

		if (Container)
		{
			MoveTarget = Container->GetActorLocation();
			MoveTarget.Z = Pawn->GetActorLocation().Z;
			bHasMoveTarget = true;
			LastMoveTargetChange = UGameplayStatics::GetTimeSeconds(GetWorld());
			return;
		}

		FVector MyLoc = Pawn->GetActorLocation();
		FVector Center = GetSafeZoneCenter();
		float Angle = BotMath::RandomRange(0, 360);
		float Dist = BotMath::RandomRange(1500, 3500);
		FVector Bias = Center.SizeSquared() > 0.0001f ? BotMath::Normalize(Center - MyLoc) : FVector(0, 0, 0);
		FVector RandomDir = BotMath::DirectionFromYaw(Angle);
		MoveTarget = MyLoc + (Bias + RandomDir) * Dist;
		MoveTarget.Z = MyLoc.Z;
		bHasMoveTarget = true;
		LastMoveTargetChange = UGameplayStatics::GetTimeSeconds(GetWorld());
	}

	// ------------------------- Bot AI -------------------------

	void AssignRandomPersonality()
	{
		int Roll = BotMath::RandomInt(0, 4);
		PersonalityType = EBotPersonalityType(Roll);

		switch (PersonalityType)
		{
		case EBotPersonalityType::Novato:
			Personality = { 0.25f, 0.25f, 0.20f, 0.40f, 0.30f, 0.25f };
			break;
		case EBotPersonalityType::Casual:
			Personality = { 0.50f, 0.45f, 0.40f, 0.60f, 0.50f, 0.50f };
			break;
		case EBotPersonalityType::Agresivo:
			Personality = { 0.85f, 0.50f, 0.45f, 0.60f, 0.55f, 0.80f };
			break;
		case EBotPersonalityType::Defensivo:
			Personality = { 0.35f, 0.50f, 0.80f, 0.55f, 0.70f, 0.25f };
			break;
		case EBotPersonalityType::Pro:
		default:
			Personality = { 0.70f, 0.90f, 0.85f, 0.90f, 0.90f, 0.70f };
			break;
		}

		// small per-bot randomization so no two bots feel identical
		auto Jitter = [](float& V)
		{
			V = std::max(0.0f, std::min(1.0f, V + BotMath::RandomRange(-0.1f, 0.1f)));
		};
		Jitter(Personality.Aggression);
		Jitter(Personality.AimSkill);
		Jitter(Personality.BuildSkill);
		Jitter(Personality.LootSkill);
		Jitter(Personality.Awareness);
		Jitter(Personality.RiskTolerance);

		StrafeSeed = UGameplayStatics::GetTimeSeconds(GetWorld());
	}

	void SetupBotAI()
	{
		AssignRandomPersonality();
		bWasAirborne = false;
		bHasEnemy = false;
		bHasLandingTarget = false;
		bHasMoveTarget = false;
		bHasLastKnownEnemyLocation = false;
		CurrentTarget = nullptr;
		CurrentContainer = nullptr;
		ContainerSearchStartTime = -100.0f;

		bool bInAircraft = PlayerState ? PlayerState->IsInAircraft() : false;
		BotState = bInAircraft ? EBotState::InBus : EBotState::Looting;

		if (bInAircraft)
		{
			float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
			NextJumpTime = Now + BotMath::RandomRange(3.0f, 10.0f - Personality.Aggression * 3.0f);
		}

		if (Pawn)
		{
			TrackedHealth = Pawn->GetHealth();
			TrackedShield = Pawn->GetShield();
			LastPawnChecked = Pawn;
		}
	}

	AFortPlayerControllerAthena* GetAthenaController()
	{
		return Cast<AFortPlayerControllerAthena>(Controller);
	}

	AFortInventory* GetBotInventory()
	{
		if (auto FortPC = Cast<AFortPlayerController>(Controller))
			return FortPC->GetWorldInventory();
		return nullptr;
	}

	UObject* GetCharacterMovement()
	{
		if (!Pawn)
			return nullptr;
		static auto CharacterMovementOffset = Pawn->GetOffset("CharacterMovement");
		return Pawn->Get(CharacterMovementOffset);
	}

	uint8 GetMovementMode()
	{
		auto CM = GetCharacterMovement();
		if (!CM)
			return 255;
		static auto MovementModeOffset = CM->GetOffset("MovementMode");
		return CM->Get<uint8>(MovementModeOffset);
	}

	bool IsPawnAirborne()
	{
		if (!Pawn)
			return false;
		// MOVE_None(0) / Walking(1) / NavWalking(2) = grounded; Falling(3)/Swimming(4)/Flying(5)/Custom(6) = airborne
		uint8 Mode = GetMovementMode();
		return Mode == 3 || Mode == 4 || Mode == 5 || Mode == 6;
	}

	bool HasLineOfSight(AActor* Other)
	{
		if (!Pawn || !Other)
			return false;
		if (Pawn->IsActorBeingDestroyed() || Other->IsActorBeingDestroyed())
			return false;

		FVector Start{};
		FVector End{};
		FRotator Dummy{};
		Pawn->GetActorEyesViewPoint(&Start, &Dummy);
		Other->GetActorEyesViewPoint(&End, &Dummy);

		TArray<AActor*> Ignore;
		Ignore.Add(Pawn);
		Ignore.Add(Other);

		FHitResult* OutHit = nullptr;
		bool bHit = UKismetSystemLibrary::LineTraceSingle(GetWorld(), Start, End, ETraceTypeQuery::TraceTypeQuery1, false, Ignore, EDrawDebugTrace::None, true, FLinearColor(), FLinearColor(), 0.0f, &OutHit);
		return !bHit;
	}

	void SetControlRotation(FRotator NewRotation)
	{
		if (!Controller)
			return;
		static auto ControlRotationOffset = Controller->GetOffset("ControlRotation");
		Controller->Get<FRotator>(ControlRotationOffset) = NewRotation;
	}

	void BotLookAt(const FVector& Location, float PitchOverride = 0)
	{
		if (!Pawn)
			return;
		FVector Eye{};
		FRotator Dummy{};
		Pawn->GetActorEyesViewPoint(&Eye, &Dummy);
		FRotator Ideal = BotMath::LookAtRotation(Eye, Location);
		float AimDeg = (1.0f - Personality.AimSkill) * 5.0f + std::min(AimErrorOffset, 5.0f);
		Ideal.Yaw += BotMath::RandomRange(-AimDeg, AimDeg);
		Ideal.Pitch += BotMath::RandomRange(-AimDeg * 0.6f, AimDeg * 0.6f);
		if (PitchOverride != 0.0f)
			Ideal.Pitch = PitchOverride;
		SetControlRotation(Ideal);
	}

	void BotMoveToward(FVector Destination, float SpeedMultiplier = 1.0f)
	{
		if (!Pawn || Pawn->IsActorBeingDestroyed())
			return;

		FVector MyLoc = Pawn->GetActorLocation();
		FVector Dir = BotMath::Normalize(Destination - MyLoc);
		if (Dir.SizeSquared() < 0.0001f)
			return;

		if (IsPawnAirborne())
		{
			auto CM = GetCharacterMovement();
			if (CM)
			{
				static auto VelocityOffset = CM->GetOffset("Velocity");
				auto& Velocity = CM->Get<FVector>(VelocityOffset);

				FVector TargetVelocity = Velocity;
				float HorizontalSpeed = 520.0f * SpeedMultiplier;
				TargetVelocity.X = Dir.X * HorizontalSpeed;
				TargetVelocity.Y = Dir.Y * HorizontalSpeed;

				float Alt = MyLoc.Z - Destination.Z;
				if (Alt > 1500.0f)
					TargetVelocity.Z = std::min((float)Velocity.Z, -1800.0f); // dive
				else if (Alt < 900.0f)
					TargetVelocity.Z = std::max((float)Velocity.Z, -600.0f);  // slow down (glider / landing)

				Velocity = TargetVelocity;
			}
			SetControlRotation(BotMath::LookAtRotation(MyLoc, Destination));
			return;
		}

		static auto AddMovementInputFn = FindObject<UFunction>(L"/Script/Engine.Pawn.AddMovementInput");
		if (AddMovementInputFn)
		{
			struct FAddMovementInputParams { FVector WorldDirection; float ScaleValue; bool bForce; } Params{ Dir, std::max(0.2f, std::min(1.2f, SpeedMultiplier)), true };
			Pawn->ProcessEvent(AddMovementInputFn, &Params);
		}

		FRotator Aim = BotMath::LookAtRotation(MyLoc, Destination);
		Aim.Pitch = 0;
		SetControlRotation(Aim);
	}

	bool BotReachedDestination(const FVector& Destination, float Acceptance = 250.0f)
	{
		if (!Pawn)
			return false;
		return BotMath::Dist2D(Pawn->GetActorLocation(), Destination) <= Acceptance;
	}

	float GetPerceptionRange()
	{
		return 12000.0f + Personality.Awareness * 8000.0f + Personality.Aggression * 4000.0f;
	}

	// ------------------------- Battle Bus / landing -------------------------

	void PickLandingSpot()
	{
		if (!Pawn)
			return;

		FVector Base = Pawn->GetActorLocation();
		float Angle = 0.0f, Dist = 0.0f;

		if (Globals::bLateGame.load())
		{
			Base = GetSafeZoneCenter();
			float Radius = std::max(GetSafeZoneRadius(), 1000.0f);
			Angle = BotMath::RandomRange(0, 360);
			Dist = BotMath::RandomRange(Radius * 0.2f, Radius * 0.6f);
		}
		else
		{
			// pick a direction+range that avoids the densest concentration of other
			// players/bots so bots spread out instead of stacking on the same spot
			int BestAngle = 0;
			float BestScore = -1.0f;
			auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
			for (int a = 0; a < 12; ++a)
			{
				float TestAngle = a * 30.0f;
				float CrowdPenalty = 0.0f;
				if (GameMode)
				{
					auto& Alive = GameMode->GetAlivePlayers();
					for (int i = 0; i < Alive.Num(); ++i)
					{
						auto OtherPC = Alive.at(i);
						if (!OtherPC || OtherPC == Controller)
							continue;
						auto OtherPawn = OtherPC->GetPawn();
						if (!OtherPawn || OtherPawn->IsActorBeingDestroyed())
							continue;
						float ToEnemy = BotMath::Dist2D(Base, OtherPawn->GetActorLocation());
						if (ToEnemy < 9000.0f)
							CrowdPenalty += (9000.0f - ToEnemy) / 9000.0f;
					}
				}
				float Score = CrowdPenalty + BotMath::RandomRange(0.0f, 2.0f);
				if (Score > BestScore)
				{
					BestScore = Score;
					BestAngle = a;
				}
			}
			Angle = BestAngle * 30.0f + BotMath::RandomRange(-10.0f, 10.0f);
			Dist = Personality.Aggression > 0.6f ? BotMath::RandomRange(400, 2200) : BotMath::RandomRange(1200, 5500);
		}

		LandingTarget = Base + BotMath::DirectionFromYaw(Angle) * Dist;
		bHasLandingTarget = true;
	}

	void JumpFromBus()
	{
		auto FortPC = Cast<AFortPlayerController>(Controller);
		if (!FortPC || !Pawn)
			return;

		FRotator Look = BotMath::LookAtRotation(Pawn->GetActorLocation(), LandingTarget);
		FortPC->ServerAttemptAircraftJumpHook(FortPC, Look);

		BotState = EBotState::Gliding;
		bHasMoveTarget = false;
		bWasAirborne = true;
		AirborneStartTime = UGameplayStatics::GetTimeSeconds(GetWorld());

		// human touch: occasionally thank the bus driver
		if (PlayerState && !PlayerState->HasThankedBusDriver() && std::rand() % 100 < 40)
		{
			static auto ServerThankBusDriverFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerControllerAthena.ServerThankBusDriver");
			if (ServerThankBusDriverFn)
				Controller->ProcessEvent(ServerThankBusDriverFn);
		}
	}

	// ------------------------- Loot -------------------------

	ABuildingContainer* FindNearestContainer(float MaxRange)
	{
		if (!Pawn)
			return nullptr;
		static auto BuildingContainerClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingContainer");
		if (!BuildingContainerClass)
			return nullptr;

		TArray<AActor*> AllContainers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), BuildingContainerClass);
		ABuildingContainer* Best = nullptr;
		float BestDist = MaxRange;
		FVector MyLoc = Pawn->GetActorLocation();

		for (int i = 0; i < AllContainers.Num(); ++i)
		{
			auto Actor = AllContainers.at(i);
			if (!Actor || Actor->IsActorBeingDestroyed())
				continue;
			auto Container = Cast<ABuildingContainer>(Actor);
			if (!Container || Container->IsAlreadySearched())
				continue;
			float Dist = BotMath::Dist2D(MyLoc, Container->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = Container;
			}
		}

		AllContainers.Free();
		return Best;
	}

	void SearchContainer(ABuildingContainer* Container)
	{
		if (!Container || !Pawn)
			return;
		if (Container->IsAlreadySearched())
			return;

		auto FortPawn = Cast<AFortPawn>(Pawn);
		if (!FortPawn)
			return;

		Container->SpawnLoot(FortPawn);
		Container->SetAlreadySearched(true);

		static auto SearchBounceDataOffset = Container->GetOffset("SearchBounceData", false);
		if (SearchBounceDataOffset != -1)
		{
			static auto SearchAnimationCountOffset = FindOffsetStruct("/Script/FortniteGame.FortSearchBounceData", "SearchAnimationCount");
			auto SearchBounceData = Container->GetPtr<void>(SearchBounceDataOffset);
			if (SearchBounceData)
				(*(int*)(__int64(SearchBounceData) + SearchAnimationCountOffset))++;
		}

		Container->BounceContainer();
		Container->ForceNetUpdate();

		++TotalContainersSearched;
		currentBotEncounter.NumChestsSearched++;
	}

	AFortPickup* FindNearestPickup(float MaxRange)
	{
		if (!Pawn)
			return nullptr;
		static auto FortPickupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPickup");
		if (!FortPickupClass)
			return nullptr;

		TArray<AActor*> AllPickups = UGameplayStatics::GetAllActorsOfClass(GetWorld(), FortPickupClass);
		AFortPickup* Best = nullptr;
		float BestScore = 0.0f;
		FVector MyLoc = Pawn->GetActorLocation();

		for (int i = 0; i < AllPickups.Num(); ++i)
		{
			auto Actor = AllPickups.at(i);
			if (!Actor || Actor->IsActorBeingDestroyed())
				continue;
			auto Pickup = Cast<AFortPickup>(Actor);
			if (!Pickup)
				continue;
			float Dist = BotMath::Dist2D(MyLoc, Pickup->GetActorLocation());
			if (Dist > MaxRange)
				continue;
			float Score = 1.0f / std::max(1.0f, Dist);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Pickup;
			}
		}

		AllPickups.Free();
		return Best;
	}

	void PickupNearbyLoot()
	{
		if (!Pawn)
			return;
		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
		if (Now - LastPickupScanTime < 0.8f)
			return;
		LastPickupScanTime = Now;

		auto Pickup = FindNearestPickup(700.0f);
		if (!Pickup)
			return;

		float Dist = BotMath::Dist2D(Pawn->GetActorLocation(), Pickup->GetActorLocation());
		if (Dist < 200.0f)
		{
			if (auto FortPawn = Cast<AFortPawn>(Pawn))
				FortPawn->PickUpActor(Pickup, nullptr);
		}
		else if (Dist < 700.0f)
		{
			BotMoveToward(Pickup->GetActorLocation(), 1.05f);
		}
	}

	void EquipBestWeapon()
	{
		auto FortPC = Cast<AFortPlayerController>(Controller);
		auto Inventory = GetBotInventory();
		if (!FortPC || !Inventory || !Pawn)
			return;

		auto& ItemInstances = Inventory->GetItemList().GetItemInstances();
		UFortItem* Best = nullptr;
		float BestScore = -1.0f;

		for (int i = 0; i < ItemInstances.Num(); ++i)
		{
			auto Item = ItemInstances.at(i);
			if (!Item)
				continue;
			auto Entry = Item->GetItemEntry();
			if (!Entry)
				continue;
			auto ItemDef = Entry->GetItemDefinition();
			if (!ItemDef || !IsPrimaryQuickbar(ItemDef))
				continue;
			auto WeaponDef = Cast<UFortWeaponItemDefinition>(ItemDef);
			if (!WeaponDef)
				continue;
			float Score = BotWeapons::GetWeaponScore(WeaponDef);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Item;
			}
		}

		if (!Best || BestScore <= 0.0f)
			return;

		auto CurrentWeapon = Pawn->GetCurrentWeapon();
		auto CurrentDef = CurrentWeapon ? CurrentWeapon->GetWeaponData<UFortWeaponItemDefinition>() : nullptr;
		if (CurrentDef == Cast<UFortWeaponItemDefinition>(Best->GetItemEntry()->GetItemDefinition()))
			return; // already equipped

		FortPC->ServerExecuteInventoryItemHook(FortPC, Best->GetItemEntry()->GetItemGuid());
	}

	// ------------------------- Combat -------------------------

	AFortPlayerPawnAthena* FindBestTarget(float MaxRange)
	{
		if (!Pawn || !Controller)
			return nullptr;
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		if (!GameMode)
			return nullptr;

		auto& Alive = GameMode->GetAlivePlayers();
		if (Alive.Num() == 0)
			return nullptr;

		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
		uint8 MyTeam = PlayerState ? PlayerState->GetTeamIndex() : 0;

		FVector Eye{};
		FRotator Dummy{};
		Pawn->GetActorEyesViewPoint(&Eye, &Dummy);
		FVector MyForward = BotMath::DirectionFromYaw(Dummy.Yaw);
		float FOV = 0.2f + (1.0f - Personality.Awareness) * 0.08f;

		AFortPlayerPawnAthena* Best = nullptr;
		float BestDist = 999999.0f;

		for (int i = 0; i < Alive.Num(); ++i)
		{
			auto OtherPC = Alive.at(i);
			if (!OtherPC || !OtherPC->IsValidLowLevel() || OtherPC->IsPendingKill() || OtherPC == Controller || OtherPC->IsActorBeingDestroyed())
				continue;

			auto OtherState = Cast<AFortPlayerStateAthena>(OtherPC->GetPlayerState());
			if (!OtherState || !OtherState->IsValidLowLevel() || OtherState->GetTeamIndex() == MyTeam)
				continue;

			auto OtherPawn = Cast<AFortPlayerPawnAthena>(OtherPC->GetPawn());
			if (!OtherPawn || !OtherPawn->IsValidLowLevel() || OtherPawn->IsActorBeingDestroyed())
				continue;

			FVector TheirLoc = OtherPawn->GetActorLocation();
			float Dist = BotMath::Dist3D(Eye, TheirLoc);
			if (Dist > MaxRange)
				continue;

			bool bSeen = false;
			if (Dist < 700.0f)
			{
				bSeen = true;
			}
			else
			{
				float Dot = BotMath::Dot(BotMath::Normalize(TheirLoc - Eye), MyForward);
				if (Dot > FOV && HasLineOfSight(OtherPawn))
					bSeen = true;
			}

			// sound: someone recently shot at us (or a fight is happening nearby) -> investigate
			float Now2 = Now; // for symmetry
			if (!bSeen && Now2 - LastDamageTakenTime < 2.5f && Dist < MaxRange * 0.6f)
				bSeen = true;

			if (!bSeen)
				continue;

			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = OtherPawn;
			}
		}

		return Best;
	}

	// Evaluate a detected opponent's threat relative to ourselves (Section 6 of the spec):
	// distance, health, shield, equipped weapon, height advantage, and the bot's own
	// skill/aggression combine into a single score. Higher = more dangerous.
	float EvaluateThreat(AFortPlayerPawnAthena* Opponent)
	{
		if (!Pawn || !Opponent || !Pawn->IsValidLowLevel() || !Opponent->IsValidLowLevel())
			return 0.0f;

		FVector MyLoc = Pawn->GetActorLocation();
		FVector TheirLoc = Opponent->GetActorLocation();
		float Dist = BotMath::Dist3D(MyLoc, TheirLoc);

		auto ThemFort = Cast<AFortPawn>(Opponent);
		float TheirHealth = ThemFort ? ThemFort->GetHealth() : 100.0f;
		float TheirShield = ThemFort ? ThemFort->GetShield() : 0.0f;
		float TheirHP = TheirHealth + TheirShield;

		float MyHP = TrackedHealth + TrackedShield;

		float Threat = 0.0f;
		Threat += std::min(1.0f, Dist / 5000.0f) * 1.0f;                    // closer = more threatening
		Threat += (100.0f - TheirHP) / 100.0f * 0.6f;                        // they're hurt = less threat
		Threat += std::max(0.0f, (TheirHP - MyHP) / 150.0f) * 1.2f;          // they out-sustain us
		Threat += (TheirLoc.Z - MyLoc.Z > 250.0f) ? 0.4f : 0.0f;             // they have height

		auto TheirWeapon = Opponent->GetCurrentWeapon();
		if (TheirWeapon)
		{
			auto TheirDef = TheirWeapon->GetWeaponData<UFortWeaponItemDefinition>();
			Threat += BotWeapons::GetWeaponScore(TheirDef) / 115.0f * 0.8f; // their weapon quality
		}

		// perception error: we don't perfectly estimate; lower awareness = more misreads
		Threat += BotMath::RandomRange(-0.3f, 0.3f) * (1.6f - Personality.Awareness);

		return Threat;
	}

	// Decide whether fighting the currently detected enemy is a good idea, given our
	// personality. Returns true if we should fight; false pushes us into cover/defensive.
	bool ShouldEngage(AFortPlayerPawnAthena* Opponent)
	{
		if (!Opponent)
			return true;

		CurrentThreatScore = EvaluateThreat(Opponent);
		float MyCombatSkill = (Personality.AimSkill + Personality.BuildSkill) * 0.5f;

		// Aggressive / Pro bots tolerate much more than defensive / novatos.
		float RiskBudget = (Personality.Aggression * 1.2f) + (Personality.RiskTolerance * 0.8f) + MyCombatSkill;

		// Always fight back if we're already under fire or mid-fight.
		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
		if (Now - LastDamageTakenTime < 1.5f)
		{
			bFightIsUnfavorable = false;
			return true;
		}

		bFightIsUnfavorable = CurrentThreatScore > RiskBudget + 1.2f;
		return !bFightIsUnfavorable;
	}

	void ApplyBotDamage(AFortPlayerPawnAthena* Victim, float Damage)
	{
		if (!Victim || !Victim->IsValidLowLevel() || Victim->IsPendingKill() || Victim->IsActorBeingDestroyed())
			return;
		auto VictimFort = Cast<AFortPawn>(Victim);
		if (!VictimFort || !VictimFort->IsValidLowLevel())
			return;

		float Shield = VictimFort->GetShield();
		float Remaining = Damage;
		if (Shield > 0.0f)
		{
			float Used = std::min(Shield, Remaining);
			VictimFort->SetShield(Shield - Used);
			Remaining -= Used;
		}

		float Health = VictimFort->GetHealth();
		if (Remaining > 0.0f && Health > 0.0f)
			VictimFort->SetHealth(std::max(0.0f, Health - Remaining));

		if (VictimFort->GetHealth() <= 0.001f)
		{
			if (!VictimFort->IsDBNO())
			{
				// knocked down
				VictimFort->SetDBNO(true);
				VictimFort->OnRep_IsDBNO();
				OnPlayerEncountered();
			}
			else
			{
				// finish the downed enemy
				static auto ForceKillFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPawn.ForceKill");
				if (!ForceKillFn)
					return;
				struct FForceKillParams { FGameplayTag DeathReason; AController* KillerController; AActor* KillerActor; }
					Params{ {}, (AController*)Controller, Pawn };
				Victim->ProcessEvent(ForceKillFn, &Params);
				CurrentTarget = nullptr;
			}
		}
	}

	void FireAt(AFortPlayerPawnAthena* Target, float DeltaTime)
	{
		if (!Target || !Pawn || !Target->IsValidLowLevel() || !Pawn->IsValidLowLevel())
			return;

		auto Weapon = Pawn->GetCurrentWeapon();
		if (!Weapon)
		{
			EquipBestWeapon();
			return;
		}
		auto Def = Weapon->GetWeaponData<UFortWeaponItemDefinition>();
		if (!Def || !BotWeapons::IsFirearm(Def))
		{
			EquipBestWeapon();
			return;
		}

		float Interval = 0.0f, Damage = 0.0f;
		BotWeapons::GetFireStats(Def, Interval, Damage);

		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
		if (Now - LastShotTime < Interval * BotMath::RandomRange(0.85f, 1.25f))
			return;
		LastShotTime = Now;
		LastGlobalGunshotTime = Now;
		LastGlobalGunshotLocation = Pawn->GetActorLocation();

		AimErrorOffset = std::min(6.0f, AimErrorOffset + BotMath::RandomRange(0.2f, 1.0f));

		float Dist = BotMath::Dist3D(Pawn->GetActorLocation(), Target->GetActorLocation());
		float Falloff = std::max(0.2f, std::min(1.0f, 1.0f - Dist * 0.00004f));
		float HitChance = (0.30f + Personality.AimSkill * 0.65f) * Falloff * (Target->IsDBNO() ? 1.5f : 1.0f);

		if (BotMath::RandomRange(0.0f, 1.0f) <= HitChance)
			ApplyBotDamage(Target, Damage * BotMath::RandomRange(0.85f, 1.15f));
	}

	void EngageTarget(float DeltaTime)
	{
		if (!CurrentTarget || !Pawn || !CurrentTarget->IsValidLowLevel() || CurrentTarget->IsPendingKill())
			return;
		if (CurrentTarget->IsActorBeingDestroyed())
		{
			CurrentTarget = nullptr;
			return;
		}

		FVector MyLoc = Pawn->GetActorLocation();
		FVector TheirLoc = CurrentTarget->GetActorLocation();
		float Dist = BotMath::Dist3D(MyLoc, TheirLoc);

		// build cover immediately when under fire (defensive reaction, scaled by skill)
		bool bUnderFire = (DeltaTime - LastDamageTakenTime) < 1.0f;
		if (bUnderFire && Personality.BuildSkill > 0.3f && BotMath::RandomRange(0.0f, 1.0f) < 0.5f)
			DefendAgainst(CurrentTarget);

		float MinRange = 400.0f + (1.0f - Personality.Aggression) * 900.0f;
		float MaxRange = 3500.0f + Personality.Aggression * 2500.0f;
		FVector DirToThem = BotMath::Normalize(TheirLoc - MyLoc);

		// When the fight is unfavorable, play conservatively (Section 8): keep medium
		// range, keep walls up, strafe off the direct line instead of pushing head-on.
		if (bFightIsUnfavorable)
		{
			if (Personality.BuildSkill > 0.3f && DeltaTime - LastBuildTime > 1.5f)
				DefendAgainst(CurrentTarget);

			float HoldRange = 1800.0f + Personality.AimSkill * 600.0f;
			FVector Perp = FVector(-DirToThem.Y, DirToThem.X, 0) * (float)CurrentStrafeDir;
			FVector TargetPos = TheirLoc - DirToThem * HoldRange + Perp * (bUnderFire ? 800.0f : 300.0f);
			BotMoveToward(TargetPos, 1.0f);
			BotLookAt(TheirLoc);
			FireAt(CurrentTarget, DeltaTime);
			return;
		}

		if (Dist > MaxRange)
		{
			BotMoveToward(TheirLoc, 1.1f); // push
		}
		else if (Dist < MinRange)
		{
			// back up / strafe out of hugging range
			FVector Perp = FVector(-DirToThem.Y, DirToThem.X, 0) * (float)CurrentStrafeDir;
			FVector BackUp = (-DirToThem) * 0.4f + Perp;
			BotMoveToward(MyLoc + BotMath::Normalize(BackUp) * MinRange, 1.0f);
		}
		else
		{
			if (DeltaTime - StrafeSeed > BotMath::RandomRange(0.8f, 1.8f))
			{
				StrafeSeed = DeltaTime;
				CurrentStrafeDir *= -1;
			}
			FVector Perp = FVector(-DirToThem.Y, DirToThem.X, 0) * (float)CurrentStrafeDir;
			float ForwardBias = Personality.Aggression > 0.6f ? 200.0f : -120.0f;
			BotMoveToward(MyLoc + (Perp + DirToThem * ForwardBias) * 1.0f, 1.0f);
		}

		BotLookAt(TheirLoc);
		FireAt(CurrentTarget, DeltaTime);
	}

	void BuildWallAt(const FVector& Location, const FRotator& Rotation)
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		if (!GameState)
			return;
		auto Container = GameState->GetPlayerBuildableClasses();
		if (!Container || Container->BuildingClasses.Num() == 0)
			return;

		UClass* WallClass = nullptr;
		for (int i = 0; i < Container->BuildingClasses.Num(); ++i)
		{
			UClass* Candidate = Container->BuildingClasses.at(i);
			if (!Candidate)
				continue;
			std::string Name = BotWeapons::Lower(Candidate->GetPathName());
			if (Name.find("wall") != std::string::npos)
			{
				WallClass = Candidate;
				break;
			}
		}
		if (!WallClass)
			WallClass = Container->BuildingClasses.at(0);
		if (!WallClass)
			return;

		FTransform Transform{};
		Transform.Translation = Location;
		Transform.Rotation = Rotation.Quaternion();
		Transform.Scale3D = FVector(1, 1, 1);

		auto Building = GetWorld()->SpawnActor<ABuildingSMActor>(WallClass, Transform);
		if (!Building)
			return;

		Building->SetPlayerPlaced(true);
		Building->InitializeBuildingActor(Controller, Building, true);
		Building->SetTeam(PlayerState ? PlayerState->GetTeamIndex() : 0);
	}

	void DefendAgainst(AFortPlayerPawnAthena* Attacker)
	{
		if (!Pawn || !Attacker)
			return;
		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
		if (Now - LastBuildTime < 1.2f)
			return;
		LastBuildTime = Now;

		int Walls = BotMath::RandomInt(1, 1 + int(Personality.BuildSkill * 2.0f));
		FVector MyLoc = Pawn->GetActorLocation();
		FVector DirToAttacker = BotMath::Normalize(Attacker->GetActorLocation() - MyLoc);

		for (int i = 0; i < Walls; ++i)
		{
			FVector WallLoc = MyLoc + DirToAttacker * (160.0f * (i + 1));
			WallLoc.Z = MyLoc.Z;
			FRotator WallRot = BotMath::LookAtRotation(MyLoc, Attacker->GetActorLocation());
			WallRot.Pitch = 0;
			WallRot.Roll = 0;
			BuildWallAt(WallLoc, WallRot);
		}
	}

	// ------------------------- Healing / Storm / Endgame -------------------------

	void TryHeal(float DeltaTime)
	{
		if (!Pawn)
			return;
		float Now = DeltaTime; // param is current time
		float Health = Pawn->GetHealth();
		float Shield = Pawn->GetShield();

		float HealTargetHealth = std::max(60.0f, 60.0f + Personality.LootSkill * 40.0f);
		float HealTargetShield = std::max(25.0f, 25.0f + Personality.LootSkill * 75.0f);

		bool bDamaged = Health < HealTargetHealth || Shield < HealTargetShield;
		if (!bDamaged)
		{
			if (BotState == EBotState::Healing)
				BotState = PrevState == EBotState::Healing ? EBotState::Exploring : PrevState;
			return;
		}

		if (Now - LastDamageTakenTime < 1.2f)
			return; // don't heal under fire

		if (BotState != EBotState::Healing)
		{
			HealingStartedTime = Now;
			BotState = EBotState::Healing;
			return;
		}

		float Duration = 2.0f + (HealTargetHealth - Health) * 0.02f + (HealTargetShield - Shield) * 0.012f;
		if (Now - HealingStartedTime >= Duration)
		{
			Pawn->SetHealth(HealTargetHealth);
			Pawn->SetShield(HealTargetShield);
			LastHealTime = Now;
			BotState = PrevState == EBotState::Healing ? EBotState::Exploring : PrevState;
		}
		else
		{
			// subtle drift while "using" the item
			FVector MyLoc = Pawn->GetActorLocation();
			SetControlRotation(BotMath::LookAtRotation(MyLoc, MyLoc + BotMath::DirectionFromYaw(BotMath::RandomRange(0, 360)) * 100.0f));
		}
	}

	FVector GetSafeZoneCenter()
	{
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		if (!GameMode)
			return FVector();
		auto Indicator = GameMode->GetSafeZoneIndicator();
		if (!Indicator)
			return FVector();

		static auto NextOffset = Indicator->GetOffset("NextSafeZoneCenter", false);
		if (NextOffset != -1)
			return Indicator->Get<FVector>(NextOffset);
		static auto CurrentOffset = Indicator->GetOffset("CurrentCenter", false);
		if (CurrentOffset != -1)
			return Indicator->Get<FVector>(CurrentOffset);
		static auto LastOffset = Indicator->GetOffset("LastCenter", false);
		if (LastOffset != -1)
			return Indicator->Get<FVector>(LastOffset);
		return FVector();
	}

	float GetSafeZoneRadius()
	{
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		if (!GameMode)
			return 100000.0f;
		auto Indicator = GameMode->GetSafeZoneIndicator();
		if (!Indicator)
			return 100000.0f;
		static auto RadiusOffset = Indicator->GetOffset("Radius", false);
		if (RadiusOffset == -1)
			return 100000.0f;
		return Indicator->Get<float>(RadiusOffset);
	}

	bool IsInsideSafeZone(float Margin = 0)
	{
		if (!Pawn)
			return true;
		FVector Center = GetSafeZoneCenter();
		float Radius = GetSafeZoneRadius();
		if (Radius <= 0.0f)
			return true;
		return BotMath::Dist2D(Pawn->GetActorLocation(), Center) <= std::max(1.0f, Radius - Margin);
	}

	void StayAwayFromStorm(float DeltaTime)
	{
		if (!Pawn)
			return;
		FVector Center = GetSafeZoneCenter();
		float Radius = GetSafeZoneRadius();
		FVector MyLoc = Pawn->GetActorLocation();
		FVector ToCenter = BotMath::Normalize(Center - MyLoc);
		if (ToCenter.SizeSquared() < 0.0001f)
			ToCenter = FVector(1, 0, 0);

		float TargetDistance = std::max(200.0f, Radius * 0.35f);
		MoveTarget = Center - ToCenter * TargetDistance;
		MoveTarget.Z = MyLoc.Z;
		bHasMoveTarget = true;
		BotMoveToward(MoveTarget, 1.2f);
	}

	void Tick()
	{
		if (!Controller || !Controller->IsValidLowLevel() || Controller->IsPendingKill())
			return;
		if (Controller->IsActorBeingDestroyed())
			return;

		float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

		auto PC = Cast<AFortPlayerControllerAthena>(Controller);
		Pawn = Cast<AFortPlayerPawnAthena>(PC ? PC->GetPawn() : Controller->GetPawn());
		PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		if (!Pawn || !Pawn->IsValidLowLevel() || !PlayerState || !PlayerState->IsValidLowLevel()
			|| Pawn->IsActorBeingDestroyed() || PlayerState->IsPendingKill())
			return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		if (!GameState || !GameMode)
			return;

		float Health = Pawn->GetHealth();
		float Shield = Pawn->GetShield();

		if (LastPawnChecked != (AFortPawn*)Pawn) // fresh pawn (e.g., after bus jump / respawn)
		{
			TrackedHealth = Health;
			TrackedShield = Shield;
			LastPawnChecked = Pawn;
		}
		else if (Health + 0.5f < TrackedHealth || Shield + 0.5f < TrackedShield)
		{
			LastDamageTakenTime = Now;
		}
		TrackedHealth = Health;
		TrackedShield = Shield;

		// ---- airborne / landing ----
		bool Airborne = IsPawnAirborne();
		if (Airborne && !bWasAirborne)
			AirborneStartTime = Now;
		if (!Airborne && bWasAirborne)
		{
			if (BotState == EBotState::Gliding || BotState == EBotState::Jumping)
			{
				BotState = EBotState::Landing;
				LandedTime = Now;
				bHasLandingTarget = false;
				bHasMoveTarget = false;
			}
		}
		bWasAirborne = Airborne;

		// ---- Battle Bus ----
		// In the lobby (GamePhase is None/Setup/Warmup, the aircraft isn't flying yet)
		// bots that the server still flags as "in aircraft" would sit frozen trying to
		// jump from a bus that hasn't started. Only jump logic when the real Aircraft
		// phase is active; otherwise treat them as on-foot and roam/loot.
		if (GameState->GetGamePhase() >= EAthenaGamePhase::Aircraft && PlayerState->IsInAircraft())
		{
			if (!bHasLandingTarget)
			{
				PickLandingSpot();
				BotState = EBotState::ChoosingLanding;
			}
			if (Now >= NextJumpTime)
				JumpFromBus();
			return;
		}
		if (BotState == EBotState::InBus || BotState == EBotState::ChoosingLanding)
		{
			BotState = EBotState::Looting;
			bHasLandingTarget = false;
		}

		// ---- perception ----
		float PerceptInterval = std::max(0.1f, 0.35f - Personality.Awareness * 0.2f);
		if (Now - LastPerceptTime >= PerceptInterval)
		{
			LastPerceptTime = Now;
			CurrentTarget = FindBestTarget(GetPerceptionRange());
			if (CurrentTarget)
			{
				bHasEnemy = true;
				LastEnemySeenTime = Now;
				LastKnownEnemyLocation = CurrentTarget->GetActorLocation();
				bHasLastKnownEnemyLocation = true;
			}
		}

		bool bEngaged = CurrentTarget != nullptr;
		if (!bEngaged && bHasEnemy && Now - LastEnemySeenTime < 4.0f)
			bEngaged = true; // hunt the last known position

		// ---- hearing: aggressive bots investigate distant gunfire and drop into SearchingEnemy ----
		if (!bEngaged && Now - LastGlobalGunshotTime < 2.5f && Personality.Aggression > 0.5f &&
			BotState != EBotState::InBus && BotState != EBotState::ChoosingLanding &&
			BotState != EBotState::Jumping && BotState != EBotState::Gliding && BotState != EBotState::Landing)
		{
			float GunDist = BotMath::Dist2D(Pawn->GetActorLocation(), LastGlobalGunshotLocation);
			float HearRange = 1800.0f + Personality.Awareness * 2200.0f;
			if (GunDist > 450.0f && GunDist < HearRange)
			{
				LastShotHeardTime = Now;
				LastShotHeardLocation = LastGlobalGunshotLocation;
				LastKnownEnemyLocation = LastGlobalGunshotLocation;
				bHasLastKnownEnemyLocation = true;
				bHasEnemy = true;
				LastEnemySeenTime = Now - 0.5f;
				if (BotState != EBotState::Fighting && BotState != EBotState::Defending && BotState != EBotState::Healing)
					PrevState = BotState;
				BotState = EBotState::SearchingEnemy;
			}
		}

		// decide how to approach the current threat (Section 6/8): engage directly or play defensively
		bool bFightDirectly = true;
		if (bEngaged && CurrentTarget && BotState != EBotState::InBus && BotState != EBotState::ChoosingLanding &&
			BotState != EBotState::Jumping && BotState != EBotState::Gliding && BotState != EBotState::Landing)
			bFightDirectly = ShouldEngage(CurrentTarget);

		if (bEngaged && BotState != EBotState::InBus && BotState != EBotState::ChoosingLanding &&
			BotState != EBotState::Jumping && BotState != EBotState::Gliding && BotState != EBotState::Landing)
		{
			if (BotState != EBotState::Fighting && BotState != EBotState::Defending && BotState != EBotState::Healing)
				PrevState = BotState;
			// when the fight is unfavorable, drop into a short defensive/wall phase first,
			// which then escalates to Fighting (the bot never runs away).
			BotState = bFightDirectly ? EBotState::Fighting : EBotState::Defending;
			if (!bFightDirectly)
				DefendingStartTime = Now;
		}

		// ---- reactive defense (build cover right after being hit, when we can't see the attacker) ----
		bool bUnderFire = (Now - LastDamageTakenTime) < 1.0f;
		if (!bEngaged && bUnderFire && Personality.BuildSkill > 0.35f &&
			BotState != EBotState::Healing && BotState != EBotState::Defending &&
			BotState != EBotState::Landing && BotState != EBotState::InBus && BotState != EBotState::ChoosingLanding &&
			BotState != EBotState::Jumping && BotState != EBotState::Gliding)
		{
			PrevState = (BotState == EBotState::Fighting || BotState == EBotState::SearchingEnemy) ? EBotState::SearchingEnemy : BotState;
			DefendingStartTime = Now;
			BotState = EBotState::Defending;
		}

		// ---- storm priority (unless actively fighting/recovering/landing) ----
		bool bBusy = BotState == EBotState::Fighting || BotState == EBotState::Defending ||
			BotState == EBotState::Healing || BotState == EBotState::Landing;

		// Also start rotating early when still inside the zone but far from the *next*
		// circle, so the bot isn't forced to sprint across the whole map when the storm moves.
		bool bNeedRotation = !IsInsideSafeZone(1000.0f);
		if (!bNeedRotation && Pawn)
		{
			float NowRadius = GetSafeZoneRadius();
			if (NowRadius > 0.0f)
			{
				float DistToNextCenter = BotMath::Dist2D(Pawn->GetActorLocation(), GetSafeZoneCenter());
				if (DistToNextCenter > NowRadius * 0.6f + 1200.0f)
					bNeedRotation = true;
			}
		}

		if (!bBusy && BotState != EBotState::EndGame && bNeedRotation)
		{
			if (BotState != EBotState::Rotating)
				PrevState = BotState;
			BotState = EBotState::Rotating;
		}
		else if (BotState == EBotState::Rotating && IsInsideSafeZone(1500.0f)
			&& (!Pawn || BotMath::Dist2D(Pawn->GetActorLocation(), GetSafeZoneCenter()) < std::max(GetSafeZoneRadius(), 1.0f) * 0.55f))
		{
			BotState = PrevState == EBotState::Rotating ? EBotState::Exploring : PrevState;
			bHasMoveTarget = false;
		}

		// ---- endgame escalation ----
		// Only escalate during the real match (SafeZones phase onward). In the lobby
		// (None/Setup/Warmup) and on the bus (Aircraft) GetPlayersLeft() is tiny or
		// meaningless, so forcing EndGame there makes bots wander to a "hold point" and
		// teleport-fight instead of roaming the lobby.
		int PlayersLeft = GameState->GetGamePhase() >= EAthenaGamePhase::SafeZones ? GameState->GetPlayersLeft() : 999;
		bool bIsEndGame = Globals::bLateGame.load() || (PlayersLeft > 0 && PlayersLeft <= 8);
		if (bIsEndGame)
		{
			if (BotState != EBotState::EndGame && BotState != EBotState::Fighting && BotState != EBotState::Defending &&
				BotState != EBotState::Healing && BotState != EBotState::Landing && BotState != EBotState::Gliding &&
				BotState != EBotState::Jumping && BotState != EBotState::InBus && BotState != EBotState::ChoosingLanding)
			{
				PrevState = BotState;
				BotState = EBotState::EndGame;
			}
		}
		else if (BotState == EBotState::EndGame)
		{
			BotState = PrevState == EBotState::EndGame ? EBotState::Exploring : PrevState;
			bHasMoveTarget = false;
		}

		// ---- main state machine ----
		switch (BotState)
		{
		case EBotState::Landing:
		{
			if (Now - LandedTime > BotMath::RandomRange(0.5f, 1.2f))
			{
				bHasMoveTarget = false;
				BotState = EBotState::Looting;
			}
			else
			{
				SetControlRotation(BotMath::LookAtRotation(Pawn->GetActorLocation(), Pawn->GetActorLocation() + BotMath::DirectionFromYaw(Now * 30.0f) * 200.0f));
			}
			break;
		}

		case EBotState::Looting:
		{
			if (Now - LastDecisionTime > 2.0f)
			{
				LastDecisionTime = Now;
				EquipBestWeapon();
			}
			PickupNearbyLoot();

			// In warmup there are no containers/floor loot to grab, so just roam and
			// keep trying to equip a weapon (bots get starting items + pickaxe at spawn).
			if (GameState->GetGamePhase() == EAthenaGamePhase::Warmup)
			{
				if (Now - LastMoveTargetChange > BotMath::RandomRange(2.0f, 5.0f) || BotReachedDestination(MoveTarget, 350.0f))
				{
					MoveTarget = Pawn->GetActorLocation() + BotMath::DirectionFromYaw(BotMath::RandomRange(0, 360)) * BotMath::RandomRange(600.0f, 2000.0f);
					MoveTarget.Z = Pawn->GetActorLocation().Z;
					LastMoveTargetChange = Now;
				}
				BotMoveToward(MoveTarget, 1.0f);
				break;
			}

			if (CurrentContainer && (CurrentContainer->IsActorBeingDestroyed() || CurrentContainer->IsAlreadySearched()))
			{
				CurrentContainer = nullptr;
				ContainerSearchStartTime = -100.0f;
			}
			if (!CurrentContainer)
				CurrentContainer = FindNearestContainer(2600.0f);

			if (CurrentContainer)
			{
				FVector ContainerLoc = CurrentContainer->GetActorLocation();
				float DistToContainer = BotMath::Dist2D(Pawn->GetActorLocation(), ContainerLoc);
				if (DistToContainer <= 260.0f)
				{
					if (ContainerSearchStartTime < 0)
						ContainerSearchStartTime = Now;
					BotLookAt(ContainerLoc);
					float SearchDelay = BotMath::RandomRange(0.7f, 1.6f) * (2.0f - Personality.LootSkill);
					if (Now - ContainerSearchStartTime >= SearchDelay)
					{
						SearchContainer(CurrentContainer);
						CurrentContainer = nullptr;
						ContainerSearchStartTime = -100.0f;
					}
				}
				else
				{
					ContainerSearchStartTime = -100.0f;
					BotMoveToward(ContainerLoc, 1.0f);
				}
			}
			else
			{
				bHasMoveTarget = false;
				MoveToNewPOI();
				BotState = EBotState::Exploring;
			}
			break;
		}

		case EBotState::Exploring:
		{
			if (Now - LastDecisionTime > 2.0f)
			{
				LastDecisionTime = Now;
				EquipBestWeapon();
			}
			PickupNearbyLoot();

			// occasionally stop and scan the surroundings (human-like pacing)
			if (Now > LookAroundUntil && Now - LastLookAroundTime > BotMath::RandomRange(5.0f, 14.0f))
			{
				LastLookAroundTime = Now;
				LookAroundUntil = Now + BotMath::RandomRange(0.8f, 2.2f);
			}
			if (Now < LookAroundUntil)
			{
				SetControlRotation(BotMath::LookAtRotation(Pawn->GetActorLocation(),
					Pawn->GetActorLocation() + BotMath::DirectionFromYaw(Now * 40.0f) * 400.0f));
				break;
			}

			if (!bHasMoveTarget || Now - LastMoveTargetChange > BotMath::RandomRange(4.0f, 10.0f) || BotReachedDestination(MoveTarget, 600.0f))
			{
				bHasMoveTarget = false;
				MoveToNewPOI();
			}
			else if (FindNearestContainer(1400.0f))
			{
				BotState = EBotState::Looting;
				break;
			}

			if (bHasMoveTarget)
				BotMoveToward(MoveTarget, 0.9f);
			break;
		}

		case EBotState::Farming:
			// resource gathering simplified: treat as looting
			BotState = EBotState::Looting;
			break;

		case EBotState::SearchingEnemy:
		{
			if (bHasLastKnownEnemyLocation)
			{
				if (Now - LastEnemySeenTime > 8.0f || BotReachedDestination(LastKnownEnemyLocation, 350.0f))
				{
					bHasLastKnownEnemyLocation = false;
					bHasEnemy = false;
					BotState = (PrevState == EBotState::Fighting || PrevState == EBotState::Defending || PrevState == EBotState::SearchingEnemy)
						? EBotState::Exploring : PrevState;
				}
				else
				{
					BotMoveToward(LastKnownEnemyLocation, 1.15f);
				}
			}
			else
			{
				bHasEnemy = false;
				BotState = (PrevState == EBotState::Fighting || PrevState == EBotState::Defending)
					? EBotState::Exploring : PrevState;
			}
			break;
		}

		case EBotState::Fighting:
			EngageTarget(Now);
			break;

		case EBotState::Defending:
		{
			if (CurrentTarget)
				DefendAgainst(CurrentTarget);
			else if (bHasLastKnownEnemyLocation)
			{
				// blind wall toward where we think the enemy is
				FVector MyLoc = Pawn->GetActorLocation();
				FVector DirTo = BotMath::Normalize(LastKnownEnemyLocation - MyLoc);
				if (DirTo.SizeSquared() > 0.0001f)
				{
					FRotator WallRot = BotMath::LookAtRotation(MyLoc, LastKnownEnemyLocation);
					WallRot.Pitch = 0;
					WallRot.Roll = 0;
					BuildWallAt(MyLoc + DirTo * 160.0f, WallRot);
				}
			}

			if (Now - DefendingStartTime > 1.2f)
				BotState = EBotState::Fighting;
			else
				BotMoveToward(Pawn->GetActorLocation() + BotMath::DirectionFromYaw(BotMath::RandomRange(0, 360)) * 100.0f, 0.6f);
			break;
		}

		case EBotState::Healing:
			TryHeal(Now);
			break;

		case EBotState::Rotating:
			StayAwayFromStorm(Now);
			break;

		case EBotState::EndGame:
		{
			if (!bHasEndGameHoldPoint)
			{
				FVector Center = GetSafeZoneCenter();
				float Radius = GetSafeZoneRadius();
				FVector MyLoc = Pawn->GetActorLocation();
				FVector ToCenter = BotMath::Normalize(Center - MyLoc);
				if (ToCenter.SizeSquared() < 0.0001f)
					ToCenter = FVector(1, 0, 0);
				EndGameHoldPoint = Center - ToCenter * std::max(200.0f, Radius * 0.3f);
				EndGameHoldPoint.Z = MyLoc.Z;
				bHasEndGameHoldPoint = true;
			}

			if (Now - LastMoveTargetChange > BotMath::RandomRange(2.5f, 5.0f) || BotReachedDestination(EndGameHoldPoint, 350.0f))
			{
				FVector Center = GetSafeZoneCenter();
				FVector MyLoc = Pawn->GetActorLocation();
				FVector ToCenter = BotMath::Normalize(Center - MyLoc);
				if (ToCenter.SizeSquared() < 0.0001f)
					ToCenter = FVector(1, 0, 0);
				float Angle = BotMath::RandomRange(0, 360);
				EndGameHoldPoint = Center + (ToCenter + BotMath::DirectionFromYaw(Angle)) * BotMath::RandomRange(100.0f, std::max(150.0f, GetSafeZoneRadius() * 0.3f));
				EndGameHoldPoint.Z = MyLoc.Z;
				LastMoveTargetChange = Now;
			}

			BotMoveToward(EndGameHoldPoint, 1.0f);
			if (!CurrentTarget)
				SetControlRotation(BotMath::LookAtRotation(Pawn->GetActorLocation(), EndGameHoldPoint + BotMath::DirectionFromYaw(Now * 20.0f) * 300.0f));
			break;
		}

		case EBotState::InBus:
		case EBotState::ChoosingLanding:
		case EBotState::Jumping:
		case EBotState::Gliding:
			break;
		}
	}

	static bool ShouldUseAIBotController()
	{
		return false;
		return Fortnite_Version >= 11 && Engine_Version < 500;
	}

	static void InitializeBotClasses()
	{
		static auto BlueprintGeneratedClassClass = FindObject<UClass>(L"/Script/Engine.BlueprintGeneratedClass");

		if (!ShouldUseAIBotController())
		{
			PawnClass = FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");
			ControllerClass = AFortPlayerControllerAthena::StaticClass();
		}
		else
		{
			PawnClass = LoadObject<UClass>(L"/Game/Athena/AI/Phoebe/BP_PlayerPawn_Athena_Phoebe.BP_PlayerPawn_Athena_Phoebe_C", BlueprintGeneratedClassClass);
			// ControllerClass = PawnClass->CreateDefaultObject()->GetAIControllerClass();
		}

		if (/* !ControllerClass
			|| */ !PawnClass
			)
		{
			LOG_ERROR(LogBots, "Failed to find a class for the bots!");
			return;
		}
	}

	static bool IsReadyToSpawnBot()
	{
		return PawnClass;
	}

	void SetupInventory()
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!ShouldUseAIBotController()) // TODO REWRITE
		{
			AFortInventory** Inventory = nullptr;

			if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
			{
				Inventory = &FortPlayerController->GetWorldInventory();
			}
			else
			{
				if (auto FortAthenaAIBotController = Cast<AFortAthenaAIBotController>(Controller))
				{
					static auto InventoryOffset = Controller->GetOffset("Inventory");
					Inventory = Controller->GetPtr<AFortInventory*>(InventoryOffset);
				}
			}

			if (!Inventory)
			{
				LOG_ERROR(LogBots, "No inventory pointer!");

				Pawn->K2_DestroyActor();
				Controller->K2_DestroyActor();
				return;
			}

			static auto FortInventoryClass = FindObject<UClass>(L"/Script/FortniteGame.FortInventory"); // AFortInventory::StaticClass()
			*Inventory = GetWorld()->SpawnActor<AFortInventory>(FortInventoryClass, FTransform{}, CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AlwaysSpawn, false, Controller));

			if (!*Inventory)
			{
				LOG_ERROR(LogBots, "Failed to spawn Inventory!");

				Pawn->K2_DestroyActor();
				Controller->K2_DestroyActor();
				return;
			}

			(*Inventory)->GetInventoryType() = EFortInventoryType::World;

			if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
			{
				static auto bHasInitializedWorldInventoryOffset = FortPlayerController->GetOffset("bHasInitializedWorldInventory");
				FortPlayerController->Get<bool>(bHasInitializedWorldInventoryOffset) = true;
			}

			// if (false)
			{
				if (Inventory)
				{
					auto& StartingItems = GameMode->GetStartingItems();

					for (int i = 0; i < StartingItems.Num(); ++i)
					{
						auto& StartingItem = StartingItems.at(i, FItemAndCount::GetStructSize());

						// TODO: Check if it is FortSmartBuildingItemDefinition

						(*Inventory)->AddItem(StartingItem.GetItem(), nullptr, StartingItem.GetCount());
					}

					if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
					{
						UFortItem* PickaxeInstance = FortPlayerController->AddPickaxeToInventory();

						if (PickaxeInstance)
						{
							FortPlayerController->ServerExecuteInventoryItemHook(FortPlayerController, PickaxeInstance->GetItemEntry()->GetItemGuid());
						}
					}

					(*Inventory)->Update();
				}
			}
		}
	}

	void PickRandomLoadout()
	{
		auto AllHeroTypes = GetAllObjectsOfClass(FindObject<UClass>(L"/Script/FortniteGame.FortHeroType"));
		std::vector<UFortItemDefinition*> AthenaHeroTypes;

		UFortItemDefinition* HeroType = FindObject<UFortItemDefinition>(L"/Game/Athena/Heroes/HID_030_Athena_Commando_M_Halloween.HID_030_Athena_Commando_M_Halloween");

		for (int i = 0; i < AllHeroTypes.size(); ++i)
		{
			auto CurrentHeroType = (UFortItemDefinition*)AllHeroTypes.at(i);

			if (CurrentHeroType->GetPathName().starts_with("/Game/Athena/Heroes/"))
				AthenaHeroTypes.push_back(CurrentHeroType);
		}

		if (AthenaHeroTypes.size())
		{
			HeroType = AthenaHeroTypes.at(std::rand() % AthenaHeroTypes.size());
		}

		static auto HeroTypeOffset = PlayerState->GetOffset("HeroType");
		PlayerState->Get(HeroTypeOffset) = HeroType;
	}

	void ApplyCosmeticLoadout()
	{
		static auto HeroTypeOffset = PlayerState->GetOffset("HeroType");
		const auto CurrentHeroType = PlayerState->Get(HeroTypeOffset);

		if (!CurrentHeroType)
		{
			LOG_WARN(LogBots, "CurrentHeroType called with an invalid HeroType!");
			return;
		}

		ApplyHID(Pawn, CurrentHeroType, true);
	}

	void SetName(const FString& NewName)
	{
		if (// true ||
			Fortnite_Version < 9
			)
		{
			if (auto PlayerController = Cast<APlayerController>(Controller))
			{
				PlayerController->ServerChangeName(NewName);
			}
		}
		else
		{
			auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
			GameMode->ChangeName(Controller, NewName, true);
		}

		PlayerState->OnRep_PlayerName(); // ?
	}

	FString GetRandomName()
	{
		static int CurrentBotNum = 1;
		std::wstring BotNumWStr;
		FString NewName;

		if (Fortnite_Version < 9)
		{
			BotNumWStr = std::to_wstring(CurrentBotNum++);
			NewName = (L"RebootBot" + BotNumWStr).c_str();
		}
		else
		{
			if (Fortnite_Version < 11 || PlayerBotNames.empty())
			{
				BotNumWStr = std::to_wstring(CurrentBotNum++ + 200);
				NewName = (std::format(L"Anonymous[{}]", BotNumWStr)).c_str();
			}
			else
			{
				NewName = PlayerBotNames.back();
				PlayerBotNames.pop_back();
			}
		}

		return NewName;
	}

	void Initialize(const FTransform& SpawnTransform, AActor* InSpawnLocator)
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!IsReadyToSpawnBot())
		{
			LOG_ERROR(LogBots, "We are not prepared to spawn a bot!");
			return;
		}

		if (!ShouldUseAIBotController())
		{
			Controller = GetWorld()->SpawnActor<AController>(ControllerClass);
			Pawn = GetWorld()->SpawnActor<AFortPlayerPawnAthena>(PawnClass, SpawnTransform, CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn));
			PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		}
		else
		{
			Pawn = GameMode->GetServerBotManager()->GetCachedBotMutator()->SpawnBot(PawnClass, InSpawnLocator, SpawnTransform.Translation, SpawnTransform.Rotation.Rotator(), false);

			if (Fortnite_Version < 17)
				Controller = Cast<AFortAthenaAIBotController>(Pawn->GetController());
			else
				Controller = GetWorld()->SpawnActor<AFortAthenaAIBotController>(Pawn->GetAIControllerClass());

			PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		}

		if (!Controller || !Pawn || !PlayerState)
		{
			LOG_ERROR(LogBots, "Failed to spawn controller, pawn or playerstate ({} {})!", bool(__int64(Controller)), bool(__int64(Pawn)), bool(__int64(Controller->GetPlayerState())));
			return;
		}

		PlayerState->SetIsBot(true);

		if (Controller->GetPawn() != Pawn)
		{
			Controller->Possess(Pawn);
		}

		FString BotNewName = GetRandomName();
		
		LOG_INFO(LogBots, "BotNewName: {}", BotNewName.ToString());
		SetName(BotNewName);

		PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Controller);

		static auto SquadIdOffset = PlayerState->GetOffset("SquadId", false);

		if (SquadIdOffset != -1)
			PlayerState->GetSquadId() = PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;

		GameState->AddPlayerStateToGameMemberInfo(PlayerState);

		Pawn->SetHealth(100);
		Pawn->SetMaxHealth(100);

		auto PlayerAbilitySet = GetPlayerAbilitySet();
		auto AbilitySystemComponent = PlayerState->GetAbilitySystemComponent();

		if (PlayerAbilitySet && AbilitySystemComponent)
		{
			PlayerAbilitySet->GiveToAbilitySystem(AbilitySystemComponent);
		}

		SetupInventory();
		PickRandomLoadout();
		ApplyCosmeticLoadout();

		if (!ShouldUseAIBotController())
		{
			++GameState->GetPlayersLeft();
			GameState->OnRep_PlayersLeft();
		}

		if (auto FortPlayerControllerAthena = Cast<AFortPlayerControllerAthena>(Controller))
		{
			GameMode->GetAlivePlayers().Add(FortPlayerControllerAthena);
		}

		SetupBotAI();

		LOG_INFO(LogDev, "Finished spawning bot!")
	}
};

inline std::vector<PlayerBot> AllPlayerBotsToTick;

namespace Bots
{
	inline EBotPersonalityType GlobalBotDifficulty = EBotPersonalityType::Casual;
	inline bool bNewBotsUseGlobalDifficulty = true;

	static bool IsBotPawn(UObject* Pawn)
	{
		if (!Pawn)
			return false;
		for (auto& PB : AllPlayerBotsToTick)
			if (PB.Pawn == Pawn || (PB.Controller && PB.Controller->GetPawn() == Pawn))
				return true;
		return false;
	}

	static bool IsBotController(AController* Controller)
	{
		if (!Controller)
			return false;
		for (auto& PB : AllPlayerBotsToTick)
			if (PB.Controller == Controller)
				return true;
		return false;
	}

	static AController* SpawnBot(FTransform SpawnTransform, AActor* InSpawnLocator);

	static void ApplyGlobalDifficulty()
	{
		for (auto& PlayerBot : AllPlayerBotsToTick)
		{
			switch (GlobalBotDifficulty)
			{
			case EBotPersonalityType::Novato:
				PlayerBot.Personality = { 0.25f, 0.25f, 0.20f, 0.40f, 0.30f, 0.25f };
				break;
			case EBotPersonalityType::Agresivo:
				PlayerBot.Personality = { 0.85f, 0.50f, 0.45f, 0.60f, 0.55f, 0.80f };
				break;
			case EBotPersonalityType::Defensivo:
				PlayerBot.Personality = { 0.35f, 0.50f, 0.80f, 0.55f, 0.70f, 0.25f };
				break;
			case EBotPersonalityType::Pro:
				PlayerBot.Personality = { 0.90f, 0.95f, 0.90f, 0.95f, 0.95f, 0.90f };
				break;
			case EBotPersonalityType::Random:
				PlayerBot.AssignRandomPersonality();
				continue;
			case EBotPersonalityType::Casual:
			default:
				PlayerBot.Personality = { 0.50f, 0.45f, 0.40f, 0.60f, 0.50f, 0.50f };
				break;
			}
			PlayerBot.PersonalityType = GlobalBotDifficulty;
		}
	}

	// Spawn bots at whatever player starts exist; returns count spawned.
	static int AddBots(int AmountOfBots, AActor* SpawnLocator = nullptr)
	{
		if (AmountOfBots <= 0)
			return 0;

		int Spawned = 0;

		if (SpawnLocator)
		{
			FTransform Transform;
			Transform.Translation = SpawnLocator->GetActorLocation();
			Transform.Translation.Z += 1000;
			Transform.Scale3D = FVector(1, 1, 1);

			for (int i = 0; i < AmountOfBots; ++i)
			{
				if (i > 0) // displace so bots don't all try to spawn in the exact same spot (collision crash)
				{
					// bump bots outward (ring 300..~3k) and clear of the ground so they
					// don't stack on top of the spawning player and fall/respawn in a loop
					float Spacing = 300.0f + (float)(i % 8) * 275.0f;
					int RingIdx = 1 + i / 8;
					float Ring = Spacing * (float)RingIdx;
					float Ang = (float)((i * 137) % 360);
					Transform.Translation.X = SpawnLocator->GetActorLocation().X + std::cos(Ang * 3.14159265f / 180.0f) * Ring;
					Transform.Translation.Y = SpawnLocator->GetActorLocation().Y + std::sin(Ang * 3.14159265f / 180.0f) * Ring;
					Transform.Translation.Z = SpawnLocator->GetActorLocation().Z + 150.0f;
				}
				if (SpawnBot(Transform, SpawnLocator))
					Spawned++;
			}
			return Spawned;
		}

		static auto FortPlayerStartCreativeClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartCreative");
		static auto FortPlayerStartWarmupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartWarmup");
		TArray<AActor*> PlayerStarts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), Globals::bCreative ? FortPlayerStartCreativeClass : FortPlayerStartWarmupClass);

		int ActorsNum = PlayerStarts.Num();

		if (ActorsNum == 0)
		{
			PlayerStarts.Free();
			return 0;
		}

		for (int i = 0; i < AmountOfBots; ++i)
		{
			int StartIndex = (ActorsNum == 1) ? 0 : (std::rand() % ActorsNum);
			AActor* PlayerStart = PlayerStarts.at(StartIndex);

			if (!PlayerStart)
			{
				PlayerStarts.Free();
				return Spawned;
			}

			FTransform SpawnTransform = PlayerStart->GetTransform();
			if (i > 0) // displace so bots don't stack on the same start (collision crash)
			{
				float Spacing = 300.0f + (float)(i % 8) * 275.0f;
				int RingIdx = 1 + i / 8;
				float Ring = Spacing * (float)RingIdx;
				float Ang = (float)((i * 137) % 360);
				SpawnTransform.Translation.X += std::cos(Ang * 3.14159265f / 180.0f) * Ring;
				SpawnTransform.Translation.Y += std::sin(Ang * 3.14159265f / 180.0f) * Ring;
				SpawnTransform.Translation.Z += 150.0f;
			}

			if (SpawnBot(SpawnTransform, PlayerStart))
				Spawned++;
		}

		PlayerStarts.Free();
		return Spawned;
	}

	static AController* SpawnBot(FTransform SpawnTransform, AActor* InSpawnLocator)
	{
		auto playerBot = PlayerBot();
		playerBot.Initialize(SpawnTransform, InSpawnLocator);

		if (bNewBotsUseGlobalDifficulty && GlobalBotDifficulty != EBotPersonalityType::Random)
		{
			switch (GlobalBotDifficulty)
			{
			case EBotPersonalityType::Novato:
				playerBot.Personality = { 0.25f, 0.25f, 0.20f, 0.40f, 0.30f, 0.25f };
				break;
			case EBotPersonalityType::Agresivo:
				playerBot.Personality = { 0.85f, 0.50f, 0.45f, 0.60f, 0.55f, 0.80f };
				break;
			case EBotPersonalityType::Defensivo:
				playerBot.Personality = { 0.35f, 0.50f, 0.80f, 0.55f, 0.70f, 0.25f };
				break;
			case EBotPersonalityType::Pro:
				playerBot.Personality = { 0.90f, 0.95f, 0.90f, 0.95f, 0.95f, 0.90f };
				break;
			case EBotPersonalityType::Casual:
			default:
				playerBot.Personality = { 0.50f, 0.45f, 0.40f, 0.60f, 0.50f, 0.50f };
				break;
			}
			playerBot.PersonalityType = GlobalBotDifficulty;
		}

		AllPlayerBotsToTick.push_back(playerBot);
		return playerBot.Controller;
	}

	static void SpawnBotsAtPlayerStarts(int AmountOfBots)
	{
		if (AmountOfBots <= 0)
			return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		static auto FortPlayerStartCreativeClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartCreative");
		static auto FortPlayerStartWarmupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartWarmup");
		TArray<AActor*> PlayerStarts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), Globals::bCreative ? FortPlayerStartCreativeClass : FortPlayerStartWarmupClass);

		int ActorsNum = PlayerStarts.Num();

		// Actors.Free();

		if (ActorsNum == 0)
		{
			// LOG_INFO(LogDev, "No Actors!");
			return;
		}

		// Find playerstart (scuffed)

		for (int i = 0; i < AmountOfBots; ++i)
		{
			int StartIndex = (ActorsNum == 1) ? 0 : (std::rand() % ActorsNum);
			AActor* PlayerStart = PlayerStarts.at(StartIndex);

			if (!PlayerStart)
			{
				return;
			}

			FTransform SpawnTransform = PlayerStart->GetTransform();
			if (i > 0)
			{
				float Spacing = 300.0f + (float)(i % 8) * 275.0f;
				int RingIdx = 1 + i / 8;
				float Ring = Spacing * (float)RingIdx;
				float Ang = (float)((i * 137) % 360);
				SpawnTransform.Translation.X += std::cos(Ang * 3.14159265f / 180.0f) * Ring;
				SpawnTransform.Translation.Y += std::sin(Ang * 3.14159265f / 180.0f) * Ring;
				SpawnTransform.Translation.Z += 150.0f;
			}

			auto NewBot = SpawnBot(SpawnTransform, PlayerStart);
			NewBot->SetCanBeDamaged(Fortnite_Version < 7); // idk lol for spawn island
		}

		return;
	}

	static void Tick()
	{
		if (AllPlayerBotsToTick.size() == 0)
			return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		// auto AllBuildingContainers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABuildingContainer::StaticClass());

		// for (int i = 0; i < GameMode->GetAlivePlayers().Num(); ++i)
		for (auto& PlayerBot : AllPlayerBotsToTick)
		{
			auto CurrentPlayer = PlayerBot.Controller;

			if (!CurrentPlayer || !CurrentPlayer->IsValidLowLevel() || CurrentPlayer->IsPendingKill())
				continue;

			if (CurrentPlayer->IsActorBeingDestroyed())
				continue;

			auto CurrentPawn = CurrentPlayer->GetPawn();

			if (!CurrentPawn || !CurrentPawn->IsValidLowLevel() || CurrentPawn->IsPendingKill())
				continue;

			if (CurrentPawn->IsActorBeingDestroyed())
				continue;

			auto CurrentPlayerState = Cast<AFortPlayerStateAthena>(CurrentPlayer->GetPlayerState());

			if (!CurrentPlayerState
				// || !CurrentPlayerState->IsBot()
				)
				continue;

			if (GameState->GetGamePhase() == EAthenaGamePhase::Warmup)
			{
				/* if (!CurrentPlayer->IsPlayingEmote())
				{
					static auto AthenaDanceItemDefinitionClass = FindObject<UClass>("/Script/FortniteGame.AthenaDanceItemDefinition");
					auto RandomDanceID = GetRandomObjectOfClass(AthenaDanceItemDefinitionClass);

					CurrentPlayer->ServerPlayEmoteItemHook(CurrentPlayer, RandomDanceID);
				} */
			}	

			if (PlayerBot.bIsAthenaController && CurrentPlayerState->IsInAircraft() && !CurrentPlayerState->HasThankedBusDriver())
			{
				static auto ServerThankBusDriverFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerControllerAthena.ServerThankBusDriver");
				CurrentPlayer->ProcessEvent(ServerThankBusDriverFn);
			}

			// Run the bot's own decision/tick logic (movement, loot, combat, storm, endgame)
			PlayerBot.Tick();
		}

		// AllBuildingContainers.Free();
	}
}

namespace Bosses
{

}
