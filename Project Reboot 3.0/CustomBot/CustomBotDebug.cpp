#include "CustomBotDebug.h"

#include "CustomBot.h"
#include "globals.h"

#include "CustomBotSpawner.h"
#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotResources.h"
#include "CustomBotCombat.h"
#include "CustomBotBuilding.h"
#include "CustomBotDestruction.h"
#include "CustomBotInteraction.h"
#include "CustomBotBreak.h"

#include "FortItem.h"
#include "BuildingSMActor.h"
#include "GameplayStatics.h"

#include <format>
#include <string>
#include <cmath>

namespace
{
	void SendBotMessage(AFortPlayerController* PlayerController, const wchar_t* Msg)
	{
		if (!PlayerController)
			return;

		FString FMsg = Msg;
		FName TypeName = FName();
		float MsgLifetime = 1;

		struct
		{
			FString S;
			FName Type;
			float MsgLifeTime;
		} PlayerController_ClientMessage_Params{ FMsg, TypeName, MsgLifetime };

		static auto ClientMessageFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ClientMessage");
		PlayerController->ProcessEvent(ClientMessageFn, &PlayerController_ClientMessage_Params);
	}

	bool TryParseFloat(const std::string& Text, float& Out)
	{
		try
		{
			Out = std::stof(Text);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool TryParseInt(const std::string& Text, int& Out)
	{
		try
		{
			Out = std::stoi(Text);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::wstring ToWide(const std::string& Text)
	{
		if (Text.empty())
			return L"";

		std::wstring Out(Text.size(), L' ');

		for (size_t i = 0; i < Text.size(); ++i)
			Out[i] = (wchar_t)(unsigned char)Text[i];

		return Out;
	}

	CustomBot* GetFirstValidBot()
	{
		auto& Bots = CustomBotSpawner::AllCustomBots;

		for (auto& Bot : Bots)
		{
			if (Bot.IsValidActor())
				return &Bot;
		}

		return nullptr;
	}
}

namespace
{
	enum class DebugBotState : uint8_t
	{
		None,
		Spawned,
		MovingForward,
		BuildingRamp,
		WalkToRampStart,
		WalkToRampMiddle,
		Jumping,
		BuildingFloor,
		EquippingPickaxe,
		DestroyingRamp,
		EquippingWeapon,
		Shooting,
		DroppingWeapon,
		WaitingToDisappear,
		Finished,
	};

	const char* DebugBotStateName(DebugBotState State)
	{
		switch (State)
		{
		case DebugBotState::Spawned:            return "Spawned";
		case DebugBotState::MovingForward:      return "MovingForward";
		case DebugBotState::BuildingRamp:       return "BuildingRamp";
		case DebugBotState::WalkToRampStart:    return "WalkToRampStart";
		case DebugBotState::WalkToRampMiddle:   return "WalkToRampMiddle";
		case DebugBotState::Jumping:            return "Jumping";
		case DebugBotState::BuildingFloor:      return "BuildingFloor";
		case DebugBotState::EquippingPickaxe:   return "EquippingPickaxe";
		case DebugBotState::DestroyingRamp:     return "DestroyingRamp";
		case DebugBotState::EquippingWeapon:    return "EquippingWeapon";
		case DebugBotState::Shooting:           return "Shooting";
		case DebugBotState::DroppingWeapon:     return "DroppingWeapon";
		case DebugBotState::WaitingToDisappear: return "WaitingToDisappear";
		case DebugBotState::Finished:           return "Finished";
		case DebugBotState::None:
		default:                                return "None";
		}
	}

	struct DebugBotContext
	{
		DebugBotState Step = DebugBotState::None;
		double NextActionTime = 0.0;
		ABuildingSMActor* RampActor = nullptr;
		ABuildingSMActor* FloorActor = nullptr;
		int ShotsFired = 0;
		bool bMoveExperimentDone = false;
		int ModeForceCount = 0;
		double LastModeReLog = -1.0;
		double PauseUntil = -1.0;
		bool bClimbStarted = false;

		bool bRampFacingLogged = false;
		int RampAttempts = 0;
		int MaxRampAttempts = 5;
	};

	static DebugBotContext gDebugBot;

	static float DebugBotTime()
	{
		return UGameplayStatics::GetTimeSeconds(GetWorld());
	}

	static AFortPlayerControllerAthena* FindFirstValidPlayer()
	{
		static auto AIControllerClass = FindObject<UClass>(L"/Script/FortniteGame.AFortAthenaAIBotController");

		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (GameMode)
		{
			auto& Alive = GameMode->GetAlivePlayers();

			for (int i = 0; i < Alive.Num(); ++i)
			{
				auto* Candidate = Alive.At(i);

				if (!Candidate || Candidate->IsActorBeingDestroyed() || !Candidate->GetPawn())
					continue;

				if (AIControllerClass && Candidate->IsA(AIControllerClass))
					continue;

				bool bIsCustomBot = false;

				for (size_t k = 0; k < CustomBotSpawner::AllCustomBots.size(); ++k)
				{
					if (CustomBotSpawner::AllCustomBots[k].Controller == Candidate)
					{
						bIsCustomBot = true;
						break;
					}
				}

				if (bIsCustomBot)
					continue;

				return Candidate;
			}
		}

		auto* Local = Cast<AFortPlayerControllerAthena>(GetLocalPlayerController());

		if (Local && !Local->IsActorBeingDestroyed() && Local->GetPawn())
			return Local;

		return nullptr;
	}

	static UFortItemDefinition* FindTestWeaponDefinition()
	{
		LOG_INFO(LogBots, "[DebugBot] Searching for weapon definition...");

		static const wchar_t* CandidatePaths[] = {
			L"/Game/Athena/Items/Weapons/Guns/Assault/Assault_Longbarrel.SAID_Assault_Longbarrel_Auto",
			L"/Game/Athena/Items/Weapons/Guns/Assault/Assault_Short.SAID_Assault_Short",
			L"/Game/Athena/Items/Weapons/Guns/Rifle/Rifle_SemiAuto.SAID_Rifle_SemiAuto",
			L"/Game/Athena/Items/Weapons/Guns/Pistol/Pistol_SemiAuto.SAID_Pistol_SemiAuto",
		};

		for (auto* Path : CandidatePaths)
		{
			if (auto* Def = FindObject<UFortItemDefinition>(Path))
			{
				LOG_INFO(LogBots, "[DebugBot] Weapon found by path: {}", Def->GetPathName());
				return Def;
			}
		}

		LOG_INFO(LogBots, "[DebugBot] Hardcoded paths failed, scanning GObjects...");

		auto WeaponClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponItemDefinition");
		auto All = GetAllObjectsOfClass<UFortItemDefinition>(WeaponClass);

		LOG_INFO(LogBots, "[DebugBot] Found {} WeaponItemDefinitions total", All.size());

		static const char* GunDirs[] = { "/Guns/", "/Rifle/", "/Pistol/", "/SMG/", "/Shotgun/", "/Launchers/", "/Sniper/" };
		static const char* Excludes[] = {
			"Pickaxe", "BuildingTools", "EditTool", "Aimlaser", "Melee", "StatDebugger",
			"WID_StatDebugger", "Harvest_Tool", "ConsumesBuildingResource", "Trap", "Grenade",
		};

		auto IsExcluded = [](const std::string& P) {
			for (auto* E : Excludes)
				if (P.find(E) != std::string::npos)
					return true;
			return false;
		};

		auto IsGun = [](const std::string& P) {
			for (auto* G : GunDirs)
				if (P.find(G) != std::string::npos)
					return true;
			return false;
		};

		for (auto* Def : All)
		{
			if (!Def)
				continue;

			std::string Path = Def->GetPathName();

			if (Path.find("/Athena/Items/Weapons/") == std::string::npos)
				continue;
			if (IsExcluded(Path))
				continue;
			if (!IsGun(Path))
				continue;

			LOG_INFO(LogBots, "[DebugBot] Gun weapon found by scan: {}", Path);
			return Def;
		}

		for (auto* Def : All)
		{
			if (!Def)
				continue;

			std::string Path = Def->GetPathName();

			if (Path.find("/Athena/Items/Weapons/") == std::string::npos)
				continue;
			if (IsExcluded(Path))
				continue;

			LOG_INFO(LogBots, "[DebugBot] Fallback weapon found by scan: {}", Path);
			return Def;
		}

		LOG_WARN(LogBots, "[DebugBot] No weapon definition found!");
		return nullptr;
	}

	static bool DebugBotGrantLoadout(CustomBot& Bot)
	{
		LOG_INFO(LogBots, "[DebugBot] === Granting loadout ===");

		LOG_INFO(LogBots, "[DebugBot] Step 1: Equipping pickaxe to exit build mode...");
		bool bPickaxeEquipped = CustomBotInventory::EquipPickaxe(Bot);
		LOG_INFO(LogBots, "[DebugBot] Pickaxe equipped: {}", bPickaxeEquipped ? "YES" : "NO");

		LOG_INFO(LogBots, "[DebugBot] Step 2: Granting materials...");
		CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, 1000);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, 1000);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, 1000);
		LOG_INFO(LogBots, "[DebugBot] Materials: Wood={} Stone={} Metal={}",
			CustomBotResources::GetResourceCount(Bot, EFortResourceType::Wood),
			CustomBotResources::GetResourceCount(Bot, EFortResourceType::Stone),
			CustomBotResources::GetResourceCount(Bot, EFortResourceType::Metal));

		LOG_INFO(LogBots, "[DebugBot] Step 3: Finding weapon...");
		UFortItemDefinition* WeaponDef = FindTestWeaponDefinition();

		if (!WeaponDef)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: no weapon item definition found for test loadout!");
			return false;
		}

		LOG_INFO(LogBots, "[DebugBot] Giving weapon: {}", WeaponDef->GetPathName());
		CustomBotInventory::GiveItem(Bot, WeaponDef, 1, 999);

		UFortItem* GrantedWeapon = CustomBotInventory::FindItemByDefinition(Bot, WeaponDef);

		if (!GrantedWeapon)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: granted weapon NOT in inventory after GiveItem!");
			return false;
		}

		LOG_INFO(LogBots, "[DebugBot] Equipping weapon...");
		if (!CustomBotInventory::EquipItem(Bot, GrantedWeapon))
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: could not equip granted weapon!");
			return false;
		}

		auto* CurrentWeapon = CustomBotInventory::GetCurrentWeapon(Bot);
		if (CurrentWeapon)
		{
			auto* WeaponData = CurrentWeapon->GetWeaponData();
			LOG_INFO(LogBots, "[DebugBot] Current weapon after equip: {}",
				WeaponData ? WeaponData->GetPathName() : "UNKNOWN");
		}
		else
		{
			LOG_WARN(LogBots, "[DebugBot] No weapon equipped after EquipFirstWeapon!");
		}

		LOG_INFO(LogBots, "[DebugBot] Step 4: Reloading weapon...");
		CustomBotCombat::Reload(Bot, 999);
		LOG_INFO(LogBots, "[DebugBot] Weapon ammo: {}", CustomBotCombat::GetCurrentAmmo(Bot));

		LOG_INFO(LogBots, "[DebugBot] Step 5: Setting shield...");
		Bot.Pawn->SetMaxShield(100);
		Bot.Pawn->SetShield(100);
		LOG_INFO(LogBots, "[DebugBot] Shield set to 100 (current={})", Bot.Pawn->GetShield());

		LOG_INFO(LogBots, "[DebugBot] === Loadout complete ===");
		return true;
	}

	static void DebugBotRemove(CustomBot& Bot)
	{
		LOG_INFO(LogBots, "[DebugBot] Removing bot");

		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());

		if (GameMode)
		{
			auto& Alive = GameMode->GetAlivePlayers();

			for (int i = 0; i < Alive.Num(); ++i)
			{
				if (Alive.At(i) == Bot.Controller)
				{
					Alive.RemoveAt(i, 1);
					break;
				}
			}
		}

		if (GameState)
		{
			GameState->GetPlayersLeft() = FMath::Clamp(GameState->GetPlayersLeft() - 1, 0, 9999);
			GameState->OnRep_PlayersLeft();
		}

		Bot.DebugTick = nullptr;
		Bot.MoveRequest = CBT::FMoveRequest{};
		Bot.bMoveRequestActive = false;
		Bot.MoveState = CBT::EMovementState::Idle;
		gDebugBot = DebugBotContext{};

		if (Bot.WorldInventory)
			Bot.WorldInventory->K2_DestroyActor();

		Bot.Destroy();

		LOG_INFO(LogBots, "[DebugBot] Finished");
	}

	static void DebugBotError(CustomBot& Bot, const char* Reason)
	{
		LOG_ERROR(LogBots, "[DebugBot] ERROR during {}: {}", DebugBotStateName(gDebugBot.Step), Reason);
		gDebugBot.NextActionTime = DebugBotTime() + 1.0;
		gDebugBot.Step = DebugBotState::WaitingToDisappear;
	}


	struct ProbeSnapshot
	{
		FVector Loc{}, Rot{};
		FVector Vel{}, Acc{};
		int Mode = -1;
		float GravScale = -1.f;
		float MaxWalk = -1.f;
		int Crouch = -1;
		int WantsCrouch = -1;
		int NetDorm = -1;
		int Rep = -1;
		int TearOff = -1;
		int OnlyOwner = -1;
		int AlwaysRel = -1;
		int UsesOwnerRel = -1;
		int NetFreq = -1;
		int MinNetFreq = -1;
		int GravGate = -1;
		int MoveGate = -1;
		int AllowGate = -1;
		int CMStartTick = -1;
		int CMIsTicking = -1;
		int CMIsActive = -1;
		int RunPhys = -1;
		UObject* CMObj = nullptr;
		int CharOwner = 0;
		int UpdComp = 0;
		int AckPawn = -1;
		bool NetConn = false;
		int Health = -1;
	};

	static int DebugBotCMIsTickEnabled(UObject* CM)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/Engine.ActorComponent.IsComponentTickEnabled");
		if (!Fn || !CM) return -1;
		struct { char Buf[32]; } Params{};
		CM->ProcessEvent(Fn, &Params);
		return Params.Buf[0] ? 1 : 0;
	}

	static int DebugBotCMForceTick(UObject* CM)
	{
		static auto FnSet = FindObject<UFunction>(L"/Script/Engine.ActorComponent.SetComponentTickEnabled");
		static auto FnActivate = FindObject<UFunction>(L"/Script/Engine.ActorComponent.Activate");

		if (FnSet && CM)
		{
			struct { char Buf[32]; } Params{};
			Params.Buf[0] = 1;
			CM->ProcessEvent(FnSet, &Params);
		}
		if (FnActivate && CM)
		{
			struct { char Buf[32]; } Params{};
			Params.Buf[0] = 1;
			CM->ProcessEvent(FnActivate, &Params);
		}
		return DebugBotCMIsTickEnabled(CM);
	}

	static int DebugBotCMIsActive(UObject* CM)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/Engine.ActorComponent.IsActive");
		if (!Fn || !CM) return -1;
		struct { char Buf[32]; } Params{};
		CM->ProcessEvent(Fn, &Params);
		return Params.Buf[0] ? 1 : 0;
	}

	static void DebugBotClaimLive(AFortPlayerControllerAthena* Controller, APawn* Pawn, bool bCallUFunction)
	{
		if (!Controller || !Pawn || !bCallUFunction)
			return;

		static auto AckFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ServerAcknowledgePossession");
		if (AckFn)
		{
			struct { APawn* NewPawn; } Params{ Pawn };
			Controller->ProcessEvent(AckFn, &Params);
			LOG_WARN(LogBots, "[DebugBot] CLAIM-LIVE: ServerAcknowledgePossession invoked on bot controller");
		}

		auto AckOff = Controller->GetOffset("AcknowledgedPawn", false);
		if (AckOff != -1 && Controller->Get<APawn*>(AckOff) != Pawn)
		{
			Controller->Get<APawn*>(AckOff) = Pawn;
			LOG_WARN(LogBots, "[DebugBot] CLAIM-LIVE: AcknowledgedPawn forced to bot pawn");
		}
	}

	static ProbeSnapshot DebugBotProbePawn(AFortPlayerPawnAthena* Pawn)
	{
		ProbeSnapshot S;

		if (!Pawn)
			return S;

		auto Loc = Pawn->GetActorLocation();
		auto R = Pawn->GetActorRotation();
		S.Loc = Loc;
		S.Rot = { R.Pitch, R.Yaw, R.Roll };

		auto ReadByte = [&](const char* Name) -> int {
			int o = Pawn->GetOffset(std::string(Name), false);
			return o != -1 ? *(uint8_t*)(__int64(Pawn) + o) : -1;
		};

		S.NetDorm = (int)Pawn->GetNetDormancy();
		S.Rep = Pawn->DoesReplicate() ? 1 : 0;
		S.TearOff = Pawn->IsTearOff() ? 1 : 0;
		S.OnlyOwner = Pawn->IsOnlyRelevantToOwner() ? 1 : 0;
		S.AlwaysRel = Pawn->IsAlwaysRelevant() ? 1 : 0;
		S.UsesOwnerRel = Pawn->UsesOwnerRelevancy() ? 1 : 0;
		S.NetFreq = (int)Pawn->GetNetUpdateFrequency();
		S.MinNetFreq = (int)Pawn->GetMinNetUpdateFrequency();

		S.GravGate = ReadByte("bSimGravityDisabled");
		S.MoveGate = ReadByte("bDisableMovementAndTurnInPlace");
		S.AllowGate = ReadByte("bAllowMovement");

		int CMOff = Pawn->GetOffset(std::string("CharacterMovement"), false);

		if (CMOff != -1)
		{
			auto* CM = Pawn->Get(CMOff);

			if (CM)
			{
				int o = CM->GetOffset(std::string("Velocity"), false);
				if (o != -1) S.Vel = CM->Get<FVector>(o);

				o = CM->GetOffset(std::string("Acceleration"), false);
				if (o != -1) S.Acc = CM->Get<FVector>(o);

				o = CM->GetOffset(std::string("MovementMode"), false);
				if (o != -1) S.Mode = *(int*)(__int64(CM) + o);

				o = CM->GetOffset(std::string("GravityScale"), false);
				if (o != -1) S.GravScale = CM->Get<float>(o);

				o = CM->GetOffset(std::string("MaxWalkSpeed"), false);
				if (o != -1) S.MaxWalk = CM->Get<float>(o);

				o = CM->GetOffset(std::string("bIsCrouching"), false);
				if (o != -1) S.Crouch = *(uint8_t*)(__int64(CM) + o);

				o = CM->GetOffset(std::string("bWantsToCrouch"), false);
				if (o != -1) S.WantsCrouch = *(uint8_t*)(__int64(CM) + o);

				int PCT = CM->GetOffset(std::string("PrimaryComponentTick"), false);
				int ST = CM->GetOffset(std::string("bStartWithTickEnabled"), false);
				if (ST == -1) ST = PCT;
				if (ST != -1) S.CMStartTick = *(uint8_t*)(__int64(CM) + ST);

				S.CMIsTicking = DebugBotCMIsTickEnabled(CM);
				S.CMIsActive = DebugBotCMIsActive(CM);
				S.CMObj = CM;

				auto OffChar = CM->GetOffset(std::string("CharacterOwner"), false);
				auto OffUpd = CM->GetOffset(std::string("UpdatedComponent"), false);
				auto OffPhys = CM->GetOffset(std::string("bRunPhysicsWithNoController"), false);
				S.CharOwner = OffChar != -1 ? (CM->Get<UObject*>(OffChar) != 0 ? 1 : 0) : -1;
				S.UpdComp = OffUpd != -1 ? (CM->Get<UObject*>(OffUpd) != 0 ? 1 : 0) : -1;
				S.RunPhys = OffPhys != -1 ? *(uint8_t*)(__int64(CM) + OffPhys) : -1;
			}
		}

		auto* Ctrl = Pawn->GetController();
		int AckOff = Ctrl ? Ctrl->GetOffset(std::string("AcknowledgedPawn"), false) : -1;
		if (AckOff != -1)
			S.AckPawn = (Ctrl->Get<APawn*>(AckOff) == Pawn) ? 1 : 0;

		S.Health = (int)Pawn->GetHealth();

		return S;
	}

	static void TickDebugBot(CustomBot& Bot)
	{
		static unsigned DebugTickCounter = 0;
		if ((++DebugTickCounter) % 120 == 0)
			LOG_INFO(LogBots, "[DebugBot] [seq.tick] step={} ready={} valid={} life={}",
				DebugBotStateName(gDebugBot.Step), Bot.IsReady(), Bot.IsValidActor(), (int)Bot.GetLifeState());

		if (gDebugBot.Step == DebugBotState::None || gDebugBot.Step == DebugBotState::Finished)
			return;

		if (!Bot.IsReady() || !Bot.IsValidActor() || Bot.GetLifeState() != CBT::ELifeState::Alive)
		{
			LOG_ERROR(LogBots, "[DebugBot] ERROR: bot lost/died during sequence (step={}, life={}), removing",
				DebugBotStateName(gDebugBot.Step), (int)Bot.GetLifeState());
			gDebugBot.NextActionTime = DebugBotTime() + 0.5;
			gDebugBot.Step = DebugBotState::WaitingToDisappear;
			return;
		}

		float T = DebugBotTime();
		FVector Pos = Bot.Pawn->GetActorLocation();
		auto* CurrentWeapon = CustomBotInventory::GetCurrentWeapon(Bot);

		switch (gDebugBot.Step)
		{
		case DebugBotState::Spawned:
		{
			auto* GameStateDBG = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
			LOG_INFO(LogBots, "[DebugBot] Match: phase={} step={}",
				GameStateDBG ? (int)GameStateDBG->GetGamePhase() : -1,
				GameStateDBG ? (int)GameStateDBG->GetGamePhaseStep() : -1);

			auto* WeaponData = CurrentWeapon ? CurrentWeapon->GetWeaponData() : nullptr;
			LOG_INFO(LogBots, "[DebugBot] [Spawned] pos=({:.0f},{:.0f},{:.0f}) weapon={} moveActive={}",
				Pos.X, Pos.Y, Pos.Z,
				WeaponData ? WeaponData->GetPathName().c_str() : (CurrentWeapon ? "UNKNOWN" : "NONE"),
				Bot.bMoveRequestActive);

			if (auto* CM = CustomBotMovement::GetCharacterMovement(Bot))
			{
				static auto VelocityOffset = CM->GetOffset("Velocity");
				static auto AccelerationOffset = CM->GetOffset("Acceleration");
				static auto MovementModeOffset = CM->GetOffset("MovementMode");
				FVector V = CM->Get<FVector>(VelocityOffset);
				FVector A = CM->Get<FVector>(AccelerationOffset);
				int Mode = 255;
				auto TryMode = CM->GetOffset("MovementMode");
				if (TryMode != -1) Mode = *(int*)(__int64(CM) + TryMode);
				LOG_INFO(LogBots, "[DebugBot] CM: vel=({:.0f},{:.0f},{:.0f}) acc=({:.0f},{:.0f},{:.0f}) mode={}",
					V.X, V.Y, V.Z, A.X, A.Y, A.Z, Mode);

				auto CMAddr = __int64(CM);
				LOG_INFO(LogBots, "[DebugBot] CM state: addr=0x{:x}",
					CMAddr);
				auto ProbeByte = [&](const char* Field, int Def = -1) -> int {
					int off = CM->GetOffset(Field, false);
					if (off == -1) return Def;
					return *(uint8_t*)(CMAddr + off);
				};
				auto ProbeTickFlags = [&](const char* StructField, const char* SubField) -> int {
					int off = CM->GetOffset(StructField, false);
					if (off == -1) off = CM->GetOffset(SubField, false);
					if (off == -1) return -1;
					return *(uint8_t*)(CMAddr + off);
				};
				LOG_INFO(LogBots, "[DebugBot] CM flags: bMovementEnabled={} bComponentShouldTick={} bRegistered={} bNeedUROUpdate={} GravityScaleField={}",
					ProbeByte("bMovementEnabled"), ProbeByte("bComponentShouldTick"),
					ProbeByte("bRegistered"), ProbeByte("bNeedUROUpdate"),
					ProbeTickFlags("PrimaryComponentTick", "bStartWithTickEnabled"));

				LOG_INFO(LogBots, "[DebugBot] Pawn: dormancy={} bTearOff={} bReplicates={} controllerPawnMatch={} netConnection={}",
					(int)Bot.Pawn->GetNetDormancy(),
					(int)Bot.Pawn->IsTearOff(),
					Bot.Pawn->DoesReplicate(),
					Bot.Controller && Bot.Controller->GetPawn() == Bot.Pawn,
					Bot.Controller ? (bool)Bot.Controller->GetNetConnection() : false);

				int GravOff = Bot.Pawn->GetOffset("bSimGravityDisabled", false);
				int StopOff = Bot.Pawn->GetOffset("bDisableMovementAndTurnInPlace", false);
				int AllowOff = Bot.Pawn->GetOffset("bAllowMovement", false);

				LOG_INFO(LogBots, "[DebugBot] Pawn gates: bSimGravityDisabled={} bDisableMovementAndTurnInPlace={} bAllowMovement={} (offs={},{},{})",
					GravOff != -1 ? *(uint8_t*)(__int64(Bot.Pawn) + GravOff) : -1,
					StopOff != -1 ? *(uint8_t*)(__int64(Bot.Pawn) + StopOff) : -1,
					AllowOff != -1 ? *(uint8_t*)(__int64(Bot.Pawn) + AllowOff) : -1,
					GravOff, StopOff, AllowOff);

				if (GravOff != -1 && *(uint8_t*)(__int64(Bot.Pawn) + GravOff))
				{
					*(uint8_t*)(__int64(Bot.Pawn) + GravOff) = 0;
					LOG_INFO(LogBots, "[DebugBot] FORCED bSimGravityDisabled=false");
				}
				if (StopOff != -1 && *(uint8_t*)(__int64(Bot.Pawn) + StopOff))
				{
					*(uint8_t*)(__int64(Bot.Pawn) + StopOff) = 0;
					LOG_INFO(LogBots, "[DebugBot] FORCED bDisableMovementAndTurnInPlace=false");
				}
				if (AllowOff != -1 && *(uint8_t*)(__int64(Bot.Pawn) + AllowOff) == 0)
				{
					*(uint8_t*)(__int64(Bot.Pawn) + AllowOff) = 1;
					LOG_INFO(LogBots, "[DebugBot] FORCED bAllowMovement=true");
				}

				auto GetCmFloat = [&](const char* Field) -> float {
					int Off = CM->GetOffset(Field, false);
					if (Off == -1) return -99999.0f;
					return *(float*)(CMAddr + Off);
				};
				LOG_INFO(LogBots, "[DebugBot] CM gravity: GravityZ={:.1f} GravityScale={:.2f}",
					GetCmFloat("GravityZ"), GetCmFloat("GravityScale"));
			}
			else
			{
				LOG_ERROR(LogBots, "[DebugBot] ERROR: CharacterMovement is NULL on bot pawn!");
			}

			gDebugBot.Step = DebugBotState::MovingForward;

			LOG_INFO(LogBots, "[DebugBot] -> MovingForward");

			LOG_INFO(LogBots, "[DebugBot] PHYSICS-PROBE: LaunchCharacter impulse Z=300");
			CustomBotMovement::Launch(Bot, FVector(0, 0, 300), false, false);

			FVector Fwd = Bot.Pawn->GetActorForwardVector();
			FVector Start = Bot.Pawn->GetActorLocation();
			FVector Dest{ Start.X + Fwd.X * 250.0f, Start.Y + Fwd.Y * 250.0f, Start.Z };
			LOG_INFO(LogBots, "[DebugBot] MoveTo dest=({:.0f},{:.0f},{:.0f}) radius=120", Dest.X, Dest.Y, Dest.Z);
			CustomBotMovement::MoveTo(Bot, Dest, 120.0f);
			gDebugBot.NextActionTime = T + 8.0;
			break;
		}

		case DebugBotState::MovingForward:
		{
			auto* CME = CustomBotMovement::GetCharacterMovement(Bot);

			if (!gDebugBot.bMoveExperimentDone)
			{
				gDebugBot.bMoveExperimentDone = true;
				DebugBotClaimLive(Bot.Controller, Bot.Pawn, true);
			}


			if (CME)
			{
				__int64 CMEAddr = __int64(CME);

				int TickNow = DebugBotCMForceTick(CME);

				auto SetFloatIfPresent = [&](const char* Name, float Value) {
					int Off = CME->GetOffset(Name, false);
					if (Off != -1) *(float*)(CMEAddr + Off) = Value;
				};
				auto MoveIntIfPresent = [&](const char* Name, int Value) -> bool {
					int Off = CME->GetOffset(Name, false);
					if (Off == -1) return false;
					*(int*)(CMEAddr + Off) = Value;
					return true;
				};

				SetFloatIfPresent("MaxWalkSpeed", CustomBotMovement::WalkSpeed);
				SetFloatIfPresent("MaxWalkSpeedCrouched", CustomBotMovement::WalkSpeed);
				SetFloatIfPresent("MaxFlySpeed", CustomBotMovement::WalkSpeed);
				SetFloatIfPresent("MaxAcceleration", 2048.0f);

				int Mode = -1;
				int ModeOff = CME->GetOffset("MovementMode", false);
				if (ModeOff != -1) Mode = *(int*)(CMEAddr + ModeOff);
				if (Mode != 1)
				{
					MoveIntIfPresent("MovementMode", 1);
					MoveIntIfPresent("GroundMovementMode", 1);
					++gDebugBot.ModeForceCount;
					if (T - gDebugBot.LastModeReLog >= 1.0)
					{
						LOG_WARN(LogBots, "[DebugBot] CLONE-EXPERIMENT: watchdog reverted mode->{} (re-forced Walking #{}) cmTickEnabled={}",
							Mode, gDebugBot.ModeForceCount, TickNow);
						gDebugBot.LastModeReLog = T;
					}
				}
			}

			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (gDebugBot.PauseUntil < 0)
				{
					gDebugBot.PauseUntil = T + 3.0;
					LOG_INFO(LogBots, "[DebugBot] [MovingForward] arrived, holding 3s before jumping (pos=({:.0f},{:.0f},{:.0f}))",
						Pos.X, Pos.Y, Pos.Z);
					break;
				}

if (T >= gDebugBot.PauseUntil)
			{
				gDebugBot.Step = DebugBotState::BuildingRamp;
				LOG_INFO(LogBots, "[DebugBot] -> BuildingRamp");
				gDebugBot.NextActionTime = T + 3.0;
				break;
			}
				break;
			}
			else
			{
				static double LastMoveLog = 0;
				auto* GameStateDBG = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
				if (T - LastMoveLog >= 2.0)
				{
					FVector V{}, A{};
					int Mode = -1;
					if (auto* CM = CustomBotMovement::GetCharacterMovement(Bot))
					{
						static const auto OffV = CM->GetOffset("Velocity");
						static const auto OffA = CM->GetOffset("Acceleration");
						static const auto OffM = CM->GetOffset("MovementMode");
						V = CM->Get<FVector>(OffV);
						A = CM->Get<FVector>(OffA);
						if (OffM != -1) Mode = *(int*)(__int64(CM) + OffM);
					}
					LOG_INFO(LogBots, "[DebugBot] [MovingForward] pos=({:.0f},{:.0f},{:.0f}) moveState={} vel=({:.0f},{:.0f},{:.0f}) acc=({:.0f},{:.0f},{:.0f}) mode={} phase={}",
						Pos.X, Pos.Y, Pos.Z, (int)Bot.MoveState, V.X, V.Y, V.Z, A.X, A.Y, A.Z, Mode,
						GameStateDBG ? (int)GameStateDBG->GetGamePhase() : -1);

if (GameStateDBG)
				{
					LOG_INFO(LogBots, "[DebugBot] [MovingForward] phaseStep={}",
						(int)GameStateDBG->GetGamePhaseStep());
				}

				LastMoveLog = T;
				}
			}
			break;
		}

		case DebugBotState::BuildingRamp:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				++gDebugBot.RampAttempts;
				if (gDebugBot.RampAttempts > gDebugBot.MaxRampAttempts)
				{
					LOG_ERROR(LogBots, "[DebugBot] BuildingRamp: {} attempts failed, skipping to Finished", gDebugBot.RampAttempts - 1);
					DebugBotError(Bot, "ramp build failed after max retries");
					gDebugBot.Step = DebugBotState::Finished;
					break;
				}

				LOG_INFO(LogBots, "[DebugBot] [BuildingRamp] pos=({:.0f},{:.0f},{:.0f}) attempt={}/{}",
					Pos.X, Pos.Y, Pos.Z, gDebugBot.RampAttempts, gDebugBot.MaxRampAttempts);

				int TotalMat = CustomBotResources::GetTotalResourceCount(Bot);
				LOG_INFO(LogBots, "[DebugBot] Total materials: {}", TotalMat);

				if (TotalMat < 10)
				{
					LOG_WARN(LogBots, "[DebugBot] Refreshing materials (was {})...", TotalMat);
					CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, 1000);
					CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, 1000);
					CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, 1000);
					TotalMat = CustomBotResources::GetTotalResourceCount(Bot);
					LOG_INFO(LogBots, "[DebugBot] Materials after refresh: {}", TotalMat);
				}

				if (TotalMat < 10)
				{
					DebugBotError(Bot, "not enough materials to build ramp");
					break;
				}

				FVector Start = Bot.Pawn->GetActorLocation();

				auto GS_Build = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
				auto SSS_Build = GS_Build ? GS_Build->GetStructuralSupportSystem() : nullptr;

				float FacingCardinal = CustomBotBuilding::SnapYawToCardinal(Bot.Pawn->GetActorRotation().Yaw);
				FRotator RampRot = Bot.Pawn->GetActorRotation();
				RampRot.Yaw = CustomBotBuilding::SnapYawToCardinal(FacingCardinal + 90.0f);

				FVector RampLoc = Start;
				if (SSS_Build && !CustomBotBuilding::CellCenterAhead(SSS_Build, Start, FacingCardinal, 1, RampLoc))
				{
					DebugBotError(Bot, "could not find grid cell for ramp");
					break;
				}
				FVector RampGround = UFortKismetLibrary::FindGroundLocationAt(GetWorld(), Bot.Pawn,
					FVector{ RampLoc.X, RampLoc.Y, 0.0f }, Pos.Z + 2000.0f, Pos.Z - 5000.0f, FName(0));
				RampLoc.Z = RampGround.Z;
				LOG_INFO(LogBots, "[DebugBot] BuildRamp (cell ahead, yaw {:.1f}) at ({:.0f},{:.0f},{:.0f}) groundZ={:.0f}",
					RampRot.Yaw, RampLoc.X, RampLoc.Y, RampLoc.Z, RampGround.Z);

				gDebugBot.RampActor = CustomBotBuilding::BuildRamp(Bot, RampLoc, RampRot);

				if (!gDebugBot.RampActor)
				{
					LOG_ERROR(LogBots, "[DebugBot] BuildRamp returned nullptr!");
					DebugBotError(Bot, "ramp could not be built (invalid location)");
					break;
				}

				LOG_INFO(LogBots, "[DebugBot] Ramp built OK (health {:.0f})", CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor));

				if (gDebugBot.RampActor && !gDebugBot.bRampFacingLogged)
				{
					gDebugBot.bRampFacingLogged = true;
					FRotator RA = gDebugBot.RampActor->GetActorRotation();
					FRotator RP = Bot.Pawn->GetActorRotation();
					LOG_INFO(LogBots, "[DebugBot] RAMP-ORIENT rampaYaw={:.1f} botYaw={:.1f} rampLoc=({:.0f},{:.0f},{:.0f}) botLoc=({:.0f},{:.0f},{:.0f})",
						RA.Yaw, RP.Yaw, gDebugBot.RampActor->GetActorLocation().X, gDebugBot.RampActor->GetActorLocation().Y, gDebugBot.RampActor->GetActorLocation().Z,
						Bot.Pawn->GetActorLocation().X, Bot.Pawn->GetActorLocation().Y, Bot.Pawn->GetActorLocation().Z);
				}

				gDebugBot.RampAttempts = 0;
				gDebugBot.Step = DebugBotState::WalkToRampStart;
				gDebugBot.PauseUntil = -1;
				gDebugBot.bClimbStarted = false;
				LOG_INFO(LogBots, "[DebugBot] -> WalkToRampStart (3s pause)");
			}
			break;
		}

		case DebugBotState::WalkToRampStart:
		{
			if (gDebugBot.PauseUntil < 0)
			{
				gDebugBot.PauseUntil = T + 3.0;
				LOG_INFO(LogBots, "[DebugBot] Ramp built; holding 3s before walking to its start");
				break;
			}

			if (!gDebugBot.bClimbStarted && T >= gDebugBot.PauseUntil)
			{
				gDebugBot.bClimbStarted = true;

				FVector StartPos = Bot.Pawn->GetActorLocation();
				FVector RampPos = gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation() : Bot.Pawn->GetActorLocation();
				FVector Entrance{ (StartPos.X + RampPos.X) * 0.5f, (StartPos.Y + RampPos.Y) * 0.5f, StartPos.Z };
				CustomBotMovement::MoveTo(Bot, Entrance, 150.0f);
				gDebugBot.NextActionTime = T + 9.0;
				LOG_INFO(LogBots, "[DebugBot] walking to ramp start/entrance ({:.0f},{:.0f},{:.0f})", Entrance.X, Entrance.Y, Entrance.Z);
				break;
			}

			if (gDebugBot.bClimbStarted && (Bot.HasArrived() || T >= gDebugBot.NextActionTime))
			{
				if (T >= gDebugBot.NextActionTime)
					LOG_INFO(LogBots, "[DebugBot] walk to ramp start watchdog (continuing sequence)");

				gDebugBot.Step = DebugBotState::WalkToRampMiddle;
				gDebugBot.bClimbStarted = false;
				LOG_INFO(LogBots, "[DebugBot] -> WalkToRampMiddle");
			}
			break;
		}

		case DebugBotState::WalkToRampMiddle:
		{
			if (!gDebugBot.bClimbStarted)
			{
				gDebugBot.bClimbStarted = true;
				FVector RampPos = gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation() : Bot.Pawn->GetActorLocation();
				FVector Middle{ RampPos.X, RampPos.Y, Bot.Pawn->GetActorLocation().Z };
				CustomBotMovement::MoveTo(Bot, Middle, 150.0f);
				gDebugBot.NextActionTime = T + 9.0;
				LOG_INFO(LogBots, "[DebugBot] walking to ramp middle ({:.0f},{:.0f},{:.0f})", Middle.X, Middle.Y, Middle.Z);
				break;
			}

			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (T >= gDebugBot.NextActionTime)
					LOG_INFO(LogBots, "[DebugBot] ramp mid watchdog (continuing sequence)");

				gDebugBot.Step = DebugBotState::Jumping;
				gDebugBot.bClimbStarted = false;
				LOG_INFO(LogBots, "[DebugBot] -> Jumping (on ramp middle)");
				CustomBotMovement::Jump(Bot);
				gDebugBot.NextActionTime = T + 1.0;
			}
			break;
		}

		case DebugBotState::Jumping:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::BuildingFloor;
				LOG_INFO(LogBots, "[DebugBot] -> BuildingFloor (suelo justo debajo del bot)");
			}
			break;
		}

		case DebugBotState::BuildingFloor:
		{
			LOG_INFO(LogBots, "[DebugBot] [BuildingFloor] pos=({:.0f},{:.0f},{:.0f})", Pos.X, Pos.Y, Pos.Z);

			FVector FloorLoc = FVector{ Pos.X, Pos.Y, (gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation().Z : Pos.Z - 16.0f) };
			auto GS_Floor = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
			auto SSS_Floor = GS_Floor ? GS_Floor->GetStructuralSupportSystem() : nullptr;
			if (SSS_Floor && CustomBotBuilding::CellCenterAhead(SSS_Floor, Pos,
				CustomBotBuilding::SnapYawToCardinal(Bot.Pawn->GetActorRotation().Yaw), 0, FloorLoc))
			{
				FloorLoc.Z = gDebugBot.RampActor ? gDebugBot.RampActor->GetActorLocation().Z : Pos.Z - 16.0f;
			}
			LOG_INFO(LogBots, "[DebugBot] BuildingFloor at ({:.0f},{:.0f},{:.0f})", FloorLoc.X, FloorLoc.Y, FloorLoc.Z);

			gDebugBot.FloorActor = CustomBotBuilding::BuildFloor(Bot, FloorLoc, FRotator{ 0.0f, 0.0f, 0.0f }, false, true);

			if (!gDebugBot.FloorActor)
			{
				DebugBotError(Bot, "floor could not be built below the bot");
				break;
			}

			LOG_INFO(LogBots, "[DebugBot] Floor built OK (health {:.0f})", CustomBotDestruction::GetStructureHealth(gDebugBot.FloorActor));

			gDebugBot.Step = DebugBotState::EquippingPickaxe;
			gDebugBot.NextActionTime = T + 0.6;
			LOG_INFO(LogBots, "[DebugBot] -> EquippingPickaxe");
			break;
		}

		case DebugBotState::EquippingPickaxe:
		{
			if (T >= gDebugBot.NextActionTime)
			{
				gDebugBot.Step = DebugBotState::DestroyingRamp;
				LOG_INFO(LogBots, "[DebugBot] Destroying ramp + floor with pickaxe");

				if (!CustomBotInventory::EquipPickaxe(Bot))
				{
					DebugBotError(Bot, "pickaxe could not be equipped");
					break;
				}

				if (gDebugBot.RampActor)
				{
					FVector RampPos = gDebugBot.RampActor->GetActorLocation();
					CustomBotMovement::MoveTo(Bot, FVector{ RampPos.X, RampPos.Y, RampPos.Z + 20.0f }, 140.0f);
				}

				gDebugBot.NextActionTime = T + 3.0;
			}
			break;
		}

		case DebugBotState::DestroyingRamp:
		{
			if (Bot.HasArrived() || T >= gDebugBot.NextActionTime)
			{
				if (!gDebugBot.RampActor || CustomBotDestruction::IsStructureDestroyed(gDebugBot.RampActor))
				{
					DebugBotError(Bot, "ramp already gone or missing to destroy");
					break;
				}

				float BeforeR = CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor);
				bool bDestroyedR = CustomBotDestruction::DestroyTarget(gDebugBot.RampActor);
				float AfterR = CustomBotDestruction::GetStructureHealth(gDebugBot.RampActor);

				if (!bDestroyedR || AfterR > 0.0f)
				{
					LOG_INFO(LogBots, "[DebugBot] ramp damage applied: {:.0f} -> {:.0f} (melee pickaxe real pendiente en Parte 2)", BeforeR, AfterR);
				}
				else
				{
					LOG_INFO(LogBots, "[DebugBot] Ramp destroyed: {:.0f} -> {:.0f}", BeforeR, AfterR);
				}

				if (gDebugBot.FloorActor && !CustomBotDestruction::IsStructureDestroyed(gDebugBot.FloorActor))
				{
					float BeforeF = CustomBotDestruction::GetStructureHealth(gDebugBot.FloorActor);
					bool bDestroyedF = CustomBotDestruction::DestroyTarget(gDebugBot.FloorActor);
					float AfterF = CustomBotDestruction::GetStructureHealth(gDebugBot.FloorActor);

					if (!bDestroyedF || AfterF > 0.0f)
					{
						LOG_INFO(LogBots, "[DebugBot] floor damage applied: {:.0f} -> {:.0f} (melee pickaxe real pendiente en Parte 2)", BeforeF, AfterF);
					}
					else
					{
						LOG_INFO(LogBots, "[DebugBot] Floor destroyed: {:.0f} -> {:.0f}", BeforeF, AfterF);
					}

					gDebugBot.NextActionTime = T + 0.1;
				}
				else
				{
					gDebugBot.NextActionTime = T + 0.1;
				}

				gDebugBot.Step = DebugBotState::EquippingWeapon;
				gDebugBot.PauseUntil = -1;
				LOG_INFO(LogBots, "[DebugBot] -> EquippingWeapon");
			}
			break;
		}

		case DebugBotState::EquippingWeapon:
		{
			if (gDebugBot.PauseUntil < 0)
			{
				gDebugBot.PauseUntil = T + 3.0;
				LOG_INFO(LogBots, "[DebugBot] Equipping weapon");

				if (!CustomBotInventory::EquipFirstWeapon(Bot))
				{
					DebugBotError(Bot, "no weapon to re-equip after destroying ramp");
					break;
				}

				CustomBotCombat::Reload(Bot, 999);
				LOG_INFO(LogBots, "[DebugBot] Weapon re-equipped (ammo {}); holding 3s before shooting", CustomBotCombat::GetCurrentAmmo(Bot));
				break;
			}

			if (T >= gDebugBot.PauseUntil)
			{
				gDebugBot.PauseUntil = -1;
				gDebugBot.Step = DebugBotState::Shooting;
				gDebugBot.ShotsFired = 0;

				FVector Fwd = Bot.Pawn->GetActorForwardVector();
				FVector Start = Bot.Pawn->GetActorLocation();
				CustomBotCombat::AimAt(Bot, FVector{ Start.X + Fwd.X * 1500.0f, Start.Y + Fwd.Y * 1500.0f, Start.Z });

				LOG_INFO(LogBots, "[DebugBot] Shooting");
				gDebugBot.NextActionTime = T + 0.3;
			}
			break;
		}

		case DebugBotState::Shooting:
		{
			if (T >= gDebugBot.NextActionTime && gDebugBot.ShotsFired <= 5)
			{
				if (gDebugBot.ShotsFired < 5)
				{
					bool bFired = CustomBotCombat::FireWeapon(Bot);
					++gDebugBot.ShotsFired;
					LOG_INFO(LogBots, "[DebugBot] Shot {}/5 fired={} ammo={}", gDebugBot.ShotsFired, bFired, CustomBotCombat::GetCurrentAmmo(Bot));
					gDebugBot.NextActionTime = T + 0.35;
				}
				else
				{
					gDebugBot.Step = DebugBotState::DroppingWeapon;
				}
			}
			break;
		}

		case DebugBotState::DroppingWeapon:
		{
			LOG_INFO(LogBots, "[DebugBot] Dropping weapon");

			UFortItem* Weapon = CustomBotInventory::FindItemByType(Bot, CustomBotPerception::EItemType::Weapon);

			if (!Weapon)
			{
				DebugBotError(Bot, "no weapon to drop");
				break;
			}

			bool bDropped = CustomBotInventory::DropItem(Bot, Weapon, Weapon->GetItemEntry()->GetCount());
			int Remaining = CustomBotInventory::GetItemCount(Bot, Weapon->GetItemEntry()->GetItemDefinition());
			LOG_INFO(LogBots, "[DebugBot] Weapon dropped={}; remaining in inventory={}", bDropped, Remaining);

			gDebugBot.Step = DebugBotState::WaitingToDisappear;
			gDebugBot.NextActionTime = DebugBotTime() + 10.0;
			LOG_INFO(LogBots, "[DebugBot] Waiting 10 seconds");
			break;
		}

		case DebugBotState::WaitingToDisappear:
		{
			if (T >= gDebugBot.NextActionTime)
				DebugBotRemove(Bot);
			break;
		}

		case DebugBotState::None:
		case DebugBotState::Finished:
		default:
			break;
		}
	}
}

void CustomBotDebug::StartDebugBot(AFortPlayerControllerAthena* ContextPlayer)
{
	if (gDebugBot.Step != DebugBotState::None && gDebugBot.Step != DebugBotState::Finished)
	{
		SendBotMessage(ContextPlayer, L"[DebugBot] A debug bot is already running!");
		LOG_INFO(LogBots, "[DebugBot] Start ignored: sequence already active");
		return;
	}

	AFortPlayerControllerAthena* TargetPlayer = FindFirstValidPlayer();

	if (!TargetPlayer || !TargetPlayer->GetPawn())
	{
		SendBotMessage(ContextPlayer, L"[DebugBot] No valid player found, bot not created!");
		LOG_INFO(LogBots, "[DebugBot] ERROR: no valid player in match, skipping spawn");
		return;
	}

	LOG_INFO(LogBots, "[DebugBot] Found player");

	APawn* TargetPawn = TargetPlayer->GetPawn();
	FVector PlayerLoc = TargetPawn->GetActorLocation();
	FRotator PlayerRot = TargetPawn->GetActorRotation();
	FVector Fwd = TargetPawn->GetActorForwardVector();

	FVector BotSpawn = PlayerLoc + Fwd * 250.0f;

	FTransform SpawnTransform{};
	SpawnTransform.Translation = BotSpawn;
	SpawnTransform.Rotation = PlayerRot.Quaternion();
	SpawnTransform.Scale3D = { 1, 1, 1 };

	CustomBot* DebugBot = CustomBotSpawner::SpawnCustomBot(SpawnTransform);

	if (!DebugBot)
	{
		SendBotMessage(ContextPlayer, L"[DebugBot] Failed to create custom bot!");
		return;
	}

	DebugBot->Pawn->TeleportTo(BotSpawn, PlayerRot);
	LOG_INFO(LogBots, "[DebugBot] Teleported to player ({}, {}, {})", BotSpawn.X, BotSpawn.Y, BotSpawn.Z);

	if (!DebugBotGrantLoadout(*DebugBot))
	{
		DebugBotRemove(*DebugBot);
		SendBotMessage(ContextPlayer, L"[DebugBot] Loadout failed, bot removed!");
		return;
	}

	LOG_INFO(LogBots, "[DebugBot] Spawned");
	SendBotMessage(ContextPlayer, L"[DebugBot] Sequence started!");

	gDebugBot = DebugBotContext{};
	gDebugBot.Step = DebugBotState::Spawned;
	DebugBot->DebugTick = &TickDebugBot;
}

bool CustomBotDebug::HandleCommand(AFortPlayerControllerAthena* PlayerController, const std::vector<std::string>& Arguments, size_t NumArgs)
{
	if (!PlayerController || Arguments.empty())
		return false;

	const std::string& Command = Arguments[0];

	if (Command == "spawncustombot")
	{
		auto Pawn = PlayerController->GetPawn();

		if (!Pawn)
		{
			SendBotMessage(PlayerController, L"No pawn to spawn at!");
			return true;
		}

		FVector Location = Pawn->GetActorLocation();
		Location.Z += 100.0f;

		FTransform SpawnTransform{};
		SpawnTransform.Translation = Location;
		SpawnTransform.Rotation = Pawn->GetActorRotation().Quaternion();
		SpawnTransform.Scale3D = { 1, 1, 1 };

		auto NewBot = CustomBotSpawner::SpawnCustomBot(SpawnTransform);

		SendBotMessage(PlayerController, NewBot ? L"Custom bot spawned!" : L"Failed to spawn custom bot!");
		return true;
	}

	CustomBot* ActiveBot = GetFirstValidBot();

	if (!ActiveBot)
	{
		SendBotMessage(PlayerController, L"No custom bots alive!");
		return true;
	}

	CustomBot& Bot = *ActiveBot;

	if (Command == "cbmove")
	{
		if (NumArgs < 3)
		{
			SendBotMessage(PlayerController, L"Usage: cbmove <x> <y> <z>");
			return true;
		}

		float X, Y, Z;

		if (!TryParseFloat(Arguments[1], X) || !TryParseFloat(Arguments[2], Y) || !TryParseFloat(Arguments[3], Z))
		{
			SendBotMessage(PlayerController, L"Invalid coordinates!");
			return true;
		}

		CustomBotMovement::MoveTo(Bot, FVector{ X, Y, Z });
		SendBotMessage(PlayerController, L"Moved!");
		return true;
	}

	if (Command == "cblookat")
	{
		if (NumArgs < 3)
		{
			SendBotMessage(PlayerController, L"Usage: cblookat <x> <y> <z>");
			return true;
		}

		float X, Y, Z;

		if (!TryParseFloat(Arguments[1], X) || !TryParseFloat(Arguments[2], Y) || !TryParseFloat(Arguments[3], Z))
		{
			SendBotMessage(PlayerController, L"Invalid coordinates!");
			return true;
		}

		CustomBotMovement::LookAt(Bot, FVector{ X, Y, Z });
		SendBotMessage(PlayerController, L"Looking!");
		return true;
	}

	if (Command == "cbscan")
	{
		float Radius = 1200.0f;

		if (NumArgs >= 1)
			TryParseFloat(Arguments[1], Radius);

		auto Pickup = CustomBotPerception::FindNearestPickup(Bot, Radius);
		auto Container = CustomBotPerception::FindNearestUnopenedContainer(Bot, Radius);
		auto Player = CustomBotPerception::FindNearestPlayer(Bot, Radius);

		SendBotMessage(PlayerController, std::format(
			L"cbscan: radius={:.0f} | pickup={:.0f}m | chest={:.0f}m | player={:.0f}m",
			Radius,
			Pickup ? CustomBotPerception::DistanceToActor(Bot, Pickup) : -1.0f,
			Container ? CustomBotPerception::DistanceToActor(Bot, Container) : -1.0f,
			Player ? CustomBotPerception::DistanceToActor(Bot, Player) : -1.0f).c_str());

		return true;
	}

	if (Command == "cbpickup")
	{
		bool bPicked = CustomBotInteraction::PickupNearest(Bot, 500.0f);
		SendBotMessage(PlayerController, bPicked ? L"Pickup collected!" : L"No pickup near!");
		return true;
	}

	if (Command == "cbequip")
	{
		if (NumArgs < 1)
		{
			SendBotMessage(PlayerController, L"Usage: cbequip <WIDPath>");
			return true;
		}

		auto WID = Cast<UFortWorldItemDefinition>(FindObject(Arguments[1], nullptr));

		if (!WID)
		{
			SendBotMessage(PlayerController, L"Invalid WID!");
			return true;
		}

		CustomBotInventory::GiveItem(Bot, WID);

		auto Item = CustomBotInventory::FindItemByDefinition(Bot, WID);

		if (!Item)
		{
			SendBotMessage(PlayerController, L"Item not found in inventory!");
			return true;
		}

		CustomBotInventory::EquipItem(Bot, Item);
		SendBotMessage(PlayerController, L"Equipped!");
		return true;
	}

	if (Command == "cbshoot")
	{
		auto Target = CustomBotPerception::FindNearestPlayer(Bot, 5000.0f);

		if (Target)
			CustomBotCombat::AimAt(Bot, Target->GetActorLocation());

		bool bFired = CustomBotCombat::FireWeapon(Bot);
		SendBotMessage(PlayerController, bFired ? L"Fired!" : L"No ability to fire!");
		return true;
	}

	if (Command == "cbbuild")
	{
		if (NumArgs < 1)
		{
			SendBotMessage(PlayerController, L"Usage: cbbuild <wall|floor|ramp|roof> [ClassPath]");
			return true;
		}

		CustomBotBuilding::EPieceType PieceType;

		if (Arguments[1] == "wall")      PieceType = CustomBotBuilding::EPieceType::Wall;
		else if (Arguments[1] == "floor") PieceType = CustomBotBuilding::EPieceType::Floor;
		else if (Arguments[1] == "ramp")  PieceType = CustomBotBuilding::EPieceType::Ramp;
		else if (Arguments[1] == "roof")  PieceType = CustomBotBuilding::EPieceType::Roof;
		else
		{
			SendBotMessage(PlayerController, L"Unknown piece type!");
			return true;
		}

		UClass* BuildingClass = nullptr;

		if (NumArgs >= 2)
			BuildingClass = LoadObject<UClass>(Arguments[2]);

		if (!BuildingClass)
			BuildingClass = CustomBotBuilding::GetPieceClass(PieceType);

		if (!BuildingClass)
		{
			SendBotMessage(PlayerController, L"No building class available! Provide a ClassPath on this version.");
			return true;
		}

		FVector Forward = Bot.Pawn->GetActorForwardVector();
		FVector BotLocation = Bot.Pawn->GetActorLocation();

		FVector BuildLocation{ BotLocation.X + Forward.X * 300.0f, BotLocation.Y + Forward.Y * 300.0f, BotLocation.Z - 20.0f };

		ABuildingSMActor* NewBuilding = CustomBotBuilding::BuildPiece(Bot, BuildingClass, BuildLocation, Bot.Pawn->GetActorRotation());

		SendBotMessage(PlayerController, NewBuilding ? L"Built!" : L"Build failed!");
		return true;
	}

	if (Command == "cbdestroy")
	{
		static auto BuildingSMActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingSMActor");

		auto Target = CustomBotPerception::FindNearestActorOfClass(Bot, BuildingSMActorClass, 600.0f);
		auto Building = Cast<ABuildingSMActor>(Target);

		if (!Building)
		{
			SendBotMessage(PlayerController, L"No structure near!");
			return true;
		}

		bool bDestroyed = CustomBotDestruction::DestroyTarget(Building);
		SendBotMessage(PlayerController, bDestroyed ? L"Destroyed!" : L"Failed to destroy!");
		return true;
	}

	if (Command == "cbgetmaterials")
	{
		int Count = 500;

		if (NumArgs >= 1)
			TryParseInt(Arguments[1], Count);

		CustomBotResources::GiveResource(Bot, EFortResourceType::Wood, Count);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Stone, Count);
		CustomBotResources::GiveResource(Bot, EFortResourceType::Metal, Count);

		SendBotMessage(PlayerController, std::format(L"Granted {} of each material!", Count).c_str());
		return true;
	}

	if (Command == "cbpath")
	{
		if (NumArgs < 3)
		{
			SendBotMessage(PlayerController, L"Usage: cbpath <x> <y> <z>");
			return true;
		}

		float X, Y, Z;

		if (!TryParseFloat(Arguments[1], X) || !TryParseFloat(Arguments[2], Y) || !TryParseFloat(Arguments[3], Z))
		{
			SendBotMessage(PlayerController, L"Invalid coordinates!");
			return true;
		}

		FVector Target{ X, Y, Z };

		bool bClear = CustomBotPerception::HasLineOfSight(Bot, Target);
		float Height = CustomBotPerception::GetHeightDifference(Bot, Target);

		CBT::EObstacleType ObstacleType;
		AActor* Obstacle = CustomBotPerception::FindNearestObstacle(Bot, 600.0f, ObstacleType);

		const wchar_t* TypeStr =
			ObstacleType == CBT::EObstacleType::OwnStructure   ? L"OWN_BUILD"
			: ObstacleType == CBT::EObstacleType::EnemyStructure ? L"ENEMY_BUILD"
			: ObstacleType == CBT::EObstacleType::WorldObject   ? L"WORLD_OBJECT"
			: ObstacleType == CBT::EObstacleType::Structure     ? L"STRUCTURE"
			: ObstacleType == CBT::EObstacleType::Actor         ? L"ACTOR"
			:                                                        L"NONE";

		SendBotMessage(PlayerController, std::format(
			L"cbpath: los={} height={:+.0f} obstacle={} dist={:.0f}m",
			bClear ? L"CLEAR" : L"BLOCKED",
			Height,
			TypeStr,
			Obstacle ? CustomBotPerception::DistanceToActor(Bot, Obstacle) : -1.0f).c_str());

		if (Obstacle)
			SendBotMessage(PlayerController, ToWide(Obstacle->ClassPrivate->GetName()).c_str());

		return true;
	}

	if (Command == "cbclimb")
	{
		int Ramps = 1;

		if (NumArgs >= 1)
			TryParseInt(Arguments[1], Ramps);

		Ramps = FMath::Clamp(Ramps, 1, 20);

		UClass* RampClass = nullptr;

		if (NumArgs >= 2)
			RampClass = LoadObject<UClass>(Arguments[2]);

		if (!RampClass)
			RampClass = CustomBotBuilding::GetPieceClass(CustomBotBuilding::EPieceType::Ramp);

		if (!RampClass)
		{
			SendBotMessage(PlayerController, L"No ramp class available! Provide a ClassPath on this version.");
			return true;
		}

		const float StepHoriz = 300.0f;
		const float StepZ = 96.0f;

		FVector BotLocation = Bot.Pawn->GetActorLocation();
		FVector Forward = Bot.Pawn->GetActorForwardVector();

		int Built = 0;
		FVector LastPos = BotLocation;

		for (int i = 0; i < Ramps; ++i)
		{
			FVector Pos{ BotLocation.X + Forward.X * StepHoriz * float(i + 1), BotLocation.Y + Forward.Y * StepHoriz * float(i + 1), BotLocation.Z - 20.0f + StepZ * float(i) };

			if (CustomBotBuilding::BuildPiece(Bot, RampClass, Pos, Bot.Pawn->GetActorRotation()))
				++Built;

			LastPos = Pos;
		}

		if (Built > 0)
			CustomBotMovement::MoveTo(Bot, FVector{ LastPos.X, LastPos.Y, LastPos.Z + 20.0f }, 120.0f);

		SendBotMessage(PlayerController, std::format(L"Climb: built {} / {} ramps, moving up!", Built, Ramps).c_str());
		return true;
	}

	if (Command == "cbuse")
	{
		bool bUsed = CustomBotInteraction::UseConsumable(Bot);
		SendBotMessage(PlayerController, bUsed ? L"Consumable used!" : L"No consumable to use!");
		return true;
	}

	return false;
}


extern "C" void TickBotSafeSEH(void (*cb)(void*), void* data);

static void BotTickCallbackImpl(void* data)
{
	CustomBot* Bot = (CustomBot*)data;
	if (!Bot || !Bot->IsValidActor() || !Bot->IsReady())
		return;

	if (Bot->GetLifeState() == CBT::ELifeState::Dead)
		return;

	static int LastMode = -1;
	int Mode = gBotTickMode;
	if (Mode != LastMode)
	{
		LOG_INFO(LogBots, "[leak] [tick-mode] -> {} (0=full 1=noAI 2=noAI/move 3=noAI/move/CMC 4=existence 5=initNoWrites 6=initNoClaim)", Mode);
		LastMode = Mode;
	}

	if (Mode < 4)
	{
		if (Mode <= 2)
			CustomBotMovement::EnsureCMCActive(*Bot, true, true);

		if (Bot->AI)
		{
			EBotState St = Bot->AI->State;
			bool bBusAir = St == EBotState::InBus || St == EBotState::Ejecting
				|| St == EBotState::Gliding || St == EBotState::Landing;
			bool bBoardingEdge = CustomBotAI::IsInAircraftPhase() && St == EBotState::Warmup;

			CustomBotMovement::SetCosmeticPossession(*Bot, !(bBusAir || bBoardingEdge));
		}

		Bot->Tick();

		if (Mode <= 1)
		{
			CustomBotMovement::UpdateMovement(*Bot);

			CustomBotBreak::TickUnstuck(*Bot);

			CustomBotMovement::SyncCosmetic(*Bot);

			if (Bot->bSkinPending && CustomBotSpawner::PendingSkinBudget > 0)
			{
				--CustomBotSpawner::PendingSkinBudget;
				CustomBotMovement::ApplyPendingSkin(*Bot);
			}
		}

		if (Mode <= 0 && Bot->AI)
			CustomBotAI::Tick(*Bot, *Bot->AI);

		return;
	}

	if (Mode == 4)
		return;

	bool bClaim = (Mode == 5);
	CustomBotMovement::EnsureCMCActive(*Bot, false, bClaim);
	Bot->Tick();
}

void TickCustomBotSafe(CustomBot* Bot)
{
	TickBotSafeSEH(BotTickCallbackImpl, Bot);
}