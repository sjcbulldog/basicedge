#!/usr/bin/env python3
"""Skill: run_tool
Calls the Infineon ModusToolbox AI Assistant server command 'run_tool' to run the tool listed on the command line

Usage:
    python .github/skills/mtb-tools/scripts/run_tool.py [tool_name]
"""

import sys
import os
import argparse

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="List ModusToolbox code examples")
parser.add_argument("tool_name", nargs="?", default="", help="Run the given GUI tool on the current project")
args = parser.parse_args()

skill = bootstrap_skill(__file__)

# ── Call the Infineon ModusToolbox AI Assistant server command and print the full result ───────────────────────────
params = {}
if args.tool_name:
    params["tool_name"] = args.tool_name

result = skill.mcp_request("run_tool", **params)
print(skill.mcp_text(result))
