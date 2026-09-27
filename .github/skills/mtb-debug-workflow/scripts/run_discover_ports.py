#!/usr/bin/env python3
"""Skill: run_discover_ports
Calls the ModusToolbox MCP tool 'discover_debug_ports' to enumerate connected debug probes.

Usage:
    python .github/skills/mtb-debug-workflow/scripts/run_discover_ports.py [--probe-type KitProg3]

Examples:
    python .github/skills/mtb-debug-workflow/scripts/run_discover_ports.py
    python .github/skills/mtb-debug-workflow/scripts/run_discover_ports.py --probe-type KitProg3
    python .github/skills/mtb-debug-workflow/scripts/run_discover_ports.py --probe-type J-Link
"""

import sys
import os
import argparse

# Add mtb-tools/scripts to path for mtbskill
current_dir = os.path.dirname(os.path.abspath(__file__))
skills_dir = os.path.dirname(os.path.dirname(current_dir))  # up to skills/
tools_dir = os.path.join(skills_dir, "mtb-tools", "scripts")
sys.path.insert(0, tools_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="Discover connected debug probes via MCP tool")
parser.add_argument("--probe-type", help="Filter by probe type (e.g., KitProg3, J-Link)")
args = parser.parse_args()

skill = bootstrap_skill(__file__)

params = {}
if args.probe_type:
    params["probe_type"] = args.probe_type

result = skill.mcp_request("discover_debug_ports", **params)
print(skill.mcp_text(result))
