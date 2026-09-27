#!/usr/bin/env python3
"""Skill: list_code_examples
Calls the Infineon ModusToolbox AI Assistant server command 'list_code_examples' and prints the full result so
AI agents have: project creation workflow

Usage:
    python .github/skills/mtb-tools/scripts/list_code_examples.py [--board-id board]
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
parser.add_argument("--board-id", required=False, help="List examples for a specific board")
args = parser.parse_args()

try:
    skill = bootstrap_skill(__file__)
    
    # ── Call the Infineon ModusToolbox AI Assistant server command and print the full result ───────────────────────────
    params = {}
    if args.board_id:
        params["board_name"] = args.board_id
    
    result = skill.mcp_request("list_code_examples", **params)
    print(skill.mcp_text(result))
except Exception as e:
    error_msg = str(e)
    print("ERROR:", error_msg)
    sys.exit(1)
