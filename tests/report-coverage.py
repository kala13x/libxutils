#!/usr/bin/env python3
"""Report library-only GCC line/branch coverage; use after run-regressions.sh coverage."""
import gzip
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
build = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/regressions-coverage"
output = build / "coverage-report"
output.mkdir(parents=True, exist_ok=True)
notes = sorted((build / "CMakeFiles/xutils.dir/src").rglob("*.gcno"))
if not notes:
    sys.exit("No GCC coverage notes found; run tests/run-regressions.sh coverage first.")
for note in notes:
    subprocess.run(["gcov", "--json-format", "--branch-probabilities", str(note)], cwd=output, check=True,
                   stdout=subprocess.DEVNULL)
rows = {}
for archive in output.glob("*.gcov.json.gz"):
    with gzip.open(archive, "rt") as stream:
        report = json.load(stream)
    for entry in report["files"]:
        source = pathlib.Path(entry["file"])
        if not source.is_absolute():
            source = pathlib.Path(report["current_working_directory"]) / source
        try:
            relative = source.resolve().relative_to(root / "src")
        except ValueError:
            continue
        if relative.suffix != ".c":
            continue
        lines = entry["lines"]
        branches = [branch for line in lines for branch in line.get("branches", [])]
        rows[str(relative)] = [sum(line["count"] > 0 for line in lines), len(lines),
                               sum(branch["count"] > 0 for branch in branches), len(branches)]

def percentage(used, total):
    return f"{used}/{total} ({used / total:.1%})" if total else "n/a"

text = ["# Library coverage", "", "Only standalone regressions are included; external consumer tests and fuzzing are excluded.",
        "", "| Module | Executed lines | Taken branches |", "| --- | ---: | ---: |"]
for name, (used, total, taken, count) in sorted(rows.items()):
    text.append(f"| `{name}` | {percentage(used, total)} | {percentage(taken, count)} |")
totals = [sum(row[i] for row in rows.values()) for i in range(4)]
text.append(f"| **Total** | {percentage(*totals[:2])} | {percentage(*totals[2:])} |")
summary = "\n".join(text) + "\n"
(output / "summary.md").write_text(summary)
print(summary)
