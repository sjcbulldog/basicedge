---
name: mtb-library-installer
description: Install a ModusToolbox library or middleware package into the current project using library-manager-cli, then launch an integration sub-agent to complete code changes. Use when asked to add a library, enable a specific feature that requires middleware (e.g., "add retarget-io", "enable WiFi support", "use MQTT", "add printf/stdio support"), integrate a middleware component, or when a library-tier skill identifies a required dependency that is not yet installed in the project.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# Library Installer

Install ModusToolbox libraries and middleware packages into the current project. This skill creates a `.mtb` dependency reference file in `deps/`, runs `make getlibs` to fetch the library source, and dispatches an integration sub-agent to complete all code changes needed to use the library.

## How It Works

MTB library installation is a two-step process:

1. **Add the `.mtb` reference** — A `.mtb` file is created in the project's `deps/` directory. It contains a single line with the library's GitHub URL, version tag, and local storage path. The bundled wrapper script (`scripts/library-mgr-wrapper.py`) handles this step.
2. **Fetch the source** — `make getlibs` reads all `.mtb` files and downloads library source to `../mtb_shared/<library-name>/<version>/`.

> **Note on `library-manager-cli`:** The `library-manager-cli` tool in the MTB toolchain manages BSP operations (adding/switching BSPs). It does not provide a CLI command for adding library dependencies. Library `.mtb` file creation is handled exclusively by the bundled wrapper script.

> ⛔ **CRITICAL — Version Pinning:** `.mtb` files committed to a project MUST use a **fixed release tag** (e.g., `release-v6.1.0`), NEVER a floating `latest-*` tag (e.g., `latest-v6.X`). Floating tags cause unintended library updates whenever `make getlibs` is called for any reason — including adding an unrelated library. Code Examples on GitHub use `latest-*` tags for convenience, but the ModusToolbox project creation flow resolves these to fixed versions at creation time. The agent must do the same: resolve the current version from the manifest (`VersionTags[0]`) or by querying GitHub tags, then use that fixed version in the `.mtb` file.

**Never** write or modify `.mtb` files directly from the skill — always use the bundled wrapper script to add library references.

## When to Use This Skill

- User asks to add a library: "add retarget-io", "install mqtt", "add freertos to my project"
- User asks to enable feature functionality requiring middleware: "enable printf", "add WiFi support", "set up an MQTT client", "add stdio support for serial debugging"
- User asks to integrate a middleware component into a ModusToolbox project
- A library-tier skill identifies a dependency that is not yet installed in `deps/`
- The user's project is missing a library needed for a code integration in progress

## Prerequisites

- A ModusToolbox project with a valid `Makefile` at the project root
- `make` available in the project (used to run `make getlibs`)
- `$CY_TOOLS_PATHS` set (ModusToolbox standard environment variable pointing to the MTB tools installation)
- This skill is pre-installed at `.github/skills/mtb-library-installer/` by the Infineon ModusToolbox for VS Code extension "Generate AI Instructions" command
- Library-tier integration skills for all compatible libraries are also pre-installed at `.github/skills/<library-name>/` by Infineon ModusToolbox AI Assistant — no additional skill registration is required after library installation

## Key Files

| File / Directory | Purpose |
| ---------------- | ------- |
| `deps/<library-name>.mtb` | Library reference file created by the wrapper script — **never modify directly** |
| `libs/<library-name>.mtb` | Legacy reference location used by some older projects — check here if `deps/` is absent |
| `../mtb_shared/<library-name>/<version>/` | Library source fetched by `make getlibs`; shared across all projects in the workspace |
| `.github/skills/<library-name>/` | Pre-installed integration skill for the library (installed by Infineon ModusToolbox AI Assistant) |
| `.github/skills/mtb-library-installer/scripts/library-mgr-wrapper.py` | Bundled wrapper script — the only supported method for creating `.mtb` reference files |

## Library Resolution

When the user's request does not directly name a canonical MTB library identifier, map the intent before proceeding:

| User Intent | MTB Library Identifier | Notes |
| --- | --- | --- |
| "printf", "stdio UART", "debug output", "serial console" | `retarget-io` | |
| "WiFi", "TCP/IP", "networking", "LwIP", "network stack" | `wifi-core-freertos-lwip-mbedtls` | Requires FreeRTOS |
| "MQTT", "MQTT client" | `mqtt` | Depends on `wifi-core-freertos-lwip-mbedtls` |
| "FreeRTOS", "RTOS" | `freertos` | |
| "TLS", "secure sockets", "mbedTLS" | `wifi-core-freertos-lwip-mbedtls` | Bundled |

- If the mapping is ambiguous, present the best-guess library name and ask for confirmation before proceeding.
- Library identifiers must match the MTB repository name exactly as used in `.mtb` files (e.g., `retarget-io`, not `cy-retarget-io` or `Retarget IO`).

Once the library identifier is resolved, use the `mtb-list-available-libraries` skill (provided by Infineon ModusToolbox AI Assistant) to obtain the canonical GitHub URI, latest BSP-compatible version tag, and live dependency list. **Always prefer manifest data over defaults.** If the skill fails or the library is not in the result, see [Troubleshooting](#troubleshooting).

## Step-by-Step Workflows

### Workflow 1 — Standalone Library Installation

1. **Resolve the library ID** from the user's request using the mapping table above. If inferred (not directly stated by the user), present the identified library and ask the user to confirm before proceeding to step 2.
2. **Check for existing installation**: look for `deps/<library-name>.mtb` (or `libs/<library-name>.mtb`). If found, inform the user it is already installed and skip directly to step 9.
3. **Query the manifest for EACH library independently** — use the `mtb-list-available-libraries` skill. From the result, locate the entry whose `MiddlewareId` matches the library identifier and extract:
   - `RemoteURI` → the GitHub URL to pass as `--url`
   - `VersionTags[0]` → the fixed version tag to pass as `--version`
   - `Dependencies` → list of dependency library IDs

   > ⚠️ **Query once per library.** Do NOT assume related libraries share version schemes. Each library has its own release cadence and tagging convention. Always query and use the manifest result for each library individually.
4. **Identify dependencies**: check `deps/` for each dependency from step 3. List all libraries to be installed (primary + dependencies) in the confirmation message.
5. **Confirm with the user** using the [User Instruction Pattern](#user-instruction-pattern). Await the user's response before proceeding.
6. **Install dependencies first**: for each missing dependency, repeat step 3 for that dependency, then invoke the wrapper:
   ```
   python .github/skills/mtb-library-installer/scripts/library-mgr-wrapper.py \
       --add <dependency-name> --url <RemoteURI> --version <VersionTag> --project <project-path>
   ```
   Check the exit code after each call — stop and report stderr output on any non-zero exit; do not proceed to `make getlibs`.
7. **Install the requested library**:
   ```
   python .github/skills/mtb-library-installer/scripts/library-mgr-wrapper.py \
       --add <library-name> --url <RemoteURI> --version <VersionTag> --project <project-path>
   ```
   Check the exit code — stop and report stderr on non-zero exit.
8. **Run `make getlibs`** via the `run_make.py` tool: execute once after **all** wrapper calls are complete. Examine any stderr output for errors. Stop and report on failure.

   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args getlibs
   ```

   > **Timing expectation:** `make getlibs` may take 2–4 minutes on first run (cloning repos). Subsequent runs are faster (cached in `mtb_shared/`). Do not assume failure based on execution time alone — wait for the command to complete.

   > ⛔ **NEVER terminate `make getlibs` early.** The process may appear stalled while resolving transitive dependencies or running post-clone scripts. Presence of library folders in `mtb_shared/` does NOT indicate completion — those folders may pre-exist from other projects. `make getlibs` also generates project-local makefiles, symlinks, and dependency tracking files that are ONLY written at the end of the process. Killing it mid-run leaves the project in an inconsistent state that causes cryptic build failures.
9. **Verify installation** per [Post-Install Verification](#post-install-verification).
10. **Dispatch integration sub-agent** per [Integration Sub-Agent Handoff](#integration-sub-agent-handoff).

### Workflow 2 — Sub-skill Invocation (called by a library-tier skill)

When a library-tier skill calls this skill to satisfy a dependency, the calling skill provides the exact MTB library identifier. Skip steps 1 and 5 (resolution and user confirmation) — proceed directly:

1. Check whether the dependency is already present in `deps/` — if so, skip to step 5.
2. Use the `mtb-list-available-libraries` skill and extract `RemoteURI`, `VersionTags[0]`, and any transitive `Dependencies` for the given library identifier.
3. Install any transitive dependencies missing from `deps/` (repeat step 2 for each).
4. Invoke the wrapper to install the library (step 7 from Workflow 1, using URL and version from step 2).
5. Run `make getlibs` via `run_make.py` (step 8 from Workflow 1).
6. Verify installation per [Post-Install Verification](#post-install-verification).
7. Return control to the calling library-tier skill.

> **Note:** Library-tier skills that call this skill as a sub-skill are pre-installed by Infineon ModusToolbox AI Assistant at `.github/skills/<library-name>/`. No skill locator registration is needed.

### Workflow 3 — Removing a Library

When a library needs to be removed (e.g., discovered to be unnecessary, replaced by another, or causing conflicts):

1. **Delete the `.mtb` file** from `deps/` (or `libs/` for legacy projects):
   ```
   rm deps/<library-name>.mtb
   ```
2. **Run `make getlibs`** via `run_make.py` — this updates the build system's internal library tracking and regenerates include paths. The library source in `../mtb_shared/` is NOT deleted (it's a shared cache), but it will no longer be referenced by this project's build.
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args getlibs
   ```
3. **Run `make clean`** — stale object files and include path caches from the removed library must be cleared.
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args clean
   ```
4. **Rebuild** — confirm no unresolved includes or linker errors.
   ```
   python .github/skills/mtb-tools/scripts/run_make.py --args build
   ```

> **If build fails after removal:** The removed library may have provided headers that other libraries depend on. Check if the required headers are provided by a remaining library (e.g., `btstack-integration` provides many of the same headers as `bluetooth-freertos`). If not, the removal may require also removing or replacing the dependent code.

> **Do NOT delete from `../mtb_shared/`** — it's a workspace-level cache shared across projects. Other projects may still use it. The build system handles project-level scoping via the `.mtb` files.

## Dependency Handling

Some MTB libraries require additional libraries to be present. Use the **live dependency data from the manifest** (via `list_available_libraries`) rather than a static table — dependency versions and transitive requirements change between MTB releases.

1. Before installing the requested library, extract its `Dependencies` list from the `list_available_libraries` result.
2. For each dependency, check `deps/` for an existing `.mtb` file — skip already-installed dependencies.
3. For each missing dependency, also call `list_available_libraries` to get its URI, version, and transitive dependencies.
4. Install all missing dependencies via the wrapper **before** running `make getlibs`.
5. Include all libraries to be installed (primary + dependencies) in the user confirmation message so the user understands the full scope.

### ⚠️ Transitive Dependencies — Do NOT Add Manually

Many Infineon libraries are **wrappers** that auto-resolve their underlying driver as a transitive dependency via `make getlibs`. Do NOT manually create a `.mtb` file for a library that is already pulled in transitively — this causes duplicate symbol errors at link time.

**How to verify before adding a dependency:** Inspect a Reference Code Example that uses the same library. Its `deps/` folder shows exactly which `.mtb` files are needed and which are resolved transitively. If the reference CE does NOT have a `.mtb` for a library, you should not add one either.

**Common wrapper patterns:**

| Wrapper Library | Auto-resolved Dependency | What Happens If Added Manually |
|---|---|---|
| `sensor-motion-bmi270` | `BMI270_SensorAPI` | Duplicate symbols (`bmi2_init`, `bmi270_init`, etc.) |
| `sensor-motion-bmi160` | `BMI160_SensorAPI` | Duplicate symbols |
| `wifi-core-freertos-lwip-mbedtls` | `lwip`, `mbedtls`, `wifi-host-driver`, etc. | Version conflicts, duplicate definitions |
| `btstack-integration` | `btstack` | Duplicate BT stack symbols |

**Signs you've added a transitive dependency manually:**
- Linker errors with "multiple definition of" for library-internal functions
- Two directories for the same library in `mtb_shared/` (e.g., both `bmi270/` and `BMI270_SensorAPI/`)
- A `.cyignore` file you had to create to suppress build errors from example/test code in the transitive library

**Fix:** Remove the manually-added `.mtb` file, delete the duplicate directory from `mtb_shared/`, and re-run `make getlibs`.

## User Instruction Pattern

When the library name was **inferred** (not directly named by the user), confirm before proceeding:

```
I've identified that your request requires the `<library-name>` library.
Here's what I'll do:

1. Install `<library-name>` into your project's `deps/` directory
[2. If dependencies are needed:] Also install required dependencies: <dep1>, <dep2>
[3.] Run `make getlibs` to fetch the library source
[4.] Launch a sub-agent to generate the required code changes

Would you like me to proceed? <Await user response>
```

Upon completion:

```
The `<library-name>` library has been installed. Starting a sub-agent to
complete code integration now.
```

**Multiple libraries:** List all libraries (primary + dependencies) in the pre-confirmation message so the user understands the full scope before proceeding.

## Post-Install Verification

After `make getlibs` completes, verify before dispatching the integration sub-agent:

1. Confirm `deps/<library-name>.mtb` exists (or `libs/<library-name>.mtb` for legacy projects).
2. Confirm library source is present in `../mtb_shared/<library-name>/` — this is a post-install confirmation only.
3. Repeat steps 1–2 for each dependency installed alongside the primary library.

> ⛔ **`../mtb_shared/` is NOT a source of truth for project dependencies.**
>
> - Do NOT browse `mtb_shared` to discover what libraries are "available" or to infer what a project uses.
> - Do NOT let the contents of `mtb_shared` influence which libraries to install or skip.
> - `mtb_shared` is a **workspace-level cache** shared across all projects. It accumulates libraries from every project ever built in the workspace, including failed experiments and deleted projects. Its contents are noise.
> - The ONLY authoritative source for a project's dependencies is the `deps/` directory (`.mtb` files).
> - The ONLY authoritative source for what libraries are needed is the skill guidance + manifest query (`mtb-list-available-libraries`).
> - Always create the `.mtb` file and run `make getlibs` regardless of what exists in `mtb_shared`.

If any check fails, do not proceed — report the failure and refer to the [Troubleshooting](#troubleshooting) table.

## Integration Sub-Agent Handoff

After verification passes, dispatch an integration sub-agent to complete code changes.

**When to dispatch:** Only after `make getlibs` succeeds and all post-install verification checks pass.

> **Note on skill availability:** Library-tier integration skills are pre-installed by Infineon ModusToolbox AI Assistant at `.github/skills/<library-name>/` and are already discoverable by Copilot. The sub-agent will find and use the correct skill automatically without any locator registration.

**Handoff prompt template:**

> The `<library-name>` library has been installed into the project. Generate all code changes necessary to fully integrate `<library-name>` into the `<target-core-subproject>` application.

**Dependency ordering:** When multiple libraries were installed (e.g., `mqtt` + `wifi-core-freertos-lwip-mbedtls`), include all library names in the handoff prompt and note any integration ordering constraints (e.g., integrate `wifi-core-freertos-lwip-mbedtls` before `mqtt`).

## Troubleshooting

| Condition | Action |
| --------- | ------ |
| Library identifier cannot be resolved from user's request | Present best-guess library name; ask user to confirm before proceeding |
| Skill `mtb-list-available-libraries` fails or Infineon ModusToolbox AI Assistant is not running | Warn user that manifest data is unavailable; fall back to default Infineon URL but **resolve the actual version tag** by checking the library's GitHub tags (e.g., `git ls-remote --tags <url> 'release-v6.*'`). Never commit a `latest-*` floating tag to a `.mtb` file — it causes unintended library updates on future `make getlibs` calls. If unable to resolve, inform the user and ask them to provide the version. |
| Library not found in `list_available_libraries` result | Inform user the library is not available for the current board/BSP; suggest verifying with the Library Manager GUI; do not silently fall back to a default URL |
| `library-mgr-wrapper.py` exits non-zero | Report the stderr output from the wrapper; do not run `make getlibs`; stop and inform user |
| Version conflict with an existing dependency | Report the specific conflict; ask user whether to update the conflicting dependency or abort |
| Library already installed (`.mtb` file exists in `deps/`) | Inform user it is already present; skip installation; proceed directly to the integration sub-agent |
| `make getlibs` fails | Report error output; check for common causes (network error, missing manifest entry, `$CY_TOOLS_PATHS` not set); stop and inform user |
| `../mtb_shared/<library-name>/` absent after `make getlibs` succeeds | Re-run `make getlibs` once; if still absent, report the inconsistency and ask user to manually inspect the project |

## References

- [Library Manager User Guide (Infineon)](https://www.infineon.com/assets/row/public/documents/30/44/infineon-modustoolbox-library-manager-user-guide-usermanual-en.pdf?fileId=8ac78c8c92416ca5019277a008f8238c)
