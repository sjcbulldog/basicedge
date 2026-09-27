#!/usr/bin/env python3
"""Skill: list_skills_for_board
Calls the extension server's 'listSkillsForBoard' JSON-RPC method (via ext_request) and prints
the AI skills (name + description) that would be installed for a given board.

Unlike the list_* tools that go through the MCP server, this queries the VS Code
extension server directly (jayson JSON-RPC), because manifest/skill resolution
lives in the extension. See ext_request() in mtblib.py.

Usage:
    python .github/skills/mtb-tools/scripts/list_skills_for_board.py --board-name CY8CEVAL-062S2-LAI-4373M2
    python .github/skills/mtb-tools/scripts/list_skills_for_board.py            # uses the current session board
    python .github/skills/mtb-tools/scripts/list_skills_for_board.py --json     # machine-readable output
"""

import sys
import os
import json
import argparse

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="List AI skills available for a board")
parser.add_argument("--board-name", required=False, help="Board name to list skills for. Defaults to the current session board.")
parser.add_argument("--json", action="store_true", help="Print the skills as JSON instead of formatted text.")
args = parser.parse_args()

skill = bootstrap_skill(__file__)

# Fall back to the current session's board target (BOARD_TARGET global from mtb_env.py) when not given.
board_name = args.board_name or getattr(skill, "BOARD_TARGET", "") or ""
if not board_name:
    print("ERROR: no board specified and no current session board found. Pass --board-name <board>.", file=sys.stderr)
    sys.exit(1)

try:
    # listSkillsForBoard is served by the extension (jayson), so use ext_request (pre-bound to EXT_PORT), not MCP.
    result = skill.ext_request("listSkillsForBoard", board_name=board_name)
except Exception as exc:  # noqa: BLE001 - surface a concise message for any transport/RPC failure
    print(f"ERROR: could not reach the extension server to list skills: {exc}", file=sys.stderr)
    print("This tool requires an active workspace with the Infineon ModusToolbox AI Assistant extension running.", file=sys.stderr)
    sys.exit(1)

# By convention the extension returns application errors in the result body, never the JSON-RPC error channel.
if not result.get("success"):
    err = result.get("error") or {}
    message = err.get("message") if isinstance(err, dict) else str(err)
    print(f"ERROR: listSkillsForBoard failed for '{board_name}': {message or 'unknown error'}", file=sys.stderr)
    sys.exit(1)

# 'skills' is a list of JSON strings, each {"name": ..., "description": ...}.
skills = []
for entry in result.get("skills") or []:
    try:
        skills.append(json.loads(entry))
    except (json.JSONDecodeError, TypeError):
        skills.append({"name": str(entry), "description": ""})  # keep unparseable entries visible

if args.json:
    print(json.dumps(skills, indent=2))
    sys.exit(0)

if not skills:
    print(f"No skills matched board '{board_name}'.")
    sys.exit(0)

print(f"Skills for board '{board_name}' ({len(skills)}):\n")
for s in skills:
    name = s.get("name", "unknown")
    desc = s.get("description", "")
    print(f"- {name}: {desc}" if desc else f"- {name}")
