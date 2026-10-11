import unicodedata
"""Inspect actual native export, compare failed replacement, and render pages."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--advanced", action="store_true")
parser.add_argument("--long", action="store_true")
parser.add_argument("--tall", action="store_true")
parser.add_argument("--race", action="store_true")
args = parser.parse_args()
folder = args.directory.resolve()
advanced = args.advanced
assert (folder / "pdf-test.marker").is_file(), "Missing isolated test marker"
output = folder / "export.pdf"
reader = PdfReader(output)
text = "\n".join(page.extract_text() or "" for page in reader.pages)
for expected in ["Markdown PDF", "导出验证", "这是中文正文", "English text.", "项目", "示例", "42", "return"]:
    assert expected in text, f"Missing PDF content: {expected}"
assert "AFTER SNAPSHOT" not in text, "PDF includes editing after snapshot"
assert "CANCELLED OUTPUT" not in text, "Canceled PDF job replaced the previous output"
if advanced:
    for expected in ["离线图表与公式", "图表起点", "图表终点", "公式标记"]:
        assert expected in text, f"Advanced PDF content missing: {expected}"
    assert "flowchart LR" not in text and "\\frac" not in text and "$E=mc^2$" not in text, "PDF printed raw graph or math source"
    normalized = "".join(unicodedata.normalize("NFKC", text).split())
    assert "E=mc2" in normalized, f"Rendered math not extracted: {normalized}"
if args.long:
    assert len(reader.pages)>=3, "Long fixture was not paginated"
    assert "END-OF-LONG-DOCUMENT" in reader.pages[-1].extract_text(), "Final paragraph was lost or moved"
    for number in range(1,61): assert f"分页段落 {number}" in text, f"Paragraph {number} missing"
elif not args.tall:
    assert len(reader.pages) == 1, "Unexpected fixture pagination"
if args.tall:
    assert all((page.extract_text() or "").strip() for page in reader.pages), "Tall diagram generated an empty page"
    assert any("超高图表" in (page.extract_text() or "") and "TALLNODE01" in (page.extract_text() or "") for page in reader.pages), "Tall diagram heading separated from first node"
    for number in range(1, 31):
        assert f"TALLNODE{number:02d}" in text, f"Tall diagram node {number} missing"
    assert any(all(f"TALLNODE{number:02d}" in (page.extract_text() or "") for number in range(1, 31)) for page in reader.pages), "Tall diagram split across pages"
    assert "TALL-DIAGRAM-END" in text, "Content after tall diagram missing"
    assert "flowchart TD" not in text, "Tall diagram printed as raw Markdown source"
page = reader.pages[0]
assert all(abs(float(item.mediabox.width)-210/25.4*72)<1 for item in reader.pages), "Page width is not A4"
assert all(abs(float(item.mediabox.height)-297/25.4*72)<1 for item in reader.pages), "Page height is not A4"
images = [item for page in reader.pages for item in page.images]
assert images and any(item.image.size == (96, 64) for item in images), "Local fixture image not embedded"
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(output) == digest(folder / "baseline.pdf"), "Failed replacement changed earlier PDF"
(folder / "extracted-text.txt").write_text(text, encoding="utf-8")
renderer = shutil.which("pdftoppm")
assert renderer, "Install Poppler or add pdftoppm to PATH for visual inspection"
subprocess.run([renderer, "-png", "-r", "120", str(output), str(folder / "page")], check=True)
race_report = None
if args.race:
    race_output = folder / "race.pdf"
    race_reader = PdfReader(race_output)
    race_text = "\n".join(p.extract_text() or "" for p in race_reader.pages)
    assert "CANCELLED OUTPUT MUST NOT APPEAR" in race_text, "Unlocked modal replacement did not include its new snapshot"
    assert "AFTER SNAPSHOT" not in race_text, "Unlocked replacement included previously undone edit"
    assert "导出验证" in race_text and "这是中文正文" in race_text, "Unlocked replacement lost original content"
    assert digest(race_output) != digest(folder / "baseline.pdf"), "Unlocked replacement did not change old PDF bytes"
    assert all(abs(float(p.mediabox.width)-210/25.4*72)<1 and abs(float(p.mediabox.height)-297/25.4*72)<1 for p in race_reader.pages), "Unlocked replacement is not A4"
    assert all((p.extract_text() or "").strip() for p in race_reader.pages), "Unlocked replacement contains blank pages"
    if args.tall:
        assert any(all(f"TALLNODE{number:02d}" in (p.extract_text() or "") for number in range(1,31)) for p in race_reader.pages), "Unlocked replacement lost or split diagram"
    subprocess.run([renderer, "-png", "-r", "120", str(race_output), str(folder / "race-page")], check=True)
    (folder / "race-extracted-text.txt").write_text(race_text, encoding="utf-8")
    race_report = {"pages": len(race_reader.pages), "sha256": digest(race_output), "new_snapshot_committed": True}
report = {"pages": len(reader.pages), "images": len(images), "a4": True, "advanced": advanced, "tall": args.tall,
          "tall_nodes": 30 if args.tall else 0, "no_blank_pages": all((page.extract_text() or "").strip() for page in reader.pages),
          "snapshot_immutable": True, "locked_replacement_preserved": True,
          "modal_replacement": race_report, "sha256": digest(output), "rendered_pages": [p.name for p in folder.glob("page-*.png")]}
(folder / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report))
