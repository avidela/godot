#!/bin/bash
# Validate a Godot project before running.
# Usage: bash scripts/validate.sh [project-dir]
set -euo pipefail

PROJECT="${1:-.}"
echo "=== Validating $PROJECT ==="

# Check project.godot exists
if [ ! -f "$PROJECT/project.godot" ]; then
  echo "ERROR: No project.godot found in $PROJECT"
  exit 1
fi

# Check main scene exists
MAIN_SCENE=$(grep 'run/main_scene' "$PROJECT/project.godot" 2>/dev/null | head -1 | sed 's/.*="//;s/"//')
if [ -z "$MAIN_SCENE" ]; then
  echo "ERROR: No main scene configured in project.godot"
  exit 1
fi
echo "  Main scene: $MAIN_SCENE"

# Check required input actions
for action in jump move_left move_right pause; do
  if grep -q "input/$action" "$PROJECT/project.godot" 2>/dev/null; then
    echo "  Input action '$action': OK"
  else
    echo "  MISSING input action: $action"
    MISSING=1
  fi
done

# Check C# project builds (if .csproj exists)
CSPROJ=$(find "$PROJECT" -name '*.csproj' -maxdepth 1 2>/dev/null | head -1)
if [ -n "$CSPROJ" ]; then
  echo "  C# project: $(basename "$CSPROJ")"
  if dotnet build "$CSPROJ" -c Debug --nologo -v q 2>/dev/null; then
    echo "  C# build: PASS"
  else
    echo "  C# build: FAIL"
    exit 1
  fi
fi

# Check asset directories exist
for dir in assets/sprites assets/backgrounds; do
  if [ -d "$PROJECT/$dir" ]; then
    echo "  Directory '$dir': OK"
  else
    echo "  Directory '$dir': MISSING (optional)"
  fi
done

if [ -n "${MISSING:-}" ]; then
  echo "WARNING: Some input actions are missing. Use godot-cli project/settings to add them."
fi

echo "=== Validation complete ==="
