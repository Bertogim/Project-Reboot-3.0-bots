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
	// Inicia la secuencia de prueba "debugbot" (un solo debugbot activo a la vez).
	// Busca el primer jugador valido y spawnea junto a el. ContextPlayer recibe los
	// mensajes de chat (puede ser nullptr). Llamado desde la pestaña "Fun" de la GUI.
	void StartDebugBot(AFortPlayerControllerAthena* ContextPlayer);

	// Devuelve true si Arguments[0] es un comando de CustomBot y se ejecuto.
	// Arguments[0] es el comando; Args empiezan en Arguments[1]. NumArgs = nº de args.
	bool HandleCommand(AFortPlayerControllerAthena* PlayerController, const std::vector<std::string>& Arguments, size_t NumArgs);
}