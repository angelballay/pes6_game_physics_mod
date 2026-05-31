# Project Overview

**PES6 Game Physics Mod** is a C++ DLL plugin for Pro Evolution Soccer 6 (PES6). Its primary purpose is to modify and revolutionize the game's pass physics, particularly targeting "first-touch" passes. By eliminating "dead passes" (passes that are excessively weak), the mod provides a more dynamic and realistic gameplay flow.

## Architecture & Technologies
- **Language**: C++
- **Build System**: Visual Studio (MSBuild / `.slnx` / `.vcxproj`)
- **Key Techniques**: Memory Patching (Hooking), Code Injection.
- **Components**:
  - `Context Hook` / `PassContext`: Analyzes the scenario, including passer and receiver positions and distance.
  - `Power Hook` / `PassPower`: Modifies and rescales the pass power dynamically before it is applied by the engine.
  - `KitserverOverlay` & `Logger`: Provides in-game visual feedback via Kitserver overlay and detailed logging for debugging.

# Building and Running

## Build
This is a Visual Studio C++ project. 
To build the project:
1. Open `AB_Gameplay_mod.slnx` or `ab_gameplay_mod/ab_gameplay_mod.vcxproj` in Visual Studio.
2. Select the `Release` or `Debug` configuration.
3. Build the solution (e.g., using `Ctrl + Shift + B` or MSBuild from the command line).

*Command Line Build Placeholder (TODO):* 
```powershell
msbuild AB_Gameplay_mod.slnx /p:Configuration=Release
```

## Running / Deployment
1. After building, the compiled `.dll` (e.g., `ab_gameplay_mod.dll`) needs to be injected into the game.
2. Typically, this is done by placing the DLL into a Kitserver plugin folder or injecting it manually into the PES6 process.
3. Once in-game, you can toggle the mod using `Ctrl + Shift + P`.

# Development Conventions

- **Modular Design**: Code is split into logical modules (`PassContext`, `PassPower`, `MemoryPatch`, `Logger`, `HotkeyToggle`).
- **Memory Hooking**: Memory manipulation routines (like `WriteJump` and `CheckBytes`) are encapsulated in `MemoryPatch.cpp/h`. Always use these safe abstractions when hooking game functions.
- **Logging**: Use the internal `Logger` for printing debug information. This is critical for understanding the game's state (pass coordinates, distance, force) before and after modification.
- **State Management**: Mod state (enabled/disabled) is managed globally via `ModState.h`.
- **C++ Standard**: Stick to modern, clean C++ constructs and ensure Windows API includes (`<windows.h>`, `<stdint.h>`) are handled properly via precompiled headers (`pch.h`) if applicable.
