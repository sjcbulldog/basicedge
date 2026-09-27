#!/usr/bin/env python3
"""Skill: list_available_libraries
Calls the Infineon ModusToolbox AI Assistant server command 'list_available_libraries' and prints the full result so
AI agents have: library addition workflow, middleware IDs, categories, GitHub
URLs, version tags, descriptions, and which projects already use each library.

Usage:
    python .github/skills/mtb-tools/scripts/list_available_libraries.py [--board-id board]
"""

import sys
import os
import argparse

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="List ModusToolbox libraries")
parser.add_argument("--board-id", required=False, help="List libraries for a specific board")
args = parser.parse_args()

try:
    skill = bootstrap_skill(__file__)
    params = {}
    if args.board_id:
        params["board_name"] = args.board_id

    result = skill.mcp_request("list_available_libraries", **params)
    print(skill.mcp_text(result))
except Exception as e:
    error_msg = str(e)
    if "connect" in error_msg.lower() or "tcp" in error_msg.lower() or "refused" in error_msg.lower() or "port" in error_msg.lower():
        print("NOTE: Library catalog is not available in this workspace context.")
        print("This tool requires an active ModusToolbox project workspace with the Infineon ModusToolbox AI Assistant server running.")
        print("")
        print("During project PLANNING: Library validation is deferred to the development phase.")
        print("After project CREATION: Re-run this tool to verify library availability.")
        sys.exit(0)
    else:
        raise
