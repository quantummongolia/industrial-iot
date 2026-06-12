"""Inject FIRMWARE_VERSION from git tag at compile time.

Format expected: butluur-v1.2.3 (annotated or lightweight)
Fallback: 0.0.0-<commit>-dev for untagged builds.
"""
import subprocess

Import("env")  # noqa: F821 — provided by PlatformIO


def _git(*args: str) -> str:
    return subprocess.check_output(["git", *args], stderr=subprocess.DEVNULL).decode().strip()


def _resolve_version() -> str:
    try:
        v = _git("describe", "--tags", "--match", "butluur-v*", "--dirty=-dirty")
        if v.startswith("butluur-v"):
            v = v[len("butluur-v"):]
        return v
    except Exception:
        pass
    try:
        sha = _git("rev-parse", "--short", "HEAD")
        return f"0.0.0-{sha}-dev"
    except Exception:
        return "0.0.0-dev"


VERSION = _resolve_version()
print(f"[version_script] FIRMWARE_VERSION = {VERSION}")
env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\\"{VERSION}\\"'])  # noqa: F821
