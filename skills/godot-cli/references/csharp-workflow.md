# C# Game Development with godot-cli

## Build Cycle

Every C# code change needs this exact sequence:

```bash
# 1. Build
dotnet build

# 2. Kill daemon
godot-cli close
# If that fails: kill -9 $(pgrep -f godot)

# 3. Restart
godot-cli open --project .

# 4. Validate
godot-cli project/validate

# 5. Run and test
godot-cli game/run
```

**There is no hot-reload for C#.** The daemon must restart after every build.

## Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Missing partial modifier` | Class needs `partial` keyword | `public partial class Player : CharacterBody2D` |
| `Action not found` | Missing `using System;` | Add `using System;` at top of file |
| `CS0104 ambiguous reference` | Type name collides with Godot type | Use full namespace (e.g. `Platformer.Visuals.GameBackground`) |
| `CS1061 no definition for X` | Missing `using` for namespace | Add `using Platformer.Scenes;` etc. |
| `GD.Load failed` | Asset not imported | Use `Image.LoadFromFile()` + `ImageTexture.CreateFromImage()` for runtime-loaded assets |

## C# Signal Pattern

### Define signals (Godot 4 C# style):

```csharp
[Signal] public delegate void CollectedEventHandler();
```

### Emit signals:

```csharp
EmitSignal(SignalName.Collected);  // correct
EmitSignal("collected");           // wrong — string syntax deprecated
```

### Connect signals:

```csharp
// Instance method (in _Ready):
coin.Collected += OnCoinCollected;

// Lambda:
coin.Collected += () => Score.AddCoin();

// Godot-style:
coin.Connect("collected", Callable.From(() => HandleCoin()));
```

### Godot signal errors:

If you see `Can't get method on CallableCustom "CharacterBody2D(Player.cs)::EventSignalMiddleman::Jumped"`, the signal was connected incorrectly. Use the `+=` C# event pattern, not string-based `Connect()`.

## Namespace Convention

```
Platformer.Core         → StateMachine, LevelData, GameSettings
Platformer.Interfaces   → IState, ICollectible, IRespawnable
Platformer.States       → PlayingState, PausedState, WonState
Platformer.Managers     → SoundManager, ScoreManager, LevelManager
Platformer.Scenes       → Player, Coin, Platform, Ground, Killzone
Platformer.Visuals      → BackgroundDrawer, ParticlePool, ScreenShake
```

## Loading Assets at Runtime

Assets added outside the Godot editor (e.g. downloaded PNGs in `assets/`) aren't imported. Use filesystem loading:

```csharp
var image = Image.LoadFromFile("assets/sprites/my_sprite.png");
var texture = ImageTexture.CreateFromImage(image);
sprite.Texture = texture;
```

## Physics Units

- Viewport: 640×480 pixels
- Gravity: 980 px/s² (Godot default)
- Jump velocity: -650 px/s → max height ≈ 216 px
- Player speed: 250 px/s
- Ground collision: 640 wide, 40 tall
