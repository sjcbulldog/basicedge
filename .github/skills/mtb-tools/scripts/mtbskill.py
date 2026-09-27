"""
mtbskill — locate the generated mtb_env session-info module.

This file is deposited in ~/.modustoolbox/ by the ModusToolbox extension so
that individual MTB skills (which may live anywhere in a user's project tree)
can find it without knowing where the extension is installed.

Usage in a skill script:

    import sys, os
    sys.path.insert(0, os.path.expanduser("~/.modustoolbox"))
    from mtbskill import find_mtb_env

    env_path = find_mtb_env(__file__)   # returns Path or None
    if env_path is None:
        raise RuntimeError("ModusToolbox extension is not running")

Search order used by find_mtb_env():
  1. Walk upward from *start* looking for .modustoolbox/mtb_env.py,
     stopping at the user's home directory or the filesystem root.
  2. $TMPDIR/mtbai-sessions/latest/mtb_env.py — written by the server
     when no MTB workspace is loaded; only returned if the server PID
     stored in the accompanying mtb_env.json is still running.
  3. $TMPDIR/mtbai-sessions/<PID>/mtb_env.py directories — each directory
     name is a server PID; candidates are checked for liveness and sorted
     newest-first by mtime.

Returns None (never raises) when nothing can be found.

bootstrap_skill(start) is the single call every skill needs:
  1. find_mtb_env(start)   — locate mtb_env.py via the three strategies.
  2. Load the module with stdlib importlib.
  3. Wire <EXTENSION_DIR>/ai-assets/lib onto sys.path and import mtblib.
  4. Return a SkillContext that wraps the MCP/HTTP calls pre-bound to
     the server's TCP port — no further imports needed in the skill.

Typical skill boilerplate (zero E402, zero bare ports, zero mtblib import):

    import sys, os
    sys.path.insert(0, os.path.expanduser("~/.modustoolbox/mtbai"))
    from mtbskill import bootstrap_skill

    skill = bootstrap_skill(__file__)
    result = skill.mcp_request("my_tool", arg1="value")
    print(skill.mcp_text(result))

    # Raw env attributes are forwarded transparently:
    print(skill.TCP_PORT, skill.WORKSPACE_PATH)

    # Escape hatch — the underlying mtb_env module:
    print(skill.env.EXTENSION_DIR)
"""

from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
from pathlib import Path


def _pid_alive(pid: int) -> bool:
    """Return True if *pid* refers to a running process."""
    if sys.platform == "win32":
        import ctypes
        SYNCHRONIZE = 0x00100000
        handle = ctypes.windll.kernel32.OpenProcess(SYNCHRONIZE, False, pid)
        if not handle:
            return False
        ctypes.windll.kernel32.CloseHandle(handle)
        return True
    else:
        try:
            os.kill(pid, 0)
            return True
        except ProcessLookupError:
            # ESRCH — no such process
            return False
        except PermissionError:
            # EPERM — process exists but we cannot signal it; still alive
            return True


def _session_pid(session_dir: Path) -> int | None:
    """Read the server PID from *session_dir*/mtb_env.json, or return None."""
    try:
        data = json.loads((session_dir / "mtb_env.json").read_text())
        pid = data.get("pid")
        if isinstance(pid, int) and pid > 0:
            return pid
    except (OSError, json.JSONDecodeError, ValueError):
        pass
    return None


def find_mtb_env(start: str | Path | None = None) -> Path | None:
    """Locate mtb_env.py by walking upward from *start*, with temp-dir fallback.

    Search order:
      1. Walk upward from *start* looking for .modustoolbox/mtb_env.py,
         stopping at the user's home directory or the filesystem root.
      2. $TMPDIR/mtbai-sessions/latest/mtb_env.py
         (written by the server for sessions with no loaded workspace).
      3. $TMPDIR/mtbai-sessions/<PID>/mtb_env.py directories,
         sorted newest-first by mtime.

    Args:
        start: Starting file or directory.  Defaults to the current working
               directory when omitted.

    Returns:
        Absolute path to the generated mtb_env.py file, or None if not found.
    """
    home = Path.home().resolve()
    current = Path.cwd() if start is None else Path(start)
    current = current.resolve()
    if current.is_file():
        current = current.parent

    # --- Strategy 1: walk upward through the directory tree ---
    probe = current
    while True:
        candidate = probe / ".modustoolbox" / "mtb_env.py"
        if candidate.exists():
            return candidate
        # Stop at the user's home directory or at the filesystem root
        # (Path('/').parent == Path('/'), so the second check catches root).
        if probe == home or probe == probe.parent:
            break
        probe = probe.parent

    # --- Strategy 2 & 3: temp-dir sessions written by writeSessionInfoAll ---
    sessions_dir = Path(tempfile.gettempdir()) / "mtbai-sessions"

    # Strategy 2: latest/ — symlink/copy to the most recently started server.
    # Verify the recorded PID is still alive before trusting it.
    latest_dir = sessions_dir / "latest"
    latest = latest_dir / "mtb_env.py"
    if latest.exists():
        pid = _session_pid(latest_dir)
        if pid is not None and _pid_alive(pid):
            return latest

    # Strategy 3: numbered PID directories, newest-first by mtime.
    # The directory name IS the PID; skip stale (dead) servers.
    if sessions_dir.is_dir():
        pid_candidates: list[tuple[float, Path]] = []
        for entry in sessions_dir.iterdir():
            if not entry.is_dir() or entry.name == "latest":
                continue
            if not entry.name.isdigit():
                continue
            pid = int(entry.name)
            if not _pid_alive(pid):
                continue
            candidate = entry / "mtb_env.py"
            if candidate.exists():
                pid_candidates.append((candidate.stat().st_mtime, candidate))
        if pid_candidates:
            pid_candidates.sort(reverse=True)
            return pid_candidates[0][1]

    return None


class SkillContext:
    """Returned by bootstrap_skill(). Pre-bound interface for MTB skills.

    Attributes:
        env: The raw mtb_env module (TCP_PORT, EXTENSION_DIR, WORKSPACE_PATH, …).

    All attributes on *env* are also accessible directly on this object:
        skill.TCP_PORT  ==  skill.env.TCP_PORT
    """

    def __init__(self, env_module: object, mtblib_module: object) -> None:
        self.env = env_module
        self._mtblib = mtblib_module

    def ext_request(self, method: str, **params) -> dict:
        """One-shot JSON-RPC 2.0 call to the extension server, pre-bound to EXT_PORT.

        Args:
            method: JSON-RPC method name (e.g. ``"hello"``, ``"openWorkspace"``).
            **params: Arguments forwarded to the method.

        Returns:
            The result dict from the JSON-RPC response.

        Raises:
            ServiceError: on connection failure or JSON-RPC error.
        """
        return self._mtblib.ext_request(self.env.EXT_PORT, method, **params)

    def mcp_request(self, tool: str, **params) -> dict:
        """One-shot MCP tool call, pre-bound to the running server's port.

        Args:
            tool: MCP tool name (e.g. ``"list_available_libraries"``).
            **params: Arguments forwarded to the tool.

        Returns:
            Unwrapped tool result dict (has ``content`` and optionally
            ``structuredContent``).

        Raises:
            ServiceError: on connection failure or tool error.
        """
        return self._mtblib.mtb_mcp_request(self.env.TCP_PORT, tool, params)

    def mcp_text(self, result: dict) -> str:
        """Extract and concatenate all text items from an mcp_request result."""
        return self._mtblib.mcp_result_text(result)

    def http_request(self, service: str, **params) -> dict:
        """One-shot HTTP request to the extension service, pre-bound to port.

        Args:
            service: Service name (e.g. ``"list_code_examples"``).
            **params: Additional key-value pairs sent in the request body.

        Returns:
            Parsed JSON response as a dict.

        Raises:
            ServiceError: on HTTP errors or invalid JSON response.
        """
        return self._mtblib.mtb_request(self.env.TCP_PORT, service, **params)

    def __getattr__(self, name: str):
        # Forward unknown attributes to the env module so callers can do
        # skill.TCP_PORT, skill.WORKSPACE_PATH, etc. without going via skill.env.
        # __getattr__ is only called when normal lookup fails, so env and
        # _mtblib themselves are never intercepted here.
        return getattr(self.env, name)


def bootstrap_skill(start: str | Path) -> SkillContext:
    """One-call setup for MTB skills: find, load, wire, and return context.

    Steps performed:
      1. find_mtb_env(start) — locate mtb_env.py via the three strategies.
      2. Load the module with stdlib importlib.
      3. Add <EXTENSION_DIR>/ai-assets/lib to sys.path and import mtblib.
      4. Return a SkillContext pre-bound to the server's TCP port.

    Args:
        start: Path to the skill script (pass ``__file__``). Used as the
               starting point for the workspace-walk search strategy.

    Returns:
        A SkillContext ready to call mcp_request(), mcp_text(), http_request().

    Raises:
        RuntimeError: if mtb_env.py cannot be found or loaded, or if mtblib
                      cannot be imported from the discovered EXTENSION_DIR.
    """
    env_path = find_mtb_env(start)
    if env_path is None:
        raise RuntimeError(
            "Could not find mtb_env.py. "
            "Is the ModusToolbox MCP extension running for this workspace?"
        )

    spec = importlib.util.spec_from_file_location("mtb_env", env_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load import spec for {env_path}")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as exc:
        raise RuntimeError(f"Failed to load {env_path}: {exc}") from exc

    ext_dir = getattr(module, "EXTENSION_DIR", None)
    if not ext_dir:
        raise RuntimeError(f"mtb_env module at {env_path} has no EXTENSION_DIR")
    lib_dir = str(Path(ext_dir) / "ai-assets" / "lib")
    if lib_dir not in sys.path:
        sys.path.insert(0, lib_dir)

    try:
        mtblib = importlib.import_module("mtblib")
    except ImportError as exc:
        raise RuntimeError(f"Could not import mtblib from {lib_dir}: {exc}") from exc

    return SkillContext(module, mtblib)
