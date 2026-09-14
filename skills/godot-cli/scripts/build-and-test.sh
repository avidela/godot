#!/bin/bash
# Full development cycle: build → restart → validate → screenshot → check errors
# Usage: bash scripts/build-and-test.sh [project-dir]
set -euo pipefail

PROJECT="${1:-.}"
cd "$PROJECT"
PROJECT="."

echo "=== Step 1: Build ==="
if find . -maxdepth 1 -name '*.csproj' 2>/dev/null | grep -q .; then
  dotnet build -c Debug --nologo -v q
fi

echo "=== Step 2: Kill old daemon ==="
godot-cli close 2>/dev/null || true
kill -9 $(pgrep -f "godot.*daemon") 2>/dev/null || true
sleep 1

echo "=== Step 3: Start daemon ==="
godot-cli open --project .

echo "=== Step 4: Validate ==="
godot-cli project/validate 2>/dev/null || {
  echo "Validation failed. Try to continue anyway."
}

echo "=== Step 5: Run game ==="
MAIN_SCENE=$(grep run/main_scene project.godot | sed 's/.*="//;s/"//' || echo "res://main.tscn")
godot-cli game/run "{\"scene\":\"$MAIN_SCENE\"}"
sleep 2

echo "=== Step 6: Screenshot ==="
godot-cli render/screenshot '{"file":"/tmp/godot_check.png"}'
echo "Screenshot: /tmp/godot_check.png"

echo "=== Step 7: Check errors ==="
godot-cli debug/errors 2>/dev/null || echo "No errors reported."

echo ""
echo "=== Done. Inspect /tmp/godot_check.png visually ==="
