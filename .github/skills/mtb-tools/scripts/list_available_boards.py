#!/usr/bin/env python3
"""Skill: list_available_boards
Calls the Infineon ModusToolbox AI Assistant server command 'list_available_boards' and prints the full result so
AI agents have: project creation workflow

Usage:
    python .github/skills/mtb-tools/scripts/list_available_boards.py
"""

import sys
import os

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

skill = bootstrap_skill(__file__)

# ── Call the Infineon ModusToolbox AI Assistant server command and print the full result ───────────────────────────
result = skill.mcp_request("list_available_boards")
print(skill.mcp_text(result))
