"""Isolated Playwright validation: Browser plugin not available."""
import functools, hashlib, html, http.server, json, pathlib, threading, time
from playwright.sync_api import sync_playwright
root=pathlib.Path(__file__).resolve().parents[1]
assets=root/'Notepad3/src/Markdown/vendor/render-assets'
folder=root/'artifacts'/('markdown-assets-test-'+str(time.time_ns()));folder.mkdir()
manifest=json.loads((assets/'manifest.json').read_text())
for relative,entry in manifest['files'].items():
    data=(assets/relative).read_bytes()
    assert len(data)==entry['bytes'] and hashlib.sha256(data).hexdigest()==entry['sha256']
fixture='''<!doctype html><html><head><meta charset="utf-8"><title>Notepad3 offline rendering fixture</title><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'self'; style-src 'self' 'unsafe-inline'; font-src 'self'; img-src data:; connect-src 'none'; object-src 'none'"><link rel="stylesheet" href="/katex.min.css"><style>body{font:16px system-ui;padding:20px;max-width:900px;margin:auto}.np3-diagram svg{max-width:100%}.np3-render-error{color:#a22}pre{background:#eee;padding:10px}</style><script defer src="/katex.min.js"></script><script defer src="/mermaid.min.js"></script><script defer src="/np3-render.js"></script></head><body><h1>离线图表与公式验证</h1>'''
diagrams=['flowchart LR\n A[开始] --> B[结束]','sequenceDiagram\n Alice->>Bob: Hello','this is invalid diagram syntax','%%{init: {"securityLevel":"loose"}}%%\nflowchart LR\n A["<img src=x onerror=window.attack=1>"] --> B[Safe]\n click B "javascript:window.attack=2"']
for source in diagrams:fixture+='<pre><code class="language-mermaid">'+html.escape(source)+'</code></pre>'
fixture+='<p>行内公式：<x-equation>E=mc^2</x-equation></p><x-equation type="display">\\frac{1}{2} + \\sum_{i=1}^{n} i</x-equation><x-equation>\\notacommand</x-equation></body></html>'
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*_):pass
    def do_GET(self):
        if self.path=='/fixture':
            data=fixture.encode();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        else:super().do_GET()
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(assets)))
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    with sync_playwright() as playwright:
        browser=playwright.chromium.launch(channel='msedge',headless=True)
        page=browser.new_page(viewport={'width':1100,'height':1000})
        errors=[]; logs=[];page.on('pageerror',lambda error:errors.append(str(error)))
        page.on('console',lambda message:logs.append({'level':message.type,'text':message.text}) if message.type in ('error','warning') else None)
        page.goto(f'http://127.0.0.1:{server.server_port}/fixture')
        page.wait_for_function('() => window.np3RenderState && window.np3RenderState.done',timeout=30000)
        state=page.evaluate('() => window.np3RenderState')
        assert state['diagrams']==3 and state['formulas']==2 and state['errors']==2,state
        assert page.locator('.np3-diagram svg').count()==3
        assert page.locator('.katex').count()==2
        assert page.locator('.np3-render-error').count()==2
        assert page.locator('x-equation').last.text_content() == '\\notacommand'
        assert page.evaluate('() => window.attack || null') is None
        assert page.locator('.np3-diagram script, .np3-diagram foreignObject, .np3-diagram image, .np3-diagram [onclick]').count()==0
        assert not errors,errors
        page.screenshot(path=str(folder/'render.png'),full_page=True)
        page.set_viewport_size({'width':420,'height':900})
        assert page.locator('.np3-diagram svg').count()==3
        page.screenshot(path=str(folder/'render-narrow.png'),full_page=True)
        evidence={'state':state,'pageErrors':errors,'console':logs,'title':page.title(),'fallbackReason':'Browser plugin not available','assetFilesVerified':len(manifest['files'])}
        (folder/'verification.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
        browser.close();print('PASS offline Mermaid, KaTeX, malformed input and unsafe configuration checks. Evidence:',folder)
finally:server.shutdown();server.server_close()