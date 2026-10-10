"""Extract a pinned GitHub archive and verify each regular file against its Git blob ID."""
import hashlib
import json
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parent.parent
tree = json.loads((root / '.cache/upstream-tree.json').read_text(encoding='utf-8-sig'))
expected = {entry['path']: entry['sha'] for entry in tree['tree'] if entry['type'] == 'blob'}
target = root / 'Notepad3'
verified = set()
with zipfile.ZipFile(root / '.cache/Notepad3-source.zip') as archive:
    for item in archive.infolist():
        if item.is_dir():
            continue
        relative = item.filename.split('/', 1)[1]
        path = (target / relative).resolve()
        if not path.is_relative_to(target.resolve()):
            raise RuntimeError('Archive path escapes source directory')
        data = archive.read(item)
        digest = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
        if expected.get(relative) != digest:
            # Git archive applies working-tree line endings from .gitattributes.
            normalized = data.replace(b'\r\n', b'\n')
            normalized_digest = hashlib.sha1(f'blob {len(normalized)}\0'.encode() + normalized).hexdigest()
            if expected.get(relative) != normalized_digest:
                raise RuntimeError(f'Upstream blob verification failed: {relative}')
        if path.exists():
            if path.read_bytes() != data:
                raise RuntimeError(f'Refusing to overwrite modified file: {relative}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        verified.add(relative)
missing = set(expected) - verified
if missing:
    raise RuntimeError(f'Incomplete archive: {sorted(missing)}')
print(f'Verified and extracted {len(verified)} upstream files at {tree["sha"]}')
