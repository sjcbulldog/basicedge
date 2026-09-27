#!/usr/bin/env python
"""Reset the connected board via MCP tool (OpenOCD reset run).

Usage:
    python run_reset_board.py [--cwd <sub-project>] [--timeout <seconds>]

Auto-detects the target device from the BSP directory.
"""
import argparse
import os
import sys

# Bootstrap mtbskill from sibling mtb-tools skill
current_dir = os.path.dirname(os.path.abspath(__file__))
skills_dir = os.path.dirname(os.path.dirname(current_dir))  # up to skills/
tools_dir = os.path.join(skills_dir, "mtb-tools", "scripts")
if tools_dir not in sys.path:
    sys.path.insert(0, tools_dir)

from mtbskill import bootstrap_skill

skill = bootstrap_skill(__file__)

parser = argparse.ArgumentParser(description="Reset the connected board via OpenOCD")
parser.add_argument("--cwd", default="", help="Project directory (for BSP detection)")
parser.add_argument("--timeout", type=int, default=20, help="Timeout in seconds (default: 20)")
args = parser.parse_args()

params = {}
if args.cwd:
    params["cwd"] = args.cwd
if args.timeout != 20:
    params["timeout_seconds"] = args.timeout

result = skill.mcp_request("reset_board", **params)
skill.mcp_text(result)
