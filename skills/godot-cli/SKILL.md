---
name: godot-cli
description: Build and debug complete video games using the Godot engine via CLI. Create scenes, write scripts, run games, inject input, capture screenshots, and debug — all from the command line. Use when the user wants to create, modify, test, or debug a Godot game project.
allowed-tools: Bash(godot-cli:*) Bash(godot:*) Bash(./bin/godot*:*)
---

# Game Development with godot-cli

## Quick start

```bash
# Open a project (starts Godot daemon in background)
godot-cli open --project ./my-game

# Explore the current scene tree (returns snapshot with node refs n0, n1...)
godot-cli scene/tree

# Add a sprite node
godot-cli scene/add_node root Sprite2D '{"position":[100,200],"name":"Player"}'

# Write a script
godot-cli script/write player.gd 'extends CharacterBody2D
@export var speed := 200.0
func _physics_process(delta):
    var dir = Input.get_axis("ui_left", "ui_right")
    if dir: velocity.x = dir * speed
    else: velocity.x = move_toward(velocity.x, 0.0, speed)
    move_and_slide()'

# Attach script to node
godot-cli scene/attach_script /root/Game/Player res://player.gd

# Run the game
godot-cli game/run

# Move the player
godot-cli input/action ui_right true

# Take a screenshot
godot-cli render/screenshot

# Check for errors
godot-cli debug/logs

# Stop the daemon
godot-cli close
```

## Commands

### Session Management

```bash
# Start a daemon session
godot-cli open --project ./my-game
godot-cli open --project ./my-game --headless         # no window
godot-cli open --project ./my-game --binary ./godot    # use specific binary

# Close the current session
godot-cli close

# Named sessions (for multi-agent / multi-worktree)
godot-cli -s=level-designer open --project ./my-game
godot-cli -s=level-designer scene/tree

# List active sessions
godot-cli list
```

### Scene Editing

```bash
# View scene tree (returns snapshot with node refs)
godot-cli scene/tree
# → n0  root [Window]
#     n1  Game [Node2D]
#       n2  Player [CharacterBody2D] pos=Vector2(100, 200)
#         n3  CollisionShape2D [CollisionShape2D]
#       n4  Ground [StaticBody2D] pos=Vector2(320, 470)

# Add nodes
godot-cli scene/add_node root Node2D '{"name":"Game"}'
godot-cli scene/add_node /root/Game CharacterBody2D '{"name":"Player","position":[100,200]}'
godot-cli scene/add_node /root/Game/Player CollisionShape2D '{"name":"CollisionShape2D"}'
godot-cli scene/add_node /root/Game StaticBody2D '{"name":"Ground","position":[320,470]}'

# Remove nodes
godot-cli scene/remove_node /root/Game/OldNode

# Inspect node properties
godot-cli scene/get /root/Game/Player
godot-cli scene/get /root/Game/Player position       # single property

# Modify node properties
godot-cli scene/set /root/Game/Player position '[300,150]'
godot-cli scene/set /root/Game/Player/CollisionShape2D shape '{"resource_type":"RectangleShape2D","size":[16,30]}'
godot-cli scene/set /root/Game/Player speed 300

# Create and save scenes
godot-cli scene/new          # clear current scene
godot-cli scene/save         # save current scene
godot-cli scene/save '{"path":"res://levels/level1.tscn"}'

# Attach scripts to nodes
godot-cli scene/attach_script /root/Game/Player res://player.gd

# Connect signals
godot-cli scene/connect '{"path":"/root/Game/Coin","signal":"body_entered","method":"_on_body_entered"}'
godot-cli scene/connect '{"path":"/root/Game/Coin","signal":"collected","target":"/root/Game","method":"add_score"}'
```

### Scripting

```bash
# Write GDScript files
godot-cli script/write player.gd 'extends CharacterBody2D
func _ready(): print("hello")'

# Read script contents
godot-cli script/read player.gd

# Validate script syntax
godot-cli script/validate player.gd
godot-cli script/validate '{"source":"extends Node\\nfunc _ready(): pass"}'
```

### Game Runtime

```bash
# Run the game (loads scene into the running engine)
godot-cli game/run
godot-cli game/run '{"scene":"res://main.tscn"}'

# Stop the game
godot-cli game/stop

# Pause / Resume
godot-cli game/pause
godot-cli game/resume

# Advance N frames
godot-cli game/step 30         # advance 30 frames
godot-cli game/step '{"frames":60}'
```

### Input Injection

```bash
# Keyboard
godot-cli input/key KEY_W true         # press W
godot-cli input/key KEY_W false        # release W
godot-cli input/key KEY_SPACE true

# Mouse
godot-cli input/mouse_move '{"x":400,"y":300}'
godot-cli input/mouse_button '{"button":"left","pressed":true}'

# Input actions (from project's Input Map)
godot-cli input/action ui_right true
godot-cli input/action ui_jump true
godot-cli input/action ui_jump false
```

### Debugging

```bash
# View logs (operations performed through godot-cli)
godot-cli debug/logs
godot-cli debug/logs '{"count":20}'

# Check runtime errors
godot-cli debug/errors

# Deep inspect any node
godot-cli debug/inspect /root/Game/Player
godot-cli debug/inspect /root/Game/Ground/CollisionShape2D

# Performance monitors
godot-cli debug/monitor fps
godot-cli debug/monitor objects
godot-cli debug/monitor draw_calls
godot-cli debug/monitor frame_time
```

### Capture

```bash
# Take a screenshot (requires non-headless display)
godot-cli render/screenshot
# Returns base64-encoded PNG in the "data" field
# Use --raw to get just the image data

# Save screenshot to file
godot-cli render/screenshot '{"file":"screenshot.png"}'
```

### Resources

```bash
# List project files
godot-cli resource/list
godot-cli resource/list '{"path":"res://scenes/"}'

# Import assets
godot-cli resource/import '{"source":"~/Downloads/spritesheet.png","dest":"res://assets/spritesheet.png"}'

# Create materials
godot-cli resource/create_material '{"name":"wall_material","type":"StandardMaterial3D","properties":{"albedo_color":{"r":0.8,"g":0.2,"b":0.2},"metallic":0.1,"roughness":0.8},"save_path":"res://wall.tres"}'

# Create meshes
godot-cli resource/create_mesh '{"name":"custom_mesh","properties":{"vertices":[[0,0,0],[1,0,0],[0,1,0]],"indices":[0,1,2]},"save_path":"res://triangle.tres"}'

# Create animations
godot-cli resource/create_animation '{"name":"bounce","properties":{"length":1.0,"loop_mode":"linear"},"save_path":"res://bounce.tres"}'
```

### Project Configuration

```bash
# List project settings
godot-cli project/settings

# Get/set project settings
godot-cli project/settings '{"action":"get","key":"application/config/name"}'
godot-cli project/settings '{"action":"set","key":"application/config/name","value":"MyGame"}'
```

### Input Bindings

⚠️ **Important:** Input bindings cannot be written as JSON dictionaries in project.godot:
```ini
# THIS DOES NOT WORK:
[input]
move_right={"deadzone":0.5,"events":[{"type":"key","keycode":"KEY_D"}]}
```

Godot 4 requires full resource serialization. **Always use the daemon's `project/settings` command to create input bindings**:
```bash
godot-cli project/settings '{"action":"set","key":"input/move_right","value":{"deadzone":0.5,"events":[{"type":"key","keycode":"KEY_D"}]}}'
```

Numeric keycodes: KEY_A=65, KEY_D=68, KEY_W=87, KEY_SPACE=32, KEY_LEFT=4194319, KEY_RIGHT=4194321

## Snapshots

After every command, godot-cli returns a structured snapshot of the current scene tree with node refs (n0, n1, n2...) that you can use to target subsequent commands.

```bash
> godot-cli scene/tree

── Scene Tree ───────────────────────────────────
  FPS: 145.0  Nodes: 3  RUNNING

  n0  Game  [Node2D]  pos=Vector2(0, 0)
  n1  Player  [CharacterBody2D]  pos=Vector2(100, 200)  script=player.gd
    n2  CollisionShape2D  [CollisionShape2D]  pos=Vector2(0, 0)
  n3  Ground  [StaticBody2D]  pos=Vector2(320, 470)
```

Use the refs (n0, n1...) to identify nodes. The actual node paths (like `/root/Game/Player`) are shown in the `path` field of each node. Always prefer using paths like `/root/Game/Player` over refs when specifying target nodes, since refs change between commands.

## Node Targeting

Node paths use Godot's path syntax:
- `/root/Game/Player` - absolute path from root
- `Player` - relative path (only works if unique)
- Use the refs from snapshots for identification, but paths for commands

## Collision Shapes

To set collision shapes on CollisionShape2D nodes, use one of these approaches:

**Option A: Create shape resource file and load it**
```bash
# Write a .tres file
godot-cli script/write player_shape.tres '[gd_resource type="RectangleShape2D" format=3]
[resource]
size = Vector2(16, 30)'

# Set the shape property to the resource file path
godot-cli scene/set /root/Game/Player/CollisionShape2D shape res://player_shape.tres
```

**Option B: Create shape in GDScript _ready()** (more reliable)
```gdscript
extends CharacterBody2D
func _ready():
    var s := RectangleShape2D.new()
    s.size = Vector2(16, 30)
    $CollisionShape2D.shape = s
```

**Option C: Resource type dictionary** (inline creation)
```bash
godot-cli scene/set /root/Game/Player/CollisionShape2D shape '{"resource_type":"RectangleShape2D","size":[16,30]}'
```

Supported shape types: `RectangleShape2D`, `CircleShape2D`, `CapsuleShape2D`, `WorldBoundaryShape2D`, `SegmentShape2D`

## Visuals

Create simple textures programmatically in GDScript:

```gdscript
# Create a colored rectangle texture
var img := Image.create(width, height, false, Image.FORMAT_RGBA8)
img.fill(Color(r, g, b, a))
var tex := ImageTexture.create_from_image(img)
$Sprite2D.texture = tex
```

## Raw output

Use `--raw` to get only the JSON result for piping into other tools:

```bash
godot-cli --raw debug/monitor fps | jq '.fps'
godot-cli --raw scene/tree > scene_state.json
godot-cli --raw render/screenshot > screenshot.b64  # base64 PNG
SCREENSHOT=$(godot-cli --raw render/screenshot)
```

## Open Parameters

```bash
# Open with specific project
godot-cli open --project ./my-game

# Run headless (no window)
godot-cli open --headless

# Use a specific Godot binary
godot-cli open --binary ./bin/godot.linuxbsd.editor.x86_64

# Verbose output
godot-cli open --verbose

# Custom port (default: auto)
godot-cli open --port 3100
```

## Sessions

```bash
# Named sessions for multi-agent workflows
godot-cli -s=designer open --project ./game
godot-cli -s=dev open --project ./game --headless

# List all sessions
godot-cli list

# Each session is independent (separate Godot process)
# Useful for: one agent edits scene, another tests gameplay
```

## Installation

The `godot-cli` command is at `/home/andres/src/godot/misc/scripts/godot-cli.py`.
A symlink is available at `/home/andres/.local/bin/godot-cli`.

The forked Godot binary is at:
- `/home/andres/src/godot/bin/godot.linuxbsd.editor.x86_64` (GDScript)
- `/home/andres/src/godot/bin/godot.linuxbsd.editor.x86_64.mono` (C# / .NET)

Set `GODOT_CLI_BIN` environment variable to use a specific binary:
```bash
export GODOT_CLI_BIN=./bin/godot.linuxbsd.editor.x86_64
```

## Examples

### Create a platformer game

```bash
# 1. Create project
godot-cli open --project ./platformer

# 2. Write scripts
godot-cli script/write player.gd 'extends CharacterBody2D
@export var speed := 200.0
@export var jump_velocity := -400.0
func _physics_process(delta):
    if not is_on_floor():
        velocity.y += 980.0 * delta
    if Input.is_action_just_pressed("ui_jump") and is_on_floor():
        velocity.y = jump_velocity
    var dir := Input.get_axis("ui_left", "ui_right")
    if dir:
        velocity.x = dir * speed
    else:
        velocity.x = move_toward(velocity.x, 0.0, speed)
    move_and_slide()'

godot-cli script/write game.gd 'extends Node2D
var score := 0
func _ready():
    for c in get_tree().get_nodes_in_group("coins"):
        c.collected.connect(_on_coin_collected)
func _on_coin_collected():
    score += 1
    $UI/ScoreLabel.text = "Score: " + str(score)'

# 3. Build scene
godot-cli scene/new
godot-cli scene/add_node root Node2D '{"name":"Game"}'
godot-cli scene/attach_script /root/Game res://game.gd
godot-cli scene/add_node /root/Game CharacterBody2D '{"name":"Player","position":[100,200]}'
godot-cli scene/add_node /root/Game/Player CollisionShape2D '{}'
godot-cli scene/attach_script /root/Game/Player res://player.gd

# 4. Run and test
godot-cli game/run
godot-cli input/action ui_right true
godot-cli game/step 30
godot-cli scene/get /root/root/Game/Player position
godot-cli close
```

### Debug a broken script

```bash
# Check logs
godot-cli debug/logs

# Inspect runtime state
godot-cli debug/inspect /root/Game/Player

# Check performance
godot-cli debug/monitor fps

# Fix script and reload
godot-cli script/write player.gd '...fixed code...'
godot-cli game/run  # reloads scene
```

### Multi-agent workflow

```bash
# Agent A: builds the level
godot-cli -s=level open --project ./game --headless
godot-cli -s=level scene/add_node /root/Game StaticBody2D '{"name":"Ground","position":[320,470]}'
godot-cli -s=level close

# Agent B: writes player behavior
godot-cli -s=code open --project ./game --headless
godot-cli -s=code script/write player.gd '...'
godot-cli -s=code scene/attach_script /root/Game/Player res://player.gd
godot-cli -s=code close

# Agent C: tests gameplay
godot-cli -s=test open --project ./game --headless
godot-cli -s=test game/run
godot-cli -s=test input/action ui_right true
godot-cli -s=test debug/logs
godot-cli -s=test close
```

## Specific tasks

* **Creating scenes and nodes** [references/scene-creation.md](references/scene-creation.md)
* **Writing and debugging GDScript** [references/gdscript-guide.md](references/gdscript-guide.md)
* **Game testing and input** [references/game-testing.md](references/game-testing.md)
* **Building a complete game** [references/game-tutorial.md](references/game-tutorial.md)
