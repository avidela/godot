# Building a Platformer with godot-cli (C#)

## Architecture (SOLID)

```
Core/           — Pure data, no Godot deps (LevelData, StateMachine, GameSettings)
Interfaces/     — IState, ICollectible, IRespawnable
States/         — PlayingState, PausedState, WonState (state pattern)
Managers/       — Single Responsibility: Sound, Score, Level, UI
Scenes/         — Self-contained entities (Player, Coin, Platform, Ground, Killzone)
Visuals/        — Rendering helpers (BackgroundDrawer, ParticlePool, ScreenShake)
GameManager.cs  — Composition root (wires everything)
main.tscn       — Just the GameManager node
```

## Key Rules

1. **No global node lookups** — No `GetNode("/root/Game/...")` in managers. Inject dependencies via constructor or Initialize().
2. **Each scene is self-contained** — Player.cs owns its collision, sprite, animation. Create children in `_Ready()`.
3. **Events for decoupling** — Signals, not direct references. `CoinCollected += Handler`.
4. **State machine for everything** — Game states (Playing/Paused/Won) AND entity states (Idle/Run/Jump/Fall).

## Entity State Machine (Player)

```
PlayerStateMachine
├── IdleState     — standing still, squash recovery, transitions to Run/Jump/Fall
├── RunState      — ground movement, animation, transitions to Idle/Jump/Fall
├── JumpState     — upward velocity, air control, transitions to Fall
└── FallState     — falling with coyote time, transitions to Idle/Run/Jump
```

Implementation pattern:

```csharp
// PlayerContext.cs — shared data
public sealed class PlayerContext {
    public required CharacterBody2D Body { get; init; }
    public required AnimatedSprite2D Sprite { get; init; }
    public float Direction { get; set; }
    public bool JumpPressed { get; set; }
    // ... physics constants, state flags
}

// PlayerState.cs — abstract base
public abstract class PlayerState {
    protected PlayerContext Ctx;
    public abstract void Enter();
    public abstract void Exit();
    public abstract string? Update(double delta);       // returns next state or null
    public abstract string? PhysicsUpdate(double delta);
}

// Usage in Player._PhysicsProcess:
ctx.Direction = Input.GetAxis("move_left", "move_right");
ctx.JumpPressed = Input.IsActionJustPressed("jump");
stateMachine.PhysicsUpdate(delta);
```

## Level Data (pure data, no Godot types)

```csharp
public static class LevelData {
    public record Level(string Name, int CoinCount,
        PlatformDef[] Platforms, CoinDef[] Coins);

    public static readonly Level[] Levels = new[] {
        new Level("Green Hills", 3,
            new[] { new PlatformDef(160, 340, 120), ... },
            new[] { new CoinDef(160, 315), ... }),
        // ... more levels
    };
}
```

## Ground / Platform Collision Setup

- Ground collision: `new RectangleShape2D { Size = new Vector2(640, 40) }`
- Ground visual: thin grass strip (16px tall) at y=-20 relative to node center
- Platform collision: `new RectangleShape2D { Size = new Vector2(120, 24) }`
- Platform visual: position at y=-12 relative to node center
- Player collision: `new RectangleShape2D { Size = new Vector2(14, 20) }`
- **Visual must be THINNER than collision** so player stands on top, not behind

## Level Layout Guidelines

- First platform: easily reachable from ground (~150px up, ~80px right)
- Gems: sit at platform Y minus 25px (bobbing adds ±3px)
- Gaps: max horizontal gap ~150px, vertical gap ~70px for comfortable jumping
- Walls: left at x=-20, right at x=660 to prevent falling out of bounds
- Killzone: at y=520 to catch falls
