# Writing and Debugging GDScript

## Writing scripts

```bash
godot-cli script/write <path> <source>
```

The path is relative to the project root. Include `res://` prefix or not (it's added automatically if missing).

```bash
# Write a simple script
godot-cli script/write player.gd 'extends CharacterBody2D
@export var speed := 200.0

func _physics_process(delta):
    var dir := Input.get_axis("ui_left", "ui_right")
    if dir:
        velocity.x = dir * speed
    else:
        velocity.x = move_toward(velocity.x, 0.0, speed)
    move_and_slide()'
```

## Attaching scripts to nodes

```bash
# Attach by file path
godot-cli scene/attach_script /root/Game/Player res://player.gd

# Write inline and attach in one step
godot-cli scene/attach_script /root/Game/Player res://player.gd '{"source":"extends CharacterBody2D..."}'
```

## Reading scripts

```bash
godot-cli script/read player.gd
# Returns: path, source, bytes
```

## Validating scripts

```bash
godot-cli script/validate player.gd
# Returns: valid (true/false), error_count, errors[]
```

## Common Script Patterns

### Player movement
```gdscript
extends CharacterBody2D
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
    move_and_slide()
```

### Creating textures programmatically
```gdscript
# Create a colored rectangle texture
var img := Image.create(width, height, false, Image.FORMAT_RGBA8)
img.fill(Color(r, g, b, a))
var tex := ImageTexture.create_from_image(img)
$Sprite2D.texture = tex
```

### Collectible coin
```gdscript
extends Area2D
signal collected
func _ready():
    var s := CircleShape2D.new()
    s.radius = 8
    $CollisionShape2D.shape = s
    var img := Image.create(16, 16, false, Image.FORMAT_RGBA8)
    img.fill(Color(1, 0.8, 0, 1))
    $Sprite2D.texture = ImageTexture.create_from_image(img)
func _on_body_entered(body):
    if body is CharacterBody2D:
        collected.emit()
        queue_free()
```

### Game manager
```gdscript
extends Node2D
var score := 0
func _ready():
    for c in get_tree().get_nodes_in_group("coins"):
        c.collected.connect(_on_coin_collected)
func _on_coin_collected():
    score += 1
    $UI/ScoreLabel.text = "Score: " + str(score)
```

## Debugging Scripts

```bash
# Check runtime errors
godot-cli debug/errors

# Check engine logs
godot-cli debug/logs

# Inspect node at runtime
godot-cli debug/inspect /root/root/Game/Player

# Check if on floor, velocity, position
godot-cli scene/get /root/root/Game/Player position
```

## Signal Connections

```bash
# Connect body_entered on coin to its own handler
godot-cli scene/connect '{"path":"/root/Game/Coin1","signal":"body_entered","method":"_on_body_entered"}'

# Connect custom signal to game manager
godot-cli scene/connect '{"path":"/root/Game/Coin1","signal":"collected","target":"/root/Game","method":"_on_coin_collected"}'
```
