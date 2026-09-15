# 08 - Diagnostico del congelamiento del DebugBot + FIX (RUNPHYS)

Documento el problema mas buscado del DebugBot (el bot spawn pero NUNCA integraba fisica:
ni caminar, ni gravedad) y la solucion que finalmente lo desbloqueo (23:03, run con la
compilacion 23:00). ESCRIBIR ESTE ARCHIVO ANTES DE CUALQUIER COMPACTION: es la pieza que
faltaba para completar la secuencia de la demo.

---

## 1. Sintoma

- El DebugBot spawn correctamente en la posicion del jugador (fisica configurada igual que
  un pawn real) pero queda CONGELADO: `pos` clavada a pesar de `Velocity=(600,0,-500)` y
  `Acceleration=(600,0,0)` escritas cada frame por `MoveTo`.
- `MovementMode` forzado a 1 (Walking) cada frame; a veces el juego lo revierte a 3
  (Falling) ~31 veces/segundo (`CLONE-EXPERIMENT: watchdog reverted mode->3`).
- Ocurría en **todas** las fases (warmup hub, InProgress post-bus) con bot poseído o no.

## 2. Mecanismo raiz (por que nadie lo simulaba)

En un servidor dedicado de UE, un `Avatar`/pawn poseído por un `APlayerController` se
mueve **exclusivamente** por los `ServerMove` que su cliente envia. El CMC del servidor
`NO` integra su propia `PerformMovement` para ese pawn mientras tenga `Controller`.
El pawn del bot tenia un `FortPlayerControllerAthena` (poseído) pero **cero clientes
conectados** a ese controller → cae en tierra de nadie:

- Rama "cliente real" (ServerMove/MoveAutonomous): nunca llega (no hay conexion,
  `netConn=0`).
- Rama "sin controller" del CMC: no entra porque `Controller != nullptr`.

Por eso todo enfoque de "hacer el pawn igual a un jugador real" fallaba: UVel/CM
identicos, `ServerAcknowledgePossession` (ackPawn=1), tick forzado, DORM_Never... nada
cambiaba la CONDICION de simulacion, solo el estado.

## 3. El FIX (TRES pasos) — aplicado SIEMPRE en el spawn de todo bot

Ya NO es un experimento atado al T+3.0/jump de la secuencia debug: se aplica a TODOS
los CustomBots al terminar `CustomBotSpawner::SpawnCustomBot` (el gate de
`MovingForward` en `TickDebugBot` se elimino). El bot queda simulado desde el momento
en que existe.

Nuevo helper `CustomBotMovement::EnableServerSimulation(CustomBot&)` en
`CustomBotMovement.h`:

1. `Bot.PlayerState->SetIsBot(false)` — quita el flag `bIsABot` del PlayerState.
2. `Bot.Controller->UnPossess()` — `Controller == nullptr`, entra en la rama
   "sin controller" del CMC.
3. `CMR->SetBitfieldValue(Off, GetFieldMask(Prop), true)` sobre
   `bRunPhysicsWithNoController` — sin este bit la rama "sin controller" esta inerte
   (por eso el experimento UNPOSSESS anterior, que SOLO soltaba el pose, no movio nada).

Codigo (exacto):

```cpp
// CustomBotMovement.h
static bool EnableServerSimulation(CustomBot& Bot)
{
    if (!Bot.PlayerState || !Bot.Controller || !Bot.Pawn)
        return false;

    Bot.PlayerState->SetIsBot(false);
    Bot.Controller->UnPossess();

    bool bBitOK = false;
    if (auto* CMR = GetCharacterMovement(Bot))
    {
        auto* Prop = CMR->GetProperty("bRunPhysicsWithNoController");
        int Off = CMR->GetOffset("bRunPhysicsWithNoController", false);
        if (Prop && Off != -1)
        {
            CMR->SetBitfieldValue(Off, GetFieldMask(Prop), true);
            bBitOK = true;
        }
    }
    LOG_WARN(LogBots, "[CustomBot] RUNPHYS-FIX: IsBot=false, pose released, bRunPhysicsWithNoController set={} (possessor={})",
        bBitOK, Bot.Pawn->GetController() != nullptr);
    return bBitOK;
}

// CustomBotSpawner.h, final de SpawnCustomBot (todos los bots)
CustomBotMovement::EnableServerSimulation(Bot);
LOG_INFO(LogBots, "[CustomBot] enableServerSimulation done");
```

Nota: la rama "sin controller" del CMC es la MISMA que usan engendros/AI de UE a los que
el motor SI simula en servidor. No hace falta controller para las primitivas de la demo:
`MoveTo` escribe Velocity/Acceleration cada frame y el CMC las integra solo.

## 4. Evidencia (run 23:03, compilacion 23:00)

```
23:03:04  RUNPHYS-EXPERIMENT: pose released, IsBot=false, bRunPhysicsWithNoController set=true (possessor=false)
23:03:05  [MovingForward] arrived=true watchdog=false pos=(-132139,-113830,3821)   ← se movio
23:03:05  [Jumping]       pos=(-132139,-113830,3979)                              ← salto real (Z +158), gravedad
```

- 1 segundo tras el UnPossess: `arrived=true` en destino (250 unidades de distancia
  recorridas caminando, sin teletransporte).
- El salto (`CustomBotMovement::Jump` → `Character.Jump`) funciono sin controller
  (Z 3811 → 3979).
- `netConn` siguio en 0 tras la solucion y la fisica corrió → `netConn` NO era la causa
  (era un sintoma del "no-sim").

## 5. Camino del diagnostico (experimentos descartados, NO repetir)

| # | Experimento | Resultado | Por que fallo |
|---|---|---|---|
| 1 | Escribir maxwalk/acc/mode=1 en el CMC cada frame | congelado | la condicion era de SIMULACION, no de config |
| 2 | `ServerAcknowledgePossession` + forzar `AcknowledgedPawn` (CLAIM-LIVE) | congelado | handshake de conexion, no de simulacion |
| 3 | `SpawnDefaultPawnAtTransform` (flujo nativo), quitado +50 Z | congelado | el flujo es el bueno; no era el spawn |
| 4 | Fase: warmup hub vs partida real post-bus | congelado en ambas | la fase no importaba |
| 5 | DORM_Never forzado cada frame | congelado | dormancia no era el problema |
| 6 | `SetComponentTickEnabled(true)`+`Activate()` cada frame | congelado | el CM ya tickeaba/estaba sano (cmPtrs=1 1 1) |
| 7 | `UnPossess()` SOLO | congelado | **falto el bit** `bRunPhysicsWithNoController` |
| 8 | `SetIsBot(false)` SOLO | congelado | quita un flag, no cambia la condicion |
| 9 | **`SetIsBot(false)` + `UnPossess()` + bit runPhysics** | **SE MOVIO** | entra en la rama que UE si simula |

## 6. Observaciones de sondas importantes (para compactar)

- `cmTick real=40/0 bot=40/0`: el probe `IsComponentTickEnabled` via ProcessEvent
  devuelve 0 TAMBIEN para el real (que si tickea) → **la lectura de return de ese probe
  esta rota**; no usarla como dato. `IsActive` (`act=1`) si es fiable.
- `cmPtrs real=1 1 1 bot=1 1 1`: CharacterOwner/UpdatedComponent presentes y activos en
  ambos — el CM del bot estaba sano.
- `runPhys real=37 bot=37` (byte crudo): es un BITFIELD; leer con `ReadBitfieldValue`
  + `GetFieldMask(GetProperty(...))`, no byte crudo.
- `GetCharacterMovement()` devuelve `UObject*` (CustomBotMovement.h:75-81).
- Esta versión NO tiene sistema AI en memoria (0 objetos `AFortAthenaAIBotController`/
  Phoebe en el ObjectsDump); `ServerMove` vive en `ACharacter` (UE viejo), no en
  `APlayerController`.

## 7. Pendiente / siguiente Aplicar el fix siempre, no solo cuando salta

- **FIX aplicado SIEMPRE** (cumplido): `EnableServerSimulation` en el spawn de todo bot
  (CustomBotSpawner.h + CustomBotMovement.h); el gate T+3.0 del debug se elimino.
  Verificar en log: `[CustomBot] enableServerSimulation done` + `RUNPHYS-FIX ... set=true`.
- **BuildRamp sigue fallando** ("BuildRamp returned nullptr! ... invalid location") —
  se anadio instrumentacion `[BuildPiece] gate=...` en `CustomBotBuilding.h`
  (worldloc / unbuildable / CantBuild / spawn) para saber QUE gate rechaza; el build
  con esa sondas quedo abortado, falta compilar.
- **Equip de la sniper falla** (se queda el pickaxe) — secundario para la demo
  movimiento+saltar.
- El bot queda UNPOSSESSED desde el spawn: es el estado correcto/funcionando para
  caminar/saltar; la secuencia no necesita `Possess` de vuelta.

## 8. Archivos tocados

- `Project Reboot 3.0/CustomBot/CustomBotMovement.h` — `GetCharacterMovement`, nuevo
  `EnableServerSimulation` (el fix).
- `Project Reboot 3.0/CustomBot/CustomBotSpawner.h` — `SpawnCustomBot` llama a
  `EnableServerSimulation` al final (todos los bots) + log `enableServerSimulation done`.
- `Project Reboot 3.0/Object.h:96-99` — `ReadBitfieldValue`/`SetBitfieldValue`.
- `Project Reboot 3.0/reboot.h:159` — `GetFieldMask`.
- `Project Reboot 3.0/Controller.cpp:19` — `AController::UnPossess`.
- `Project Reboot 3.0/PlayerState.cpp:46` — `APlayerState::SetIsBot`.