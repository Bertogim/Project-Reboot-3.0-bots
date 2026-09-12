#pragma once

#include "reboot.h"

// TODO-PATH: toggle global de pathfinding (navmesh) para los bots custom.
// Lo gestiona el checkbox "Pathfinding de bots (navmesh)" en gui.h (pestaña Bots).
// true  = los bots consultan el navmesh del mundo y siguen la ruta de
//         waypoints (cuando el mapa lo tiene); si no hay ruta, caen al
//         fallback lineal. false = siempre linea recta + desatascado fisico
//         (retroceder/saltar/romper). Por defecto OFF (linea recta + desatasco,
//         mas ligero y fiable); se puede activar en la UI, pestaña Bots.
inline bool bCustomBotPathfinding = false;

// CustomBotTypes - Tipos compartidos entre los modulos del sistema de bot "Season 3".
// Parte 1: SOLO cuerpo/capacidades (sin decisiones de IA). Estos tipos estan
// diseñados para que los modulos de movimiento/percepcion/inventario/combate/etc.
// operen sobre un CustomBot.
//
// NOTA: el namespace se llama CBT (Custom Bot Types) para no colisionar con la
// clase CustomBot (que vive en el scope global).
namespace CBT
{
	// Resultado generico de cualquier operacion de capacidad del bot.
	enum class EResult : uint8_t
	{
		Success,      // la operacion se realizo correctamente
		Failure,      // la operacion fallo de forma generica
		NotReady,     // el bot/pawn/controller/inventario no esta listo
		NotFound,     // el target/objeto/item no se encontro
		OutOfRange,   // fuera de rango (distancia o altura)
		Blocked,      // el camino/bloqueo impide la accion
		NoResources,  // faltan materiales/municion/recursos
		Cooldown,     // en cooldown o ya en ejecucion
	};

	// Estado del movimiento del bot en un tick dado.
	enum class EMovementState : uint8_t
	{
		Idle,        // no se mueve
		Moving,      // en movimiento hacia un objetivo
		BlockedPath, // hay un obstaculo/colina que impide avanzar
		Arrived,     // alcanzo la posicion objetivo
	};

	// Estado de vida del pawn (vivo, DBNO, muerto).
	enum class ELifeState : uint8_t
	{
		Alive,
		Downed,   // DBNO (knocked)
		Dead,
	};

	// Tipo de superficie/obstaculo detectado frente al bot.
	enum class EObstacleType : uint8_t
	{
		None,
		EnemyStructure,  // estructura construible de otro equipo
		OwnStructure,    // estructura construible propia
		WorldObject,     // objeto del mundo (arbol, roca, etc.) -> cosechable
		Structure,       // estructura generica
		LargeDip,        // gran diferencia de altura hacia abajo
		LargeRise,       // gran diferencia de altura hacia arriba (pendiente)
		Actor,           // otro actor (pickup, container, jugador...)
	};

	// Descriptor de posicion hacia la que el bot quiere moverse.
	struct FMoveRequest
	{
		FVector Destination{};     // posicion objetivo
		FVector Start{};           // posicion desde donde se origino (opcional)
		float AcceptanceRadius = 100.0f; // distancia para considerarlo "llegado"
		float MoveSpeed = 600.0f;  // velocidad de re-aplicacion (Walk/Sprint original)
		bool bStopOnArrival = true;
	};

	// Resultado de un barrido de percepcion.
	struct FScanResult
	{
		TArray<AActor*> Actors;    // actores detectados dentro del radio
		float Radius = 0.0f;       // radio usado para el barrido

		~FScanResult()
		{
			Actors.FreeEngine();
		}
	};
}
