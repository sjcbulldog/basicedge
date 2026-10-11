#!/usr/bin/env python3
"""Skill: list_code_examples
Calls the Infineon ModusToolbox AI Assistant server command 'list_code_examples' and prints the full result so
AI agents have: project creation workflow

Usage:
    python .github/skills/mtb-tools/scripts/list_code_examples.py [--board-id board] [--keyword term ...] [--category cat] [--limit N] [--summary-only]
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
parser.add_argument(
    "--keyword",
    required=False,
    action="append",
    help="Case-insensitive substring matched across name, keywords, category, and description. "
    "Repeat the flag or comma-separate to pass a set; an example matches if it contains ANY keyword.",
)
parser.add_argument("--category", required=False, help="Case-insensitive category filter (e.g. Bluetooth, Wi-Fi, Peripherals, Machine Learning)")
parser.add_argument("--limit", required=False, type=int, help="Max examples to return. Applies ONLY with --keyword (defaults to 20 then); a bare or category-only call returns the full list.")
parser.add_argument("--summary-only", required=False, action="store_true", help="Return only total_unfiltered + counts_by_category (no example list). Cheapest way to discover the valid categories for the current board.")
args = parser.parse_args()

try:
    skill = bootstrap_skill(__file__)

    # ── Call the Infineon ModusToolbox AI Assistant server command and print the full result ───────────────────
    params = {}
    if args.board_id:
        params["board_name"] = args.board_id
    keywords = []
    for k in args.keyword or []:
        keywords.extend(p.strip() for p in k.split(",") if p.strip())
    if keywords:
        params["keywords"] = keywords
    if args.category:
        params["category"] = args.category
    if args.limit is not None:
        params["limit"] = args.limit
    if args.summary_only:
        params["summary_only"] = True

    result = skill.mcp_request("list_code_examples", **params)
    print(skill.mcp_text(result))
except Exception as e:
    error_msg = str(e)
    print("ERROR:", error_msg)
    sys.exit(1)
