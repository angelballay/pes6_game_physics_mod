# PES6 AI Difficulty Research — fix7 11C253 deep state logger

## Objetivo

Esta versión reemplaza el logger amplio de `fix6b` por una versión más semántica. Sigue sin aplicar tweaks de gameplay: no modifica dificultad, tablas, timers ni estados. Solo observa.

El foco nuevo es entender si `PES6.exe+11C253` está cerca de una decisión real de IA, porque en el análisis de logs anterior todos los eventos `state16=0x0F / sub18=0x0004` aparecieron asociados a `PROBE_CMP_11C253_DIFF_GT_2_GATE`.

## Header esperado

```text
[AI_DIFFICULTY_RESEARCH] start ... version=fix7_11C253_deep_state_logger_no_tweaks tweaks=none ...
```

## Qué mantiene de fix6b

Se mantienen probes neutrales sobre:

```text
65F21   mov cl,[diff]
550C1   mov bl,[diff] -> jmp 11E517
59A5FD  movzx eax,[diff] -> jmp 11D298
11C253  cmp [diff],02
11D3D2  cmp [diff],02
11D646  cmp [diff],02
11D9BB  validate 0..5
11DBFC  cmp [diff],03
11DCC3  cmp [diff],02
11DD20  cmp [diff],03
11DE91  cmp [diff],03
11DF53  tabla local 16,12,10,10,8,8 neutral
11E5F3  cmp [diff],02
11EAC4  mov bl,[diff]
11EB66  cmp [diff],02
11EBE1  jump table diff -> CL class
```

## Logs nuevos

### `[AI_STATE_CHANGE]`

Se emite cuando el logger detecta que un actor probable cambió `state16` o `sub18` desde la última muestra vista.

Campos clave:

```text
from=state/sub anterior
to=state/sub actual
branch=rama donde se observó el cambio
prev_branch=rama anterior vista para ese actor
positive_candidate=1 si el estado actual es 0x0F/0x0004
```

Esto no prueba todavía que la rama haya escrito el estado; prueba que el cambio ya es observable en esa rama. Es el rastro que necesitamos para acercarnos a la escritura real.

### `[AI_11C253_FOCUS]`

Se emite siempre que:

```text
branch == PROBE_CMP_11C253_DIFF_GT_2_GATE
```

O cuando aparece:

```text
state16 = 0x0F
sub18   = 0x0004
```

Esto permite filtrar rápido los eventos importantes sin parsear todo el ruido de `65F21`.

### `[AI_PROBE]` extendido

Ahora incluye:

```text
likely_actor
prev_state
prev_sub
state_changed
positive_candidate
stack08/0C/10/14/18/1C/20
```

## Filtro anti-ruido

El logger ya no deja que `65F21` tape todo. Sigue logueando cuando cambia contexto, pero siempre prioriza:

```text
11C253
state changes
state16=0x0F
sub18=0x0004
550C1 -> 11E517
59A5FD -> 11D298
11DBFC
11EB66
11EBE1
11EAC4
```

## Qué buscar en el próximo log

Primero filtrar por:

```text
[AI_STATE_CHANGE]
positive_candidate=1
[AI_11C253_FOCUS]
state16=15/0x0F sub18=4/0x0004
```

Preguntas que queremos responder:

1. ¿El cambio `0x16/0x17 -> 0x0F` aparece justo antes o dentro de `11C253`?
2. ¿`0x0F/0004` aparece más en diff 5 que en diff 3?
3. ¿Qué valores de `f48`, `f4A`, `f62`, `timer114`, `f98`, `eax/ebx/ecx/edx` rodean ese cambio?
4. ¿La ruta previa suele venir de `550C1 -> 11E517`, `11DBFC`, `11EB66` o `11EBE1`?

## Compilación

Usar igual que antes:

```text
Win32 / Release
PlatformToolset v142
```

Carga con Kitserver/injector como el proyecto anterior.

## Seguridad

No hay tweaks activos. Esta versión no debería cambiar el gameplay.
