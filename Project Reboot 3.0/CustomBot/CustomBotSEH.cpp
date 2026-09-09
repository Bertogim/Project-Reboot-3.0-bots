// Pure C SEH wrapper — no C++ headers, no destructors.
// MSVC C2712: __try forbidden in TUs that require object unwinding.
// By keeping this file free of C++ includes, it compiles with /EHsc fine.

typedef void (*BotTickCallback)(void*);

extern "C" void TickBotSafeSEH(BotTickCallback cb, void* data)
{
	__try
	{
		cb(data);
	}
	__except (1 /*EXCEPTION_EXECUTE_HANDLER*/)
	{
		// Silently skip — the caller already checked IsValidActor/IsReady.
		// Logging here would risk pulling in C++ headers.
	}
}

typedef void (*BotSpawnCallback)(void*);

// SEH envelope for the spawn path: returns 1 if the callback returned, 0 if it
// crashed. The C++ side (CustomBotSpawner) cleans up the half-built bot and
// logs which stage crashed, so a single broken spawn no longer kills the game.
extern "C" int SpawnBotSafeSEH(BotSpawnCallback cb, void* data)
{
	__try
	{
		cb(data);
		return 1;
	}
	__except (1)
	{
		return 0;
	}
}
