# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenClaw is a multiplatform C++11 reimplementation of the Captain Claw (1997) platformer, written from scratch. It uses assets from the original game archive (`CLAW.REZ`), which must be present in `Build_Release/` — the game cannot run without it. Targets: Windows, Linux, macOS, Android, and WebAssembly (Emscripten). Tech: SDL2 (+ Image, TTF, Mixer, Gfx), Box2D for physics, TinyXML for data-driven content.

## Build Commands

### Windows
Visual Studio solution `OpenClaw.sln` at the repo root has all libraries and include paths preset (projects: OpenClaw, libwap, libwap_tests). Or use CMake:
```
mkdir build && cd build
cmake -G "Visual Studio 15 2017" ..
msbuild OpenClaw.sln
```
Do not swap in different SDL library versions than the ones included in `Build_Release/`.

### Linux
```
./build_and_run.sh   # cmake + make, re-zips ASSETS.ZIP, runs the game
```
Or manually: `mkdir build && cd build && cmake .. && make -j$(nproc)`. The binary lands in `Build_Release/`.

### Emscripten (WASM)
```
mkdir build && cd build
emcmake cmake -DEmscripten=1 ..    # add -DExtern_Config=0 to embed config.xml
make
```
Outputs `openclaw.{html,js,wasm,data}` into `Build_Release/`; serve over HTTP to run (e.g. `python -m http.server` in `Build_Release`). Emscripten-specific gaps are marked with `TODO: [EMSCRIPTEN]` comments. MIDI (.XMI) audio is disabled under Emscripten.

### Assets
After changing anything under `Build_Release/ASSETS/`, re-zip it: the game loads `ASSETS.ZIP`, not the directory (`build_and_run.sh` does this automatically):
```
cd Build_Release/ASSETS && zip -r ../ASSETS.ZIP .
```

### Tests
`libwap_tests` (Catch framework) covers only the libwap library; it builds as a VS project from the solution, not via CMake. There are no tests for the engine/game itself.

## Architecture

The engine follows the "Game Coding Complete" style layered architecture. Each layer has a `Base*` engine class in `OpenClaw/Engine/` and a `Claw*` game-specific subclass in `OpenClaw/`:

- **Application layer** — `Engine/GameApp/BaseGameApp` / `ClawGameApp`: SDL init, window, `config.xml` loading, main loop (`Engine/GameApp/MainLoop.cpp` → `RunGameEngine()`, called from `main.cpp`). A global `g_pApp` pointer gives access to the app, resource cache, audio, etc.
- **Game logic layer** — `Engine/GameApp/BaseGameLogic` / `ClawGameLogic`: owns all actors (`ActorMap`), the physics world, game state transitions (menu → loading level → running → score screen), and the list of game views. Also owns a `ProcessMgr` (`Engine/Process/`) for cooperative time-sliced processes.
- **View layer** — `Engine/UserInterface/HumanView` / `ClawHumanView`: rendering via the scene graph (`Engine/Scene/` — tile-plane, actor, and HUD scene nodes with a camera), the in-game console (`Engine/UserInterface/Console` + `Engine/GameApp/CommandHandler` for cheat/debug commands), HUD, input via `MovementController`, and touch UI (`Engine/UserInterface/Touch/`).

Cross-cutting systems:

- **Actor–component model** (`Engine/Actor/`): Actors are bags of components (render, physics, position, animation, AI, pickups, triggers, enemy-specific AI in `Components/EnemyAI/`, etc.). `ActorFactory` builds actors from XML definitions — actor prototypes live as XML in `Build_Release/ASSETS/ACTOR_PROTOTYPES/` — while `ActorTemplates.cpp` creates common actors (projectiles, pickups, sounds) programmatically. Adding a new component means registering its creator in `ActorFactory` and adding it to the component CMakeLists.
- **Global event system** (`Engine/Events/`): decouples the layers. Logic/physics broadcast events (actor created, moved, took damage…); views and components subscribe via delegates. Game-specific events are in `ClawEvents.h`. Almost all inter-system communication goes through `IEventMgr`.
- **Resource system** (`Engine/Resource/`): `ResourceCache` with pluggable loaders (`Resource/Loaders/`) serving data from two archives: `CLAW.REZ` (original game formats, parsed by **libwap**) and `ASSETS.ZIP` (XML actor prototypes, level metadata, menus).
- **libwap** (`libwap/`): standalone library parsing original Claw file formats — REZ archives, WWD (level/world), PID (images), PAL (palettes), ANI (animations), XMI (MIDI).
- **Physics** (`Engine/Physics/`): Box2D wrapper implementing `IGamePhysics`; physics results flow back into components/logic via events.

Conventions: `SharedDefines.h` is the common include for engine code (typedefs, smart-pointer aliases, logger, XML macros). Virtual interface methods use a `V` prefix (`VOnUpdate`, `VCreateActor`); interfaces are declared in `Engine/Interfaces.h`. Data-driven configuration is pervasive — game behavior changes are often XML changes under `Build_Release/ASSETS/` rather than code changes.

When adding/removing source files, update both the directory's `CMakeLists.txt` and the VS project (`OpenClaw/OpenClaw.vcxproj`) — both build systems are maintained in parallel.
