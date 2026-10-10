import http.server, json, pathlib, subprocess, threading, time, re, argparse
class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_POST(self):
        try:
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            assert body['model'] == 'panel-test-model' and body['stream'] is False
            assert self.headers.get('Authorization') == 'Bearer fake-panel-test-key'
            assert self.headers.get('Cookie') is None
            content = body['messages'][1]['content']
            assert content in ('alpha', '生成中文', 'old\r\nkeep\r\nsecond')
            with self.server.lock: self.server.requests.append(body)
            if 'delay' in body['messages'][0]['content']: time.sleep(2)
            answer = 'new\r\nkeep\r\nthird' if content == 'old\r\nkeep\r\nsecond' else '模拟结果'
            data = json.dumps({'choices': [{'message': {'content': answer}, 'finish_reason': 'stop'}]}).encode()
            self.send_response(200); self.send_header('Content-Length', str(len(data))); self.send_header('Content-Type', 'application/json'); self.end_headers(); self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError): pass
        except Exception as exc:
            with self.server.lock: self.server.errors.append(str(exc))
            try: self.send_response(500); self.end_headers()
            except OSError: pass
parser=argparse.ArgumentParser(); parser.add_argument("--preview",action="store_true"); options=parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
folder = root / 'artifacts' / ('ai-panel-test-' + str(time.time_ns())); folder.mkdir()
server = Server(('127.0.0.1', 0), Handler); server.lock = threading.Lock(); server.requests = []; server.errors = []
thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
try:
    process = subprocess.run(['pwsh','-NoProfile','-File',str(root/'scripts/test-workspace.ps1'),'-Executable','Notepad3/Bin/Release_x64_v143/Notepad3.exe','-AiEndpoint',f'http://127.0.0.1:{server.server_port}/panel'] + (['-Preview'] if options.preview else []), cwd=root, capture_output=True, encoding='utf-8', errors='replace', timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
    (folder/'native-test.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    evidence = {'requests': server.requests, 'errors': server.errors, 'exitCode': process.returncode}
    (folder/'server-verification.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    print(process.stdout); print(process.stderr)
    assert process.returncode == 0, 'native panel checks failed'
    assert not server.errors and len(server.requests) == 6, 'expected four actions, multi-hunk rewrite and canceled request'
    assert server.requests[3]['messages'][1]['content'] == '生成中文'
    assert server.requests[4]['messages'][1]['content'] == 'old\r\nkeep\r\nsecond'
    match = re.search(r'Evidence: (.+workspace-test-[a-f0-9]+)', process.stdout)
    assert match, 'workspace evidence path missing'
    settings = (pathlib.Path(match.group(1).strip()) / 'test.ini').read_text(encoding='utf-8-sig')
    assert 'panel-test-model' in settings and f'127.0.0.1:{server.server_port}/panel' in settings
    assert 'fake-panel-test-key' not in settings, 'credential leaked into INI'
    print('PASS local AI panel requests independently verified. Evidence:', folder)
finally:
    server.shutdown(); server.server_close()
