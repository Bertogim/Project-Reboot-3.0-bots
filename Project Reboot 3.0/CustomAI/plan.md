
# PARTE 2 — IA COMPLETA DE BOTS + BATTLE ROYALE + PESTAÑA DE BOTS

## CONTEXTO

La Parte 1 ya ha creado un sistema CUSTOM de bots independiente. Project Reboot 3.0/CustomBot

IMPORTANTE:

NO volver a utilizar `PlayerBot`/`AFortAthenaAIBotController` del sistema antiguo como base.

NO reemplazar el Custom Bot creado en la Parte 1.

NO crear un tercer sistema de bots.

Esta Parte 2 debe utilizar las capacidades del Custom Bot de la Parte 1.

Primero inspecciona los cambios realizados en la Parte 1 y entiende exactamente qué funciones y clases están disponibles.

Hacer toda esta logica en otra carpeta como CustomBot, pero que se use CustomBot

Esto prompt esta en /home/bertogim/Documentos/Project-Reboot-3.0-bots/Project Reboot 3.0/CustomAI/plan.md, despues de cada compactacion leelo

---

# 1. OBJETIVO

Convertir el Custom Bot en un jugador autónomo de Battle Royale para Season 3.

Debe poder jugar una partida completa:

```text
Lobby/Inicio
 ↓
Battle Bus
 ↓
Elegir lugar de aterrizaje
 ↓
Saltar del bus
 ↓
Glider
 ↓
Aterrizar
 ↓
Loot
 ↓
Farmear
 ↓
Explorar
 ↓
Detectar jugadores/bots
 ↓
Combatir
 ↓
Construir
 ↓
Curarse
 ↓
Rotar con Storm
 ↓
Endgame
 ↓
Ganar o morir
```

Además, crear una pestaña/interfaz de Bots que permita controlar y observar los bots.

---

# 2. BATTLE BUS

Los bots deben comportarse como jugadores normales.

Cada bot debe:

1. Entrar al Battle Bus.
2. Permanecer dentro durante la ruta.
3. Elegir un destino.
4. Elegir cuándo saltar.
5. Saltar.
6. Utilizar el glider.
7. Dirigirse hacia el lugar seleccionado.
8. Aterrizar.
9. Empezar a jugar.

NO hacer que todos los bots salten juntos.

Cada bot debe tener cierta variación.

---

# 3. ELECCIÓN DE LANDING

Cada bot debe elegir un destino.

Factores:

* POIs disponibles.
* Calidad de loot.
* Distancia.
* Preferencia de personalidad.
* Número de jugadores/bots detectados en la zona.
* Aleatoriedad.

Ejemplos:

```text
Bot agresivo
→ prefiere POIs con muchos jugadores.

Bot defensivo
→ prefiere zonas tranquilas.

Bot random
→ elección variable.
```

No es necesario que la elección sea perfecta.

Debe parecer una decisión de jugador.

---

# 4. LOOT

Después de aterrizar:

```text
Buscar arma
 ↓
Munición
 ↓
Escudo
 ↓
Curación
 ↓
Materiales
 ↓
Mejorar inventario
```

Utilizar las funciones de loot/inventario de la Parte 1.

El bot debe evaluar los objetos.

No recoger indiscriminadamente todo.

---

# 5. FARMEO

Los bots deben conseguir materiales.

Pueden:

* Destruir árboles.
* Destruir rocas.
* Destruir objetos.
* Destruir estructuras.
* Recoger materiales.

La elección debe depender de la situación.

Ejemplo:

```text
Pocos materiales
 ↓
Buscar recursos cercanos
 ↓
Destruir
 ↓
Recoger materiales
```

No quedarse farmeando indefinidamente.

---

# 6. MOVIMIENTO AUTÓNOMO

Utilizar el sistema de movimiento de la Parte 1.

El bot debe:

* Explorar.
* Ir hacia loot.
* Ir hacia objetivos.
* Rodear obstáculos.
* Buscar rutas.
* Cambiar de dirección.
* Saltar cuando sea necesario.
* Utilizar construcción para superar terrenos complicados.

---

# 7. MONTAÑAS / OBSTÁCULOS

Caso:

```text
Bot quiere llegar a un punto
        ↓
Montaña demasiado alta
```

Comportamiento:

```text
¿Puede rodearla?
 ├── Sí → rodear
 └── No
      ↓
¿Tiene materiales?
 ├── Sí → construir rampas
 └── No → buscar otra ruta
```

Debe poder construir hacia arriba para superar desniveles.

No teletransportarse.

---

# 8. PERCEPCIÓN

Utilizar el sistema de percepción de la Parte 1.

El bot debe detectar:

* Jugadores.
* Otros bots.
* Loot.
* Cofres.
* Recursos.
* Estructuras.
* Disparos/eventos cuando sea posible.

No permitir conocimiento omnisciente.

---

# 9. BOT VS BOT

Los bots deben poder atacarse entre ellos.

Reglas:

```text
Bot A
 ↓
detecta Bot B
 ↓
¿Son enemigos?
 ↓
Sí
 ↓
Combatir
```

Debe funcionar:

```text
BOT → BOT
BOT → JUGADOR
JUGADOR → BOT
```

Respetar equipos.

Si están en el mismo equipo:

```text
NO atacar
```

Si son enemigos:

```text
PUEDEN ATACARSE
```

Los bots deben poder matarse entre ellos y reducir el número real de jugadores vivos.

---

# 10. COMBATE

Cuando detecte un enemigo:

Evaluar:

* Distancia.
* Vida.
* Escudo.
* Arma.
* Munición.
* Posición.
* Altura.
* Cobertura.
* Materiales.
* Habilidad.
* Agresividad.
* Riesgo.

---

# 11. SI EL JUGADOR VIENE HACIA EL BOT

MUY IMPORTANTE:

El bot NO debe huir automáticamente.

Si un jugador se acerca:

```text
Jugador detectado
 ↓
Preparar arma
 ↓
Buscar cobertura
 ↓
Apuntar
 ↓
Combatir
```

Si el jugador dispara:

```text
Recibe disparo
 ↓
Buscar cobertura
 ↓
Construir si es necesario
 ↓
Devolver fuego
```

---

# 12. SI EL BOT ESTÁ EN DESVENTAJA

Si el enemigo es superior:

NO hacer:

```text
Jugador fuerte
 ↓
Bot corre sin defenderse
```

Hacer:

```text
Jugador fuerte
 ↓
Buscar cobertura
 ↓
Construir
 ↓
Devolver fuego
 ↓
Reposicionarse
 ↓
Curarse si existe oportunidad
 ↓
Volver a combatir
```

Solo escapar cuando la situación sea realmente insostenible.

---

# 13. CONSTRUCCIÓN DE COMBATE

Utilizar la construcción de la Parte 1.

El bot puede:

* Construir paredes.
* Construir rampas.
* Construir suelo/techo cuando corresponda.
* Crear cobertura.
* Ganar altura.
* Protegerse mientras se cura.
* Construir durante un enfrentamiento.

No intentar que todos construyan como jugadores competitivos.

La habilidad dependerá de `BuildSkill`.

---

# 14. DESTRUIR PARA PASAR

Si una estructura bloquea el camino:

```text
Camino bloqueado
 ↓
¿Puede rodear?
 ├── Sí → rodear
 └── No → destruir estructura
```

Utilizar la capacidad de destrucción de la Parte 1.

---

# 15. CURACIÓN

Si tiene poca vida/escudo:

```text
¿Está siendo atacado?
 ├── Sí → cobertura
 └── No → curarse
```

Después:

```text
Curado
 ↓
Reevaluar combate
```

No quedarse curándose indefinidamente.

---

# 16. STORM

La Storm debe tener alta prioridad.

Si está fuera:

```text
ROTATE
```

Si el siguiente círculo está lejos:

```text
Preparar rotación
```

No esperar hasta que la Storm esté encima.

Durante la rotación:

* Buscar ruta.
* Evitar zonas innecesariamente peligrosas.
* Mantener atención a enemigos.
* Utilizar cobertura.
* Construir si hace falta.

---

# 17. MID GAME

Después del landing:

* Loot.
* Farm.
* Explorar.
* Buscar mejores armas.
* Buscar recursos.
* Investigar combates.
* Rotar.

Los bots agresivos pueden buscar combates.

Los defensivos pueden evitarlos.

---

# 18. PERSONALIDADES

Cada bot debe tener parámetros distintos.

Ejemplo:

```cpp
struct BotPersonality
{
    float Aggression;
    float AimSkill;
    float BuildSkill;
    float LootSkill;
    float Awareness;
    float RiskTolerance;
};
```

Tipos:

### Novato

* Mala puntería.
* Poca construcción.
* Baja percepción.
* Baja agresividad.

### Casual

* Equilibrado.

### Agresivo

* Busca jugadores.
* Investiga disparos.
* Entra en peleas.

### Defensivo

* Prioriza supervivencia.
* Utiliza más cobertura.

### Pro

* Buena puntería.
* Buena construcción.
* Buena percepción.
* Mejores decisiones.

### Random

* Parámetros variables.

---

# 19. IMPERFECCIONES HUMANAS

No hacer bots perfectos.

Deben poder:

* Fallar disparos.
* Reaccionar con diferentes tiempos.
* Tomar decisiones imperfectas.
* Cambiar de objetivo.
* Equivocarse de ruta.
* Tardar en detectar enemigos.
* Construir peor bajo presión.

NO permitir:

* Wallhack.
* Aim perfecto.
* Conocimiento de posiciones que no pueden conocer.
* Decisiones instantáneamente perfectas.

---

# 20. THIRD PARTY

Los bots deben poder intervenir en peleas.

Ejemplo:

```text
Jugador pelea con Bot A
        ↓
Bot B escucha/detecta combate
        ↓
Bot B llega
        ↓
Bot B decide atacar
        ↓
Combate de 3 participantes
```

Esto debe producir partidas dinámicas.

---

# 21. ENDGAME

A medida que quedan menos jugadores:

### Top 20

Más atención a Storm.

### Top 10

Más cobertura.

### Top 5

Más precaución.

### Top 2

Intentar ganar activamente.

En 1v1:

* Disparar.
* Construir.
* Reposicionarse.
* Curarse.
* Buscar altura.
* Perseguir si tiene ventaja.
* Defenderse si está en desventaja.

---

# 22. MÁQUINA DE ESTADOS

Crear una máquina de estados para controlar el comportamiento.

Estados recomendados:

```cpp
enum class EBotState
{
    InBus,
    ChoosingLanding,
    Jumping,
    Gliding,
    Landing,
    Looting,
    Farming,
    Exploring,
    SearchingEnemy,
    Fighting,
    Defending,
    Healing,
    Rotating,
    EndGame,
    Dead
};
```

Adaptar nombres si el proyecto requiere otros.

---

# 23. TRANSICIONES

Ejemplo:

```text
InBus
 ↓
ChoosingLanding
 ↓
Jumping
 ↓
Gliding
 ↓
Landing
 ↓
Looting
 ↓
Exploring
```

Desde Exploring:

```text
Enemy detected
 → Fighting

Low materials
 → Farming

Storm approaching
 → Rotating

Interesting loot
 → Looting
```

Desde Fighting:

```text
Winning
 → Fighting

Losing
 → Defending

Enemy eliminated
 → Looting/Exploring

Low HP
 → Healing
```

---

# 24. PRIORIDADES

El bot debe evaluar prioridades.

Una prioridad razonable:

```text
1. Sobrevivir a peligro inmediato
2. Defenderse de enemigo
3. Evitar Storm
4. Curarse cuando sea necesario
5. Combatir
6. Rotar
7. Loot
8. Farmear
9. Explorar
```

Estas prioridades pueden modificarse según personalidad.

---

# 25. PESTAÑA "BOTS"

Crear una pestaña/interfaz llamada:

```text
Bots
```

Debe permitir gestionar y observar los bots.

Como mínimo mostrar información textual sobre el estado global:

```text
BOTS
────────────────────────

Total bots: 30

In Bus:             30
Choosing Landing:    0
Jumping:             0
Gliding:             0
Landing:             0
Looting:             0
Farming:             0
Exploring:           0
Searching Enemy:     0
Fighting:            0
Defending:           0
Healing:             0
Rotating:            0
End Game:            0
Dead:                0
```

Los números deben actualizarse en tiempo real.

NO necesito una lista individual de todos los bots.

Lo importante es mostrar **cuántos bots hay actualmente en cada estado**.

---

# 26. CONTROL DE CANTIDAD

La pestaña Bots debe permitir configurar el número de bots.

Ejemplo:

```text
Number of Bots:
[ 30 ]

[ Spawn Bots ]
```

Si existe ya un sistema de configuración apropiado, integrarlo en lugar de crear otro innecesariamente.

---

# 27. DIFICULTAD

Añadir, si es compatible con la interfaz:

```text
Difficulty:
[ Easy ]
[ Normal ]
[ Hard ]
```

Esto debe modificar parámetros de la personalidad/IA.

Ejemplo:

### Easy

* Reacción lenta.
* Mala puntería.
* Mala construcción.

### Normal

* Comportamiento equilibrado.

### Hard

* Mejor percepción.
* Mejor puntería.
* Mejor construcción.
* Mejores decisiones.

NO utilizar dificultad para dar información ilegal al bot.

---

# 28. ESTADO EN TIEMPO REAL

El contador de estados debe reflejar el estado real de cada bot.

Si 7 bots están luchando:

```text
Fighting: 7
```

Si 4 están farmeando:

```text
Farming: 4
```

Si un bot cambia de estado:

```text
Looting: 8 → 7
Fighting: 2 → 3
```

La interfaz debe actualizarse automáticamente.

No utilizar valores falsos o simulados.

---

# 29. ARQUITECTURA DE LA PESTAÑA

No mezclar la UI con la lógica de IA.

Separar:

```text
Bot Manager
     ↓
Bot Instances
     ↓
Bot AI
     ↓
Custom Bot
```

La UI consulta el Bot Manager:

```text
Bot Manager
 ↓
GetBots()
 ↓
GetStateCounts()
 ↓
UI
```

---

# 30. BOT MANAGER

Crear un sistema central para administrar los bots.

Debe poder:

* Crear bots.
* Eliminar bots.
* Contarlos.
* Consultar sus estados.
* Consultar bots vivos.
* Consultar bots muertos.
* Cambiar configuración.
* Acceder a cada instancia.

Conceptualmente:

```cpp
class CustomBotManager
{
public:
    SpawnBot();
    SpawnBots(int Count);
    RemoveBot(...);

    GetBots();
    GetAliveBots();
    GetDeadBots();

    GetStateCounts();
};
```

Adaptar a la arquitectura existente.

---

# 31. NO DUPLICAR SISTEMAS

Antes de crear BotManager, comprobar si existe un GameMode/GameState adecuado para almacenar esta información.

No crear sistemas duplicados innecesariamente.

La arquitectura debe integrarse con Project Reboot 3.0.

---

# 32. JUGADORES REALES Y BOTS

El sistema debe tratar a los bots como participantes reales de la partida.

El número de vivos debe actualizarse correctamente.

Ejemplo:

```text
Players: 5
Bots: 25

Alive:
Players: 5
Bots: 22

Total Alive: 27
```

Si un bot muere:

```text
Bots Alive: 21
```

Esto debe integrarse con el sistema de jugadores de la partida cuando sea posible.

---

# 33. VICTORY ROYALE

Si el bot es el último participante vivo:

Debe utilizar el sistema de victoria existente del proyecto.

No crear una victoria falsa simplemente mostrando un mensaje.

---

# 34. ELIMINACIÓN

Cuando un bot muere:

* Marcarlo como Dead.
* Actualizar contador.
* Ejecutar lógica de eliminación existente.
* No permitir que siga actuando.
* Actualizar UI.
* Registrar quién lo eliminó si el sistema lo permite.

Debe funcionar tanto:

```text
Jugador → Bot
Bot → Bot
Bot → Jugador
```

---

# 35. RENDIMIENTO

NO ejecutar una IA pesada para cada bot en cada frame.

Separar:

### Cada frame

* Movimiento esencial.
* Acciones necesarias.

### Intervalos

* Escaneo.
* Evaluación de loot.
* Evaluación de enemigos.
* Decisiones.

### Eventos

* Recibir daño.
* Detectar disparos.
* Cambio de Storm.
* Eliminación.
* Nuevo enemigo.

Optimizar para que 20–50 bots no destruyan el rendimiento innecesariamente.

---

# 36. DEBUG

Añadir herramientas de debug si son útiles.

Por ejemplo:

```text
Bot 001
State: Fighting
Target: Bot 014
HP: 100
Shield: 50
Ammo: 32
Materials: 420
```

Esto será extremadamente útil para depurar la IA.

Si existe un sistema de logging del proyecto, utilizarlo.

---

# 37. ORDEN DE IMPLEMENTACIÓN

NO implementar todo simultáneamente.

Seguir este orden:

## FASE 1

Integrar Custom Bot de Parte 1.

## FASE 2

Bot Manager.

## FASE 3

Battle Bus.

## FASE 4

Jump + Glider + Landing.

## FASE 5

Loot autónomo.

## FASE 6

Farm autónomo.

## FASE 7

Exploración.

## FASE 8

Percepción de enemigos.

## FASE 9

Combate BOT vs JUGADOR.

## FASE 10

Combate BOT vs BOT.

## FASE 11

Defensa + construcción.

## FASE 12

Curación.

## FASE 13

Storm + rotación.

## FASE 14

Endgame.

## FASE 15

Personalidades.

## FASE 16

Bot Manager + contadores.

## FASE 17

Pestaña Bots.

## FASE 18

Optimización.

---

# 38. VERIFICACIÓN

No es necesario compilar después de cada fase.

El repositorio utiliza GitHub Actions para realizar la compilación y puede tardar bastante.

Por tanto:

* Trabaja sobre las fases de forma coherente.
* Revisa el código antes de terminar.
* Evita cambios innecesarios.
* Comprueba referencias, tipos, includes y nombres.
* No inventes APIs.
* Utiliza las implementaciones reales encontradas en el repositorio.
* Deja el proyecto preparado para que GitHub Actions pueda compilarlo.

No esperes una compilación completa después de cada modificación.

Si existe alguna comprobación local rápida que no implique una compilación completa, puedes utilizarla.

---

# 39. REGLA PRINCIPAL DE DESARROLLO

No sacrificar funcionalidad real por hacer que el código "parezca terminado".

Por ejemplo:

MAL:

```cpp
void Bot::Loot()
{
    // TODO
}
```

y marcarlo como terminado.

BIEN:

El bot realmente encuentra un objeto, se mueve hasta él, interactúa y lo añade al inventario mediante el sistema real.

---

# 40. CRITERIO DE ÉXITO

Al terminar, debe ser posible iniciar una partida con bots y observar:

```text
Battle Bus
 ↓
Bots saltan en diferentes momentos
 ↓
Aterrizan
 ↓
Loot
 ↓
Farm
 ↓
Exploran
 ↓
Bot detecta Bot
 ↓
Se disparan
 ↓
Uno construye cobertura
 ↓
El otro responde
 ↓
Combate termina
 ↓
Continúan jugando
 ↓
Storm
 ↓
Rotación
 ↓
Más combates
 ↓
Endgame
 ↓
Últimos bots/jugador
 ↓
Victoria o eliminación
```

Y en la pestaña:

```text
BOTS

Total: 30

In Bus: 0
Gliding: 0
Looting: 4
Farming: 3
Exploring: 8
Searching Enemy: 2
Fighting: 6
Defending: 2
Healing: 1
Rotating: 4
End Game: 0
Dead: 0
```

Los números deben representar estados reales.

---

# 41. PRUEBA FINAL

Realizar una prueba con varios bots.

Comprobar específicamente:

### Battle Bus

* ¿Todos entran?
* ¿Saltan?
* ¿Saltan en diferentes momentos?
* ¿Aterrizan correctamente?

### Movimiento

* ¿Se desplazan?
* ¿Detectan obstáculos?
* ¿Pueden subir mediante construcción?

### Loot

* ¿Encuentran objetos?
* ¿Los recogen?
* ¿Los equipan?

### Recursos

* ¿Pueden destruir objetos?
* ¿Obtienen materiales?
* ¿Construyen usando materiales?

### Combate

* ¿Pueden atacar jugadores?
* ¿Los jugadores pueden atacarlos?
* ¿Pueden atacar otros bots?
* ¿Los bots pueden matarse entre sí?

### Defensa

* ¿Construyen al recibir disparos?
* ¿Buscan cobertura?
* ¿Se defienden si el jugador es superior?

### Storm

* ¿Rotan correctamente?
* ¿Dejan de lootear cuando necesitan moverse?

### Endgame

* ¿Continúan jugando hasta el final?
* ¿Se actualiza correctamente el número de vivos?

### UI

* ¿Existe la pestaña Bots?
* ¿Muestra el número total?
* ¿Muestra cada estado?
* ¿Los contadores se actualizan realmente?

---

# 42. RESULTADO FINAL ESPERADO

Quiero un sistema de bots que NO parezca:

> "NPC que se mueve y dispara."

Quiero:

> "Jugador artificial que participa en una partida de Season 3."

Debe poder tomar decisiones, cometer errores, luchar, defenderse, construir, farmear, lootear, rotar y morir.

Los bots deben poder luchar tanto contra jugadores humanos como contra otros bots.

El jugador humano debe poder encontrarse con:

```text
Bot vs Bot
Bot vs Player
Player vs Bot
Bot third-party
Player third-party
```

Todo utilizando las mecánicas reales del proyecto siempre que sea posible.

---

# 43. VERIFICACIÓN Y GITHUB ACTIONS

Antes de terminar:

* Revisar todos los archivos modificados.
* Revisar includes.
* Revisar forward declarations.
* Revisar nombres de clases.
* Revisar referencias entre módulos.
* Revisar tipos.
* Revisar posibles errores de compilación obvios.
* Revisar que no haya funciones declaradas pero sin implementación cuando deban estar implementadas.
* Revisar que no haya código muerto innecesario.
* Revisar que la Parte 2 utilice realmente el Custom Bot de la Parte 1.
* Revisar que no se haya reactivado accidentalmente el sistema antiguo como dependencia.

NO es necesario ejecutar una compilación completa después de cada cambio.

El proyecto será compilado mediante GitHub Actions.

---

# 44. INFORME FINAL

Cuando termines, proporciona:

1. Archivos creados.
2. Archivos modificados.
3. Funciones principales implementadas.
4. Qué funciones de la Parte 1 se reutilizaron.
5. Cómo funciona el Bot Manager.
6. Cómo funciona la máquina de estados.
7. Cómo funciona el Battle Bus.
8. Cómo funciona el combate BOT vs BOT.
9. Cómo funciona la defensa.
10. Cómo funciona la Storm.
11. Cómo funciona la pestaña Bots.
12. Qué estados muestra la pestaña.
13. Qué pruebas se realizaron.
14. Qué errores aparecieron y cómo se solucionaron.
15. Qué limitaciones quedan pendientes.

NO afirmar que algo funciona si no se ha compilado/probado.

Si alguna parte no puede implementarse por una limitación real del código de Project Reboot 3.0, indicarlo claramente y explicar exactamente qué falta.
