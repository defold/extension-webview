#!/usr/bin/env python3
"""Run host regression tests against a built Defold SDK on macOS."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--defold-home", default=os.environ.get("WEBVIEW_DEFOLD_HOME"),
                    help="Defold build/install directory containing sdk/include and lib/<platform>")
args = parser.parse_args()
if not args.defold_home:
    parser.error("provide --defold-home or WEBVIEW_DEFOLD_HOME")
if platform.system() != "Darwin":
    parser.error("this runner currently supports macOS hosts")
sdk = Path(args.defold_home).resolve()
root = Path(__file__).resolve().parents[1]
host = "arm64-macos" if platform.machine() == "arm64" else "x86_64-macos"
with tempfile.TemporaryDirectory(prefix="webview-tests-") as directory:
    executable = Path(directory) / "test_callbacks"
    subprocess.run([
        "clang++", "-std=c++11", "-g", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-Wl,-dead_strip", "-Wno-nontrivial-memcall",
        "-DDM_PLATFORM_OSX", '-DDLIB_LOG_DOMAIN="WEBVIEW_TEST"',
        "-I" + str(sdk / "sdk/include"), "-I" + str(sdk / "include"),
        "-I" + str(root / "webview/src"),
        str(root / "tests/test_callbacks.cpp"), str(root / "webview/src/webview_common.cpp"),
        "-L" + str(sdk / "lib" / host), "-lscript", "-llua", "-ldlib",
        "-lprofile_null", "-lddf", "-framework", "CoreFoundation", "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
