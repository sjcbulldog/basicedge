#!/usr/bin/env python3
"""Skill: describe_code_example

Fetches live metadata from Infineon code example (CE) repos on GitHub to provide
accurate descriptions, dependency lists, and Makefile COMPONENTS for the planning
agent. This eliminates hallucinated descriptions and incorrect template assumptions.

Usage (arguments are POSITIONAL repo names — no --id or --board-id flags):
    python .github/skills/mtb-tools/scripts/describe_code_example.py <repo-name> [repo-name2 ...]
    python .github/skills/mtb-tools/scripts/describe_code_example.py mtb-example-psoc-edge-empty-app
    python .github/skills/mtb-tools/scripts/describe_code_example.py mtb-example-psoc-edge-empty-app mtb-example-psoc-edge-wifi-mqtt-client

Source code inspection (fetch source files, then read specific ones):
    python .github/skills/mtb-tools/scripts/describe_code_example.py <repo-name> --source
    python .github/skills/mtb-tools/scripts/describe_code_example.py <repo-name> --read <path> [path2 ...]
    python .github/skills/mtb-tools/scripts/describe_code_example.py mtb-example-psoc-edge-gfx-lvgl-demo --source
    python .github/skills/mtb-tools/scripts/describe_code_example.py mtb-example-psoc-edge-gfx-lvgl-demo --read proj_cm55/main.c

Environment variables:
    GITHUB_TOKEN — Optional. Increases rate limit from 60/hr to 5000/hr.
    CE_CACHE_DIR — Optional. Directory to cache fetched metadata (default: ~/.modustoolbox/ce-cache)

Output:
    For each CE repo, prints a structured block with:
    - Description (first paragraph from README)
    - Dependencies (from deps/*.mtb or libs/*.mtb files)
    - Makefile COMPONENTS (from CM33_NS/Makefile or top-level Makefile)
    - BSP target (from Makefile TARGET variable)
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Optional, Union

# Ensure UTF-8 output on Windows (VS Code terminal handles this fine,
# but raw PowerShell may use cp1252)
if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


# ── Configuration ─────────────────────────────────────────────────────────────
GITHUB_ORG = "Infineon"
DEFAULT_BRANCH = "master"
CACHE_TTL_SECONDS = 86400  # 24 hours
CACHE_DIR = Path(os.environ.get("CE_CACHE_DIR", Path.home() / ".modustoolbox" / "ce-cache"))
README_MAX_CHARS = 1500  # Enough for description without overwhelming context


# ── GitHub Fetching ───────────────────────────────────────────────────────────

def _github_headers() -> dict:
    """Return headers for GitHub API requests, including auth if available."""
    headers = {
        "Accept": "application/vnd.github.v3+json",
        "User-Agent": "mtbmcp-describe-ce/1.0",
    }
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"token {token}"
    return headers


def _raw_headers() -> dict:
    """Return headers for raw.githubusercontent.com requests."""
    headers = {"User-Agent": "mtbmcp-describe-ce/1.0"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"token {token}"
    return headers


def _fetch_url(url: str, headers: dict, timeout: int = 15) -> Optional[str]:
    """Fetch a URL and return its text content, or None on failure."""
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except (urllib.error.HTTPError, urllib.error.URLError, OSError):
        return None


def _fetch_json(url: str, headers: dict, timeout: int = 15) -> Optional[Union[list, dict]]:
    """Fetch a URL and parse JSON response, or return None on failure."""
    content = _fetch_url(url, headers, timeout)
    if content is None:
        return None
    try:
        return json.loads(content)
    except json.JSONDecodeError:
        return None


# ── Caching ───────────────────────────────────────────────────────────────────

def _cache_path(repo_name: str) -> Path:
    """Return the metadata cache file path for a given repo."""
    return CACHE_DIR / repo_name / "_metadata.json"


def _cache_repo_dir(repo_name: str) -> Path:
    """Return the cache directory for a given repo."""
    return CACHE_DIR / repo_name


def _read_cache(repo_name: str) -> Optional[dict]:
    """Read cached metadata if it exists and is fresh."""
    path = _cache_path(repo_name)
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if time.time() - data.get("_cached_at", 0) < CACHE_TTL_SECONDS:
            return data
    except (json.JSONDecodeError, OSError):
        pass
    return None


def _write_cache(repo_name: str, data: dict) -> None:
    """Write metadata to cache."""
    repo_dir = _cache_repo_dir(repo_name)
    repo_dir.mkdir(parents=True, exist_ok=True)
    data["_cached_at"] = time.time()
    try:
        _cache_path(repo_name).write_text(
            json.dumps(data, indent=2), encoding="utf-8"
        )
    except OSError:
        pass  # Cache write failure is non-fatal


def _is_source_cached(repo_name: str) -> bool:
    """Check if source files are cached and fresh."""
    marker = _cache_repo_dir(repo_name) / "_source_cached.json"
    if not marker.exists():
        return False
    try:
        data = json.loads(marker.read_text(encoding="utf-8"))
        return time.time() - data.get("_cached_at", 0) < CACHE_TTL_SECONDS
    except (json.JSONDecodeError, OSError):
        return False


def _write_source_cache(repo_name: str, files: list[dict]) -> None:
    """Write source files to cache as actual files on disk."""
    repo_dir = _cache_repo_dir(repo_name)
    repo_dir.mkdir(parents=True, exist_ok=True)

    file_manifest = []
    for f in files:
        file_path = repo_dir / f["path"]
        file_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            file_path.write_text(f["content"], encoding="utf-8")
            file_manifest.append({"path": f["path"], "chars": len(f["content"])})
        except OSError:
            pass

    # Write marker with manifest
    marker = repo_dir / "_source_cached.json"
    try:
        marker.write_text(json.dumps({
            "_cached_at": time.time(),
            "files": file_manifest,
        }, indent=2), encoding="utf-8")
    except OSError:
        pass


def _read_source_manifest(repo_name: str) -> Optional[list[dict]]:
    """Read the cached source file manifest (paths + sizes, not content)."""
    marker = _cache_repo_dir(repo_name) / "_source_cached.json"
    if not marker.exists():
        return None
    try:
        data = json.loads(marker.read_text(encoding="utf-8"))
        if time.time() - data.get("_cached_at", 0) < CACHE_TTL_SECONDS:
            return data.get("files", [])
    except (json.JSONDecodeError, OSError):
        pass
    return None


def _read_source_file(repo_name: str, file_path: str) -> Optional[str]:
    """Read a single source file from the cache."""
    full_path = _cache_repo_dir(repo_name) / file_path
    if full_path.exists():
        try:
            return full_path.read_text(encoding="utf-8")
        except OSError:
            pass
    return None


# ── Metadata Extraction ───────────────────────────────────────────────────────

def _extract_description(readme_text: str) -> str:
    """Extract the first meaningful paragraph from a README.

    Skips the title line (# ...) and any badges/shields, then takes the first
    paragraph of actual description text.
    """
    lines = readme_text.split("\n")
    desc_lines = []
    started = False

    for line in lines:
        stripped = line.strip()

        # Skip title
        if stripped.startswith("# ") and not started:
            continue
        # Skip badges (typically ![...](...)  or [![...](...)
        if stripped.startswith("![") or stripped.startswith("[!["):
            continue
        # Skip empty lines before content starts
        if not stripped and not started:
            continue
        # Skip HTML comments
        if stripped.startswith("<!--"):
            continue

        # Once we hit content, collect until empty line
        if stripped:
            started = True
            desc_lines.append(stripped)
        elif started:
            break  # End of first paragraph

    description = " ".join(desc_lines)
    # Clean up common HTML entities from GitHub markdown
    description = description.replace("&trade;", "™")
    description = description.replace("&reg;", "®")
    description = description.replace("&amp;", "&")
    description = description.replace("&lt;", "<")
    description = description.replace("&gt;", ">")
    # Truncate if too long
    if len(description) > 500:
        description = description[:497] + "..."
    return description


def _extract_components(makefile_text: str) -> list[str]:
    """Extract COMPONENTS from a Makefile.

    Looks for patterns like:
        COMPONENTS+=FREERTOS
        COMPONENTS += LWIP MBEDTLS
        COMPONENTS = CUSTOM_DESIGN_MODUS

    Ignores commented lines and DISABLE_COMPONENTS.
    """
    components = set()
    for line in makefile_text.split("\n"):
        stripped = line.strip()
        # Skip comments
        if stripped.startswith("#"):
            continue
        # Match only COMPONENTS (not DISABLE_COMPONENTS)
        match = re.match(r"^COMPONENTS\s*\+?=\s*(.+)", stripped)
        if match:
            values = match.group(1).strip()
            # Remove trailing comments
            if "#" in values:
                values = values[: values.index("#")]
            values = values.rstrip("\\").strip()
            for comp in values.split():
                if comp:
                    components.add(comp)
    return sorted(components)


def _extract_target(makefile_text: str) -> Optional[str]:
    """Extract TARGET= from Makefile."""
    match = re.search(r"^TARGET\s*[?:]?=\s*(\S+)", makefile_text, re.MULTILINE)
    if match:
        return match.group(1)
    return None


def _extract_defines(makefile_text: str) -> list[str]:
    """Extract DEFINES from Makefile (useful for knowing enabled features)."""
    defines = set()
    for line in makefile_text.split("\n"):
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        match = re.match(r"^DEFINES\s*\+?=\s*(.+)", stripped)
        if match:
            values = match.group(1).strip()
            if "#" in values:
                values = values[: values.index("#")]
            values = values.rstrip("\\").strip()
            for d in values.split():
                if d:
                    defines.add(d)
    return sorted(defines)


def _parse_mtb_file_content(content: str) -> dict:
    """Parse a .mtb file line into url, version, and local path.

    Format: https://github.com/Infineon/retarget-io#release-v1.9.0#$$ASSET_REPO$$/retarget-io/release-v1.9.0
    """
    parts = content.strip().split("#")
    result = {"url": parts[0] if parts else content.strip()}
    if len(parts) >= 2:
        result["version"] = parts[1]
    if len(parts) >= 3:
        result["local_path"] = parts[2]
    # Extract library name from URL
    url = result["url"]
    if "/" in url:
        result["name"] = url.rstrip("/").split("/")[-1]
    return result


# ── Main Describe Function ────────────────────────────────────────────────────

def describe_code_example(repo_name: str, use_cache: bool = True) -> dict:
    """Fetch and return structured metadata for a code example repo.

    Args:
        repo_name: The GitHub repo name (e.g., 'mtb-example-psoc-edge-empty-app')
        use_cache: Whether to use cached results

    Returns:
        Dict with keys: repo, description, dependencies, components, target,
        defines, structure_type, error
    """
    # Check cache first
    if use_cache:
        cached = _read_cache(repo_name)
        if cached:
            cached["_from_cache"] = True
            return cached

    result = {
        "repo": repo_name,
        "github_url": f"https://github.com/{GITHUB_ORG}/{repo_name}",
        "description": "",
        "dependencies": [],
        "components": [],
        "target": None,
        "defines": [],
        "structure_type": "unknown",  # "single-core", "dual-core", "multi-project"
        "error": None,
    }

    api_headers = _github_headers()
    raw_headers = _raw_headers()
    base_raw = f"https://raw.githubusercontent.com/{GITHUB_ORG}/{repo_name}/{DEFAULT_BRANCH}"
    base_api = f"https://api.github.com/repos/{GITHUB_ORG}/{repo_name}"

    # ── 1. Fetch README ───────────────────────────────────────────────────────
    readme = _fetch_url(f"{base_raw}/README.md", raw_headers)
    if readme is None:
        result["error"] = f"Could not fetch README — repo may not exist or is private"
        return result
    result["description"] = _extract_description(readme)

    # ── 2. Determine project structure ────────────────────────────────────────
    # Check for dual-core structure (proj_cm33_ns/, proj_cm55/)
    # Try fetching the top-level directory listing
    top_contents = _fetch_json(f"{base_api}/contents", api_headers)

    if top_contents and isinstance(top_contents, list):
        dir_names = [item["name"] for item in top_contents if item.get("type") == "dir"]

        if "proj_cm33_ns" in dir_names:
            result["structure_type"] = "dual-core"
        elif "bsps" in dir_names and "Makefile" in [
            item["name"] for item in top_contents if item.get("type") == "file"
        ]:
            result["structure_type"] = "single-core"
        else:
            result["structure_type"] = "single-core"

    # ── 3. Fetch dependencies (.mtb files) ────────────────────────────────────
    deps_dirs_to_try = []
    if result["structure_type"] == "dual-core":
        deps_dirs_to_try = ["proj_cm33_ns/deps", "proj_cm55/deps", "deps"]
    else:
        deps_dirs_to_try = ["deps", "libs"]

    all_deps = {}
    for deps_dir in deps_dirs_to_try:
        deps_listing = _fetch_json(f"{base_api}/contents/{deps_dir}", api_headers)
        if deps_listing and isinstance(deps_listing, list):
            for item in deps_listing:
                if item.get("name", "").endswith(".mtb"):
                    # Fetch the .mtb file content
                    mtb_content = _fetch_url(
                        f"{base_raw}/{deps_dir}/{item['name']}", raw_headers
                    )
                    if mtb_content:
                        dep_info = _parse_mtb_file_content(mtb_content)
                        dep_info["source_dir"] = deps_dir
                        dep_name = dep_info.get("name", item["name"])
                        if dep_name not in all_deps:
                            all_deps[dep_name] = dep_info

    result["dependencies"] = list(all_deps.values())

    # ── 4. Fetch Makefile (prefer proj_cm33_ns for dual-core) ─────────────────
    makefile_paths = []
    if result["structure_type"] == "dual-core":
        makefile_paths = ["proj_cm33_ns/Makefile", "Makefile"]
    else:
        makefile_paths = ["Makefile"]

    for mf_path in makefile_paths:
        makefile = _fetch_url(f"{base_raw}/{mf_path}", raw_headers)
        if makefile:
            result["components"] = _extract_components(makefile)
            result["target"] = _extract_target(makefile)
            result["defines"] = _extract_defines(makefile)
            break

    # For dual-core projects, TARGET is often in common.mk at the root
    if result["target"] is None:
        common_mk = _fetch_url(f"{base_raw}/common.mk", raw_headers)
        if common_mk:
            result["target"] = _extract_target(common_mk)

    # Cache the result
    if use_cache:
        _write_cache(repo_name, result)

    return result


# ── Source Code Fetching ──────────────────────────────────────────────────────

# File extensions to fetch
SOURCE_EXTENSIONS = {".c", ".h", ".cpp", ".hpp"}

# Additional files to always include (useful for understanding project config)
INCLUDE_BY_NAME = {"Makefile", "FreeRTOSConfig.h", "common.mk"}

# Paths/patterns to EXCLUDE from source fetch
EXCLUDE_PATH_SEGMENTS = {
    "libs", "bsps", "build", "deps", "mtb_shared",
    "GeneratedSource", "COMPONENT_BSP_DESIGN_MODUS",
    "COMPONENT_CUSTOM_DESIGN_MODUS", ".git",
}

# File name prefixes to exclude (generated configs)
EXCLUDE_FILE_PREFIXES = ("cycfg_", "cyfitter", "cybootloader")

# Files to exclude by exact name
EXCLUDE_FILE_NAMES = {".cyignore", ".gitignore"}


def _should_include_source(path: str) -> bool:
    """Determine if a file path should be included in source output."""
    parts = path.replace("\\", "/").split("/")
    filename = parts[-1]

    # Always include specific useful files
    if filename in INCLUDE_BY_NAME:
        # But still exclude if inside libs/bsps/etc.
        for part in parts[:-1]:
            if part in EXCLUDE_PATH_SEGMENTS:
                return False
        return True

    # Check extension
    ext = os.path.splitext(path)[1].lower()
    if ext not in SOURCE_EXTENSIONS:
        return False

    # Check path segments for excluded directories
    for part in parts:
        if part in EXCLUDE_PATH_SEGMENTS:
            return False

    # Check file name exclusions
    if filename in EXCLUDE_FILE_NAMES:
        return False
    if any(filename.startswith(prefix) for prefix in EXCLUDE_FILE_PREFIXES):
        return False

    return True


def fetch_source_code(repo_name: str, use_cache: bool = True) -> dict:
    """Fetch all user source files from a CE repo using the Git Tree API.

    Uses a single API call to get the full repo tree, then filters to
    source files only. Files are cached as actual files on disk.

    Args:
        repo_name: GitHub repo name
        use_cache: Whether to use cached results

    Returns:
        Dict with 'repo', 'files' (list of {path, content}), 'error', 'total_chars'
    """
    # Check file-tree cache first
    if use_cache and _is_source_cached(repo_name):
        manifest = _read_source_manifest(repo_name)
        if manifest:
            # Reconstruct result from cached files
            files = []
            total_chars = 0
            for entry in manifest:
                content = _read_source_file(repo_name, entry["path"])
                if content:
                    files.append({"path": entry["path"], "content": content})
                    total_chars += len(content)
            return {
                "repo": repo_name,
                "files": files,
                "error": None,
                "total_chars": total_chars,
                "_from_cache": True,
            }

    result = {
        "repo": repo_name,
        "files": [],
        "error": None,
        "total_chars": 0,
    }

    api_headers = _github_headers()
    raw_headers = _raw_headers()
    base_raw = f"https://raw.githubusercontent.com/{GITHUB_ORG}/{repo_name}/{DEFAULT_BRANCH}"
    base_api = f"https://api.github.com/repos/{GITHUB_ORG}/{repo_name}"

    # Use Git Tree API — single call gets entire repo structure
    tree_data = _fetch_json(
        f"{base_api}/git/trees/{DEFAULT_BRANCH}?recursive=1", api_headers
    )

    if tree_data is None:
        result["error"] = "Could not access repo — may not exist or is private"
        return result

    tree = tree_data.get("tree", [])
    if not tree:
        result["error"] = "Empty repository tree"
        return result

    # Filter to source files
    source_paths = [
        item["path"] for item in tree
        if item.get("type") == "blob" and _should_include_source(item["path"])
    ]

    if not source_paths:
        result["error"] = "No source files found after filtering"
        return result

    # Fetch content for each source file
    for path in sorted(source_paths):
        content = _fetch_url(f"{base_raw}/{path}", raw_headers)
        if content:
            result["files"].append({
                "path": path,
                "content": content,
            })
            result["total_chars"] += len(content)

    if not result["files"]:
        result["error"] = "Found source paths but could not fetch content"
        return result

    # Write to file-tree cache
    if use_cache:
        _write_source_cache(repo_name, result["files"])

    return result


def format_source_result(data: dict, index_only: bool = False) -> str:
    """Format source code output.

    Args:
        data: Result from fetch_source_code()
        index_only: If True, show only file manifest (paths + sizes + first line).
                    If False, show full file contents.
    """
    lines = []
    lines.append(f"═══ Source: {data['repo']} ═══")

    if data.get("error"):
        lines.append(f"⚠️  ERROR: {data['error']}")
        return "\n".join(lines)

    files = data.get("files", [])
    total = data.get("total_chars", 0)
    cache_note = " (from cache)" if data.get("_from_cache") else ""
    lines.append(f"Files: {len(files)} | Total: {total:,} chars{cache_note}")
    lines.append("")

    if index_only:
        lines.append("File manifest (use --read <repo> <path> to fetch specific files):")
        lines.append("")
        for f in sorted(files, key=lambda x: x["path"]):
            path = f["path"]
            size_kb = len(f["content"]) / 1024
            # Extract first non-comment, non-blank line as summary
            summary = _first_meaningful_line(f["content"])
            lines.append(f"  {path:55s} ({size_kb:5.1f} KB) {summary}")
        lines.append("")
        lines.append(f"To read specific files:")
        lines.append(f"  python .github/skills/mtb-tools/scripts/describe_code_example.py {data['repo']} --read <path> [path2 ...]")
    else:
        for f in sorted(files, key=lambda x: x["path"]):
            lines.append(f"──── {f['path']} ────")
            lines.append(f["content"])
            lines.append("")

    return "\n".join(lines)


def _first_meaningful_line(content: str) -> str:
    """Extract the first meaningful line from source content for the manifest."""
    for line in content.split("\n"):
        stripped = line.strip()
        # Skip empty lines
        if not stripped:
            continue
        # Skip comment lines (C, C++, Makefile)
        if stripped.startswith(("//", "/*", "*", "/**", "///", "/*!", "#")):
            continue
        # For Makefiles — find first assignment after comments
        if "=" in stripped:
            if len(stripped) > 60:
                return f"— {stripped[:57]}..."
            return f"— {stripped}"
        # Skip preprocessor directives
        if stripped.startswith(("#include", "#ifndef", "#define", "#ifdef", "#pragma")):
            continue
        # Return truncated meaningful line
        if len(stripped) > 60:
            return f"— {stripped[:57]}..."
        return f"— {stripped}"
    return ""


def read_cached_files(repo_name: str, file_paths: list[str]) -> str:
    """Read specific files from the file-tree cache.

    Args:
        repo_name: The CE repo name
        file_paths: List of file paths to read from cache

    Returns:
        Formatted output with requested file contents
    """
    if not _is_source_cached(repo_name):
        return (
            f"⚠️  No cached source for '{repo_name}'. "
            f"Run with --source first to fetch and cache the files:\n"
            f"  python .github/skills/mtb-tools/scripts/describe_code_example.py --source {repo_name}"
        )

    manifest = _read_source_manifest(repo_name)
    available_paths = {entry["path"] for entry in manifest} if manifest else set()

    lines = []
    lines.append(f"═══ {repo_name} — selected files ═══")
    lines.append("")

    for path in file_paths:
        content = _read_source_file(repo_name, path)
        if content:
            lines.append(f"──── {path} ────")
            lines.append(content)
            lines.append("")
        else:
            lines.append(f"──── {path} ── NOT FOUND ────")
            # Suggest close matches
            filename = path.split("/")[-1]
            matches = [p for p in available_paths if filename in p]
            if matches:
                lines.append(f"  Did you mean: {', '.join(sorted(matches)[:5])}")
            lines.append("")

    return "\n".join(lines)


# ── Output Formatting ─────────────────────────────────────────────────────────

def format_result(data: dict) -> str:
    """Format metadata into human-readable output for the agent during planning."""
    lines = []
    lines.append(f"═══ {data['repo']} ═══")
    lines.append(f"URL: {data.get('github_url', 'N/A')}")
    lines.append(f"Structure: {data.get('structure_type', 'unknown')}")

    if data.get("error"):
        lines.append(f"⚠️  ERROR: {data['error']}")
        lines.append("")
        return "\n".join(lines)

    lines.append(f"Target: {data.get('target', 'N/A')}")
    lines.append("")

    # Description
    lines.append("Description:")
    lines.append(f"  {data.get('description', 'No description available')}")
    lines.append("")

    # Dependencies
    deps = data.get("dependencies", [])
    if deps:
        lines.append(f"Dependencies ({len(deps)}):")
        for dep in sorted(deps, key=lambda d: d.get("name", "")):
            name = dep.get("name", "unknown")
            version = dep.get("version", "?")
            source = dep.get("source_dir", "")
            lines.append(f"  • {name} ({version}) [{source}]")
    else:
        lines.append("Dependencies: NONE (bare template — all libraries must be added post-creation)")
    lines.append("")

    # Components
    components = data.get("components", [])
    if components:
        lines.append(f"Makefile COMPONENTS: {' '.join(components)}")
    else:
        lines.append("Makefile COMPONENTS: (none)")

    # Defines
    defines = data.get("defines", [])
    if defines:
        lines.append(f"Makefile DEFINES: {' '.join(defines)}")
    lines.append("")

    # Cache indicator
    if data.get("_from_cache"):
        lines.append("  (from cache)")
    lines.append("")

    return "\n".join(lines)


# ── CLI Entry Point ───────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Fetch live metadata from Infineon code example repos on GitHub",
        epilog="Examples:\n"
        "  describe_code_example.py mtb-example-psoc-edge-empty-app\n"
        "  describe_code_example.py mtb-example-psoc-edge-empty-app mtb-example-psoc-edge-wifi-mqtt-client\n"
        "  describe_code_example.py --no-cache mtb-example-psoc-edge-empty-app\n"
        "  describe_code_example.py --source mtb-example-psoc-edge-pwm-square-wave",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "repos",
        nargs="+",
        metavar="REPO_NAME",
        help="One or more CE repo names (e.g., mtb-example-psoc-edge-empty-app)",
    )
    parser.add_argument(
        "--no-cache",
        action="store_true",
        help="Skip cache and fetch fresh data from GitHub",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output raw JSON instead of formatted text",
    )
    parser.add_argument(
        "--source",
        action="store_true",
        help="Fetch and cache user source code, then display file manifest (index only). "
        "Use --read to retrieve specific files from cache.",
    )
    parser.add_argument(
        "--source-full",
        action="store_true",
        help="Fetch and display ALL source code (large output). Prefer --source + --read.",
    )
    parser.add_argument(
        "--read",
        nargs="+",
        metavar="PATH",
        help="Read specific file(s) from cached source. First positional arg is repo name. "
        "Requires prior --source call to populate cache.",
    )

    args = parser.parse_args()

    if args.read:
        # Read mode: first repo arg already consumed, read paths are in --read
        repo = args.repos[0]
        file_paths = args.read
        print(read_cached_files(repo, file_paths))
    elif args.source or args.source_full:
        # Source code fetch mode
        for repo in args.repos:
            data = fetch_source_code(repo, use_cache=not args.no_cache)
            if args.json:
                data.pop("_cached_at", None)
                data.pop("_from_cache", None)
                print(json.dumps(data, indent=2))
            else:
                print(format_source_result(data, index_only=not args.source_full))
    else:
        # Metadata mode (default)
        results = []
        for repo in args.repos:
            data = describe_code_example(repo, use_cache=not args.no_cache)
            results.append(data)

        if args.json:
            for r in results:
                r.pop("_cached_at", None)
                r.pop("_from_cache", None)
            print(json.dumps(results, indent=2))
        else:
            for data in results:
                print(format_result(data))


if __name__ == "__main__":
    main()
