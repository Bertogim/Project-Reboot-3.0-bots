#pragma once

#include <string>
#include <vector>

class AFortPlayerControllerAthena;

// CustomBot - Comandos de depuracion.
//
// Commandos de consola (cheat) para probar las capacidades del bot "Season 3".
// Se invocan desde ServerCheatHook (commands.cpp). Cada comando devuelve true
// si fue manejado por este sistema.

namespace CustomBotDebug
{
	// Devuelve true si Arguments[0] es un comando de CustomBot y se ejecuto.
	// Arguments[0] es el comando; Args empiezan en Arguments[1]. NumArgs = nº de args.
	bool HandleCommand(AFortPlayerControllerAthena* PlayerController, const std::vector<std::string>& Arguments, size_t NumArgs);
}