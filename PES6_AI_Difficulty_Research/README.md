# PES6 AI Difficulty Research — fix8c behavior blackbox logger

## Objetivo

Esta versión sigue siendo neutral y no toca gameplay:

```text
version=fix8c_behavior_blackbox_active_resolver_reopenable_log_no_tweaks
tweaks=none
```

El objetivo de fix8c es mejorar dos problemas detectados en los logs de fix8b:

1. El resolver de `active player` muchas veces quedaba sin contexto útil.
2. `D:/pes/IA/logs.txt` quedaba bloqueado y no se podía renombrar/borrar durante una sesión.

## Cambio 1 — Log renombrable/borrable en caliente

`FileLogSink` ahora no mantiene `logs.txt` abierto permanentemente.

Cada línea hace:

```text
open D:/pes/IA/logs.txt
append line
flush
close
```

Además usa:

```cpp
FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
```

Esto permite:

```text
1. Jugar una microprueba.
2. Alt+Tab.
3. Renombrar D:/pes/IA/logs.txt a test1_xxx.txt.
4. Volver al juego.
5. La siguiente línea recrea logs.txt automáticamente.
```

También podés borrar `logs.txt` mientras PES está abierto. La siguiente línea del logger lo vuelve a crear.

## Cambio 2 — Active resolver mejorado

Antes `activePlayer` dependía casi solo de:

```text
[pes6.exe+37E0AA0]
```

En algunos hooks de IA ese puntero no alcanzaba para calcular `dist_actor_active`, así que fix8c intenta resolver contexto con más fuentes:

```text
1. [pes6.exe+37E0AA0] si parece jugador válido y tiene posición.
2. ball owner inferido desde [pes6.exe+37E09CC].
3. registros EAX/EBX/ECX/EDX/ESI/EDI/EBP.
4. stack cercano.
```

Nuevos campos en log:

```text
active_valid
active_source
ball_owner
ball_owner_valid
ball_owner_source
```

Ejemplos de `active_source` esperados:

```text
global_active
ball_owner
reg_edi_target
st14_target
global_active_no_pos
not_found
```

## Cambio 3 — Spatial intent

Además de `phase`, ahora aparece:

```text
spatial_intent
```

Valores posibles:

```text
NEAR_REAL_BALL_CLOSE
NEAR_REAL_BALL
NEAR_PREDICTED_BALL_CLOSE
NEAR_PREDICTED_BALL
PRESS_ACTIVE_OWNER_LIKE
PASS_OR_LOOSE_PREDICTIVE_CHASE
PASS_OR_LOOSE_FAR_OR_UNKNOWN
FAR_OR_UNKNOWN
```

Esto ayuda a leer eventos positivos como `0x0F/0004` sin mirar a mano todas las distancias.

## Offsets espaciales activos

Pelota actual:

```text
ball+0x20 = X
ball+0x24 = altura / Y
ball+0x28 = Z
```

Pelota predictiva/destino:

```text
ball+0x1454 = X probable/destino
ball+0x1458 = altura / Y probable
ball+0x145C = Z probable/destino
```

Jugador/actor primario:

```text
player/actor+0xD0 = X
player/actor+0xD4 = altura / Y
player/actor+0xD8 = Z
```

Jugador/actor alternativo:

```text
player/actor+0xE0 = X
player/actor+0xE4 = altura / Y
player/actor+0xE8 = Z
```

## Eventos importantes

### `[AI_DECISION]`

Sale cuando hay cambio de estado, candidato positivo o paso por `11C253`.

### `[AI_FIELD_CHANGE]`

Sale cuando cambian campos internos relevantes:

```text
f48
f4A
f62
timer114
f98
f124
```

### `[AI_STATE_CHANGE]`

Transiciones reales de `state16/sub18`.

### `[AI_11C253_FOCUS]`

Foco principal para `11C253` y `0x0F/0004`.

## Herramienta de análisis

Uso:

```text
python tools/analyze_behavior_log.py D:/pes/IA/logs.txt out_fix8c_analysis
```

Nuevos CSV:

```text
spatial_intent_counts.csv
active_source_counts.csv
```

`ai_decisions_sample.csv` ahora incluye:

```text
active
active_valid
active_source
ball_owner
ball_owner_valid
ball_owner_source
spatial_intent
```

## Cómo probar

Ahora sí podés separar pruebas sin cerrar PES:

```text
1. Entrar al partido.
2. Hacer prueba 1 durante 5–10 segundos.
3. Alt+Tab.
4. Renombrar logs.txt a test1_quieto_con_pelota_diff5.txt.
5. Volver al juego.
6. Hacer prueba 2.
7. Repetir.
```

Pruebas sugeridas:

```text
1. Humano quieto con pelota, CPU cerca.
2. Humano corre hacia defensor.
3. Pase corto cerca de defensor CPU.
4. Pelota suelta entre humano y CPU.
5. CPU atacando cerca del área.
```

Buscar especialmente:

```text
positive_candidate=1
state/sub 0x16:1, 0x16:2, 0x16:3, 0x0F:1, 0x0F:4
phase=...
spatial_intent=...
active_source=...
dist_actor_ball
dist_actor_active
dist_actor_ball_pred
```

## fix9_behavior_blackbox_motion_funnel_logger

Neutral research logger. No gameplay tweaks.

Main additions over fix8c/fix8e:

- Motion context in every semantic line:
  - `actor_speed`, `ball_speed`, `active_speed`, `pred_speed`
  - previous distances and distance deltas:
    `prev_dist_actor_ball`, `delta_actor_ball`, `delta_actor_ball_pred`, etc.
  - direction flags: `moving_to_ball`, `moving_to_active`, `moving_to_pred`
  - `motion_intent`: `TOWARD_REAL_BALL`, `TOWARD_PRED_BALL`, `TOWARD_REAL_AND_PRED_BALL`, `TOWARD_ACTIVE`, `NO_CLOSING_SIGNAL`
- Corrected `actor_best` / `active_best` labeling. All-zero D0 positions are no longer labeled as valid D0; the logger falls back to E0 when E0 is the reasonable coordinate set.
- New semantic commit line:
  - `[AI_COMMIT_POSITIVE]` whenever the observed state is `state16=0x0F` and `sub18=0x0004`.
- Optional direct hook for the documented positive site `pes6.exe+11F172` exists but is disabled by default:
  - `ResearchConfig::kInstallExperimental11F172PositiveHook = false`
  - Only enable after confirming the bytes around `11F172` match the documented pattern.
- Analyzer upgraded to `fix9_motion_funnel_analyzer`:
  - batch mode still works with `python analyze_behavior_log.py`
  - outputs funnel by diff, distance bins, positive/negative 11C253 context groups, motion intent counts, and positive unique events.

Recommended next analysis files:

- `decision_funnel_by_diff.csv`
- `positive_unique.csv`
- `negative_11C253_sample.csv`
- `positive_negative_context_11C253.csv`
- `distance_bins_11C253.csv`
- `motion_intent_counts.csv`
