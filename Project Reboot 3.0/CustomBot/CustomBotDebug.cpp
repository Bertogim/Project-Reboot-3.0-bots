#include "CustomBot/CustomBotDebug.h"

#include "CustomBot.h"

#include "CustomBotSpawner.h"
#include "CustomBotMovement.h"
#include "CustomBotPerception.h"
#include "CustomBotInventory.h"
#include "CustomBotResources.h"
#include "CustomBotCombat.h"
#include "CustomBotBuilding.h"
#include "CustomBotDestruction.h"
#include "CustomBotInteraction.h"

#include "FortItem.h"
#include "BuildingSMActor.h"
#include "GameplayStatics.h"

#include <format>
#include <string>

namespace
{
	// Envia un mensaje al chat del jugador (misma tecnica que SendMessageToConsole
	// de commands.h, pero local para no arrastrar el sistema antiguo de bots).
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

	// Convierte el nombre de clase (std::string, UTF-8/ASCII) a wchar para SendBotMessage.
	std::wstring ToWide(const std::string& Text)
	{
		if (Text.empty())
			return L"";

		std::wstring Out(Text.size(), L' ');

		for (size_t i = 0; i < Text.size(); ++i)
			Out[i] = (wchar_t)(unsigned char)Text[i];

		return Out;
	}

	// Bot objetivo de los comandos (el primer bot custom valido).
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

// Devuelve true si Arguments[0] es un comando de CustomBot y se ejecuto.
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

	// El resto de comandos requieren un bot existente.
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

		// Seleccionar la pieza (build mode, igual que un jugador).
		CustomBotBuilding::SelectPiece(Bot, PieceType);

		// Posicionar la estructura justo al frente del bot.
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

		// Seleccionar la pieza (build mode) y construir N rampas hacia arriba.
		CustomBotBuilding::SelectPiece(Bot, CustomBotBuilding::EPieceType::Ramp);

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