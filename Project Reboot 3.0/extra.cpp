#include "die.h"

#include "gui.h"

void SetZoneToIndexHook(AFortGameModeAthena* GameModeAthena, int OverridePhaseMaybeIDFK)
{
	static auto ZoneDurationsOffset = Fortnite_Version >= 15.20 && Fortnite_Version < 18 ? 0x258
		: std::floor(Fortnite_Version) >= 18 ? 0x248
		: 0x1F8; // S13-S14

	static auto GameMode_SafeZonePhaseOffset = GameModeAthena->GetOffset("SafeZonePhase");
	auto GameState = Cast<AFortGameStateAthena>(GameModeAthena->GetGameState());

	if (!GameState)
		return SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);

	auto SafeZoneIndicator = GameModeAthena->GetSafeZoneIndicator();

	static auto GameState_SafeZonePhaseOffset = GameState->GetOffset("SafeZonePhase");

	static int NewLateGameSafeZonePhase = 2;
	static int LastZoneRestart = -1;
	if (LastZoneRestart != AmountOfRestarts)
	{
		LastZoneRestart = AmountOfRestarts;
		NewLateGameSafeZonePhase = 2;
		bZoneReversing = false;
	}

	// Aplicar una sola fase inicial, sin saltar inmediatamente a la siguiente.
	if (bPopulationSafeZonePending && !Globals::bLateGame.load())
	{
		bPopulationSafeZonePending = false;
		if (GameMode_SafeZonePhaseOffset == -1 || GameState_SafeZonePhaseOffset == -1)
		{
			LOG_WARN(LogZone, "[population-zone] Phase offsets unavailable; preserving native start.");
			return SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
		}

		const int Target = InitialPopulationSafeZonePhase;
		// Compensacion provisional de V7.20; verificar con la fase real registrada.
		const bool bV720 = Fortnite_Version >= 7.19 && Fortnite_Version < 7.21;
		const int Seed = bV720 && Target > 0 ? Target - 1 : Target;
		GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset) = Seed;
		GameState->Get<int>(GameState_SafeZonePhaseOffset) = Seed;
		SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);

		const int Actual = GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset);
		LOG_INFO(LogZone, "[population-zone] Initial phase: target={}, seed={}, actual={}, game state={}, override={}.",
			Target, Seed, Actual,
			GameState->Get<int>(GameState_SafeZonePhaseOffset), OverridePhaseMaybeIDFK);
		if (Actual != Target)
			LOG_WARN(LogZone, "[population-zone] Native phase differs from target; further verification required.");

		const bool PreserveNativeWait = AmountOfPlayersWhenBusStart >= 11
			&& AmountOfPlayersWhenBusStart <= 20;
		auto Indicator = GameModeAthena->GetSafeZoneIndicator();
		if (Indicator && PreserveNativeWait)
		{
			LOG_INFO(LogZone, "[population-zone] Native wait preserved: participants={}, phase={}, start={}, finish={}.",
				AmountOfPlayersWhenBusStart, Actual,
				Indicator->GetSafeZoneStartShrinkTime(), Indicator->GetSafeZoneFinishShrinkTime());
			Indicator->ForceNetUpdate();
		}
		else if (Indicator && AmountOfPlayersWhenBusStart >= 1 &&
			AmountOfPlayersWhenBusStart <= 5)
		{
			const auto NextCenterOffset = Indicator->GetOffset("NextCenter", false);
			const auto LastCenterOffset = Indicator->GetOffset("LastCenter", false);
			const auto NextRadiusOffset = Indicator->GetOffset("NextRadius", false);
			const auto LastRadiusOffset = Indicator->GetOffset("LastRadius", false);
			static auto SetCircleFn = FindObject<UFunction>(
				L"/Script/FortniteGame.FortSafeZoneIndicator.SetSafeZoneRadiusAndCenter");

			struct FSetCircleParams
			{
				FVector InLocation;
				float InRadius;
			};

			const float Duration = Indicator->GetSafeZoneFinishShrinkTime()
				- Indicator->GetSafeZoneStartShrinkTime();
			const bool FieldsAvailable = NextCenterOffset != -1 && LastCenterOffset != -1
				&& NextRadiusOffset != -1 && LastRadiusOffset != -1;
			const int ParamSize = SetCircleFn ? SetCircleFn->GetPropertiesSize() : 0;
			const bool ParamsCompatible = ParamSize >= sizeof(FVector) + sizeof(float)
				&& ParamSize <= sizeof(FSetCircleParams);

			if (FieldsAvailable && ParamsCompatible && Duration > 0.f)
			{
				const FVector Center = Indicator->Get<FVector>(NextCenterOffset);
				const float Radius = Indicator->Get<float>(NextRadiusOffset);
				if (Radius > 0.f)
				{
					FSetCircleParams Params{};
					Params.InLocation = Center;
					Params.InRadius = Radius;
					Indicator->ProcessEvent(SetCircleFn, &Params);

					// El tick interpola entre dos circulos identicos: no vuelve a abrirse.
					Indicator->Get<FVector>(LastCenterOffset) = Center;
					Indicator->Get<FVector>(NextCenterOffset) = Center;
					Indicator->Get<float>(LastRadiusOffset) = Radius;
					Indicator->Get<float>(NextRadiusOffset) = Radius;

					// No finalizar el temporizador en 0.2 segundos ni avanzar otra fase.
					const float Now = GameState->GetServerWorldTimeSeconds();
					Indicator->GetSafeZoneStartShrinkTime() = Now;
					Indicator->GetSafeZoneFinishShrinkTime() = Now + Duration;
					LOG_INFO(LogZone, "[population-zone] Initial circle already closed: participants={}, phase={}, radius={}, next advance in={} seconds.",
						AmountOfPlayersWhenBusStart, Actual, Radius, Duration);
				}
				else
				{
					LOG_WARN(LogZone, "[population-zone] Invalid destination radius; native geometry preserved.");
				}
			}
			else
			{
				LOG_WARN(LogZone, "[population-zone] Cannot close initial circle safely: fields={}, params={}, duration={}; native geometry preserved.",
					FieldsAvailable, ParamSize, Duration);
			}
			Indicator->ForceNetUpdate();
		}
		else if (Indicator)
		{
			const float Duration = Indicator->GetSafeZoneFinishShrinkTime()
				- Indicator->GetSafeZoneStartShrinkTime();
			if (Duration > 0.f)
			{
				const float Now = GameState->GetServerWorldTimeSeconds();
				Indicator->GetSafeZoneStartShrinkTime() = Now;
				Indicator->GetSafeZoneFinishShrinkTime() = Now + Duration;
				LOG_INFO(LogZone, "[population-zone] Initial shrink: phase={}, duration={} seconds; no forced skip.",
					Actual, Duration);
			}
			else
			{
				LOG_WARN(LogZone, "[population-zone] Invalid shrink duration; native timers preserved.");
			}
			Indicator->ForceNetUpdate();
		}
		else
		{
			LOG_WARN(LogZone, "[population-zone] Initial indicator unavailable; native timers preserved.");
		}
		GameState->ForceNetUpdate();
		return;
	}

	LOG_INFO(LogDev, "NewLateGameSafeZonePhase: {}", NewLateGameSafeZonePhase);

	if (Fortnite_Version < 13)
	{
		if (Globals::bLateGame.load())
		{
			GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset) = NewLateGameSafeZonePhase;
			GameState->Get<int>(GameState_SafeZonePhaseOffset) = NewLateGameSafeZonePhase;
			SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);

			if (NewLateGameSafeZonePhase == EndReverseZonePhase)
			{
				bZoneReversing = false;
			}

			if (NewLateGameSafeZonePhase == 2 || NewLateGameSafeZonePhase == 3)
			{
				if (SafeZoneIndicator)
					SafeZoneIndicator->SkipShrinkSafeZone();
				else
					LOG_WARN(LogZone, "Invalid SafeZoneIndicator!");
			}

			if (NewLateGameSafeZonePhase >= StartReverseZonePhase) // This means instead of going to the 8th phase its gonna go down.
			{
				bZoneReversing = true;
			}

			if (bZoneReversing && bEnableReverseZone) NewLateGameSafeZonePhase--;
			else NewLateGameSafeZonePhase++;

			return;
		}

		return SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
	}

	if (!SafeZoneIndicator)
	{
		LOG_WARN(LogZone, "Invalid SafeZoneIndicator!");
		return SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
	}

	static auto SafeZoneFinishShrinkTimeOffset = SafeZoneIndicator->GetOffset("SafeZoneFinishShrinkTime");
	static auto SafeZoneStartShrinkTimeOffset = SafeZoneIndicator->GetOffset("SafeZoneStartShrinkTime");
	static auto RadiusOffset = SafeZoneIndicator->GetOffset("Radius");

	static auto SafeZonePhaseOffset = GameModeAthena->GetOffset("SafeZonePhase");

	static auto MapInfoOffset = GameState->GetOffset("MapInfo");
	auto MapInfo = GameState->Get<AActor*>(MapInfoOffset);

	if (!MapInfo)
	{
		LOG_WARN(LogZone, "Invalid MapInfo!")
			return SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
	}

	static auto SafeZoneDefinitionOffset = MapInfo->GetOffset("SafeZoneDefinition");
	auto SafeZoneDefinition = MapInfo->GetPtr<__int64>(SafeZoneDefinitionOffset);

	LOG_INFO(LogDev, "SafeZoneDefinitionOffset: 0x{:x}", SafeZoneDefinitionOffset);

	static auto ZoneHoldDurationsOffset = ZoneDurationsOffset - 0x10; // fr

	auto& ZoneDurations = *(TArray<float>*)(__int64(SafeZoneDefinition) + ZoneDurationsOffset);
	auto& ZoneHoldDurations = *(TArray<float>*)(__int64(SafeZoneDefinition) + ZoneHoldDurationsOffset);

	static bool bFilledDurations = false;

	if (!bFilledDurations)
	{
		bFilledDurations = true;

		auto CurrentPlaylist = GameState->GetCurrentPlaylist();
		static auto GameDataOffset = CurrentPlaylist->GetOffset("GameData");
		UCurveTable* FortGameData = CurrentPlaylist ? CurrentPlaylist->Get<TSoftObjectPtr<UCurveTable>>(GameDataOffset).Get() : nullptr;

		if (!FortGameData)
			FortGameData = FindObject<UCurveTable>(L"/Game/Balance/AthenaGameData.AthenaGameData");

		if (!FortGameData)
		{
			LOG_ERROR(LogZone, "Unable to get FortGameData.");
			return SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
		}

		auto ShrinkTimeFName = UKismetStringLibrary::Conv_StringToName(L"Default.SafeZone.ShrinkTime");
		auto HoldTimeFName = UKismetStringLibrary::Conv_StringToName(L"Default.SafeZone.WaitTime");

		for (int i = 0; i < ZoneDurations.Num(); i++)
		{
			ZoneDurations.at(i) = FortGameData->GetValueOfKey(FortGameData->GetKey(ShrinkTimeFName, i));
		}
		for (int i = 0; i < ZoneHoldDurations.Num(); i++)
		{
			ZoneHoldDurations.at(i) = FortGameData->GetValueOfKey(FortGameData->GetKey(HoldTimeFName, i));
		}
	}

	LOG_INFO(LogZone, "SafeZonePhase: {}", GameModeAthena->Get<int>(SafeZonePhaseOffset));
	LOG_INFO(LogZone, "OverridePhaseMaybeIDFK: {}", OverridePhaseMaybeIDFK);
	LOG_INFO(LogZone, "TimeSeconds: {}", UGameplayStatics::GetTimeSeconds(GetWorld()));

	if (Globals::bLateGame.load())
	{
		GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset) = NewLateGameSafeZonePhase;
		GameState->Get<int>(GameState_SafeZonePhaseOffset) = NewLateGameSafeZonePhase;
		SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);

		if (NewLateGameSafeZonePhase == EndReverseZonePhase)
		{
			bZoneReversing = false;
		}

		if (NewLateGameSafeZonePhase >= StartReverseZonePhase) // This means instead of going to the 8th phase its gonna go down.
		{
			bZoneReversing = true;
		}

		if (bZoneReversing && bEnableReverseZone) NewLateGameSafeZonePhase--;
		else NewLateGameSafeZonePhase++;
	}
	else
	{
		SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
	}

	LOG_INFO(LogZone, "SafeZonePhase After: {}", GameModeAthena->Get<int>(SafeZonePhaseOffset));

	float ZoneHoldDuration = 0;

	if (GameModeAthena->Get<int>(SafeZonePhaseOffset) >= 0 && GameModeAthena->Get<int>(SafeZonePhaseOffset) < ZoneHoldDurations.Num())
		ZoneHoldDuration = ZoneHoldDurations.at(GameModeAthena->Get<int>(SafeZonePhaseOffset));

	SafeZoneIndicator->Get<float>(SafeZoneStartShrinkTimeOffset) = GameState->GetServerWorldTimeSeconds() + ZoneHoldDuration;

	float ZoneDuration = 0;

	if (GameModeAthena->Get<int>(SafeZonePhaseOffset) >= 0 && GameModeAthena->Get<int>(SafeZonePhaseOffset) < ZoneDurations.Num())
		ZoneDuration = ZoneDurations.at(GameModeAthena->Get<int>(SafeZonePhaseOffset));

	LOG_INFO(LogZone, "ZoneDuration: {}", ZoneDuration);
	LOG_INFO(LogZone, "Duration: {}", SafeZoneIndicator->Get<float>(RadiusOffset));

	SafeZoneIndicator->Get<float>(SafeZoneFinishShrinkTimeOffset) = SafeZoneIndicator->Get<float>(SafeZoneStartShrinkTimeOffset) + ZoneDuration;

	if (NewLateGameSafeZonePhase == 3 || NewLateGameSafeZonePhase == 4)
	{
		if (SafeZoneIndicator)
			SafeZoneIndicator->SkipShrinkSafeZone();
		else
			LOG_WARN(LogZone, "Invalid SafeZoneIndicator!");
	}
}
