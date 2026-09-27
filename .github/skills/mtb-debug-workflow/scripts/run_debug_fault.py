#!/usr/bin/env python3
"""Skill: run_debug_fault
Orchestrates fault capture via the ModusToolbox MCP 'debug_run_gdb_script' tool.

This script handles:
1. Determining the correct GDB script based on target type
2. Calling the MCP tool (which handles OpenOCD/GDB lifecycle)
3. Parsing raw GDB output into structured sections
4. Deciding if reset-and-catch is needed (TrustZone register clearing)
5. Running a second pass if needed

Usage:
    python .github/skills/mtb-debug-workflow/scripts/run_debug_fault.py [options]

Examples:
    python .github/skills/mtb-debug-workflow/scripts/run_debug_fault.py
    python .github/skills/mtb-debug-workflow/scripts/run_debug_fault.py --target-core cm55
    python .github/skills/mtb-debug-workflow/scripts/run_debug_fault.py --cwd proj_cm55 --config Debug --target-core cm55
"""

import sys
import os
import re
import argparse

# Add mtb-tools/scripts to path for mtbskill
current_dir = os.path.dirname(os.path.abspath(__file__))
skills_dir = os.path.dirname(os.path.dirname(current_dir))  # up to skills/
tools_dir = os.path.join(skills_dir, "mtb-tools", "scripts")
sys.path.insert(0, tools_dir)

from mtbskill import bootstrap_skill  # type: ignore

SCRIPTS_DIR = os.path.join(current_dir, "gdb-scripts")  # current_dir is the scripts/ dir


def select_gdb_script(target: str, target_core: str) -> str:
    """Select the appropriate GDB script based on target and core."""
    is_pse84 = "PSE84" in target.upper()

    if is_pse84 and target_core == "cm55":
        return os.path.join(SCRIPTS_DIR, "fault-capture-cm55.gdb")
    elif is_pse84:
        return os.path.join(SCRIPTS_DIR, "fault-capture-cm33-trustzone.gdb")
    else:
        return os.path.join(SCRIPTS_DIR, "fault-capture-default.gdb")


def extract_section(text: str, start_marker: str, end_marker: str) -> str:
    """Extract text between markers from GDB output."""
    start = text.find(start_marker)
    if start == -1:
        return ""
    start += len(start_marker)
    nl = text.find("\n", start)
    if nl >= 0:
        start = nl + 1
    end = text.find(end_marker, start)
    if end == -1:
        return text[start:].strip()
    return text[start:end].strip()


def fault_regs_all_zero(fault_regs: str) -> bool:
    """Check if all fault register values are 0x00000000."""
    if not fault_regs:
        return True
    hex_values = re.findall(r"0x([0-9a-fA-F]{8})", fault_regs)
    if not hex_values:
        return True
    # Skip values that are addresses (before the colon)
    # Only check values after :\t or :\s patterns
    value_lines = re.findall(r":\s*0x([0-9a-fA-F]{8})", fault_regs)
    if not value_lines:
        return True
    return all(v == "00000000" for v in value_lines)


def get_openocd_vars(target: str, bsp_config: str = "", project_dir: str = "") -> dict:
    """Determine OpenOCD variables needed for the target."""
    vars_dict = {}
    is_pse84 = "PSE84" in target.upper()

    if is_pse84 and bsp_config:
        flm_path = os.path.join(bsp_config, "PSE84_SMIF.FLM")
        if os.path.isfile(flm_path):
            vars_dict["QSPI_FLASHLOADER"] = flm_path

        for rel in ["proj_cm33_s/packets/debug_token.bin", "packets/debug_token.bin"]:
            cert_path = os.path.join(project_dir, rel)
            if os.path.isfile(cert_path):
                vars_dict["DEBUG_CERTIFICATE"] = cert_path.replace("\\", "/")
                break

    return vars_dict


def main():
    parser = argparse.ArgumentParser(description="Run debug fault analysis via MCP tool")
    parser.add_argument("--cwd", help="Project directory containing the built ELF")
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    parser.add_argument("--target-core", default="cm33", choices=["cm33", "cm55"])
    parser.add_argument("--gdb-port", type=int, help="Override OpenOCD GDB base port (default: 3333)")
    parser.add_argument("--timeout", type=int, default=60, help="Max session time (seconds)")
    parser.add_argument("--no-reset-catch", action="store_true",
                        help="Disable automatic reset-and-catch on TrustZone targets")
    args = parser.parse_args()

    skill = bootstrap_skill(__file__)

    # First call: run the appropriate script for the target
    params = {
        "config": args.config,
        "target_core": args.target_core,
        "timeout_seconds": args.timeout,
    }
    if args.cwd:
        params["cwd"] = args.cwd
    if args.gdb_port:
        params["gdb_port"] = args.gdb_port

    # We need target info first — do a preliminary call or use workspace context
    # For now, select script based on known project info
    # The MCP tool returns target info in the result, so we can use a two-pass approach
    # Pass 1: Use default/TrustZone script based on available context
    script = os.path.join(SCRIPTS_DIR, "fault-capture-cm33-trustzone.gdb")
    if args.target_core == "cm55":
        script = os.path.join(SCRIPTS_DIR, "fault-capture-cm55.gdb")

    params["gdb_script"] = script
    result = skill.mcp_request("debug_run_gdb_script", **params)
    result_text = skill.mcp_text(result)

    import json
    try:
        data = json.loads(result_text)
    except json.JSONDecodeError:
        print(result_text)
        return

    # Parse the raw output
    raw = data.get("raw_output", "")
    target = data.get("target", "")

    # If we used the wrong script (not PSE84 but used TrustZone script), re-run
    is_pse84 = "PSE84" in target.upper()
    if not is_pse84 and args.target_core != "cm55":
        correct_script = os.path.join(SCRIPTS_DIR, "fault-capture-default.gdb")
        if correct_script != script:
            params["gdb_script"] = correct_script
            result = skill.mcp_request("debug_run_gdb_script", **params)
            result_text = skill.mcp_text(result)
            try:
                data = json.loads(result_text)
            except json.JSONDecodeError:
                print(result_text)
                return
            raw = data.get("raw_output", "")

    # Extract structured sections from raw output
    backtrace = extract_section(raw, "=== Backtrace ===", "===")
    registers = extract_section(raw, "=== Registers ===", "===")
    fault_regs = extract_section(raw, "=== Fault Status Registers", "=== Done ===")

    # For TrustZone targets: check if reset-and-catch is needed
    if is_pse84 and args.target_core == "cm33" and not args.no_reset_catch:
        ns_section = extract_section(raw, "Non-Secure SCB", "=== NS Exception Frame")
        if not ns_section:
            ns_section = fault_regs
        if fault_regs_all_zero(ns_section):
            print("[INFO] NS fault registers cleared by TrustZone. Running reset-and-catch...")
            rc_script = os.path.join(SCRIPTS_DIR, "fault-capture-cm33ns-reset-catch.gdb")
            rc_params = dict(params)
            rc_params["gdb_script"] = rc_script
            rc_params["timeout_seconds"] = args.timeout + 30  # Extra time for reset
            rc_result = skill.mcp_request("debug_run_gdb_script", **rc_params)
            rc_text = skill.mcp_text(rc_result)
            try:
                rc_data = json.loads(rc_text)
                rc_raw = rc_data.get("raw_output", "")
                rc_fault = extract_section(rc_raw, "=== Fault Status Registers", "=== Done ===")
                if rc_fault and not fault_regs_all_zero(rc_fault):
                    # Reset-and-catch captured live registers
                    raw = rc_raw
                    backtrace = extract_section(rc_raw, "=== Backtrace ===", "===")
                    registers = extract_section(rc_raw, "=== Registers ===", "===")
                    fault_regs = rc_fault
                    data["capture_method"] = "reset_catch"
            except json.JSONDecodeError:
                pass

    # Build structured output
    output = {
        "success": data.get("success", False),
        "capture_method": data.get("capture_method", "direct"),
        "target": target,
        "target_core": data.get("target_core", args.target_core),
        "elf_path": data.get("elf_path", ""),
        "backtrace": backtrace,
        "registers": registers,
        "fault_regs": fault_regs,
        "raw_output": raw,
        "error": data.get("error", ""),
    }

    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
