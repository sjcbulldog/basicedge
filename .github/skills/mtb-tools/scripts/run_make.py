#!/usr/bin/env python3
"""Skill: run_make
Calls the Infineon ModusToolbox AI Assistant server command 'run_make' to run make with the args listed on the command line

Usage:
    python .github/skills/mtb-tools/scripts/run_make.py --args build
    python .github/skills/mtb-tools/scripts/run_make.py --working-dir <path> --args vscode
    python .github/skills/mtb-tools/scripts/run_make.py --env-vars CONFIG Debug --args build
    python .github/skills/mtb-tools/scripts/run_make.py --filter-progress --args build
    python .github/skills/mtb-tools/scripts/run_make.py --heartbeat-secs 10 --args build
    python .github/skills/mtb-tools/scripts/run_make.py --args build CONFIG=Debug

NOTE: --working-dir, --env-vars, --filter-progress and --heartbeat-secs MUST appear BEFORE --args (--args consumes all remaining arguments).
NOTE: Do NOT quote multiple args as one string. Each make argument must be a separate token after --args.
NOTE: When using with a SKILL or an LLM, always use --filter-progress for 'build' target. It reduces noise and prevents flooding the output with frequent progress updates.
"""

import sys
import os
import argparse
import subprocess
import shlex
import threading
import time
import re

# Surface buffered "[N/M]" progress at most this often (seconds). Real output is
# flushed immediately; this only paces the collapsed progress heartbeat so a long
# build still shows a pulse without flooding, which matters on slow machines.
_FLUSH_INTERVAL = 2.0
# Safety valve: flush early if the pending buffer reaches this size, so a burst
# arriving within one interval can't grow memory without bound.
_FLUSH_MAX_BYTES = 65536
# ninja edge lines under a pipe, e.g. "[12/164] Building object file ...".
_PROGRESS_RE = re.compile(rb"^\[\d+/\d+\]")
# Default seconds of total silence before the heartbeat pulses; 0 disables.
_HEARTBEAT_INTERVAL = 15.0


class _Activity:
    """Shared last-output timestamp. The pump threads and the heartbeat take
    'lock' around every write so they serialize and agree on when output last
    happened (used to fire the heartbeat only during true silence)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.last = time.monotonic()


def _heartbeat(activity, stream, stop, interval):
    r"""Pulse `stream` whenever `interval` seconds pass with no real output.

    Slow, silent phases (LTO codegen, linking, signing) can emit nothing for a
    long time; a caller watching for activity may mistake that for a hang. This
    runs on its own thread so it fires even while the pumps are blocked reading
    the child. Drop the thread in run_streaming to remove the feature entirely.
    """
    while not stop.wait(interval):
        with activity.lock:
            if time.monotonic() - activity.last >= interval:
                # Deliberately no elapsed/idle time: this only keeps the output
                # stream alive. Reporting "no output for Ns" would hand a reader
                # the very signal that invites killing a slow-but-healthy build.
                stream.write(b"[run_make] still working; a quiet compile/link phase can take several minutes\n")
                stream.flush()
                activity.last = time.monotonic()


def _pump(src, dst, activity=None, filter_progress=False):
    r"""Forward src to dst, collapsing carriage-return progress updates.

    make/ModusToolbox shows progress by overwriting one line in place with '\r'
    (e.g. "1%\r2%\r...100%\r"). Echoing every update floods the caller and can
    blow past an output cap, while emitting nothing until the next '\n' can look
    like a hang. So we keep only the latest text of the line being overwritten
    and emit it: once when the line is committed with '\n', and at most once per
    _FLUSH_INTERVAL seconds as a heartbeat while a '\r' line is still being
    rewritten. Anything terminated by '\n' is never dropped.

    When filter_progress is set, ninja's newline-terminated "[N/M] ..." edge
    lines (what it prints to a pipe) are collapsed globally, regardless of
    contiguity: only the latest is surfaced, at most once per _FLUSH_INTERVAL
    seconds and once at EOF, so interleaved `make -j` output can't defeat the
    collapse. Every non-"[N/M]" line (errors, warnings, sizes, results) is
    written immediately, so real output is never delayed by the throttle.
    """
    if activity is None:   # standalone use (e.g. tests); real runs share one
        activity = _Activity()
    out = bytearray()      # committed bytes waiting to be written
    line = bytearray()     # current line content, after the last '\r'
    cr_pending = False     # saw '\r'; the next visible char overwrites the line
    progress = False       # this line has been overwritten at least once
    changed = False        # 'line' changed since the last heartbeat
    held = None            # latest "[N/M]" line seen so far
    held_dirty = False     # 'held' not yet emitted
    last_flush = time.monotonic()

    def emit(data):
        # Serialize with the other pump and the heartbeat, and record the time
        # of real output so the heartbeat only fires during true silence.
        with activity.lock:
            dst.write(data)
            dst.flush()
            activity.last = time.monotonic()

    def flush():
        nonlocal last_flush
        if out:
            emit(bytes(out))
            out.clear()
        last_flush = time.monotonic()

    def commit(text):
        # Route a completed line (no trailing newline) through the filter.
        nonlocal held, held_dirty
        if filter_progress and _PROGRESS_RE.match(text):
            held = bytes(text)   # snapshot: 'text' is reused/cleared by caller
            held_dirty = True
            return
        # A real (anti-filter) line: emit at once so errors/results are never
        # held back. Buffered "[N/M]" progress is surfaced by the heartbeat/EOF.
        out.extend(text)
        out.append(0x0A)
        flush()

    try:
        while True:
            data = src.read(4096)
            if not data:
                break
            for b in data:
                if b == 0x0D:          # '\r'
                    cr_pending = True
                elif b == 0x0A:        # '\n' commits the visible line
                    commit(line)
                    line.clear()
                    cr_pending = progress = changed = False
                else:
                    if cr_pending:     # overwrite: discard what was there
                        line.clear()
                        cr_pending = False
                        progress = True
                    line.append(b)
                    changed = True
            now = time.monotonic()
            if len(out) >= _FLUSH_MAX_BYTES:
                flush()
            elif now - last_flush >= _FLUSH_INTERVAL:
                # Surface the newest progress line so a long burst keeps a
                # steady stream of complete lines instead of looking hung.
                if held_dirty:
                    out.extend(held)
                    out.append(0x0A)
                    held_dirty = False
                if progress and changed and not out:
                    out.extend(line)
                    out.append(0x0A)
                    changed = False
                flush()
    finally:
        # Never drop the tail: the last filtered line, a final progress line, or
        # an error printed without a trailing newline is what we need to see.
        if held_dirty:
            out.extend(held)
            out.append(0x0A)
        if line:
            out.extend(line)
            out.append(0x0A)
        if out:
            emit(bytes(out))


def run_streaming(command, cwd, env, filter_progress=False, heartbeat_secs=0.0):
    """Run command, live-streaming its stdout/stderr on the parent's own handles
    with carriage-return progress collapsing. Returns the child's exit code."""
    proc = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    activity = _Activity()
    stop = threading.Event()
    beat = None
    if heartbeat_secs and heartbeat_secs > 0:
        # stderr keeps the pulse out of stdout parsers; switch to
        # sys.stdout.buffer if the harness only watches stdout for activity.
        beat = threading.Thread(target=_heartbeat, args=(activity, sys.stderr.buffer, stop, heartbeat_secs), daemon=True)
        beat.start()
    pumps = [
        threading.Thread(target=_pump, args=(proc.stdout, sys.stdout.buffer, activity, filter_progress), daemon=True),
        threading.Thread(target=_pump, args=(proc.stderr, sys.stderr.buffer, activity, filter_progress), daemon=True),
    ]
    for t in pumps:
        t.start()
    proc.wait()
    for t in pumps:
        t.join()
    stop.set()
    if beat:
        beat.join()
    return proc.returncode

# Get the directory of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add it to the end of the search path, so we can find the mtbskill.py
sys.path.insert(0, current_dir)

from mtbskill import bootstrap_skill  # type: ignore

parser = argparse.ArgumentParser(description="List ModusToolbox code examples")
parser.add_argument("--args", nargs=argparse.REMAINDER, help="Arguments to send to make (e.g., build CONFIG=Debug).")
parser.add_argument("--working-dir", required=False, nargs=1, help="Optional, directory to run the command from, default is the current directory.")
parser.add_argument("--env-vars", required=False, action='append', metavar=('key', 'value'), nargs="+", help="Optional, series of (name value) pairs of environment variables to use during the make command.")
parser.add_argument("--filter-progress", action="store_true", help="Optional, collapse ninja '[N/M] ...' progress lines globally to reduce output volume (surfaces only the latest, plus the final one).")
parser.add_argument("--heartbeat-secs", type=float, default=_HEARTBEAT_INTERVAL, help="Optional, pulse stderr after this many seconds with no output so a silent phase (slow link/codegen) is not mistaken for a hang; 0 disables. Default %d." % int(_HEARTBEAT_INTERVAL))
args = parser.parse_args()

skill = bootstrap_skill(__file__)
useCwd = args.working_dir[0] if args.working_dir else skill.WORKSPACE_PATH
useEnv = os.environ.copy()
useEnv["CY_TOOLS_PATHS"] = skill.CY_TOOLS_PATH
if args.env_vars:
    for env_var in args.env_vars:
        if len(env_var) != 2:
            parser.error("--env_vars requires pairs of NAME and VALUE.")
        key, value = env_var
        useEnv[key] = value

command = ["make"] + args.args    
if sys.platform == "win32":
    bash_path = os.path.join(skill.MODUS_SHELL_PATH, "bin", "bash.exe")
    if os.path.exists(bash_path):
        command = [bash_path, "--norc", "-c", f"export PATH=/bin:/usr/bin:$PATH ; make {' '.join(shlex.quote(a) for a in args.args)}"]
    # else: fall back to the plain make command already set above

result = run_streaming(command, cwd=useCwd, env=useEnv, filter_progress=args.filter_progress, heartbeat_secs=args.heartbeat_secs)
print("")
exit(result)
