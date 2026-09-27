#!/usr/bin/env python3
"""Skill: run_tool
Uses the Infineon ModusToolbox AI Assistant established environment to locate and run the project-creator-cli tool 

Usage:
    python .github/skills/mtb-tools/scripts/project_creator_wrapper.py 
            --board-id <board-id> 
            --app-id <template-id> 
            --target-dir <output-dir> 
            --user-app-name <project-name>
"""

import sys
import os
import argparse
import subprocess

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="Run the project-creator-cli tool")
parser.add_argument("--board-id", required=True, help="Target board identifier")
parser.add_argument("--app-id", required=True, help="Template/application identifier")
parser.add_argument("--target-dir", required=True, help="Output directory for the project")
parser.add_argument("--user-app-name", required=True, help="Name for the created project")
args = parser.parse_args()

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

# ── Build and run the project-creator-cli command ─────────────────────────
project_creator = os.path.join(tools_path, "project-creator", "project-creator-cli")

cmd = [
    project_creator,
    "--board-id", args.board_id,
    "--app-id", args.app_id,
    "--target-dir", args.target_dir,
    "--user-app-name", args.user_app_name,
]

print(f"Running: {' '.join(cmd)}")
with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as proc:
    import threading

    def stream(src, dst):
        for line in src:
            print(line, end="", file=dst, flush=True)

    t_out = threading.Thread(target=stream, args=(proc.stdout, sys.stdout))
    t_err = threading.Thread(target=stream, args=(proc.stderr, sys.stderr))
    t_out.start()
    t_err.start()
    t_out.join()
    t_err.join()
    proc.wait()

print("")
sys.exit(proc.returncode)
