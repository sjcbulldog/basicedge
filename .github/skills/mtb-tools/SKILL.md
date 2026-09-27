---
name: mtb-tools
description: Core ModusToolbox build, project creation, and GUI tool launcher scripts. Use this skill when you need to build firmware, run make targets, create projects, list boards/libraries/examples, or launch Device Configurator and other GUI tools.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# ModusToolbox Tools Skill — Build, Create, Launch

This skill provides Python wrapper scripts that communicate with the ModusToolbox MCP server to build projects, manage libraries, create new projects, and launch GUI tools.

## How to Invoke These Tools

Run all scripts from the workspace root:

```bash
# Build the project:
python .github/skills/mtb-tools/scripts/run_make.py --filter-progress --args build

# Build with environment variables:
python .github/skills/mtb-tools/scripts/run_make.py --filter-progress --args build CONFIG=Debug

# Get libraries (after adding .mtb files):
python .github/skills/mtb-tools/scripts/run_make.py --args getlibs

# Clean:
python .github/skills/mtb-tools/scripts/run_make.py --args clean

# Program the board:
python .github/skills/mtb-tools/scripts/run_make.py --args program

# Build with working directory override:
python .github/skills/mtb-tools/scripts/run_make.py --filter-progress --working-dir proj_cm55 --args build

# Open Device Configurator:
python .github/skills/mtb-tools/scripts/run_tool.py config

# Open Library Manager:
python .github/skills/mtb-tools/scripts/run_tool.py library-manager

# List available boards:
python .github/skills/mtb-tools/scripts/list_available_boards.py

# List code examples for a board:
python .github/skills/mtb-tools/scripts/list_code_examples.py --board-id <board-id>

# Describe a code example (fetch metadata from GitHub):
python .github/skills/mtb-tools/scripts/describe_code_example.py <repo-name>

# List available libraries:
python .github/skills/mtb-tools/scripts/list_available_libraries.py --board-id <board-id>

# Find ModusToolbox installation:
python .github/skills/mtb-tools/scripts/list_modustoolbox_location.py

# Create a new project from template:
python .github/skills/mtb-tools/scripts/project_creator_wrapper.py \
    --board-id <board-id> --app-id <template-id> \
    --target-dir <output-dir> --user-app-name <project-name>

# List skills available for a board (JSON output, best for AI; --board-name defaults to the current session board):
python .github/skills/mtb-tools/scripts/list_skills_for_board.py --json --board-name <board-name>
```

**Windows note:** Use `python` (NOT `python3`).

## Available Scripts

### `run_make.py`
Run arbitrary make targets (build, clean, program, getlibs, vscode, etc.) via the MCP server. Handles modus-shell, CY_TOOLS_PATHS, and platform differences automatically.

**Arguments:**
- `--args` (required, remainder): Make target and arguments. Each argument must be a separate token.
- `--working-dir` (optional): Override working directory (for sub-projects)
- `--env-vars` (optional): Key-value pairs for environment variables
- `--filter-progress` (optional): Collapse ninja `[N/M] ...` progress lines to cut output volume. Use it for the `build` target.
- `--heartbeat-secs` (optional): Emit a liveness pulse to stderr after this many seconds with no output, so a silent slow link/codegen phase is not mistaken for a hang. Default 15; `0` disables.

**Important:**
- `--working-dir`, `--env-vars`, `--filter-progress` and `--heartbeat-secs` MUST appear BEFORE `--args`
- Do NOT quote multiple args together: `--args build CONFIG=Debug` (correct), NOT `--args "build CONFIG=Debug"` (wrong)
- Always use `timeout=0` for build, getlibs, and clean commands
- Do NOT invoke modus-shell bash directly — `run_make.py` handles all environment setup

### `run_tool.py`
Launch ModusToolbox GUI configurators (Device Configurator, Library Manager, etc.).

**Arguments:**
- `tool_name` (positional): Tool to launch (e.g., `config`, `library-manager`)

### `list_available_boards.py`
List all boards available for project creation from the ModusToolbox manifest.

### `list_code_examples.py`
List code examples available for a specific board.

**Arguments:**
- `--board-id` (optional): Filter examples for a specific board

### `describe_code_example.py`
Fetch live metadata from Infineon code example repos on GitHub (descriptions, dependencies, COMPONENTS).

**Arguments:**
- Positional repo names (one or more)
- `--source`: List source files in the example
- `--read <path>`: Read specific source files

### `list_available_libraries.py`
List middleware libraries available for a board.

**Arguments:**
- `--board-id` (optional): Filter libraries for a specific board

### `list_modustoolbox_location.py`
Return the installed ModusToolbox tools path.

### `project_creator_wrapper.py`
Create a new project from a template using the project-creator-cli.

**Arguments:**
- `--board-id` (required): Target board identifier
- `--app-id` (required): Template/application identifier
- `--target-dir` (required): Output directory
- `--user-app-name` (required): Project name

### `list_skills_for_board.py`
List the AI skills (name + description) that would be installed for a board. Queries the extension server directly (not MCP), since manifest/skill resolution lives in the extension.

**Arguments:**
- `--board-name` (optional): Board to list skills for. Defaults to the current session board. May be exact or partial; it is matched against the manifests' board globs.
- `--json` (optional): Emit the skills as JSON. Prefer this for AI consumption.

## Shared Module: `mtbskill.py`

All scripts depend on `mtbskill.py` (co-located in `scripts/`). This module locates the MCP server session and provides `bootstrap_skill()` for connecting to the running ModusToolbox extension.

## Important Notes

- **Build/getlibs are recursive makes** that build each sub-project and can take several minutes. Wait for the exit code.
- **Do NOT invoke modus-shell bash directly.** The `run_make.py` script handles all environment setup internally.
- **Do NOT construct raw bash commands** — it is error-prone and unnecessary.
- **Always verify build output** (exit_code == 0) before proceeding to program.
