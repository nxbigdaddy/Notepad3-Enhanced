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
args = parser.parse_args()
folder = args.directory.resolve()
assert (folder / "pdf-test.marker").is_file(), "Missing isolated test marker"
output = folder / "export.pdf"
reader = PdfReader(output)
text = "\n".join(page.extract_text() or "" for page in reader.pages)
for expected in ["Markdown PDF", "导出验证", "这是中文正文", "English text.", "项目", "示例", "42", "return"]:
    assert expected in text, f"Missing PDF content: {expected}"
assert "AFTER SNAPSHOT" not in text, "PDF includes editing after snapshot"
assert len(reader.pages) == 1, "Unexpected fixture pagination"
page = reader.pages[0]
assert abs(float(page.mediabox.width) - 210 / 25.4 * 72) < 1, "Page width is not A4"
assert abs(float(page.mediabox.height) - 297 / 25.4 * 72) < 1, "Page height is not A4"
images = [item for page in reader.pages for item in page.images]
assert images and images[0].image.size == (96, 64), "Local fixture image not embedded"
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert digest(output) == digest(folder / "baseline.pdf"), "Failed replacement changed earlier PDF"
(folder / "extracted-text.txt").write_text(text, encoding="utf-8")
renderer = shutil.which("pdftoppm")
assert renderer, "Install Poppler or add pdftoppm to PATH for visual inspection"
subprocess.run([renderer, "-png", "-r", "120", str(output), str(folder / "page")], check=True)
report = {"pages": len(reader.pages), "images": len(images), "a4": True,
          "snapshot_immutable": True, "locked_replacement_preserved": True,
          "sha256": digest(output), "rendered_pages": [p.name for p in folder.glob("page-*.png")]}
(folder / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report))
