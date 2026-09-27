---
name: mtb-dev-rules
description: Universal development rules for ModusToolbox project implementation. Applies to all device families during code generation and peripheral integration.
applyTo: "**/*.c,**/*.h,**/Makefile,**/*.mtb"
---

# Universal Development Rules

These rules apply to ALL ModusToolbox™ development work regardless of device family. They are non-negotiable and override any conflicting guidance from plans or user prompts.

---

## ⛔ CRITICAL INVARIANTS (must be followed in EVERY response)

These cause **total implementation failure** if missed. Read these first.

### 1. API Family — cyhal ≠ PDL

| Device          | Peripheral Drivers                                     | Middleware (retarget-io, WiFi) |
| --------------- | ------------------------------------------------------ | ------------------------------ |
| PSOC Edge E84   | PDL: `Cy_SCB_*`, `Cy_TCPWM_*`, `Cy_DMA_*`, `Cy_GPIO_*` | MTB-HAL: `mtb_hal_*`           |
| PSOC Control C3 | PDL: `Cy_SCB_*`, `Cy_TCPWM_*`, `Cy_GPIO_*`             | MTB-HAL: `mtb_hal_*`           |
| PSOC 6          | HAL: `cyhal_*` (handles init internally)               | HAL: `cyhal_*`                 |

**⛔ NEVER use `cyhal_*` APIs on PSOC Edge or PSOC Control.** They do not exist.

### 2. Script Execution — CWD Must Be Project Root

**ALL skill scripts resolve paths relative to CWD.** Running from the wrong directory = silent failure.

```
# CORRECT:
cd <project-path> && python .github/skills/mtb-tools/scripts/run_tool.py config
cd <project-path> && python .github/skills/mtb-tools/scripts/run_make.py --args build -j8

# WRONG — will fail:
python .github/skills/mtb-tools/scripts/run_tool.py config
python C:\Users\...\run_make.py --args build    # absolute path without cd
```

### 3. Read Skill Files Before Implementing

**Before writing ANY peripheral code, read the matching skill reference file.** Your training data contains outdated PSOC 6 patterns that fail on PSOC Edge.

1. Browse `.github/skills/mtb-device-configurator/references/` for `*-patterns.md` files
2. Read the file and follow patterns EXACTLY — they are hardware-verified
3. If no skill file exists, tell the user and ask for guidance

### 4. Build ≠ Verified — Hardware Test Required

```
⛔ WRONG: "Build passed → phase complete"
✅ RIGHT: "Build passed → ask user to flash → confirm hardware behavior"
```

A passing `make build` means ONLY that syntax is correct. Logic errors, wrong init sequences, and misconfigured peripherals all compile clean. You MUST output the `⏸️ FLASH CHECK` prompt and wait for user response before proceeding.

### 5. FreeRTOS Tickless + SCB Peripherals

If `configUSE_TICKLESS_IDLE == 1` in FreeRTOSConfig.h, you MUST register a SysPm callback for each active SCB peripheral (UART, SPI, I2C) to block DeepSleep during active transfers. Without this, Deep Sleep corrupts pending transmissions.

**Symptom:** First few printf lines work, then output becomes garbled (`BþBBBB...`).

See `uart-runtime-patterns.md`, `spi-transfer-patterns.md`, `i2c-bus-patterns.md` for callback code.

### 6. FreeRTOS BASEPRI Masking

If the project links FreeRTOS libraries (even without starting the scheduler), BASEPRI may silently mask ALL peripheral interrupts at priority ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`. See `freertos-interrupt-priority.md`.

---

## Implementation Workflow

### Skill Plan — Declare Before Implementing

Before writing code in a new phase, declare which skill files you will reference:

```
📋 SKILL PLAN — Phase [N]:
Skills I will reference:
- [ ] timer-counter-patterns.md — Periodic timer with TC interrupt
- [ ] gpio-interrupt-patterns.md — Button ISR for user input
```

Browse `.github/skills/` to discover available files. If no match exists, state that explicitly.

**⛔ Sub-reference completeness:** When a skill references sub-files marked MANDATORY or contains a Gates table, you MUST read and complete ALL referenced gates — reading only the top-level SKILL.md does NOT satisfy this requirement.

### Peripheral Runtime Patterns — Quick Reference

> **⚠️ If FreeRTOS is present:** Also read `mtb-freertos/SKILL.md` and `mtb-freertos/references/config-guide.md`.

| Use Case              | Pattern File                      | Key Content                              |
| --------------------- | --------------------------------- | ---------------------------------------- |
| GPIO interrupts / ISR | `gpio-interrupt-patterns.md`      | Port-shared IRQ, debounce, mask handling |
| UART send/receive     | `uart-runtime-patterns.md`        | Ring buffer, deep-sleep protection       |
| Multi-core UART       | `multi-core-debug-output.md`      | UART ownership, IPC channel              |
| I2C transactions      | `i2c-bus-patterns.md`             | Multi-byte read, NAK, bus recovery       |
| SPI transfers         | `spi-transfer-patterns.md`        | CS control, DMA chaining                 |
| PWM control           | `pwm-runtime-patterns.md`         | Duty update, alignment, dead-time        |
| Timer / Counter       | `timer-counter-patterns.md`       | Periodic timer, capture, event counting  |
| DMA setup             | `dma-patterns.md`                 | Descriptor chains, trigger routing       |
| Audio I2S/PDM         | `audio-pdm-i2s-patterns.md`       | Clock master, DMA double-buffer          |
| SD/SDIO               | `sdhc-card-lifecycle-patterns.md` | Card detect, init, error recovery        |
| ADC / Analog          | `autanalog-sampling-patterns.md`  | Scan mode, trigger, calibration          |
| Power modes           | `power-mode-patterns.md`          | HP/LP/ULP, DeepSleep, Hibernate          |
| Memory power          | `memory-power-patterns.md`        | PD1 disable, SRAM, SoCMEM                |

All paths relative to `.github/skills/mtb-device-configurator/references/`.

> ⚠️ Pattern files contain **use-case recipes and gotchas** — NOT API signatures. Verify function signatures against the actual PDL headers in `mtb_shared/`.

### Resource Resolution Order

| Priority    | Source                                  | What It Provides                        |
| ----------- | --------------------------------------- | --------------------------------------- |
| 1 (highest) | Installed headers + BSP `cycfg_*.h`     | Exact API signatures — **ground truth** |
| 2           | Skill reference files (`*-patterns.md`) | Ordering, architecture, gotchas         |
| 3           | Plan's "Required Skill References"      | Points to skills                        |
| 4           | Plan's "Design Decisions"               | Architectural choices                   |
| 5 (lowest)  | Training knowledge                      | **NEVER use for PSOC peripheral code**  |

When a skill template's function signature conflicts with the installed header, **the header wins**.

### Device Configurator — MANDATORY Tool Invocation

When presenting DC instructions, you MUST invoke `vscode/askQuestions` (the tool, not plain text):

```yaml
vscode/askQuestions:
    question: "Device Configurator settings are ready. Would you like me to open Device Configurator now?"
    options:
        - "Yes — open Device Configurator"
        - "I'll open it myself"
        - "Skip — already configured"
```

After invoking: **STOP.** Do NOT write code or settings until the user responds.

> ⚠️ **CWD REQUIREMENT:** `cd <project-path> && python .github/skills/mtb-tools/scripts/run_tool.py config`

If response contains DC instructions AND code in the same message → gate violation.

### BSP-First Rule

Before opening Device Configurator:

1. Check `GeneratedSource/cycfg_peripherals.h` for `CYBSP_*` defines
2. If a matching config exists (e.g., `CYBSP_DEBUG_UART_HW`), use it directly
3. Only open DC for peripherals NOT pre-configured or needing different settings

### TBD Markers — MUST Ask, Never Infer

When the plan contains TBD markers, use `vscode/askQuestions` to resolve EVERY one BEFORE implementation. Never infer from BSP defaults — TBD means the user must choose.

### Phase Verification

After each phase, output a verification block:

**[BUILD] phases** (no observable output): Output `✅ PHASE [N] COMPLETE — BUILD PASSED` and proceed.

**[FLASH] phases** (observable hardware behavior): MUST use `vscode/askQuestions`:

```yaml
vscode/askQuestions:
    question: "Phase [N] build passed. Would you like to flash and verify on hardware?"
    options:
        - "Yes — flash and verify"
        - "Skip — proceed without verification"
        - "Done for now — stop here"
```

⛔ You are PROHIBITED from writing Phase N+1 code until the user responds.

**Session override:** If user says "always flash" or "always skip," respect for the session.

---

## Build Environment

### Build Commands — Use `run_make.py`

**CRITICAL: Use the `run_make.py` skill tool for ALL make commands.** This script handles modus-shell, CY_TOOLS_PATHS, and platform differences automatically via the MCP server connection.

**ARGUMENT ORDER: `--args` MUST be the LAST flag.** It consumes all remaining arguments. Any flags after `--args` are passed to `make` instead of being parsed by the script.

```
# Build (from workspace root):
python .github/skills/mtb-tools/scripts/run_make.py --args build

# Get libraries (run ONCE after all .mtb files are added):
python .github/skills/mtb-tools/scripts/run_make.py --args getlibs

# Clean:
python .github/skills/mtb-tools/scripts/run_make.py --args clean

# Program the board:
python .github/skills/mtb-tools/scripts/run_make.py --args program

# Run make in a subdirectory (e.g., multi-core project):
python .github/skills/mtb-tools/scripts/run_make.py --working-dir proj_cm33_ns --args getlibs

# Build with make variable overrides (e.g., Debug config):
python .github/skills/mtb-tools/scripts/run_make.py --args build CONFIG=Debug
```

**Rules:**

- `--args` uses REMAINDER — place `--working-dir` and `--env-vars` BEFORE it
- Never quote multiple args together (`--args "build CONFIG=Debug"` is WRONG, use `--args build CONFIG=Debug`)
- Use `python` (NOT `python3`) on Windows
- Do NOT invoke modus-shell directly — `run_make.py` handles environment setup

**ModusToolbox tools path:** Read from `.vscode/settings.json` (`modustoolbox.toolsPath`). ⛔ NEVER hardcode paths.

**Do NOT quote multiple make arguments together.** Each argument must be separate: `--args CONFIG=Release build` (correct), NOT `--args "CONFIG=Release build"` (wrong).

### GUI Tools — Use `run_tool.py`

**Launch Device Configurator, Library Manager, and other GUI tools using `run_tool.py` per the mtb-tools skill:**

```
# Launch Device Configurator (from workspace root):
python .github/skills/mtb-tools/scripts/run_tool.py config

# Launch Library Manager:
python .github/skills/mtb-tools/scripts/run_tool.py library-manager
```

**Common Errors:**
| Error | Cause | Fix |
|-------|-------|-----|
| `make: unrecognized option '--working-dir'` | `--args` not last — it swallowed `--working-dir` | Move `--working-dir` BEFORE `--args` |
| `Could not find mtb_env.py` | MCP extension not running | Ensure ModusToolbox AI extension is active in VS Code |
| `python3` not found (Windows) | Windows App Execution Alias | Use `python` instead of `python3` |
| Build hangs with no output | Normal — prebuild phase | Wait 60-180s for clean builds |

### Multi-Core Build Rules

- Always build from the **application root** (parent of `proj_cm*` folders)
- Never `cd` into a sub-project to build individually
- If tool-not-found error: `make clean` from root, then rebuild

### Build Duration

- Clean builds: 60–180+ seconds (200+ source files per sub-project)
- Prebuild phase: 20–40 seconds with no visible output — this is NOT a failure
- Incremental builds: 5–15 seconds

---

## Library Management

### ⛔ No Speculative Additions

Libraries are installed ONLY through:

1. **Skill-driven** — skill workflow explicitly names it
2. **User-explicit** — user directly requests it
3. **Dependency resolution** — build fails with specific missing dependency

Do NOT install based on name inference or manifest browsing.

### Version Resolution

| Priority | Source                                          | Action                                              |
| -------- | ----------------------------------------------- | --------------------------------------------------- |
| 1        | Reference CE's `.mtb` files (same board/family) | Use those tags — Infineon-validated                 |
| 2        | `mtb-list-available-libraries` MCP tool         | Check README "Supported platforms" before accepting |
| 3        | `git ls-remote --tags <url> 'release-v*'`       | Latest tag + README compatibility check             |

Never write a `.mtb` file with an unverified version tag.

### Reference Code Examples — MANDATORY Before Library Integration

Before integrating ANY middleware/sensor library:

1. Find a reference CE using the same library (`describe_code_example.py`)
2. Inspect its `deps/` directory (what's present and what's transitive)
3. Inspect its `Makefile` (COMPONENTS, DEFINES, DISABLE_COMPONENTS)
4. Follow its init sequence

**Anti-pattern:** Installing a wrapper AND its underlying driver separately → duplicate symbols.

### After Adding Any Library

1. Read the library's `README.md` — authoritative for integration requirements
2. Check for required `COMPONENTS+=` entries
3. Check for required `DEFINES+=` entries
4. Check for companion dependencies

---

## Debugging & Recovery

### Debugging Protocol

When runtime behavior doesn't match expectations:

1. **RE-READ** the skill that guided the failed implementation — scan ALL gotchas and linked references
2. **OPEN every reference file** linked from that skill you haven't read yet
3. **ONLY after exhausting the skill chain:** attempt inference or alternative approaches

⛔ Do NOT abandon an approach (e.g., interrupts → polling) without first reading ALL reference files.

### Escalation Rule

If you hit **2+ build failures or unexpected runtime behavior:**

1. STOP expanding scope
2. Scale back to strict one-phase-at-a-time
3. Debug within smallest possible scope first (wrong API > wrong param > missing init)

---

## Code Style & Documentation

For source file organization, README documentation standards, code comment conventions, and Doxygen templates, read:

→ [mtb-code-style.instructions.md](./mtb-code-style.instructions.md)

**Mandatory minimums (always apply even without reading the full style guide):**

- `main.c` must not exceed 150 lines — refactor into `<feature>_task.c/h` files
- Every `.c/.h` file needs a brief header comment (file name + description)
- README is a **Phase 1 deliverable** — create/update before Phase 1 verification
- `while(1)` is never acceptable as an error handler

### Error Handling

All error conditions must either:

- Recover automatically (retry with backoff)
- Report and continue in degraded mode
- Escalate via UART with actionable information
