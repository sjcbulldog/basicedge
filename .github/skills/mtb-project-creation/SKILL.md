---
name: mtb-project-creation
description: "Use when the user asks to create a new ModusToolbox project, scaffold a starter application, generate a project from a code example or template, or set up a new embedded project for an Infineon board or MCU."
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# ModusToolbox Project Creation

Create a new ModusToolbox project using the `project-creator-cli` tool via the bundled wrapper script. This skill gathers the required parameters — board, application template, output directory, and project name — then invokes the CLI to scaffold a complete, buildable project.

## When to Use

- User asks to create a new ModusToolbox project
- User wants to start a project from a code example or application template
- User asks to scaffold a new embedded application for an Infineon board
- User says "create a project for <board>", "new project from <template>", or similar

## Prerequisites

- The Infineon ModusToolbox AI Assistant VS Code extension is active and has generated AI instructions
- `$CY_TOOLS_PATHS` is set (standard ModusToolbox environment variable)
- The wrapper script is available at `.github/skills/mtb-tools/scripts/project_creator_wrapper.py`
- `~/.modustoolbox/mtbmcp/mtbskill.py` exists (deposited by the Infineon ModusToolbox AI Assistant extension)

## Required Information

Before invoking the project creator, gather all four required parameters:

| Parameter | Flag | Description | How to Resolve |
| --------- | ---- | ----------- | -------------- |
| Board ID | `--board-id` | Target board identifier (e.g., `CY8CPROTO-062-4343W`, `KIT_PSE84_EVAL`) | Ask the user, or run `python .github/skills/mtb-tools/scripts/list_available_boards.py` to list valid boards |
| App/Template ID | `--app-id` | Code example or application template identifier (e.g., `mtb-example-psoc6-hello-world`) | Ask the user, or run `python .github/skills/mtb-tools/scripts/list_code_examples.py --board-id <board-id>` to list available templates for the board |
| Target Directory | `--target-dir` | Filesystem path where the project will be created | Ask the user, or default to the current workspace directory |
| Project Name | `--user-app-name` | Name for the created project (becomes the project folder name) | Ask the user |

## Step-by-Step Workflow

1. **Identify the board** — If the user specifies a board, use it directly. Otherwise, run `python .github/skills/mtb-tools/scripts/list_available_boards.py` to present available boards and ask the user to choose.

2. **Identify the application template** — If the user specifies a template or code example, use it directly. Otherwise, run `python .github/skills/mtb-tools/scripts/list_code_examples.py --board-id <board-id>` (using the board selected in step 1) to list templates compatible with that board and ask the user to choose.

3. **Determine the output directory** — Ask the user where the project should be created. If not specified, default to the current workspace root.

4. **Determine the project name** — Ask the user for a project name. If not specified, suggest a sensible default based on the template name.

5. **Confirm with the user** — Present the full set of parameters and ask for confirmation:
   ```
   I'll create a new ModusToolbox project with these settings:

   - Board: <board-id>
   - Template: <app-id>
   - Directory: <target-dir>
   - Project name: <user-app-name>

   Shall I proceed?
   ```

6. **Run the project creator** — Execute from the workspace root:
   ```
   python .github/skills/mtb-tools/scripts/project_creator_wrapper.py \
       --board-id <board-id> \
       --app-id <template-id> \
       --target-dir <output-dir> \
       --user-app-name <project-name>
   ```

7. **Verify success** — Check the exit code. On success, confirm the project was created and note the project path. On failure, report the error output to the user.

8. **Post-creation guidance** — After successful creation, suggest next steps:
   - Open the project folder if not already in the workspace
   - Run `make build` to verify the project compiles
   - Point out key files (`main.c`, `Makefile`, `design.modus`)

## Parameter Resolution Helpers

### List Available Boards

```
python .github/skills/mtb-tools/scripts/list_available_boards.py
```

Returns the set of boards supported by the current ModusToolbox installation.

### List Code Examples

```
python .github/skills/mtb-tools/scripts/list_code_examples.py --board-id <board-id>
```

Requires a board name argument. Returns code examples and application templates compatible with the specified board.

## Example Intents

- "Create a new project for the CY8CPROTO-062-4343W board"
- "Start a hello-world project for PSoC 6"
- "Scaffold a BLE project for the KIT_PSE84_EVAL"
- "New project from the mtb-example-psoc6-hello-world template"
- "Create a FreeRTOS starter for my board"

## Troubleshooting

| Problem | Resolution |
| ------- | ---------- |
| Wrapper script not found | Ensure Infineon ModusToolbox for VS Code extension has run "Generate AI Instructions" |
| Board ID not recognized | Run `list_available_boards.py` to get valid identifiers |
| App ID not found | Run `list_code_examples.py` to get valid template identifiers |
| Permission denied on target directory | Verify the path exists and is writable |
| `CY_TOOLS_PATH` not set | Open a project in VS Code with ModusToolbox extension active |

## Notes

- The wrapper script uses the `mtbskill` bootstrap to locate `CY_TOOLS_PATH` and the `project-creator-cli` binary automatically.
- Board IDs and app IDs are case-sensitive — always use the exact identifiers from the manifest.
- The created project will include all necessary BSP files, middleware references, and a default `Makefile`.
