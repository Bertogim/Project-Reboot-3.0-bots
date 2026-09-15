#pragma once

#include <string>
#include <vector>

class AFortPlayerControllerAthena;
class CustomBot;


namespace CustomBotDebug
{
	void StartDebugBot(AFortPlayerControllerAthena* ContextPlayer);
	bool HandleCommand(AFortPlayerControllerAthena* PlayerController, const std::vector<std::string>& Arguments, size_t NumArgs);
}

void TickCustomBotSafe(CustomBot* Bot);