#!/usr/bin/env python3
"""Skill: list_modustoolbox_location
Returns the location of the ModusToolbox tools

Usage:
    python .github/skills/mtb-tools/scripts/list_modustoolbox_location.py
"""

import sys
import os
import argparse

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

skill = bootstrap_skill(__file__)

# ── Validate environment ──────────────────────────────────────────────────
if not skill.env:
    print("ERROR: skill.env is empty — the Infineon ModusToolbox AI Assistant environment was not initialized.", file=sys.stderr)
    sys.exit(1)

tools_path = getattr(skill.env, "CY_TOOLS_PATH", None)
if not tools_path:
    print("ERROR: CY_TOOLS_PATH is not set in the skill environment.", file=sys.stderr)
    sys.exit(1)

if not os.path.isdir(tools_path):
    print(f"ERROR: CY_TOOLS_PATH does not exist: {tools_path}", file=sys.stderr)
    sys.exit(1)

print(tools_path)
