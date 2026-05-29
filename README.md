# PES6 Game Physics Mod - PassPower Enhancement

![Version](https://img.shields.io/badge/version-1.0-blue.svg)
![Author](https://img.shields.io/badge/author-pitycharly-green.svg)

## 📌 ¿Qué es este Mod?

Este es un plugin (DLL) diseñado para **Pro Evolution Soccer 6** que busca revolucionar la física y la respuesta de los pases, con un enfoque especial en los **pases de primera**. 

El objetivo principal es eliminar esa sensación de "pases muertos" o excesivamente débiles cuando se intenta jugar rápido al primer toque, permitiendo que el flujo del juego sea mucho más dinámico y realista.

## 🏗️ Arquitectura del Proyecto

El mod está construido con una arquitectura modular en C++, diseñada para intervenir el motor del juego de forma quirúrgica:

### 1. Sistema de Hooks (Memory Patching)
Utilizamos técnicas de inyección de código para interceptar las funciones internas de PES6 en tiempo real:
- **Context Hook**: Captura el momento exacto en que se inicia un pase, identificando al pasador y al receptor.
- **Power Hook**: Interviene el cálculo de la fuerza del pase justo antes de que el motor la aplique al balón.

### 2. Gestión de Contexto (`PassContext`)
No es solo cuestión de fuerza bruta. El mod analiza el **escenario**:
- Posición del pasador y receptor en el campo.
- Distancia euclidiana entre ambos.

### 3. Modificador de Potencia (`PassPower`)
El "cerebro" del mod. Aplica algoritmos para reescalar la fuerza del balón basándose en la información recolectada por el sistema de contexto.

### 4. Interfaz y Feedback (`KitserverOverlay` & `Logger`)
- **Overlay**: Integración con Kitserver para mostrar mensajes en pantalla (por ejemplo, cuando se activa/desactiva el mod).
- **Logger**: Un sistema de logs detallado para debugear cada pase (coordenadas, distancias, fuerza original vs. modificada).

## 💡 Concepto: Adaptación de la Fuerza

La idea conceptual no es simplemente hacer que todos los pases sean fuertes, sino **inteligentes**. 

### El Problema
En el PES6 original, los pases de primera a veces pierden demasiada inercia porque el juego prioriza la animación o posicionamiento por sobre la intención del usuario o la distancia del compañero.

### Nuestra Solución: El Re-escalado Dinámico
Estamos adaptando la fuerza mediante una **curva de potencia basada en la distancia**:
1. **Detección de Intención**: Si el usuario carga potencia para un compañero lejano pero el pase es de primera, el mod asegura que el balón mantenga la energía necesaria para llegar a destino sin ser interceptado por la falta de velocidad.
2. **Compensación de Ángulos**: Se ajustan los pases en ángulos difíciles (awkward angles) donde el motor nativo suele fallar en la entrega de fuerza.
3. **Control por Hotkey**: El usuario puede activar o desactivar este comportamiento en tiempo real (`Ctrl + Shift + P`), permitiendo comparar la mejora instantáneamente.

---
**Desarrollado por:** [pitycharly]  
**Versión:** 1.0  
*Mod de físicas de pases desarrollado para la comunidad de PES6.*
