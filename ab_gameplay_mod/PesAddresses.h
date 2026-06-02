#pragma once

#include <stdint.h>

// ------------------------------------------------------------
// Offsets relativos a pes6.exe
// ------------------------------------------------------------

namespace PesAddresses
{
    // Hook A:
    // Contexto del pase.
    // Aquí esta:
    //   EDX = pasador
    //   ESI = receptor
    constexpr uintptr_t PASS_CONTEXT_HOOK = 0x1A5905;

    // Retorno después del hook de contexto.
    // Reemplazamos 7 bytes:
    //   call pes6.exe+1A1570
    //   mov ebx,eax
    constexpr uintptr_t PASS_CONTEXT_RETURN = 0x1A590C;

    // Función original llamada en el hook de contexto.
    constexpr uintptr_t CALL_1A1570 = 0x1A1570;

    // Hook B:
    // Potencia del pase.
    // Aquí EDI contiene la potencia calculada antes de llamar a 78020.
    constexpr uintptr_t PASS_POWER_HOOK = 0x1A637B;

    // Retorno después del hook de potencia.
    // Reemplazamos 11 bytes:
    //   push 00
    //   push ebp
    //   push edi
    //   push eax
    //   push esi
    //   call pes6.exe+78020
    constexpr uintptr_t PASS_POWER_RETURN = 0x1A6386;

    // Función que aplica parámetros a la pelota.
    // Esta termina escribiendo el valor de EDI en ball+50.
    constexpr uintptr_t CALL_78020 = 0x78020;

    // Puntero global de pelota:
    // [pes6.exe+7CCE94] = ball base
    constexpr uintptr_t BALL_GLOBAL_PTR = 0x7CCE94;

    // Constante fisica estatica usada como peso/magnetismo del balon.
    // Direccion absoluta historica: 00B8AE70
    // Offset relativo a pes6.exe: pes6.exe+78AE70
    constexpr uintptr_t BALL_WEIGHT_STATIC = 0x78AE70;

    constexpr uintptr_t ACTIVE_PLAYER_PTR = 0x37E0AA0;
    constexpr uintptr_t BALL_ACTOR_ID = 0x37E09CC;
}

// ------------------------------------------------------------
// Offsets dentro de estructuras runtime
// ------------------------------------------------------------

namespace PesOffsets
{
    // Coordenadas discretas del jugador.
    // Usadas para calcular distancia simple entre pasador/receptor.
    constexpr uintptr_t PLAYER_CELL_X = 0x204;
    constexpr uintptr_t PLAYER_CELL_Y = 0x205;

    // Potencia/inercia principal de la pelota.
    constexpr uintptr_t BALL_POWER = 0x50;

    // Estado/tipo de accion del balon observado en pruebas:
    //   0 = conduccion/control con pelota
    //   1 = pase S/W
    //   3 = centro/corner
    //   4 = tiro/remate
    //   5 = pelota quieta/control detenido
    constexpr uintptr_t BALL_STATE = 0x84;
}