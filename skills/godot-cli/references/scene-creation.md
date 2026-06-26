# Scene Creation

## Creating a new scene

```bash
godot-cli scene/new
```

This clears the current scene tree. All existing nodes are removed.

## Adding nodes

```bash
godot-cli scene/add_node <parent_path> <node_type> <properties_json>
```

Parameters:
- `parent_path`: Path to parent node (e.g. `root`, `/root/Game`)
- `node_type`: Godot class name (e.g. `Sprite2D`, `CharacterBody2D`, `StaticBody2D`, `Area2D`, `Node2D`, `Camera2D`, `Label`, `CanvasLayer`, `CollisionShape2D`)
- `properties_json`: JSON object with property values

Examples:

```bash
# Simple node
godot-cli scene/add_node root Node2D '{"name":"Game"}'

# Node with position
godot-cli scene/add_node /root/Game Sprite2D '{"name":"PlayerSprite","position":[100,200]}'

# Node with multiple properties
godot-cli scene/add_node /root/Game Label '{"name":"ScoreLabel","position":[10,10],"text":"Score: 0","theme_override_colors/font_color":[1,1,1,1]}'
```

## Recommended Node Hierarchy

```
root (Window)
└── Game (Node2D)            # Root game node
    ├── Background (ColorRect)  # Background fill
    ├── Camera2D              # Game camera
    ├── Player (CharacterBody2D)  # Player character
    │   ├── CollisionShape2D  # Hitbox
    │   └── Sprite2D          # Visual
    ├── Ground (StaticBody2D) # Floor
    │   ├── CollisionShape2D
    │   └── Sprite2D
    ├── Platform1 (StaticBody2D)  # Platforms
    │   ├── CollisionShape2D
    │   └── Sprite2D
    ├── Coin1 (Area2D)        # Collectibles
    │   ├── CollisionShape2D
    │   └── Sprite2D
    └── UI (CanvasLayer)      # UI layer
        └── ScoreLabel (Label)
```

## Node Types Reference

| Type | Use | Properties |
|------|-----|------------|
| Node2D | Root container | position, rotation, scale |
| CharacterBody2D | Player, enemies | position, velocity |
| StaticBody2D | Platforms, walls | position |
| Area2D | Triggers, zones | position, gravity |
| Sprite2D | Visual element | texture, position, flip_h |
| CollisionShape2D | Physics shape | shape (resource) |
| Camera2D | Viewport camera | position, zoom |
| Label | Text display | text, position, font_color |
| ColorRect | Colored rectangle | color, size |
| CanvasLayer | UI overlay | layer |
