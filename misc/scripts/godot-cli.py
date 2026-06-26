#!/usr/bin/env python3
"""
godot-cli — AI agent interface for the Godot game engine.

Usage:
  godot-cli open --project ./my-game [--headless] [--port 3100]
  godot-cli scene/tree
  godot-cli scene/add_node n0 Sprite2D '{"texture":"icon.png","position":[100,200]}'
  godot-cli scene/set n3 position '[300,150]'
  godot-cli scene/get n3
  godot-cli script/write player.gd 'extends CharacterBody2D...'
  godot-cli game/run
  godot-cli input/key KEY_SPACE true
  godot-cli render/screenshot
  godot-cli debug/logs
  godot-cli close
  godot-cli list

Each command returns a JSON response with OK status, result data, and a
scene tree snapshot (with node refs like n0, n1, n2...) for targeting.

Use --raw to get only the JSON result value (for piping into other tools).
Use -s <name> for named sessions (multi-agent / multi-worktree support).
"""

import argparse
import json
import os
import platform
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time

# ── Constants ───────────────────────────────────────────────────────────

DEFAULT_PORT = 3100
SESSION_DIR = os.path.join(os.path.expanduser("~"), ".godot-cli")
CLI_VERSION = "0.1.0"

# ── Session Management ─────────────────────────────────────────────────

def _session_path(name="default"):
    """Return the path to the session file for the given session name."""
    os.makedirs(SESSION_DIR, exist_ok=True)
    safe_name = name.replace("/", "_").replace("\\", "_")
    return os.path.join(SESSION_DIR, f"{safe_name}.json")


def _read_session(name="default"):
    """Read session state (port, pid, project)."""
    path = _session_path(name)
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return {}


def _write_session(data, name="default"):
    """Write session state."""
    path = _session_path(name)
    with open(path, "w") as f:
        json.dump(data, f)


def _delete_session(name="default"):
    """Remove a session file."""
    path = _session_path(name)
    if os.path.exists(path):
        os.remove(path)


def _find_free_port():
    """Find a free TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _find_godot():
    """Locate the Godot binary. Check common locations."""
    candidates = []

    # Check GODOT_CLI_BIN env var.
    env_bin = os.environ.get("GODOT_CLI_BIN")
    if env_bin:
        candidates.append(env_bin)

    # Check for godot in PATH.
    which = subprocess.run(["which", "godot"], capture_output=True, text=True)
    if which.returncode == 0:
        candidates.append(which.stdout.strip())

    # Check common paths.
    home = os.path.expanduser("~")
    for p in [
        os.path.join(home, "src", "godot", "bin", "godot.linuxbsd.editor.x86_64"),
        os.path.join(home, "src", "godot", "bin", "godot.linuxbsd.editor.x86_64.san"),
        os.path.join(home, "src", "godot", "bin", "godot.linuxbsd.editor.x86_64.llvm"),
        os.path.join(os.getcwd(), "godot"),
        os.path.join(os.getcwd(), "bin", "godot*"),
        "/usr/local/bin/godot",
        "/usr/bin/godot",
    ]:
        candidates.append(p)

    for c in candidates:
        if os.path.isfile(c) or (not os.path.isabs(c) and c.strip()):
            # Strip wildcards for glob expansion.
            if "*" in c:
                import glob as gb
                matches = gb.glob(c)
                if matches:
                    return matches[0]
            else:
                if os.path.isfile(c):
                    return c

    # Try a broader search.
    for search_path in [
        os.path.join(home, "src", "godot", "bin"),
        os.path.join(os.getcwd(), "bin"),
    ]:
        if os.path.isdir(search_path):
            for f in os.listdir(search_path):
                if f.startswith("godot."):
                    return os.path.join(search_path, f)

    return None


# ── TCP Communication ──────────────────────────────────────────────────

def _send_command(port, command, params=None, timeout=30):
    """Send a JSON command to the Godot CLI daemon and return the response."""
    if params is None:
        params = {}

    cmd = {
        "cmd": command,
        "params": params,
        "id": 1,
    }

    payload = json.dumps(cmd)
    header = f"Content-Length: {len(payload)}\r\n\r\n"

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(("127.0.0.1", port))
        sock.sendall(header.encode() + payload.encode())

        # Read response header.
        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
            # Check if we have the full content.
            if b"\r\n\r\n" in response:
                parts = response.split(b"\r\n\r\n", 1)
                header_end = parts[0]
                body = parts[1]
                # Parse content length.
                for line in header_end.decode().split("\r\n"):
                    if line.lower().startswith("content-length:"):
                        content_length = int(line.split(":")[1].strip())
                        break
                if len(body) >= content_length:
                    return json.loads(body.decode())
                # Need more data.
                continue
    except socket.timeout:
        return {"ok": False, "error": "Timeout waiting for response"}
    except ConnectionRefusedError:
        return {"ok": False, "error": "Connection refused. Is the daemon running?"}
    except Exception as e:
        return {"ok": False, "error": str(e)}
    finally:
        sock.close()

    return {"ok": False, "error": "Incomplete response"}


# ── Output Formatting ──────────────────────────────────────────────────

def _format_response(response, raw=False):
    """Format a JSON response for display."""
    if raw:
        # Only output the result value.
        if response.get("ok"):
            result = response.get("result", {})
            if "data" in result:
                # Binary data (base64) - output as-is.
                return result["data"]
            return json.dumps(result, indent=2)
        else:
            return json.dumps(response, indent=2)

    # Pretty output.
    lines = []

    if response.get("ok"):
        result = response.get("result", {})
        if result:
            lines.append("── Result ──────────────────────────────────────")
            for k, v in result.items():
                if k in ('_ok', '_error', '_code', '_no_snapshot', 'data'):
                    continue
                if isinstance(v, list):
                    lines.append(f"  {k}: [{len(v)} items]")
                elif isinstance(v, dict):
                    lines.append(f"  {k}: {{{len(v)} keys}}")
                    for k2, v2 in list(v.items())[:5]:
                        if isinstance(v2, (list, dict)):
                            lines.append(f"    {k2}: ({len(v2)} items)")
                        else:
                            lines.append(f"    {k2}: {str(v2)[:80]}")
                elif isinstance(v, str) and len(v) > 80:
                    lines.append(f"  {k}: {str(v)[:80]}...")
                else:
                    lines.append(f"  {k}: {v}")
    else:
        lines.append(f"✗ Error: {response.get('error', 'Unknown error')}")

    # Snapshot.
    snapshot = response.get("snapshot")
    if snapshot and not raw:
        nodes = snapshot.get("nodes", [])
        if nodes:
            lines.append("")
            lines.append("── Scene Tree ───────────────────────────────────")
            if "fps" in snapshot:
                lines.append(f"  FPS: {snapshot['fps']}  "
                             f"Nodes: {snapshot.get('node_count', 0)}  "
                             f"{'RUNNING' if snapshot.get('running') else 'stopped'}")
            lines.append("")
            _format_nodes(nodes, lines, indent=2)

    # Screenshot data inline (raw base64).
    result = response.get("result", {})
    if "data" in result and not raw:
        data = result["data"]
        lines.append(f"\n  Screenshot: {len(data)} bytes (base64)")
        lines.append(f"  (use --raw to get the raw base64 data)")

    return "\n".join(lines)


def _format_nodes(nodes, lines, indent=0, max_depth=5, depth=0):
    """Recursively format a scene tree node list."""
    prefix = "  " * (indent // 2 + depth) if depth > 0 else "  " * (indent // 2)
    for node in nodes:
        ref = node.get("ref", "?")
        name = node.get("name", "?")
        ntype = node.get("type", "?")
        props = node.get("properties", {})
        pos = props.get("position", "")
        script = node.get("script", "")

        line = f"{prefix}{ref}  {name}  [{ntype}]"
        if pos:
            line += f"  pos={pos}"
        if script:
            line += f"  script={os.path.basename(script)}"
        lines.append(line)

        children = node.get("children", [])
        if children and depth < max_depth:
            _format_nodes(children, lines, indent, max_depth, depth + 1)
        elif children and depth >= max_depth:
            lines.append(f"{prefix}    ... ({len(children)} children)")


# ── Command Handlers ───────────────────────────────────────────────────

def cmd_open(args):
    """Open a Godot project as a daemon session."""
    session = _read_session(args.session)

    # Check if already running.
    if session.get("pid"):
        try:
            os.kill(session["pid"], 0)  # Check if process exists
            port = session["port"]
            resp = _send_command(port, "daemon/ping")
            if resp.get("ok"):
                print("Session already running.")
                return port
        except (OSError, ProcessLookupError):
            pass  # Stale session, clean up.

    # Find Godot binary.
    godot_bin = args.binary or _find_godot()
    if not godot_bin:
        print("Error: Could not find Godot binary.")
        print("Set GODOT_CLI_BIN environment variable or pass --binary.")
        sys.exit(1)

    # Find a free port.
    port = args.port or DEFAULT_PORT

    # Build command.
    cmd = [
        godot_bin,
        "--daemon",
        "--daemon-port", str(port),
        "--path", args.project or os.getcwd(),
    ]
    if args.headless:
        cmd.append("--headless")
    if args.display_driver:
        cmd.extend(["--display-driver", args.display_driver])
    if args.verbose:
        cmd.append("--verbose")

    # Start the daemon.
    try:
        os.makedirs(SESSION_DIR, exist_ok=True)
        logfile = os.path.join(SESSION_DIR, f"daemon-{args.session}.log")
        proc = subprocess.Popen(
            cmd,
            stdout=open(logfile, "w"),
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        print(f"Error: Could not execute Godot binary: {godot_bin}")
        sys.exit(1)

    # Wait for daemon to be ready.
    for attempt in range(50):
        time.sleep(0.1)
        resp = _send_command(port, "daemon/ping")
        if resp.get("ok"):
            break
    else:
        print("Warning: Daemon started but not responding. Check logs:")
        print(f"  {logfile}")
        # Try anyway.

    # Save session.
    _write_session({
        "pid": proc.pid,
        "port": port,
        "binary": godot_bin,
        "project": args.project or os.getcwd(),
        "logfile": logfile,
    }, args.session)

    print(f"Godot CLI daemon started [session: {args.session}]")
    print(f"  PID: {proc.pid}")
    print(f"  Port: {port}")
    print(f"  Binary: {godot_bin}")
    print(f"  Project: {args.project or os.getcwd()}")
    print(f"  Log: {logfile}")

    # Send initial ping and show version.
    resp = _send_command(port, "daemon/version")
    if resp.get("ok"):
        ver = resp.get("result", {})
        print(f"  Engine: {ver.get('version', 'unknown')}")

    # Get initial scene tree.
    resp = _send_command(port, "scene/tree")
    output = _format_response(resp, raw=args.raw)
    if output:
        print()
        print(output)

    return port


def cmd_close(args):
    """Close a running daemon session."""
    session = _read_session(args.session)
    if not session:
        print("No session found.")
        return

    port = session.get("port")
    pid = session.get("pid")

    if port:
        _send_command(port, "daemon/shutdown")

    if pid:
        try:
            os.kill(pid, signal.SIGTERM)
            for _ in range(10):
                try:
                    os.kill(pid, 0)
                    time.sleep(0.1)
                except ProcessLookupError:
                    break
            else:
                os.kill(pid, signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass

    _delete_session(args.session)
    print(f"Session '{args.session}' closed.")


def cmd_list(args):
    """List running sessions."""
    sessions = []
    if os.path.isdir(SESSION_DIR):
        for f in os.listdir(SESSION_DIR):
            if f.endswith(".json"):
                name = f[:-5]
                session = _read_session(name)
                pid = session.get("pid")
                alive = False
                if pid:
                    try:
                        os.kill(pid, 0)
                        alive = True
                    except ProcessLookupError:
                        pass
                sessions.append((name, session, alive))

    if not sessions:
        print("No sessions.")
        return

    print(f"{'NAME':<20} {'PID':<8} {'PORT':<6} {'STATUS':<10} PROJECT")
    print("-" * 80)
    for name, session, alive in sessions:
        status = "RUNNING" if alive else "DEAD"
        pid = session.get("pid", 0)
        port = session.get("port", 0)
        project = session.get("project", "")
        print(f"{name:<20} {pid:<8} {port:<6} {status:<10} {project}")


def cmd_send(args):
    """Send a command to a running daemon."""
    session = _read_session(args.session)
    if not session:
        print("No session found. Use 'godot-cli open' first.")
        sys.exit(1)

    port = session.get("port")
    if not port:
        print("No port in session.")
        sys.exit(1)

    # Ping to check liveliness.
    ping = _send_command(port, "daemon/ping")
    if not ping.get("ok"):
        print("Daemon is not responding.")
        print(f"  {ping.get('error', '')}")
        sys.exit(1)

    # Parse the command.
    cmd_parts = args.command_args
    if not cmd_parts:
        print("No command specified.")
        sys.exit(1)

    command = cmd_parts[0]
    params = {}

    # Parse parameters from remaining args.
    i = 1
    while i < len(cmd_parts):
        part = cmd_parts[i]
        if part.startswith("--"):
            key = part[2:]
            i += 1
            if i < len(cmd_parts):
                val = cmd_parts[i]
                # Try to parse as JSON.
                try:
                    params[key] = json.loads(val)
                except (json.JSONDecodeError, ValueError):
                    params[key] = val
            else:
                params[key] = True
        elif "=" in part:
            key, val = part.split("=", 1)
            try:
                params[key] = json.loads(val)
            except (json.JSONDecodeError, ValueError):
                params[key] = val
        else:
            i += 1
            continue
        i += 1

    # Handle simple positional params for common commands.
    if command in ("scene/add_node",):
        if len(cmd_parts) >= 3:
            params.setdefault("parent", cmd_parts[1])
            params.setdefault("type", cmd_parts[2])
            if len(cmd_parts) >= 4:
                try:
                    params["properties"] = json.loads(cmd_parts[3])
                except (json.JSONDecodeError, ValueError):
                    params["properties"] = {"name": cmd_parts[3]}

    elif command in ("scene/set", "scene/get", "scene/remove_node"):
        if len(cmd_parts) >= 2:
            params.setdefault("path", cmd_parts[1])
        if command == "scene/set" and len(cmd_parts) >= 3:
            params.setdefault("property", cmd_parts[2])
            if len(cmd_parts) >= 4:
                try:
                    params["value"] = json.loads(cmd_parts[3])
                except (json.JSONDecodeError, ValueError):
                    params["value"] = cmd_parts[3]

    elif command in ("input/key",):
        if len(cmd_parts) >= 2:
            params.setdefault("key", cmd_parts[1])
        if len(cmd_parts) >= 3:
            params.setdefault("pressed", cmd_parts[2].lower() in ("true", "1", "yes"))

    elif command in ("script/write",):
        if len(cmd_parts) >= 2:
            params.setdefault("path", cmd_parts[1])
        if len(cmd_parts) >= 3:
            params.setdefault("source", cmd_parts[2])

    elif command in ("script/read",):
        if len(cmd_parts) >= 2:
            params.setdefault("path", cmd_parts[1])

    elif command == "render/screenshot":
        if len(cmd_parts) >= 2:
            params.setdefault("file", cmd_parts[1])

    resp = _send_command(port, command, params)
    output = _format_response(resp, raw=args.raw)
    print(output)

    # If this was a screenshot with no file path, offer to save it.
    if command == "render/screenshot" and "file" not in params:
        result = resp.get("result", {})
        data = result.get("data", "")
        if data and not args.raw:
            # Don't auto-save, but tell user about --raw.
            pass


# ── Main Entry Point ───────────────────────────────────────────────────

def main():
    # If the first argument contains a slash (like scene/tree, game/run),
    # treat it as a daemon command, not a subcommand.
    if len(sys.argv) > 1 and "/" in sys.argv[1]:
        # Rewrite: godot-cli scene/tree [args...]
        #       -> godot-cli send scene/tree [args...]
        sys.argv.insert(1, "send")

    parser = argparse.ArgumentParser(
        description="godot-cli — Control the Godot game engine from the command line",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  godot-cli open --project ./my-game --headless
  godot-cli scene/tree
  godot-cli scene/add_node n0 Sprite2D '{"position":[100,200],"texture":"icon.png"}'
  godot-cli scene/set n2 position '[300,150]'
  godot-cli script/write player.gd 'extends CharacterBody2D: func _ready(): pass'
  godot-cli game/run
  godot-cli input/key KEY_SPACE true
  godot-cli render/screenshot
  godot-cli debug/logs
  godot-cli close
        """
    )

    # Global options.
    parser.add_argument("--raw", action="store_true",
                        help="Output raw JSON result only (for piping)")
    parser.add_argument("-s", "--session", default="default",
                        help="Session name (for multi-agent / multi-worktree)")

    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # open
    p_open = subparsers.add_parser("open", help="Start a Godot CLI daemon session")
    p_open.add_argument("--project", "-p", default=None,
                        help="Project directory (default: current directory)")
    p_open.add_argument("--binary", "-b", default=None,
                        help="Path to Godot binary")
    p_open.add_argument("--headless", action="store_true",
                        help="Run in headless mode (no window)")
    p_open.add_argument("--port", type=int, default=None,
                        help=f"Port for daemon (default: auto)")
    p_open.add_argument("--display-driver", default=None,
                        help="Display driver override")
    p_open.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")

    # close
    p_close = subparsers.add_parser("close", help="Stop a Godot CLI daemon session")

    # list
    p_list = subparsers.add_parser("list", help="List running sessions")

    # Generic command sender.
    p_send = subparsers.add_parser("send", help="Send a raw command (advanced)")
    p_send.add_argument("command_args", nargs="+",
                        help="Command and parameters")

    args, unknown = parser.parse_known_args()

    # If no command, show help.
    if not args.command:
        parser.print_help()
        sys.exit(0)

    # Handle the command.
    if args.command == "open":
        cmd_open(args)
    elif args.command == "close":
        cmd_close(args)
    elif args.command == "list":
        cmd_list(args)
    elif args.command == "send":
        cmd_send(args)
    else:
        # Unknown command - try sending directly to daemon.
        # This allows: godot-cli scene/tree  directly.
        # Reconstruct from unknown args.
        all_args = [args.command] + unknown
        # Fake it as a send.
        class SendArgs:
            pass
        send_args = SendArgs()
        send_args.session = args.session
        send_args.raw = args.raw
        send_args.command_args = all_args
        cmd_send(send_args)


if __name__ == "__main__":
    main()
