---
name: godot-cli
description: >
  Build, test, and debug complete video games using the Godot engine via CLI
  (godot-cli). Use this skill when the user wants to create, modify, run, or
  debug a Godot game project — even if they don't explicitly mention
  "godot-cli" or "daemon." Includes scene editing, input injection, asset
  import, screenshot capture, and C# / Mono support. The game MUST be
  fully playable, winnable, and not just a prototype.
allowed-tools: Bash(godot-cli:*) Bash(godot:*) Bash(./bin/godot*:*)
---

# Game Development with godot-cli

Build complete, playable, polished games using Godot Engine via CLI. The game **must work visually** and **must be winnable** — no prototypes.

## Diagnostic workflow (no vision required)

Use these commands to detect visual bugs without looking at the game:

```bash
# 1. Detect all overlapping visual nodes
# This catches: player-behind-ground, gem-overlapping-platform, UI-clipping
godot-cli debug/visual_overlaps
# → Shows every pair of overlapping visual nodes with overlap rect

# 2. Inspect canvas layers and z-index
# Catches: CanvasLayer bleed (dark rectangle), wrong draw order
godot-cli debug/render_layers

# 3. Get full scene tree with computed visual bounds
# Each node shows its screen-space bounds (x, y, width, height)
# Useful for finding: objects extending below viewport, off-screen elements
godot-cli scene/tree
# Look for "visual_bounds" in the output

# 4. Validate level layout (detects thin platforms, missing bounds)
godot-cli game/check_level

# 5. Read C# script variables (check ScoreManager.TotalCoins, game state)
godot-cli debug/script_vars /root/Game

# 6. Check for runtime errors (missing inputs, null references)
godot-cli debug/errors
```

```bash
# 1. Open your project
godot-cli open --project ./my-game

# 2. Validate before running (catches missing input actions, scenes, etc.)
godot-cli project/validate

# 3. Run the game
godot-cli game/run

# 4. Test with input
godot-cli input/key KEY_D true
godot-cli input/key KEY_SPACE true

# 5. Take a screenshot and inspect it
godot-cli render/screenshot '{"file":"screenshot.png"}'
# Visually check the screenshot for bugs

# 6. Check for runtime errors
godot-cli debug/errors

# 7. Iterate: edit code → build → close → open → go to step 2
```

## Iteration Loop

Every code change requires a full cycle:

```
1. Edit C# / GDScript / .tscn
2. dotnet build                (C# only)
3. godot-cli close             (kill daemon)
4. godot-cli open --project .  (restart)
5. godot-cli project/validate  (check for common setup bugs)
6. godot-cli game/run
7. godot-cli game/check_level          (detect thin platforms, unreachable coins)
8. godot-cli debug/visual_overlaps  (detect overlapping visuals)
9. godot-cli debug/render_layers    (check draw order)
10. godot-cli debug/script_vars /root/Game (read C# state)
11. godot-cli debug/errors           (check runtime errors)
12. godot-cli scene/tree --compact   (check positions and visual bounds)
13. Goto 1 if anything is wrong
```

The diagnostic steps (7-10) replace looking at a screenshot. A model without vision can
use these to catch visual bugs: player behind ground, platform overlap, UI clipping.

## Commands

### Session Management

| Command | Purpose |
|---------|---------|
| `godot-cli open --project .` | Start daemon in project directory |
| `godot-cli open --headless` | Start without window (faster) |
| `godot-cli close` | Kill daemon |
| `godot-cli -s=NAME open` | Named session (multi-agent) |
| `godot-cli list` | List active sessions |

### Scene Editing

| Command | Purpose |
|---------|---------|
| `godot-cli scene/tree` | View full scene tree with positions, visual bounds, texture status |
| `godot-cli scene/tree compact:true` | **Compact mode** — names, types, positions, bounds only (much shorter output) |
| `godot-cli scene/get /path prop` | Get a single property |
| `godot-cli scene/set /path prop val` | Set a property at runtime |
| `godot-cli scene/add_node /parent Type '{"name":"N"}'` | Add node |
| `godot-cli scene/remove_node /path` | Remove node |
| `godot-cli scene/attach_script /path res://script.cs` | Attach script |
| `godot-cli scene/connect '{"path":"/n","signal":"s","method":"m"}'` | Connect signal |

### Game Runtime

| Command | Purpose |
|---------|---------|
| `godot-cli game/run '{"scene":"res://main.tscn"}'` | Load and run scene |
| `godot-cli game/stop` | Stop game |
| `godot-cli game/pause` / `game/resume` | Pause/resume |
| `godot-cli game/step 30` | Advance N frames |

### Input Injection

| Command | Purpose |
|---------|---------|
| `godot-cli input/key KEY_D true` | Press/release a key |
| `godot-cli input/mouse_move '{"x":400,"y":300}'` | Move mouse |
| `godot-cli input/mouse_button '{"button":"left","pressed":true}'` | Click |
| `godot-cli input/action jump true` | Trigger input action |

### Debugging

| Command | Purpose |
|---------|---------|
| `godot-cli debug/errors` | **Run this first when something breaks** |
| `godot-cli debug/logs` | View recent CLI operation logs |
| `godot-cli debug/inspect /path` | Deep inspect a node |
| `godot-cli debug/monitor fps` | Check FPS, memory, draw calls |
| `godot-cli debug/visual_overlaps` | **Detect overlapping visual nodes** — catches player-behind-ground, gem-overlapping-platform, UI clipping (no vision needed) |
| `godot-cli debug/render_layers` | **Show canvas layers and z-index info** — catches CanvasLayer bleed, wrong draw order |
| `godot-cli debug/script_vars /path` | **Read C# script properties at runtime** — check ScoreManager.TotalCoins, state values |
| `godot-cli game/check_level` | **Validate level layout** — detects thin platforms (<10px), unreachable coins, missing bounds |

### Capture

| Command | Purpose |
|---------|---------|
| `godot-cli render/screenshot '{"file":"shot.png"}'` | Screenshot (uses `file` param, not `path`) |

### Project

| Command | Purpose |
|---------|---------|
| `godot-cli project/validate` | **Run first** — checks input actions, main scene, assets |
| `godot-cli project/settings` | List/get/set project settings |
| `godot-cli project/settings '{"action":"get","key":"app..."}'` | Get specific setting |

### Input Bindings

Always use the daemon, never edit project.godot directly:

```bash
godot-cli project/settings '{"action":"set","key":"input/move_right","value":{"deadzone":0.5,"events":[Object(InputEventKey,"keycode":68)]}}'
```

## Key Gotchas (from painful experience)

### C# Game Development

- **Every Godot-derived class needs `partial`**: `public partial class Player : CharacterBody2D` — not `public class Player : CharacterBody2D`
- **`partial` is required**: Godot 4 C# source generators need it. Without it, signals and other Godot features won't work.
- **Build and restart**: Every C# change needs `dotnet build` then kill and restart the daemon. There is no hot-reload.
- **C# signals**: Use `SignalName.Collected` syntax: `EmitSignal(SignalName.Collected)`. Don't use the old `EmitSignal("collected")` string syntax. Connect with `coin.Collected += Handler`.
- **C# signal warning "Can't get method on CallableCustom"**: This is a Godot 4.8 Mono bug. It fires when C# signal delegates are connected using `+=` syntax inside `_Ready()`. The signals still work despite the error. To suppress, use `Callable.From(() => Handler())` instead of direct `+=`.
- **`using System;` required** for `Action`, `Action<>`, `EventHandler` types. These live in `System` namespace.
- **Private fields are invisible to debug/script_vars**: Only `[Signal]` delegates and `[Export]` properties show up. Plain `public int Score { get; set; }` won't appear. Make key state variables `[Export]` or create a debug method.
- **ScoreManager.TotalCoins must be set explicitly**: `ScoreManager.TotalCoins` defaults to 0. The `AllCollected` check (`Score >= TotalCoins && TotalCoins > 0`) will NEVER trigger if you don't set `TotalCoins = level.CoinCount` in `PlayingState.Enter()`.

### Input Actions

- **`InputMap` actions must be defined** before the game starts. `IsActionPressed("jump")` will error if `jump` doesn't exist.
- **Use `project/validate`** to catch missing input actions.
- **D and A keycodes**: KEY_D=68, KEY_A=65, KEY_W=87, KEY_SPACE=32, KEY_ESCAPE=4194305, KEY_LEFT=4194319, KEY_RIGHT=4194321.

### Visual / Rendering

- **CanvasLayer has a separate canvas** with its own clear color. If you see a dark rectangle in the corner, that's the CanvasLayer's canvas background. Use a regular Node2D at ZIndex=-100 instead.
- **Node `ZIndex` only works on CanvasItem subclasses**. Set ZIndex on sprites, not on non-visual parents.
- **`_Draw()` is not available on CanvasLayer**. Use a child Node2D for custom drawing.

### Scene / Nodes

- **Node groups in .tscn files are ignored at runtime**: `groups = ["coins"]` in a `.tscn` file does nothing. Always call `AddToGroup("coins")` / `add_to_group("coins")` in `_Ready()`.
- **Collision shapes must be created in `_Ready()`**: Setting shapes via `scene/set` with a `resource_type` dictionary doesn't work reliably. Create them in code.

### Screenshots

- **Parameter is `file` not `path`**: `godot-cli render/screenshot '{"file":"shot.png"}'` (the code takes `file` but the old docs said `path` — don't be fooled).
- **Always inspect the screenshot visually** after every major change. Don't assume it looks right.

### Platformer Physics

Jump height = `v² / (2g)`. For a 216px max jump with g=980:
```csharp
jump_velocity = -650f;
gravity = 980f;
```
The player collision should be smaller than the visual sprite (so the player fits through tight spaces). Visual can be 2-3x scaled.

## Project Structure for C# Games

```
my-game/
├── Platformer.sln
├── Platformer.csproj
├── project.godot
├── main.tscn
├── GameManager.cs            # Composition root
├── Core/                     # Pure logic, zero Godot deps
│   ├── StateMachine.cs
│   ├── LevelData.cs
│   └── IState.cs
├── States/                   # Game state pattern
├── Managers/                 # Single responsibility services
├── Scenes/                   # Self-contained entities
│   ├── Player/
│   ├── Coin/
│   └── Platform/
└── Visuals/                  # Rendering helpers
```

## Binaries

| Variant | Binary | Use when |
|---------|--------|----------|
| GDScript | `./godot.linuxbsd.editor.x86_64` | Fastest builds, no C# needed |
| Mono (C#) | `./godot.linuxbsd.editor.x86_64.mono` | C# / .NET projects |

Set `DOTNET_ROOT=$HOME/.dotnet` for Mono builds.

## Reference Files

- **`references/csharp-workflow.md`** — C# build/debug cycle, common errors, namespace conventions
- **`references/platformer-guide.md`** — Complete platformer architecture with SOLID patterns
- **`references/scene-creation.md`** — Detailed scene editing with godot-cli commands

## Scripts

- **`scripts/validate.sh`** — Validate project before running (checks build, inputs, assets)
- **`scripts/screenshot.py`** — Take screenshot and analyze for common visual bugs
- **`scripts/build-and-test.sh`** — Full iteration cycle: build → restart → screenshot → check errors

## Extended Commands

| Command | Purpose |
|---------|---------|
| `godot-cli debug/script_vars /path` | Read C# script properties at runtime — catch TotalCoins mismatches, score state |
| `godot-cli game/check_level` | Validates level — detects thin platforms (<10px), unreachable coins, bounds |
