#!/usr/bin/env python3
"""generate_sbom.py — Generate an SPDX 2.3 tag-value SBOM for the newbase engine.

Usage:
    python scripts/generate_sbom.py [--output PATH]

Output defaults to <repo-root>/sbom.spdx.
"""

import argparse
import re
import subprocess
import uuid
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Ordered from most specific to least to avoid false positives.
LICENSE_PATTERNS = [
    (r"Mozilla Public License.*2\.0|MPL-2\.0",          "MPL-2.0"),
    (r"Apache License.*Version 2\.0|Apache-2\.0",       "Apache-2.0"),
    # Require "under the … GNU … License" so that MPL-2.0's definitional
    # reference to GPL does not trigger a false positive.
    (r"under the (?:terms of the )?GNU Lesser General Public License",
                                                         "LGPL-2.1-or-later"),
    (r"under the (?:terms of the )?GNU General Public License",
                                                         "GPL-2.0-or-later"),
    (r"BSD 3-Clause|3-clause BSD|3-Clause BSD",          "BSD-3-Clause"),
    (r"BSD 2-Clause",                                    "BSD-2-Clause"),
    (r"The MIT License|MIT License",                     "MIT"),
    (r"\bMIT\b",                                         "MIT"),
    # Classic MIT body used without the "MIT License" header (e.g. rapidyaml).
    (r"Permission is hereby granted, free of charge",   "MIT"),
    # Zlib: SDL and icon-font-cpp-headers use the classic zlib phrasing.
    (r"This software is provided 'as-is'.*without any express or implied",
                                                         "Zlib"),
    (r"public domain",                                   "LicenseRef-PublicDomain"),
]

# Overrides for packages whose LICENSE file trips up the regex heuristics
# above (e.g. multi-license bundles where an embedded sub-license's text
# gets matched instead of the project's actual license). Keyed by package
# name as it appears in the vendored/ directory (or nested submodule dirname).
LICENSE_OVERRIDES: dict[str, str] = {
    # LICENSE.TXT bundles the LLVM/NCSA license plus several embedded
    # third-party snippets; the MIT/public-domain patterns above match one
    # of those snippets instead of the actual top-level license (NCSA).
    "DirectXShaderCompiler": "NCSA",
}


def detect_license(text: str) -> str:
    """Return an SPDX license expression detected from license file text."""
    found: list[str] = []
    for pattern, spdx_id in LICENSE_PATTERNS:
        if re.search(pattern, text, re.IGNORECASE | re.DOTALL):
            if spdx_id not in found:
                found.append(spdx_id)
    if not found:
        return "NOASSERTION"
    if len(found) == 1:
        return found[0]
    return " OR ".join(found)


def extract_copyright(text: str) -> str:
    """Return the first copyright line found in the license text."""
    m = re.search(
        r"Copyright\s+(?:\(c\)\s*|\(C\)\s*|©\s*)?\d{4}[^\n]*",
        text,
    )
    if m:
        return m.group(0).strip()
    return "NOASSERTION"


def find_license_files(path: Path) -> list[Path]:
    """Return all license/copying files in *path* (sorted for determinism)."""
    return sorted(
        f for f in path.iterdir()
        if f.is_file() and re.match(r"licen[sc]e|copying", f.name, re.IGNORECASE)
    )


def parse_gitmodules(base_dir: Path) -> dict[str, dict]:
    """
    Parse base_dir/.gitmodules into a dict keyed by submodule path.
    Each value holds the key/value pairs from that submodule block
    (excluding 'path' itself).
    """
    gitmodules = base_dir / ".gitmodules"
    if not gitmodules.exists():
        return {}

    result: dict[str, dict] = {}
    current_name: str | None = None
    current_data: dict = {}

    def flush():
        if current_name is not None and "path" in current_data:
            path_key = current_data.pop("path")
            result[path_key] = current_data.copy()

    for line in gitmodules.read_text().splitlines():
        line = line.strip()
        m = re.match(r'\[submodule "(.+)"\]', line)
        if m:
            flush()
            current_name = m.group(1)
            current_data = {}
        elif current_name is not None and "=" in line:
            key, val = line.split("=", 1)
            current_data[key.strip()] = val.strip()

    flush()
    return result


def get_submodule_info(base_dir: Path) -> dict[str, dict]:
    """
    Combine base_dir/.gitmodules URL data with live commit/tag data from
    `git submodule status` run inside base_dir.
    Returns dict: relative-path -> {url, commit, version_tag}.
    """
    by_path = parse_gitmodules(base_dir)

    r = subprocess.run(
        ["git", "submodule", "status"],
        cwd=base_dir, capture_output=True, text=True,
    )
    for line in r.stdout.splitlines():
        if not line:
            continue
        # Format: [status_char]<SHA1> <path> (<describe>)
        rest = line[1:]          # strip leading status char (' ', '+', '-', 'U')
        parts = rest.split(None, 2)
        if len(parts) < 2:
            continue
        commit, path = parts[0], parts[1]
        tag = parts[2].strip("() \t") if len(parts) == 3 else commit[:12]
        if path in by_path:
            by_path[path]["commit"] = commit
            by_path[path]["version_tag"] = tag

    return by_path


def clean_version(tag: str) -> str:
    """Strip trailing -N-gHASH describe suffix to produce a clean version."""
    return re.sub(r"-\d+-g[0-9a-f]+$", "", tag)


def spdx_id(name: str) -> str:
    return "SPDXRef-Package-" + re.sub(r"[^a-zA-Z0-9.\-]", "-", name)


def multiline(value: str) -> str:
    """Wrap value in SPDX <text>…</text> tags when it contains newlines."""
    if "\n" in value:
        return f"<text>{value}</text>"
    return value


def format_package(pkg: dict) -> str:
    lines = [
        f"PackageName: {pkg['name']}",
        f"SPDXID: {pkg['spdx_id']}",
        f"PackageVersion: {pkg['version']}",
        f"PackageDownloadLocation: {pkg['download_location']}",
        "FilesAnalyzed: false",
        f"PackageLicenseConcluded: {pkg['license']}",
        f"PackageLicenseDeclared: {pkg['license']}",
        f"PackageCopyrightText: {multiline(pkg['copyright'])}",
    ]
    if pkg.get("vcs_ref"):
        lines.append(f"ExternalRef: OTHER vcs {pkg['vcs_ref']}")
    return "\n".join(lines)


def get_project_version() -> str:
    cmake = REPO_ROOT / "CMakeLists.txt"
    if cmake.exists():
        m = re.search(r'NEWBASE_VERSION\s+"([^"]+)"', cmake.read_text())
        if m:
            return m.group(1)
    return "NOASSERTION"


def get_remote_url(path: Path) -> str:
    r = subprocess.run(
        ["git", "remote", "get-url", "origin"],
        cwd=path, capture_output=True, text=True,
    )
    url = r.stdout.strip()
    return url if url else "NOASSERTION"


def make_package(
    *,
    name: str,
    version: str,
    download_location: str,
    lic_text: str,
    vcs_ref: str | None = None,
    parent_spdx_id: str | None = None,
) -> dict:
    license = LICENSE_OVERRIDES.get(name) or (detect_license(lic_text) if lic_text else "NOASSERTION")
    return {
        "name": name,
        "spdx_id": spdx_id(name),
        "version": version or "NOASSERTION",
        "download_location": download_location,
        "license": license,
        "copyright": extract_copyright(lic_text) if lic_text else "NOASSERTION",
        "vcs_ref": vcs_ref,
        "parent_spdx_id": parent_spdx_id,
    }


def get_nested_dep_packages(vdir: Path, parent_spdx_id: str) -> list[dict]:
    """
    Discover submodules nested inside a vendored dependency (e.g. the
    SPIRV-Cross/SPIRV-Tools/SPIRV-Headers/DirectXShaderCompiler submodules
    that live inside vendored/SDL_shadercross/external/). Skips submodules
    that aren't checked out (empty directory).
    """
    nested_info = get_submodule_info(vdir)
    packages: list[dict] = []

    for rel_path, info in sorted(nested_info.items()):
        nested_path = vdir / rel_path
        if not nested_path.is_dir() or not any(nested_path.iterdir()):
            continue  # not checked out — skip rather than emit a NOASSERTION stub

        lic_files = find_license_files(nested_path)
        lic_text = "\n".join(f.read_text(errors="replace") for f in lic_files)

        url = info.get("url", "NOASSERTION")
        commit = info.get("commit", "")
        raw_tag = info.get("version_tag", "")
        version = clean_version(raw_tag) if raw_tag else "NOASSERTION"
        vcs_ref = f"git+{url}@{commit}" if url != "NOASSERTION" and commit else None

        packages.append(make_package(
            name=nested_path.name,
            version=version,
            download_location=url,
            lic_text=lic_text,
            vcs_ref=vcs_ref,
            parent_spdx_id=parent_spdx_id,
        ))

    return packages


def filter_ignored(packages: list[dict], ignore_names: set[str]) -> list[dict]:
    """
    Drop packages named in ignore_names, along with any descendants (packages
    whose parent_spdx_id chain leads to a dropped package). Used for
    dependencies that are present in the source tree (e.g. as a submodule)
    but weren't actually compiled into this particular build — CMake knows
    which build flags were used and passes those names via --ignore.
    """
    if not ignore_names:
        return packages

    removed_ids: set[str] = set()
    kept = list(packages)
    changed = True
    while changed:
        changed = False
        next_kept = []
        for pkg in kept:
            if pkg["name"] in ignore_names or pkg["parent_spdx_id"] in removed_ids:
                removed_ids.add(pkg["spdx_id"])
                changed = True
                continue
            next_kept.append(pkg)
        kept = next_kept
    return kept


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate an SPDX 2.3 tag-value SBOM for the newbase engine.",
    )
    parser.add_argument(
        "--output", "-o",
        default=str(REPO_ROOT / "sbom.spdx"),
        help="Output file path (default: <repo-root>/sbom.spdx)",
    )
    parser.add_argument(
        "--ignore", action="append", default=[], metavar="NAME",
        help="Package name to exclude from the SBOM, e.g. a dependency present "
             "in the source tree but not compiled into this build "
             "(descendants are dropped too). May be passed multiple times.",
    )
    # When newbase is used as a dependency from an external project, CMake
    # passes these so the caller appears as the top-level SPDX package.
    parser.add_argument("--caller-name",       default=None,
                        help="Name of the calling project (external build only)")
    parser.add_argument("--caller-version",    default="",
                        help="Version string of the calling project")
    parser.add_argument("--caller-source-dir", default=None,
                        help="Source directory of the calling project "
                             "(used to read its LICENSE file and git remote URL)")
    args = parser.parse_args()

    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    # --- Caller package (external build only) ---
    caller_pkg: dict | None = None
    if args.caller_name:
        caller_src = Path(args.caller_source_dir) if args.caller_source_dir else None
        caller_lic_text = ""
        if caller_src and caller_src.is_dir():
            lic_files = find_license_files(caller_src)
            caller_lic_text = "\n".join(
                f.read_text(errors="replace") for f in lic_files
            )
        caller_url = get_remote_url(caller_src) if caller_src else "NOASSERTION"
        caller_pkg = make_package(
            name=args.caller_name,
            version=args.caller_version,
            download_location=caller_url,
            lic_text=caller_lic_text,
        )
        doc_name = args.caller_name
    else:
        doc_name = "newbase"

    doc_namespace = f"https://spdx.org/spdxdocs/{doc_name}-{uuid.uuid4()}"

    # --- newbase package ---
    root_lic_file = REPO_ROOT / "LICENSE"
    root_lic_text = root_lic_file.read_text() if root_lic_file.exists() else ""
    root_pkg = make_package(
        name="newbase",
        version=get_project_version(),
        download_location=get_remote_url(REPO_ROOT),
        lic_text=root_lic_text,
    )
    root_pkg["spdx_id"] = "SPDXRef-Package-newbase"  # stable well-known ID

    # --- Vendored dependencies ---
    submodule_info = get_submodule_info(REPO_ROOT)
    vendored_dir = REPO_ROOT / "vendored"
    dep_packages: list[dict] = []

    for vdir in sorted(vendored_dir.iterdir()):
        if not vdir.is_dir():
            continue

        rel_path = str(vdir.relative_to(REPO_ROOT))
        sub_info = submodule_info.get(rel_path, {})

        lic_files = find_license_files(vdir)
        lic_text = "\n".join(f.read_text(errors="replace") for f in lic_files)

        url = sub_info.get("url", "NOASSERTION")
        commit = sub_info.get("commit", "")
        raw_tag = sub_info.get("version_tag", "")
        version = clean_version(raw_tag) if raw_tag else "NOASSERTION"
        vcs_ref = f"git+{url}@{commit}" if url != "NOASSERTION" and commit else None

        dep_pkg = make_package(
            name=vdir.name,
            version=version,
            download_location=url,
            lic_text=lic_text,
            vcs_ref=vcs_ref,
        )
        dep_packages.append(dep_pkg)

        # Recurse into submodules nested inside this dependency (e.g.
        # SDL_shadercross's vendored SPIRV-Cross/SPIRV-Tools/SPIRV-Headers/
        # DirectXShaderCompiler), which are statically linked into newbase
        # but not listed in the top-level .gitmodules.
        dep_packages.extend(get_nested_dep_packages(vdir, dep_pkg["spdx_id"]))

    dep_packages = filter_ignored(dep_packages, set(args.ignore))

    # --- Assemble SPDX document ---
    out: list[str] = []

    def section(title: str) -> None:
        out.append("##-------------------------")
        out.append(f"## {title}")
        out.append("##-------------------------")
        out.append("")

    out.append("SPDXVersion: SPDX-2.3")
    out.append("DataLicense: CC0-1.0")
    out.append("")
    section("Document Information")
    out.append("SPDXID: SPDXRef-DOCUMENT")
    out.append(f"DocumentName: {doc_name}")
    out.append(f"DocumentNamespace: {doc_namespace}")
    out.append("")
    section("Creation Information")
    out.append("Creator: Tool: generate_sbom.py")
    out.append(f"Created: {now}")
    out.append("")
    section("Packages")
    all_packages = ([caller_pkg] if caller_pkg else []) + [root_pkg] + dep_packages
    for pkg in all_packages:
        out.append(format_package(pkg))
        out.append("")
    section("Relationships")
    if caller_pkg:
        out.append(f"Relationship: SPDXRef-DOCUMENT DESCRIBES {caller_pkg['spdx_id']}")
        out.append(f"Relationship: {caller_pkg['spdx_id']} DEPENDS_ON SPDXRef-Package-newbase")
    else:
        out.append("Relationship: SPDXRef-DOCUMENT DESCRIBES SPDXRef-Package-newbase")
    for dep in dep_packages:
        parent_spdx_id = dep["parent_spdx_id"] or "SPDXRef-Package-newbase"
        out.append(f"Relationship: {parent_spdx_id} DEPENDS_ON {dep['spdx_id']}")
    out.append("")

    output_path = Path(args.output)
    output_path.write_text("\n".join(out))

    print(f"SBOM written to: {output_path}")
    if caller_pkg:
        print(f"  Caller       : {caller_pkg['name']} {caller_pkg['version']} ({caller_pkg['license']})")
    print(f"  newbase      : {root_pkg['version']} ({root_pkg['license']})")
    print(f"  Dependencies : {len(dep_packages)}")
    for dep in dep_packages:
        print(f"    {dep['name']:<30s} {dep['version']:<25s} {dep['license']}")


if __name__ == "__main__":
    main()
