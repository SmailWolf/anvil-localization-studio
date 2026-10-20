"""Copy imported MSYS2 runtime DLLs recursively; never execute inspected files."""
from pathlib import Path
import re
import shutil
import subprocess
import sys

stage, runtime = (Path(arg).resolve() for arg in sys.argv[1:])
index = {path.name.lower(): path for path in runtime.glob("*.dll")}
queue = [p for p in stage.rglob("*") if p.suffix.lower() in {".exe", ".dll"}]
seen = set()
while queue:
    binary = queue.pop()
    if binary in seen:
        continue
    seen.add(binary)
    result = subprocess.run(["objdump", "-p", str(binary)], check=True, capture_output=True, text=True)
    for name in re.findall(r"DLL Name:\s*([^\r\n]+)", result.stdout):
        name = name.strip()
        source = index.get(name.lower())
        if source is None:  # Windows system DLL or a bundled llama/Qt library.
            continue
        # The application directory is searched for app/plugin imports. Keep
        # the independent engine self-contained in its own directory.
        parent = stage / "bin" if binary.is_relative_to(stage / "bin") or binary.is_relative_to(stage / "plugins") else binary.parent
        destination = parent / source.name
        if not destination.exists():
            shutil.copy2(source, destination)
            queue.append(destination)
print(f"Inspected {len(seen)} executables and DLLs")
