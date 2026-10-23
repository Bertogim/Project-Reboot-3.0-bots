#include "die.h"

#include "gui.h"

// Politica comun al inicio, hot swap y progresion.
static float PopulationWaitDuration(int Participants)
{
    // 1-5: 3 min; 6-10: 4 min; 11-20: 5 min;
    // 21-49: 6 min; 50+: 8 min antes de cada cierre.
    return Participants <= 5 ? 180.f : Participants <= 10 ? 240.f
        : Participants <= 20 ? 300.f : Participants <= 49 ? 360.f : 480.f;
}

static float PopulationShrinkDuration(int Participants, float Native)
{
    const float Maximum = Participants <= 5 ? 60.f
        : Participants <= 10 ? 90.f : Participants <= 20 ? 120.f
        : Participants <= 49 ? 150.f : 180.f;
    // Duracion efectiva, no un limite que el tiempo nativo pueda acortar.
    (void)Native;
    return Maximum;
}

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
	static int LastPopulationPhase = -1;
	if (LastZoneRestart != AmountOfRestarts)
	{
		LastZoneRestart = AmountOfRestarts;
		LastPopulationPhase = -1;
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
		LastPopulationPhase = Actual;
		LOG_INFO(LogZone, "[population-zone] Initial phase: target={}, seed={}, actual={}, game state={}, override={}.",
			Target, Seed, Actual,
			GameState->Get<int>(GameState_SafeZonePhaseOffset), OverridePhaseMaybeIDFK);
		if (Actual != Target)
			LOG_WARN(LogZone, "[population-zone] Native phase differs from target; further verification required.");

        auto Indicator = GameModeAthena->GetSafeZoneIndicator();
        if (Indicator &&
            Indicator->GetOffset("SafeZoneStartShrinkTime", false) >= 0 &&
            Indicator->GetOffset("SafeZoneFinishShrinkTime", false) >= 0)
        {
            const int Participants = GetZoneModeParticipants(AmountOfPlayersWhenBusStart);
            const float NativeDuration = Indicator->GetSafeZoneFinishShrinkTime()
                - Indicator->GetSafeZoneStartShrinkTime();
            const float Wait = PopulationWaitDuration(Participants);
            const float Duration = PopulationShrinkDuration(Participants, NativeDuration);
            if (std::isfinite(Duration) && Duration > 0.f)
            {
                const float Now = GameState->GetServerWorldTimeSeconds();
                // El motor conserva e interpola la geometria nativa.
                // Todos alcanzan el mismo radio inicial en un segundo.
                // La espera y el cierre posteriores escalan con la poblacion.
                const bool FastInitial = Participants > 0
                    && Actual == Target;
                const float InitialWait = FastInitial ? 0.f : Wait;
                const float InitialDuration = FastInitial ? 1.f : Duration;
                Indicator->GetSafeZoneStartShrinkTime() = Now + InitialWait;
                Indicator->GetSafeZoneFinishShrinkTime() = Now + InitialWait + InitialDuration;
                LOG_INFO(LogZone, "[population-zone] Initial transition: phase={} wait={} shrink={} nextPhaseWait={}",
                    Actual, InitialWait, InitialDuration, Wait);
                Indicator->ForceNetUpdate();
                LOG_INFO(LogZone, "[population-zone] Initial storm schedule: participants={}, phase={}, wait={} seconds, shrink={} seconds.",
                    Participants, Actual, InitialWait, InitialDuration);
            }
            else
                LOG_WARN(LogZone, "[population-zone] Invalid duration; native timers preserved.");
        }
        else
            LOG_WARN(LogZone, "[population-zone] Initial storm timer fields unavailable.");
		GameState->ForceNetUpdate();
		PopulationStormGuard::Capture(GameModeAthena);
		return;
	}

	// La inicializacion nativa tardia no debe reiniciar una tormenta adelantada.
	if (InitialPopulationSafeZonePhase > 0 && !Globals::bLateGame.load()
		&& LastPopulationPhase >= 0 && GameMode_SafeZonePhaseOffset != -1
		&& GameState_SafeZonePhaseOffset != -1)
	{
		const int Current = GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset);
		const float Now = GameState->GetServerWorldTimeSeconds();
		const bool Premature = PopulationStormGuard::Ready
			&& Now + 0.1f < PopulationStormGuard::Finish;
		if (Current < LastPopulationPhase || Premature)
		{
			GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset) = LastPopulationPhase;
			GameState->Get<int>(GameState_SafeZonePhaseOffset) = LastPopulationPhase;
			LOG_WARN(LogZone, "[population-zone] Ignored native reset: current={}, override={}, active={}.",
				Current, OverridePhaseMaybeIDFK, LastPopulationPhase);
			PopulationStormGuard::Restore(GameModeAthena);
			GameState->ForceNetUpdate();
			return;
		}
        const bool LeavingInitialPhaseOne =
            InitialPopulationSafeZonePhase == 1 && LastPopulationPhase == 1;
        SetZoneToIndexOriginal(GameModeAthena, OverridePhaseMaybeIDFK);
        LastPopulationPhase = GameModeAthena->Get<int>(GameMode_SafeZonePhaseOffset);
        auto ProgressIndicator = GameModeAthena->GetSafeZoneIndicator();
        // Use the selected band, or the starting population in automatic mode.
        // Eliminations must not switch the configured timing mid-match.
        const int Participants = GetZoneModeParticipants(AmountOfPlayersWhenBusStart);
        if (ProgressIndicator && Participants > 0 &&
            ProgressIndicator->GetOffset("SafeZoneStartShrinkTime", false) >= 0 &&
            ProgressIndicator->GetOffset("SafeZoneFinishShrinkTime", false) >= 0)
        {
            // Solo 6-10: tras llegar a fase 1 en un segundo,
            // cerrar inmediatamente hacia fase 2 durante cinco minutos y medio.
            const bool ImmediateFirstClose = Participants >= 6 && Participants <= 10
                && LeavingInitialPhaseOne && LastPopulationPhase == 2;
            float Wait = ImmediateFirstClose ? 0.f : PopulationWaitDuration(Participants);
            float Duration = ImmediateFirstClose ? 330.f : PopulationShrinkDuration(Participants,
                ProgressIndicator->GetSafeZoneFinishShrinkTime()
                - ProgressIndicator->GetSafeZoneStartShrinkTime());
            // Desde fase 3, reducir espera y cierre por fase de destino.
            // Calcular desde la base evita acumular divisiones en llamadas
            // repetidas y mantiene el resultado si el motor salta una fase.
            // 1-10 comparte tiempos desde fase 3; 50+ conserva el recorrido nativo.
            if (Participants >= 1 && Participants < 50 && LastPopulationPhase >= 3)
            {
                const float Divisor = Participants <= 10 ? 2.f
                    : Participants <= 20 ? 1.5f : 1.25f;
                const float Scale = std::pow(Divisor,
                    -static_cast<float>(LastPopulationPhase - 2));
                Wait = PopulationWaitDuration(Participants) * Scale;
                const float BaseShrink = Participants <= 10 ? 330.f
                    : PopulationShrinkDuration(Participants, 0.f);
                Duration = BaseShrink * Scale;
                // 1-10: fase 3 = 90 s de espera + 60 s de cierre.
                // Cada fase posterior divide ambos tiempos entre dos.
                // El cierre inicial hacia fase 2 conserva sus 330 s.
                if (Participants <= 10)
                {
                    const float PhaseThreeScale = std::pow(2.f,
                        -static_cast<float>(LastPopulationPhase - 3));
                    Wait = 90.f * PhaseThreeScale;
                    Duration = 60.f * PhaseThreeScale;
                }
            }
            if (std::isfinite(Duration) && Duration > 0.f)
            {
                const float Now = GameState->GetServerWorldTimeSeconds();
                ProgressIndicator->GetSafeZoneStartShrinkTime() = Now + Wait;
                ProgressIndicator->GetSafeZoneFinishShrinkTime() = Now + Wait + Duration;
                ProgressIndicator->ForceNetUpdate();
                LOG_INFO(LogZone, "[population-zone] Progression storm schedule: participants={}, phase={}, wait={} seconds, shrink={} seconds.",
                    Participants, LastPopulationPhase, Wait, Duration);
            }
            else
                LOG_WARN(LogZone, "[population-zone] Invalid progression shrink duration; native timers preserved.");
        }
        PopulationStormGuard::Capture(GameModeAthena);
		LOG_INFO(LogZone, "[population-zone] Native progression: phase={}.", LastPopulationPhase);
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

	if (Globals::bLateGame.load() && (NewLateGameSafeZonePhase == 3 || NewLateGameSafeZonePhase == 4))
	{
		if (SafeZoneIndicator)
			SafeZoneIndicator->SkipShrinkSafeZone();
		else
			LOG_WARN(LogZone, "Invalid SafeZoneIndicator!");
	}
}

// Consumed only by the server game-thread tick.
void TickHostZoneMode(AFortGameModeAthena* Mode)
{
    static int Pending = -2;
    static int Generation = -1;
    static bool StartRequested = false;
    static auto Deadline = std::chrono::steady_clock::time_point{};
    if (Generation != AmountOfRestarts)
    {
        Generation = AmountOfRestarts;
        Pending = -2;
        StartRequested = false;
    }
    const int Requested = HostZoneModeRequest.exchange(-2);
    if (Requested != -2)
    {
        if (Requested != -1 && Requested != 4 && Requested != 10 &&
            Requested != 20 && Requested != 49 && Requested != 100)
            return;
        Pending = Requested;
        StartRequested = false;
        Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    }
    if (Pending == -2) return;
    auto State = Mode ? Mode->GetGameStateAthena() : nullptr;
    if (!State || Globals::bCreative || Globals::bGoingToPlayEvent ||
        Globals::bLateGame.load() || State->GetGamePhase() >= EAthenaGamePhase::EndGame)
    {
        Pending = -2;
        LOG_WARN(LogZone, "[zone-mode] Request rejected: unavailable in this game state.");
        return;
    }
    const bool Live = State->GetGamePhase() > EAthenaGamePhase::Warmup;
    if (Live)
    {
        if (!SetZoneToIndexOriginal ||
            Mode->GetOffset("SafeZonePhase", false) == -1 ||
            State->GetOffset("SafeZonePhase", false) == -1)
        {
            Pending = -2;
            LOG_WARN(LogZone, "[zone-mode] Required native phase API unavailable.");
            return;
        }
        // Clear the native veto before starting or switching a storm.
        const int SkipOffset = Mode->GetOffset("bSkipSafeZones", false);
        auto SkipProperty = SkipOffset >= 0
            ? Mode->GetProperty("bSkipSafeZones", false) : nullptr;
        if (SkipProperty)
            Mode->SetBitfieldValue(SkipOffset, GetFieldMask(SkipProperty), false);
        const int PhaseOffset = Mode->GetOffset("SafeZonePhase", false);
        const int ActiveOffset = Mode->GetOffset("bSafeZoneActive", false);
        auto ActiveProperty = ActiveOffset >= 0
            ? Mode->GetProperty("bSafeZoneActive", false) : nullptr;
        const bool Inactive = ActiveProperty &&
            !Mode->ReadBitfieldValue(ActiveOffset, GetFieldMask(ActiveProperty));
        const bool NeedsStart = !Mode->GetSafeZoneIndicator() ||
            (PhaseOffset >= 0 && Mode->Get<int>(PhaseOffset) <= 0) || Inactive;
        if (NeedsStart && !StartRequested)
        {
            StartRequested = true;
            PopulationStormGuard::Ready = false;
            InitialPopulationSafeZonePhase = 0;
            bPopulationSafeZonePending = false;
            LOG_INFO(LogZone, "[zone-mode] Activating storm from phase zero/inactive state.");
            UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), L"startsafezone", nullptr);
            // Allow native initialization to finish before applying the band.
            return;
        }
        if (!Mode->GetSafeZoneIndicator())
        {
            if (std::chrono::steady_clock::now() >= Deadline)
            {
                Pending = -2;
                LOG_WARN(LogZone, "[zone-mode] Timed out waiting for storm indicator.");
                return;
            }
            if (!StartRequested)
            {
                StartRequested = true;
                LOG_INFO(LogZone, "[zone-mode] Starting storm; waiting for indicator.");
                UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), L"startsafezone", nullptr);
            }
            return;
        }
    }

    const int Selected = Pending;
    Pending = -2;
    HostZoneModeParticipants.store(Selected);
    const int Actual = State->GetPlayersLeft();
    const int Effective = GetZoneModeParticipants(Actual);
    LOG_INFO(LogZone, "[zone-mode] Selected band={} actualPlayers={} effectivePlayers={}",
        Selected, Actual, Effective);
    if (!Live) return;
    InitialPopulationSafeZonePhase = GetPopulationSafeZonePhase(Effective);
    if (InitialPopulationSafeZonePhase <= 0) return;
    bPopulationSafeZonePending = true;
    PopulationStormGuard::Ready = false;
    SetZoneToIndexHook(Mode, 0);
}

// Server-side goal based on individual KillScore, grouped by actual team.
// Do not sum TeamKillScore: it may already contain the whole team's total.
void TickHostRumbleGoal(AFortGameModeAthena* Mode)
{
    // Diagnose the actual loaded version, not the old on-disk object dump.
    static int DiagnosticGeneration = -1;
    static int DiagnosticGoal = -1;
    if (Mode && Globals::bInitializedPlaylist &&
        IsHostTeamRumblePlaylist(PlaylistName) &&
        (DiagnosticGeneration != AmountOfRestarts ||
         DiagnosticGoal != HostRumbleKillGoal.load()))
    {
        DiagnosticGeneration = AmountOfRestarts;
        DiagnosticGoal = HostRumbleKillGoal.load();
        LOG_INFO(LogDev, "[rumble-schema] version={} goal={} playlist={}",
            Fortnite_Version, DiagnosticGoal, PlaylistName);
        auto Inspect = [](UObject* Object, const char* Label)
        {
            if (!Object) return;
            LOG_INFO(LogDev, "[rumble-schema] object={} path={}", Label, Object->GetFullName());
            int ClassCount = 0;
            int Hits = 0;
            for (auto Class = Object->ClassPrivate; Class && ClassCount++ < 32;
                Class = *(UClass**)(__int64(Class) + Offsets::SuperStruct))
            {
                auto Property = *(void**)(__int64(Class) + Offsets::Children);
                int PropertyCount = 0;
                while (Property && PropertyCount++ < 2048)
                {
                    const std::string Name = GetFNameOfProp(Property)->ToString();
                    if (Name.find("Score") != std::string::npos ||
                        Name.find("Goal") != std::string::npos ||
                        Name.find("Kill") != std::string::npos ||
                        Name.find("Target") != std::string::npos ||
                        Name.find("WinCondition") != std::string::npos ||
                        Name.find("Victory") != std::string::npos)
                    {
                        LOG_INFO(LogDev, "[rumble-schema] object={} field={}", Label, Name);
                        if (++Hits >= 100) return;
                    }
                    Property = GetNext(Property);
                }
            }
        };
        Inspect(Mode, "GameMode");
        auto DiagnosticState = Mode->GetGameStateAthena();
        Inspect(DiagnosticState, "GameState");
        if (DiagnosticState)
            Inspect(DiagnosticState->GetCurrentPlaylist(), "Playlist");
        LoopMutators([&](AFortAthenaMutator* Mutator)
        {
            Inspect(Mutator, "Mutator");
        });
    }

    static int Generation = -1;
    static bool Finished = false;
    static auto LastCheck = std::chrono::steady_clock::time_point{};
    if (Generation != AmountOfRestarts)
    {
        Generation = AmountOfRestarts;
        Finished = false;
        LastCheck = {};
        HostRumbleLeadingKills.store(0);
    }
    const int Goal = HostRumbleKillGoal.load();
    if (!Mode || Finished || Goal <= 0 || !IsHostTeamRumblePlaylist(PlaylistName))
        return;
    auto State = Mode->GetGameStateAthena();
    if (!State || State->GetGamePhase() <= EAthenaGamePhase::Warmup ||
        State->GetGamePhase() >= EAthenaGamePhase::EndGame)
        return;
    const auto Now = std::chrono::steady_clock::now();
    if (Now - LastCheck < std::chrono::milliseconds(500)) return;
    LastCheck = Now;
    const int PlayersOffset = State->GetOffset("PlayerArray", false);
    if (PlayersOffset < 0) return;
    auto& Players = State->Get<TArray<APlayerState*>>(PlayersOffset);
    if (Players.Num() < 0 || Players.Num() > 1024 ||
        (Players.Num() > 0 && !Players.Data)) return;

    std::map<int, int> Scores;
    for (int i = 0; i < Players.Num(); ++i)
    {
        auto PS = Cast<AFortPlayerStateAthena>(Players.at(i));
        if (!PS) continue;
        const int Offset = PS->GetOffset("KillScore", false);
        if (Offset < 0) continue;
        const int Kills = PS->Get<int>(Offset);
        const int Team = PS->GetTeamIndex();
        if (Team >= 3 && Team <= 102 && Kills >= 0 && Kills <= 100000)
            Scores[Team] += Kills;
    }
    int Leading = 0;
    int Winner = -1;
    bool Tied = false;
    for (const auto& Pair : Scores)
    {
        if (Pair.second > Leading)
        {
            Leading = Pair.second;
            Winner = Pair.first;
            Tied = false;
        }
        else if (Pair.second == Leading)
            Tied = true;
    }
    HostRumbleLeadingKills.store(Leading);
    if (Leading < Goal || Winner < 3 || Tied) return;

    auto End = FindObject<UFunction>("/Script/Engine.GameMode.EndMatch");
    const int WinnerOffset = State->GetOffset("WinningTeam", false);
    if (!End || End->GetPropertiesSize() != 0 || WinnerOffset < 0)
    {
        LOG_WARN(LogDev, "[rumble-goal] Cannot finish safely: native API unavailable.");
        return;
    }
    Finished = true;
    State->Get<int>(WinnerOffset) = Winner;
    State->ForceNetUpdate();
    Mode->ProcessEvent(End);
    LOG_INFO(LogDev, "[rumble-goal] Team {} reached {} kills; goal={}. Native EndMatch requested.",
        Winner, Leading, Goal);
}
