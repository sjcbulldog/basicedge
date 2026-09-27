---
name: mtb-debug-workflow
description: Automated build, program, serial capture, and fault analysis workflow using MCP debug tools. Use this skill when you need to flash firmware, verify serial output, or diagnose crashes.
license: "Apache-2.0"
metadata:
  author: "Infineon ModusToolbox Team"
  version: "1.1.0"
---

# Debug Workflow Skill — Build, Program, Capture, Analyze

This skill chains the MCP tools to build, program, capture serial output, and perform fault analysis on connected boards.

**Critical Note:** ALL skill scripts resolve paths relative to CWD. Running from the wrong directory = silent failure.

## How to Invoke These Tools

Each MCP tool has a Python wrapper script. **YOU MUST** run them from the **workspace root**:

```
# Discover connected debug probes (find port name):
python .github/skills/mtb-debug-workflow/scripts/run_discover_ports.py

# Reset the board (auto-detects target from BSP):
python .github/skills/mtb-debug-workflow/scripts/run_reset_board.py

# Capture serial output (use port from discover step):
python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port <PORT> --timeout 10 --stop-patterns HardFault fault crash

# Capture with board reset (resets board, then captures boot output):
python .github/skills/mtb-debug-workflow/scripts/run_capture_serial.py --port <PORT> --timeout 10 --reset

# Debug fault analysis (specify core and sub-project as needed):
python .github/skills/mtb-debug-workflow/scripts/run_debug_fault.py --target-core <cm33|cm55> --cwd <sub-project>

# Build (use run_make from mtb-tools):
python .github/skills/mtb-tools/scripts/run_make.py --args build

# Program:
python .github/skills/mtb-tools/scripts/run_make.py --args program
```

**Windows note:** Use `python` (NOT `python3`).

## Available MCP Tools

### `discover_debug_ports`
Enumerate USB serial ports and identify connected debug probes.

**Parameters:**
- `probe_type` (optional): Filter by `"KitProg3"` or `"J-Link"`

**Example response:**
```json
{
  "ports": [
    {"port": "COM64", "probe_type": "KitProg3", "vid": "04B4", "pid": "F155", "serial_number": "091F16F9001E2400"},
    {"port": "COM73", "probe_type": "J-Link", "vid": "1366", "pid": "0105", "serial_number": "000599021050"}
  ],
  "count": 2
}
```

### `reset_board`
Reset the connected board via OpenOCD. Auto-detects the target device from the BSP directory.

**Parameters:**
- `cwd` (optional): Project directory for BSP detection. If empty, uses current workspace.
- `timeout_seconds` (optional, default 20): Max time to wait for reset

**Example response:**
```json
{
  "success": true,
  "target": "APP_KIT_PSE84_EVAL_EPC2",
  "message": "Board reset successful (target: APP_KIT_PSE84_EVAL_EPC2, config: target/infineon/pse84xgxs2.cfg)"
}
```

### `capture_serial_output`
Open a serial port and capture output until a stop condition.

**Parameters:**
- `port` (required): Port name (e.g., `"COM64"`)
- `baud_rate` (optional, default 115200): Serial baud rate
- `timeout_seconds` (optional, default 10): Max capture duration
- `stop_patterns` (optional): Array of strings — stop when any appears
- `wait_for_port_seconds` (optional): Wait for port after programming (handles KitProg3 USB re-enumeration)
- `max_bytes` (optional, default 65536): Max bytes to capture
- `strip_ansi` (optional): Remove ANSI escape codes

**Example response:**
```json
{
  "success": true,
  "port": "COM64",
  "bytes_captured": 325,
  "lines": ["[CM55:heartbeat t=5s]", "[CM55:heartbeat t=10s]"],
  "stop_pattern_hit": true,
  "matched_pattern": "heartbeat"
}
```

### `debug_run_gdb_script`
Start OpenOCD, run any GDB script against the target, and return raw output. The server is a platform-agnostic executor — all MCU-specific logic (script selection, output parsing) lives here in the skill layer.

**Parameters:**
- `cwd` (optional): Project directory containing the ELF file
- `gdb_script` (required): Path to .gdb script (absolute or relative to skill scripts/gdb-scripts/ dir)
- `config` (optional, default "Debug"): Build configuration
- `target_core` (optional, default "cm33"): Which core to debug
- `gdb_port` (optional, default 3333): Override OpenOCD GDB base port; CM55 connects on base+1
- `timeout_seconds` (optional, default 60): Max debug session time
- `openocd_vars` (optional): Object of key-value pairs to set before loading openocd.tcl

**Available GDB scripts:**
- `scripts/gdb-scripts/fault-capture-default.gdb` — Standard Cortex-M (no TrustZone)
- `scripts/gdb-scripts/fault-capture-cm33-trustzone.gdb` — CM33 with TrustZone (reads S + NS registers)
- `scripts/gdb-scripts/fault-capture-cm55.gdb` — CM55 core (no TrustZone, port 3334)
- `scripts/gdb-scripts/fault-capture-cm33ns-reset-catch.gdb` — Reset + Vector Catch for TrustZone-cleared registers

**Example response:**
```json
{
  "success": true,
  "target": "APP_KIT_PSE84_EVAL",
  "target_core": "cm33",
  "elf_path": "build/APP_KIT_PSE84_EVAL/Debug/proj_cm33_ns.elf",
  "raw_output": "=== Registers ===\nr0  0x0...\n=== Backtrace ===\n...",
  "openocd_stderr": "Info : Listening on port 3333...",
  "error": ""
}
```

**Note:** Use `run_debug_fault.py` for fault analysis — it handles script selection, parsing, and automatic reset-and-catch orchestration.

### `run_make` (existing tool)
Run arbitrary make targets for building and programming.

## Standard Workflow

### Step 1: Discover connected board
```
Call discover_debug_ports with probe_type="KitProg3"
→ Note the port name for serial capture
```

### Step 2: Build the project
```
Call run_make with arguments=["build"] and env_vars={"CONFIG": "Debug"}
→ Check exit_code == 0
→ If non-zero, analyze output for error: lines
```

### Step 3: Program the board
```
Call run_make with arguments=["program"] and env_vars={"CONFIG": "Debug"}
→ Check exit_code == 0
→ Note: KitProg3 USB port will briefly disappear during programming
```

### Step 4: Capture serial output
```
Call capture_serial_output with:
  port = (from step 1)
  wait_for_port_seconds = 5  (wait for KitProg3 re-enumeration)
  timeout_seconds = 15
  stop_patterns = ["initialization complete", "Error", "HardFault"]
```

> ⚠️ **Only attempt serial capture if the project includes retarget-io** (check
> `deps/retarget-io` or `COMPONENTS` in Makefile). Projects without retarget-io
> produce NO serial output — skip this step and verify via hardware observation
> (LED state, GPIO behavior, debugger breakpoints).

### Step 5: Fault analysis (if crash detected)
```
If serial output contains "HardFault", "BusFault", "UsageFault", or "MemManage":
  Run: python .github/skills/mtb-debug-workflow/scripts/run_debug_fault.py --target-core <cm33|cm55> --cwd <sub-project>
  → Examine backtrace for crash location
  → Check CFSR register bits for fault type
  → Check MMFAR/BFAR for faulting address
```

## Important Notes

- **KitProg3 re-enumeration**: After programming, the USB CDC port disappears for 2-5 seconds while the probe resets. Always use `wait_for_port_seconds` when capturing immediately after programming.
- **J-Link boards**: Same serial capture works, but programming uses `make program` with the J-Link GDB server (configured in Makefile).
- **Baud rate**: Most MTB BSPs default to 115200. Check `cybsp.h` or retarget-io config if no output appears.
- **Stop patterns**: Use firmware-specific strings like initialization messages or error indicators to stop capture early rather than waiting for full timeout.
- **Fault analysis**: Only useful when the target is halted (e.g., stuck in a fault handler). If the application is running normally, the tool will show normal execution state, not a fault.
- **PSE84 targets**: OpenOCD initialization takes longer (~8s vs ~4s). The tool auto-detects PSE84 targets and adjusts wait times accordingly.
