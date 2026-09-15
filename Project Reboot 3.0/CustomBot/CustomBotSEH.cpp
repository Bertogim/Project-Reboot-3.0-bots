typedef void (*BotTickCallback)(void*);

extern "C" void TickBotSafeSEH(BotTickCallback cb, void* data)
{
	__try
	{
		cb(data);
	}
	__except (1 )
	{
	}
}

typedef void (*BotSpawnCallback)(void*);

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
