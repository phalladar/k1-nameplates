"""Build src/ into build/dinput8.dll with zig, then package it with mod/ + LICENSE into
dist/K1-Nameplates-<version>.zip (version read from the source's startup log line).

    python build.py            build + package
    python build.py --dll-only build only (used by the author's install script)
"""
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = [ROOT / "src" / "nameplates.c", ROOT / "src" / "dinput8.def"]
DLL = ROOT / "build" / "dinput8.dll"


def build():
    if DLL.exists() and all(s.stat().st_mtime <= DLL.stat().st_mtime for s in SRC):
        return
    zig = shutil.which("zig")
    if not zig:
        sys.exit("zig not on PATH (winget install zig.zig)")
    DLL.parent.mkdir(exist_ok=True)
    r = subprocess.run([zig, "cc", "-target", "x86-windows-gnu", "-shared", "-O2", "-s", "-o", str(DLL),
                        *map(str, SRC), "-lopengl32", "-lgdi32", "-luser32"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("build failed:\n" + r.stderr)
    for junk in DLL.parent.iterdir():
        if junk != DLL:
            junk.unlink()
    print(DLL, DLL.stat().st_size, "bytes")


def package():
    version = re.search(r'"KOTOR nameplates (\d+\.\d+(?:\.\d+)?) ', SRC[0].read_text()).group(1)
    name = f"K1-Nameplates-{version}"
    out = ROOT / "dist" / f"{name}.zip"
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(DLL, f"{name}/dinput8.dll")
        for f in sorted((ROOT / "mod").iterdir()):
            z.write(f, f"{name}/{f.name}")
        z.write(ROOT / "LICENSE", f"{name}/LICENSE.txt")
    print(out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    build()
    if "--dll-only" not in sys.argv:
        package()
