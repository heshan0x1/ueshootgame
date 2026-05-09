#!/usr/bin/env python3
"""
Build UE dedicated server and package into zip.

Usage:
    python build_ds.py
    python build_ds.py --server-config Shipping
    python build_ds.py --skip-build
"""

import argparse
import os
import sys
import subprocess
import zipfile
from datetime import datetime
from pathlib import Path

# ============================================================
# Default Configuration
# ============================================================

DEFAULTS = {
    # Root paths
    # "engine_root":     r"D:\workspace\ShootGame\UnrealEngine",
    "engine_root":     r"D:\workspace\UnrealEngine-Angelscript",
    "project_root":    r"D:\workspace\ShootGame\FirstPerson",

    # Build settings
    "build_platform":  "Linux",
    "build_target":    "FirstPersonServer",
    "client_config":   "Development",
    "server_config":   "Development",

    # Zip settings
    "zip_name_prefix": "LinuxServer",
}

# Unix permissions to set in zip (octal)
UNIX_FILE_PERM = 0o100777  # -rwxrwxrwx
UNIX_DIR_PERM  = 0o040777  # drwxrwxrwx


# ============================================================
# Helpers
# ============================================================

def log_info(msg: str):
    print(f"[INFO] {msg}")


def log_error(msg: str):
    print(f"[ERROR] {msg}", file=sys.stderr)


def run_build(args) -> int:
    """Run UE BuildCookRun and return the exit code."""
    cmd = [
        "call", args.run_uat, "BuildCookRun",
        f'-project="{args.uproject_file}"',
        "-noP4",
        f"-platform={args.build_platform}",
        f"-target={args.build_target}",
        f"-clientconfig={args.client_config}",
        f"-serverconfig={args.server_config}",
        "-cook",
        "-build",
        "-stage",
        "-pak",
        "-archive",
        f'-archivedirectory="{args.archive_dir}"',
        "-utf8output",
        "-compile",
    ]

    log_info("Starting build ...")
    log_info(f"  UAT     : {args.run_uat}")
    log_info(f"  Project : {args.uproject_file}")
    log_info(f"  Platform: {args.build_platform}")
    log_info(f"  Target  : {args.build_target}")
    log_info(f"  Config  : client={args.client_config}, server={args.server_config}")
    log_info(f"  Archive : {args.archive_dir}")

    # Use shell=True with 'call' prefix to match the behavior of the
    # working build_ds.bat — ensures environment variables (e.g.
    # LINUX_MULTIARCH_ROOT) are properly propagated within the cmd session.
    result = subprocess.run(" ".join(cmd), shell=True)
    return result.returncode


def create_zip(source_dir: str, output_path: str):
    """
    Create a zip from source_dir with Unix 777 permissions on all entries.
    The zip contains the *contents* of source_dir (not the directory itself).
    """
    source = Path(source_dir)
    if not source.is_dir():
        raise FileNotFoundError(f"Source directory not found: {source_dir}")

    log_info(f"Packaging {source_dir}")
    log_info(f"       -> {output_path}")

    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for file_path in sorted(source.rglob("*")):
            arcname = file_path.relative_to(source).as_posix()

            if file_path.is_dir():
                dir_info = zipfile.ZipInfo(arcname + "/")
                dir_info.external_attr = UNIX_DIR_PERM << 16
                zf.writestr(dir_info, "")
            else:
                info = zipfile.ZipInfo.from_file(file_path, arcname)
                info.external_attr = UNIX_FILE_PERM << 16
                with open(file_path, "rb") as f:
                    zf.writestr(info, f.read())

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    log_info(f"Zip package created: {output_path} ({size_mb:.1f} MB)")


def parse_args():
    parser = argparse.ArgumentParser(description="Build UE Dedicated Server and package into zip")

    # Root paths
    parser.add_argument("--engine-root",     default=DEFAULTS["engine_root"],     help="UE engine root directory")
    parser.add_argument("--project-root",    default=DEFAULTS["project_root"],    help="UE project root directory")

    # Build settings
    parser.add_argument("--build-platform",  default=DEFAULTS["build_platform"],  help="Target platform")
    parser.add_argument("--build-target",    default=DEFAULTS["build_target"],    help="Build target name")
    parser.add_argument("--client-config",   default=DEFAULTS["client_config"],   help="Client build configuration")
    parser.add_argument("--server-config",   default=DEFAULTS["server_config"],   help="Server build configuration")

    # Derived paths (optional overrides)
    parser.add_argument("--uproject-file",   default=None, help="Path to .uproject file")
    parser.add_argument("--run-uat",         default=None, help="Path to RunUAT.bat")
    parser.add_argument("--archive-dir",     default=None, help="Archive output directory")
    parser.add_argument("--zip-source-dir",  default=None, help="Directory to zip")
    parser.add_argument("--zip-output-dir",  default=None, help="Zip output directory")
    parser.add_argument("--zip-name-prefix", default=DEFAULTS["zip_name_prefix"], help="Zip file name prefix")

    # Workflow control
    parser.add_argument("--skip-build",      action="store_true", help="Skip build step, only do zip packaging")

    args = parser.parse_args()

    # Resolve derived defaults
    if args.uproject_file is None:
        args.uproject_file = os.path.join(args.project_root, "FirstPerson.uproject")
    if args.run_uat is None:
        args.run_uat = os.path.join(args.engine_root, "Engine", "Build", "BatchFiles", "RunUAT.bat")
    if args.archive_dir is None:
        args.archive_dir = os.path.join(args.project_root, "Build")
    if args.zip_source_dir is None:
        args.zip_source_dir = os.path.join(args.archive_dir, "LinuxServer")
    if args.zip_output_dir is None:
        args.zip_output_dir = args.archive_dir

    return args


# ============================================================
# Main
# ============================================================

def main():
    args = parse_args()

    # --- Step 1: Build ---
    if not args.skip_build:
        ret = run_build(args)
        if ret != 0:
            log_error(f"Build failed with exit code {ret}, skip zip packaging.")
            return ret
    else:
        log_info("Build step skipped (--skip-build)")

    # --- Step 2: Package ---
    datestamp = datetime.now().strftime("%Y%m%d_%H%M")
    zip_name = f"{args.zip_name_prefix}-{datestamp}.zip"
    zip_output = os.path.join(args.zip_output_dir, zip_name)

    try:
        create_zip(args.zip_source_dir, zip_output)
    except Exception as e:
        log_error(f"Zip packaging failed: {e}")
        return 1

    log_info("All done!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
