"""Fetch pinned browser assets; verify npm integrity before writing selected regular files."""
import base64, hashlib, io, json, pathlib, tarfile, urllib.request
PACKAGES = {
    'mermaid': ('12.1.0', 'sha512-wlVCp+8eTupfCeeFvoZNNiTuHrvag0P2jz/ILgb/f/6jkVokUefOcujefi8qUe/j2asHiSePncVsz/xzzA80LQ=='),
    'katex': ('0.19.0', 'sha512-v6Tznz3zJ7u3niRCoDTsumM2+HA2XXcCu+WAacCeHD2z3p9A9Ks987o5FzfTGBN0e8A0vjEgIDvLbICrXpdw/Q=='),
}
root = pathlib.Path(__file__).resolve().parents[1] / 'Notepad3/src/Markdown/vendor/render-assets'
root.mkdir(parents=True, exist_ok=True)
manifest = {'packages': {}, 'files': {}}
for name, (version, integrity) in PACKAGES.items():
    url = f'https://registry.npmjs.org/{name}/-/{name}-{version}.tgz'
    data = urllib.request.urlopen(url, timeout=60).read()
    actual = 'sha512-' + base64.b64encode(hashlib.sha512(data).digest()).decode()
    if actual != integrity: raise RuntimeError(f'{name} archive integrity mismatch')
    archive = tarfile.open(fileobj=io.BytesIO(data), mode='r:gz')
    selected = ['package/LICENSE', 'package/dist/mermaid.min.js'] if name == 'mermaid' else ['package/LICENSE', 'package/dist/katex.min.js', 'package/dist/katex.min.css']
    if name == 'katex': selected += [m.name for m in archive if m.name.startswith('package/dist/fonts/') and m.name.endswith('.woff2')]
    for filename in selected:
        member = archive.getmember(filename)
        if not member.isfile(): raise RuntimeError('Expected regular asset')
        relative = pathlib.PurePosixPath(filename).relative_to('package')
        if filename == 'package/LICENSE': relative = pathlib.PurePosixPath('LICENSE-' + name + '.txt')
        else: relative = relative.relative_to('dist')
        if '..' in relative.parts: raise RuntimeError('Unsafe member')
        destination = root.joinpath(*relative.parts)
        if not destination.resolve().is_relative_to(root.resolve()): raise RuntimeError('Unsafe output')
        payload = archive.extractfile(member).read()
        destination.parent.mkdir(parents=True, exist_ok=True); destination.write_bytes(payload)
        manifest['files'][str(relative)] = {'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()}
    manifest['packages'][name] = {'version':version,'url':url,'integrity':integrity}
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n', encoding='utf-8')
print('Verified',len(manifest['files']),'offline assets, bytes:',sum(v['bytes'] for v in manifest['files'].values()))
