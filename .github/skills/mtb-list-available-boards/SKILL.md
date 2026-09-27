---
name: mtb-list-available-boards
description: "Use when the user asks which boards, kits, or development platforms are available for ModusToolbox project creation, or needs to select a target board for a new project."
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# ModusToolbox Available Boards

This skill helps an agent discover the boards, kits, and development platforms available for creating ModusToolbox projects.

## When to use

Use this skill when the user asks for:

- a list of available boards or kits
- which development platforms they can target
- board selection help before creating a new project
- supported hardware for ModusToolbox
- what BSPs (Board Support Packages) are available

## Recommended workflow

1. Run `python .github/skills/mtb-tools/scripts/list_available_boards.py` from the workspace root to obtain the full list of available boards.
2. Parse the output to identify boards matching the user's requirements (MCU family, connectivity, features).
3. Present matching boards in a concise, organized list.
4. If the user has not specified constraints, summarize the available options grouped by device family or category.

## Output guidelines

- Present results in a compact, scannable list.
- Group boards by device family or category when the list is large (e.g., PSoC 6, PSoC Edge, XMC, AIROC).
- Include the board identifier (BSP name) that can be used for project creation.
- Highlight boards that are popular or well-supported as starting points.
- Do not invent board names or capabilities not present in the tool output.

## Suggested response structure

Include:

- a brief summary of how many boards are available
- a grouped or filtered list of boards relevant to the user's query
- the BSP identifier for each board (usable with project creation tools)
- a recommendation if the user is unsure which to choose

## Example intent

- "What boards can I use with ModusToolbox?"
- "List available PSoC Edge kits"
- "Which development kits support Bluetooth?"
- "Show me boards I can target for a new project"
- "What BSPs are available?"

## Notes

This skill is typically used as a prerequisite step before project creation. Once the user selects a board, other skills such as `mtb-project-creation` or `mtb-list-code-examples` can continue the workflow.
