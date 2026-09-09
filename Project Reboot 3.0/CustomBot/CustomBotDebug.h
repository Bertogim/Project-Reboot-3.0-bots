#pragma once

#include <string>
#include <vector>

class AFortPlayerControllerAthena;
class CustomBot;

// CustomBot - Comandos de depuracion.

namespace CustomBotDebug
{
	void StartDebugBot(AFortPlayerControllerAthena* ContextPlayer);
	bool HandleCommand(AFortPlayerControllerAthena* PlayerController, const std::vector<std::string>& Arguments, size_t NumArgs);
}

// SEH-safe bot tick: __try/__except en funcion sin destructores C++.
// Definida en CustomBotDebug.cpp.
void TickCustomBotSafe(CustomBot* Bot);