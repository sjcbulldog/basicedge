---
name: mtb-list-available-libraries
description: 'Lists ModusToolbox middleware libraries available for this workspace, including category, GitHub URL, latest version, description, and whether already installed.  Also provides the exact command-line workflow to add a library to a project.  USE FOR: answering questions like "what libraries are available?", "which middleware can I add?", "how do I add library X?", "list all available ModusToolbox libraries".  DO NOT USE FOR: listing already-installed libraries (use the deps/ or libs/ folders for that).'
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# Skill: mtb-list-available-libraries

## Purpose

Call the Infineon ModusToolbox AI Assistant server command `list_available_libraries` and return the full
result to the AI agent. The output is a YAML document containing:

- All **used** libraries (already referenced by a project): `middleware_id`,
  `category`, `remote_uri`, `local_uri`, `used_by_projects`, `version_tags`,
  `description`
- All **unused** libraries (available to add): `middleware_id`, `category`,
  `remote_uri`, `version_tags`, `description`
- A **summary** with counts by category

## When to Invoke

Invoke this skill when the user asks any of:

- "What libraries are available?"
- "List available ModusToolbox middleware"
- "Which middleware can I add to my project?"
- "How do I add library X to my project?"
- "What is the GitHub URL for library X?"
- "What version of library X is available?"

## How to Run

Execute the bundled Python script from the **workspace root** and capture
its output:

```
python .github/skills/mtb-tools/scripts/list_available_libraries.py
```

The script:

1. Walks up from its own location to find `.modustoolbox/mtb_env.py`
2. Loads it to get `EXTENSION_DIR` and adds `mtblib` to `sys.path`
3. Calls the `list_available_libraries` Infineon ModusToolbox AI Assistant server command via `TCP_PORT`
4. Prints the **full** result (not filtered)

## How to Add a Library to a Project

The Infineon ModusToolbox AI Assistant output does NOT include the command-line workflow. Use these steps:

1. Find the library in the output: note its `remote_uri` and the first entry
   in `version_tags` (that is the latest version)
2. Middleware libraries are added **per project**
3. Create the `.mtb` file at `<project>/deps/<middleware_id>.mtb` with this content:
    ```
    <remote_uri>#<version_tag>#$$ASSET_REPO$$/<middleware_id>/<version_tag>
    ```
4. Fetch libraries from workspace root:
    ```
    python .github/skills/mtb-tools/scripts/run_make.py --args getlibs
    ```
    Dependencies are resolved automatically; `<project>/libs/` is updated.
    Library source is downloaded to `../mtb_shared/`.
5. Add the component to `<project>/Makefile`:
    ```makefile
    COMPONENTS+=COMPONENT_MW_<MIDDLEWARE_ID_UPPERCASE_UNDERSCORES>
    ```
6. Rebuild from workspace root:
    ```
    python .github/skills/mtb-tools/scripts/run_make.py --args build
    ```

### Example — adding `retarget-io` to a project

1. From the skill output, note: `remote_uri` = `https://github.com/cypresssemiconductorco/retarget-io`, `version_tag` = `release-v1.9.0`
2. Create `<project>/deps/retarget-io.mtb` with content:
   ```
   https://github.com/cypresssemiconductorco/retarget-io#release-v1.9.0#$$ASSET_REPO$$/retarget-io/release-v1.9.0
   ```
3. Fetch libraries:
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args getlibs
   ```
4. Add to `<project>/Makefile`: `COMPONENTS+=COMPONENT_MW_RETARGET_IO`
5. Rebuild:
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args build
   ```

## Error Handling

If the script exits with a non-zero code or prints an `ERROR:` line, the Infineon ModusToolbox AI Assistant
extension may not be running. Ask the user to verify the Infineon ModusToolbox AI Assistant
extension is active for this workspace.

- "What libraries are available?"
- "List available ModusToolbox middleware"
- "Which middleware can I add to my project?"
- "How do I add library X to my project?"
- "What is the GitHub URL for library X?"
- "What version of library X is available?"

## How to Run

Execute the bundled Python script from the **workspace root** and capture
its output:

```
python .github/skills/mtb-tools/scripts/list_available_libraries.py
```

The script walks up from its own location to find
`.modustoolbox/docs/library-catalog.yaml` and prints the full file content.

## Interpreting the Output

The output is a YAML document with these top-level sections:

### `metadata`

Workspace path, MCU device, BSP name, project names.

### Workflow comments (at the top of the file)

Step-by-step instructions for adding a library to a project under the workspace:

1. Find the library's `remote_uri` and latest `version_tags` entry in the catalog
2. Create `<project>/deps/<middleware_id>.mtb` with content:
   `<remote_uri>#<version_tag>#$$ASSET_REPO$$/<middleware_id>/<version_tag>`
3. Add `COMPONENTS+=COMPONENT_MW_<UPPERCASE_ID>` to the project `Makefile`
4. Run `make getlibs` from the workspace root
5. Rebuild: `make clean && make build` from the workspace root

### `summary`

Counts of used/unused libraries by category.

### `used_libraries`

Libraries already referenced by a project. Fields:

- `middleware_id` — library identifier
- `category` — e.g. Core, Middleware, Bluetooth®, Wi-Fi, Peripheral, Utilities
- `used_by_projects` — which projects use it
- `remote_uri` — GitHub URL
- `local_uri` — path in `../mtb_shared/`
- `version_tags` — installed version (and how many others are available)
- `description` — what the library does

### `unused_libraries`

Libraries available to download and add. Fields:

- `middleware_id`, `category`, `remote_uri`, `version_tags`, `description`
  (same as above but without `local_uri` / `used_by_projects`)

## Adding a Library (example)

To add `retarget-io` to a project:

1. Get `remote_uri` and latest version from the catalog output
2. Create `<project>/deps/retarget-io.mtb` with content:
   ```
   https://github.com/cypresssemiconductorco/retarget-io#release-v1.9.0#$$ASSET_REPO$$/retarget-io/release-v1.9.0
   ```
3. Fetch the library:
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args getlibs
   ```
4. Add to `<project>/Makefile`: `COMPONENTS+=COMPONENT_MW_RETARGET_IO`
5. Rebuild:
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args build
   ```

## Error Handling

If the script exits with a non-zero code or prints an `ERROR:` line, the catalog
file has not been generated yet. Ask the user to run the Infineon ModusToolbox AI Assistant extension
or use `Command Palette > ModusToolbox: Generate AI Instructions` to regenerate it.
