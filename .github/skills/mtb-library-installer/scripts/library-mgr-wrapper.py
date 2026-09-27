#!/usr/bin/env python3
"""
library-mgr-wrapper.py — Adds a ModusToolbox library to a project.

Creates a .mtb dependency reference file in the project's deps/ directory
for the specified library, using the standard MTB .mtb file format.
After this script completes, run 'make getlibs' to download the library
source to ../mtb_shared/<library-name>/<version>/.

Background — how MTB library installation works:
    Libraries are referenced via .mtb files in the project's deps/ directory.
    Each .mtb file is a single line with three fields separated by '#':
        <github-url>#<version-tag>#<storage-path>
    Running 'make getlibs' reads these files and downloads the source to the
    location specified by the storage path (typically ../mtb_shared/).
    The library-manager-cli tool manages BSP operations only; library .mtb
    file creation is handled by this wrapper script.

Usage:
    python library-mgr-wrapper.py --add <library-name> [options]
    python library-mgr-wrapper.py --help

Exit codes:
    0  .mtb file created (or already present — run 'make getlibs' next).
    1  Argument or filesystem error (reported to stderr).
"""

import argparse
import os
import sys

GITHUB_DEFAULT_ORG = "Infineon"
DEFAULT_VERSION = "latest-v1.X"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _fatal(*lines: str) -> None:
    """Print one or more error lines to stderr and exit with code 1."""
    for line in lines:
        print(f"ERROR: {line}", file=sys.stderr)
    sys.exit(1)


def build_mtb_content(url: str, library_name: str, version: str, local: bool) -> str:
    """Return the single-line content for a .mtb reference file.

    Format: <github-url>#<version-tag>#<storage-path>

    The storage path uses $$ASSET_REPO$$ (resolved by the build system to the
    shared mtb_shared location) for shared libraries, or $$LOCAL$$ for
    libraries stored locally within the application.
    """
    path = f"$$LOCAL$$/{library_name}" if local else f"$$ASSET_REPO$$/{library_name}/{version}"
    return f"{url}#{version}#{path}\n"


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="library-mgr-wrapper.py",
        description=(
            "Add a ModusToolbox library to a project by creating a .mtb "
            "reference file in the project's deps/ directory. "
            "Run 'make getlibs' after this script to download the library source."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
.mtb file format (one line written to deps/<library>.mtb):
    <github-url>#<version-tag>#<storage-path>

    <github-url>    URL to the library's GitHub repository (from MCP manifest RemoteURI)
    <version-tag>   Git tag specifying the library version (from MCP manifest VersionTags[0])
    <storage-path>  $$ASSET_REPO$$/<lib>/<ver> for shared (default)
                    $$LOCAL$$/<lib> for app-local storage

examples:
  Add retarget-io using URL and version from the MCP manifest (recommended):
    python library-mgr-wrapper.py --add retarget-io \\
        --url https://github.com/Infineon/retarget-io/ --version release-v1.9.0

  Add retarget-io using defaults (when MCP manifest is unavailable):
    python library-mgr-wrapper.py --add retarget-io

  Add a library from a different GitHub organization (fallback, no MCP):
    python library-mgr-wrapper.py --add my-library --org MyOrg

  Add a library as app-local (stored in libs/ rather than mtb_shared/):
    python library-mgr-wrapper.py --add my-library \\
        --url https://github.com/Infineon/my-library/ --version release-v2.0.0 --local

  Add a library to a specific project sub-directory (e.g., multi-core):
    python library-mgr-wrapper.py --add retarget-io \\
        --url https://github.com/Infineon/retarget-io/ --version release-v1.9.0 \\
        --project /path/to/CM33_NS

notes:
  - Always obtain --url and --version from the 'list_available_libraries' MCP tool
    (provided by mtbmcp). This ensures the URL and version are correct for the
    current board/BSP and MTB version.
  - The default version '{DEFAULT_VERSION}' is used only when --version is omitted
    (MCP unavailable). For production, always use a fixed version tag.
  - If a .mtb file already exists for the library, this script exits
    successfully without overwriting it. Existing installations are preserved.
  - After running this script for all required libraries, execute
    'make getlibs' once to download all pending library sources.
""",
    )

    parser.add_argument(
        "--url",
        metavar="URL",
        default=None,
        help=(
            "Exact GitHub URL for the library repository "
            "(e.g., https://github.com/Infineon/retarget-io/). "
            "Obtain this from the 'list_available_libraries' MCP tool (RemoteURI field). "
            "When provided, overrides the URL constructed from --org and --add. "
            "Recommended: always pass --url when calling from a Copilot skill."
        ),
    )
    parser.add_argument(
        "--add",
        metavar="LIBRARY",
        required=True,
        help=(
            "MTB library identifier to add (e.g., retarget-io, mqtt, "
            "wifi-core-freertos-lwip-mbedtls). Must match the GitHub "
            "repository name for the library."
        ),
    )
    parser.add_argument(
        "--project",
        metavar="PATH",
        default=os.getcwd(),
        help=(
            "Path to the ModusToolbox project (or sub-project) directory. "
            "Defaults to the current working directory. For multi-core "
            "projects, pass the target core sub-project path (e.g., CM33_NS/)."
        ),
    )
    parser.add_argument(
        "--version",
        metavar="VERSION",
        default=DEFAULT_VERSION,
        help=(
            f"Version tag to embed in the .mtb file (default: {DEFAULT_VERSION!r}). "
            "For reproducible builds use a fixed release tag such as "
            "'release-v1.9.0'. Check the library's GitHub releases page for "
            "available versions."
        ),
    )
    parser.add_argument(
        "--org",
        metavar="ORG",
        default=GITHUB_DEFAULT_ORG,
        help=(
            f"GitHub organization hosting the library (default: {GITHUB_DEFAULT_ORG!r}). "
            "Used only when --url is not provided. Override for libraries not hosted "
            "under the Infineon GitHub org. Prefer using --url (from the MCP manifest) "
            "rather than relying on this default."
        ),
    )
    parser.add_argument(
        "--local",
        action="store_true",
        help=(
            "Store the library locally in the application's libs/ directory "
            "instead of the shared mtb_shared/ location. Use for libraries "
            "that should not be shared across workspace projects."
        ),
    )
    return parser


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    project_path = os.path.abspath(args.project)
    if not os.path.isdir(project_path):
        _fatal(f"Project path does not exist or is not a directory: {project_path}")

    # Resolve the GitHub URL: prefer --url (from MCP manifest), fall back to org+name.
    if args.url:
        library_url = args.url.rstrip("/") + "/"  # ensure trailing slash
    else:
        library_url = f"https://github.com/{args.org}/{args.add}/"

    deps_dir = os.path.join(project_path, "deps")
    mtb_file = os.path.join(deps_dir, f"{args.add}.mtb")

    # Respect existing installations — do not overwrite.
    if os.path.exists(mtb_file):
        print(
            f"SKIPPED: '{args.add}' is already present at {mtb_file}.\n"
            "Run 'make getlibs' if the library source has not been downloaded yet."
        )
        sys.exit(0)

    # Create deps/ if it does not exist.
    try:
        os.makedirs(deps_dir, exist_ok=True)
    except OSError as exc:
        _fatal(f"Could not create deps/ directory at {deps_dir}: {exc}")

    # Write the .mtb reference file.
    content = build_mtb_content(library_url, args.add, args.version, args.local)
    try:
        with open(mtb_file, "w") as fh:
            fh.write(content)
    except OSError as exc:
        _fatal(f"Failed to write {mtb_file}: {exc}")

    url_source = "(from MCP manifest)" if args.url else "(default — MCP manifest not used)"
    location_type = "local (libs/)" if args.local else "shared (mtb_shared/)"
    print(
        f"SUCCESS: Added '{args.add}' [{location_type}]\n"
        f"  .mtb file : {mtb_file}\n"
        f"  content   : {content.strip()}\n"
        f"  url source: {url_source}\n"
        f"Run 'make getlibs' to download the library source."
    )


if __name__ == "__main__":
    main()
