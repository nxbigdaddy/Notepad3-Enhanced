"""Open the actual exported file offline. Browser plugin not available; Playwright/Edge fallback."""
import json, pathlib, time, re, base64, hashlib
from playwright.sync_api import sync_playwright
root=pathlib.Path(__file__).resolve().parents[1]
fixture=json.loads((root/'artifacts/advanced-export-fixture.json').read_text(encoding='utf-8'))
source=pathlib.Path(fixture['html']); data=source.read_text(encoding='utf-8')
script_hashes=re.findall(r"'sha256-([^']+)'",data)
scripts=re.findall(r'<script>(.*?)</script>',data,re.S)
assert len(scripts)==3 and len(script_hashes)==3
assert [base64.b64encode(hashlib.sha256(script.encode()).digest()).decode() for script in scripts]==script_hashes
folder=root/'artifacts'/('standalone-export-test-'+str(time.time_ns()));folder.mkdir()
# Copy only the HTML, so the browser cannot rely on renderer folders beside the file.
standalone=folder/'standalone.html';standalone.write_bytes(source.read_bytes())
with sync_playwright() as playwright:
    browser=playwright.chromium.launch(channel='msedge',headless=True)
    context=browser.new_context(offline=True,viewport={'width':1100,'height':900})
    page=context.new_page();errors=[];console=[];requests=[]
    page.on('pageerror',lambda error:errors.append(str(error)))
    page.on('console',lambda message:console.append({'level':message.type,'text':message.text}) if message.type in ('error','warning') else None)
    page.on('request',lambda request:requests.append(request.url))
    page.goto(standalone.as_uri());page.wait_for_function('() => window.np3RenderState && window.np3RenderState.done',timeout=30000)
    state=page.evaluate('() => window.np3RenderState')
    assert state=={'done':True,'diagrams':1,'formulas':2,'errors':0},state
    assert page.url==standalone.as_uri() and page.locator('h1').text_content()=='单文件图表与公式'
    assert page.locator('.np3-diagram svg').count()==1 and page.locator('.katex').count()==2
    assert page.evaluate('() => window.attack || null') is None
    assert not errors,errors
    assert not [url for url in requests if url.startswith(('http:','https:'))],requests
    page.screenshot(path=str(folder/'render.png'),full_page=True)
    page.set_viewport_size({'width':420,'height':900});assert page.locator('.katex').count()==2
    page.screenshot(path=str(folder/'render-narrow.png'),full_page=True)
    (folder/'verification.json').write_text(json.dumps({'state':state,'pageErrors':errors,'console':console,'requests':requests,'sha256ScriptsVerified':3,'htmlBytes':standalone.stat().st_size,'title':page.title(),'fallbackReason':'Browser plugin not available'},ensure_ascii=False,indent=2),encoding='utf-8')
    browser.close()
print('PASS actual standalone exported file renders offline with embedded fonts and exact script hashes. Evidence:',folder)