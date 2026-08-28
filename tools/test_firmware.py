"""Compile and run the firmware's pure logic on this PC.

    python tools/test_firmware.py
    python tools/test_firmware.py --filter EventBus
    python tools/test_firmware.py --verbose

WHAT THIS PROVES
    The C++ logic executes correctly under a host compiler
    with the Arduino layer stubbed out. HOST-VERIFIED.

WHAT IT DOES NOT PROVE
    Anything about the physical ESP32, the SH1106, the
    DS3231, or any sensor. Those stay HARDWARE-UNVERIFIED
    until the board exists and runs this firmware.

Needs no board, no USB, no Arduino IDE, no network.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "sketch_bkl"
TESTS = ROOT / "tests" / "firmware"
BUILD = TESTS / "build"

# Firmware translation units under test. Anything not listed
# here is not compiled, and so is not covered -- keep the
# report honest by keeping this list honest.
FIRMWARE_SOURCES = [
    "src/core/Events.cpp",
    "src/core/StateMachine.cpp",
    "src/display/ExpressionMap.cpp",
    "src/display/MochiPlayer.cpp",
    "src/input/TouchInput.cpp",
    "src/lighting/LedController.cpp",
    "src/power/BatteryMonitor.cpp",
    "src/sensors/ProximitySensor.cpp",
    "src/audio/Microphone.cpp",
    "src/net/MiniWebSocket.cpp",
]

TEST_SOURCES = ["support/tinytest.cpp", "stubs/stubs.cpp"]


def find_compiler() -> str | None:
    """Locate g++ on PATH, or where winget puts WinLibs."""
    found = shutil.which("g++") or shutil.which("clang++")
    if found:
        return found

    winget = (
        Path(os.environ.get("LOCALAPPDATA", ""))
        / "Microsoft"
        / "WinGet"
        / "Packages"
    )
    if winget.is_dir():
        for candidate in winget.glob("*/mingw64/bin/g++.exe"):
            return str(candidate)

    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--filter", default="", help="substring of Suite.test")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--rebuild", action="store_true")
    args = parser.parse_args()

    compiler = find_compiler()
    if compiler is None:
        print(
            "No C++ compiler found.\n"
            "  Install one with:  winget install BrechtSanders.WinLibs.POSIX.UCRT\n"
            "  or put g++/clang++ on PATH."
        )
        return 2

    BUILD.mkdir(parents=True, exist_ok=True)
    binary = BUILD / ("firmware_tests.exe" if os.name == "nt" else "firmware_tests")

    test_files = sorted(TESTS.glob("test_*.cpp"))
    if not test_files:
        print(f"no test files in {TESTS}")
        return 2

    sources = (
        [str(TESTS / s) for s in TEST_SOURCES]
        + [str(TESTS / f.name) for f in test_files]
        + [str(FIRMWARE / s) for s in FIRMWARE_SOURCES]
    )

    command = [
        compiler,
        "-std=c++17",
        "-O1",
        "-Wall",
        # The stubs directory must come first so its Arduino.h
        # and friends win over anything else on the path.
        f"-I{TESTS / 'stubs'}",
        f"-I{TESTS / 'support'}",
        f"-I{FIRMWARE}",
        *sources,
        "-o",
        str(binary),
    ]

    if args.verbose:
        print(" ".join(command), "\n")

    print(f"compiling {len(test_files)} test files + "
          f"{len(FIRMWARE_SOURCES)} firmware modules ...")

    build = subprocess.run(command, capture_output=True, text=True)

    if build.returncode != 0:
        print("\nCOMPILE FAILED\n")
        print(build.stderr[:8000])
        return 1

    if build.stderr.strip() and args.verbose:
        print("warnings:\n", build.stderr[:4000])

    run_args = [str(binary)]
    if args.filter:
        run_args.append(args.filter)

    result = subprocess.run(run_args)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
