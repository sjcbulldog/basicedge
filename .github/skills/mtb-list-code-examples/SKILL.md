---
name: mtb-list-code-examples
description: "Find and summarize the ModusToolbox code examples most relevant to the current board, MCU family or requested feature. Use it when the user asks for sample projects, starter apps or reference implementations for a peripheral, connectivity, ML or FreeRTOS use case."
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# ModusToolbox Code Examples

This skill helps an agent find and summarize the most relevant ModusToolbox code examples for the current target, board, MCU family, or requested feature.

## When to use

Use this skill when the user asks for:

- code examples or sample projects
- starter apps or reference implementations
- examples for a specific peripheral or middleware feature
- examples for a board or MCU family
- a good repo to clone as a starting point

Once the board to use is resolved, run `python .github/skills/mtb-tools/scripts/list_code_examples.py` from the workspace root to obtain the set of code examples available. **Always prefer manifest data over defaults.**

## Recommended workflow

1. Run `python .github/skills/mtb-tools/scripts/list_code_examples.py` from the workspace root to list code examples for the current environment.
2. Identify examples that best match the user's requested feature, board, or device family.
3. Summarize the top matches in a compact list.
4. Explain why each example is relevant and note any important limitations.
5. If there is no exact match, suggest the closest alternatives and say why.

## Output guidelines

- Prefer the most relevant 3-8 examples instead of dumping everything.
- Group results by feature area when helpful, such as connectivity, RTOS, AI/ML, sensors, or peripheral drivers.
- Mention board or device compatibility when known.
- Do not invent repository names, features, or support claims.
- Be clear when an example is adjacent rather than an exact match.

## Suggested response structure

Include:

- a brief one-line summary
- a short bullet list of recommended examples
- the main feature or use case for each example
- a note on which one is the best starting point

## Example intent

- "Show me UART examples for this board"
- "Find a FreeRTOS sample I can adapt"
- "What code examples exist for BLE or Wi-Fi?"
- "Give me a good ModusToolbox starter project"

## Notes

This skill is especially useful before generating new code, because a real example project often reveals the correct middleware, initialization order, build settings, and board-specific patterns.
