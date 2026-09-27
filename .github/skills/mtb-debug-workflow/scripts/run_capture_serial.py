#!/usr/bin/env python3
"""Skill: run_capture_serial
Calls the ModusToolbox MCP tool 'capture_serial_output' to read serial port output.

Usage:
    python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port COM64 [options]

Examples:
    python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port COM64 --timeout 10
    python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port COM64 --timeout 15 --stop-patterns HardFault fault crash
    python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port COM64 --reset --timeout 10 --stop-patterns HardFault fault
    python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port COM64 --wait-for-port 5 --strip-ansi
"""

import sys
import os
import argparse
import threading
import time

# Add mtb-tools/scripts to path for mtbskill
current_dir = os.path.dirname(os.path.abspath(__file__))
skills_dir = os.path.dirname(os.path.dirname(current_dir))  # up to skills/
tools_dir = os.path.join(skills_dir, "mtb-tools", "scripts")
sys.path.insert(0, tools_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="Capture serial port output via MCP tool")
parser.add_argument("--port", required=True, help="Serial port name (e.g., COM64, /dev/ttyACM0)")
parser.add_argument("--baud-rate", type=int, default=115200, help="Baud rate (default: 115200)")
parser.add_argument("--timeout", type=int, default=10, help="Timeout in seconds (default: 10)")
parser.add_argument("--stop-patterns", nargs="+", help="Stop when any of these strings appear in output")
parser.add_argument("--wait-for-port", type=int, default=0, help="Seconds to wait for port to appear after programming (default: 0)")
parser.add_argument("--strip-ansi", action="store_true", help="Strip ANSI escape sequences from output")
parser.add_argument("--reset", action="store_true", help="Reset the board via MCP reset_board tool (captures from boot)")
args = parser.parse_args()

skill = bootstrap_skill(__file__)


def reset_board_via_mcp():
    """Reset the target MCU using the reset_board MCP tool."""
    try:
        result = skill.mcp_request("reset_board")
        text = skill.mcp_text(result)
        print(f"Board reset: {text}", file=sys.stderr)
        return True
    except Exception as e:
        print(f"WARNING: Board reset failed: {e}", file=sys.stderr)
        return False


# If --reset, trigger reset with a short delay to let capture open the port first
if args.reset:
    def delayed_reset():
        time.sleep(1.5)  # Give MCP tool time to open the serial port
        reset_board_via_mcp()

    reset_thread = threading.Thread(target=delayed_reset, daemon=True)
    reset_thread.start()

params = {
    "port": args.port,
    "baud_rate": args.baud_rate,
    "timeout_seconds": args.timeout,
}
if args.stop_patterns:
    params["stop_patterns"] = args.stop_patterns
if args.wait_for_port > 0:
    params["wait_for_port_seconds"] = args.wait_for_port
if args.strip_ansi:
    params["strip_ansi"] = True

try:
    result = skill.mcp_request("capture_serial_output", **params)
    print(skill.mcp_text(result))
except (TimeoutError, OSError) as e:
    print(f"ERROR: Serial capture failed: {e}", file=sys.stderr)
    sys.exit(1)
