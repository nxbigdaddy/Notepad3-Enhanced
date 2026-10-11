import http.server, json, pathlib, subprocess, threading, time, re, argparse
class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_POST(self):
        try:
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            assert body['model'] == 'panel-test-model' and body['stream'] is False
            authorization = self.headers.get('Authorization')
            assert authorization in ('Bearer fake-panel-test-key', 'Bearer fake-panel-test-key-a', 'Bearer fake-panel-test-key-b')
            assert self.headers.get('Cookie') is None
            content = body['messages'][1]['content']
            assert content in ('alpha', '生成中文', 'old\r\nkeep\r\nsecond', 'old\r\n' * 1200, 'Question:\n文档有什么内容？\n\nCurrent document:\nalpha\r\nA')
            with self.server.lock:
                self.server.requests.append(body)
                self.server.authorizations.append(authorization)
            if 'delay' in body['messages'][0]['content']: time.sleep(2)
            answer = 'new\r\nkeep\r\nthird' if content == 'old\r\nkeep\r\nsecond' else '模拟结果'
            if content == 'old\r\n' * 1200: answer = 'new\r\n' * 120000
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
server = Server(('127.0.0.1', 0), Handler); server.lock = threading.Lock(); server.requests = []; server.errors = []; server.authorizations = []
thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
try:
    process = subprocess.run(['pwsh','-NoProfile','-File',str(root/'scripts/test-workspace.ps1'),'-Executable','Notepad3/Bin/Release_x64_v143/Notepad3.exe','-AiEndpoint',f'http://127.0.0.1:{server.server_port}/panel'] + (['-Preview'] if options.preview else []), cwd=root, capture_output=True, encoding='utf-8', errors='replace', timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
    (folder/'native-test.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    evidence = {'requests': server.requests, 'errors': server.errors, 'exitCode': process.returncode, 'testAuthorizations': server.authorizations}
    (folder/'server-verification.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    print(process.stdout); print(process.stderr)
    assert process.returncode == 0, 'native panel checks failed'
    assert not server.errors and len(server.requests) == 16, 'expected nine actions, multi-hunk and grouped rewrites and cancel'
    assert server.authorizations[:13] == ['Bearer fake-panel-test-key'] * 13
    assert server.authorizations[13:] == ['Bearer fake-panel-test-key-a', 'Bearer fake-panel-test-key-b', 'Bearer fake-panel-test-key-a']
    assert server.requests[3]['messages'][1]['content'] == '生成中文'
    for index, prompt in enumerate(('Summarize', 'Polish', 'Expand', 'Shorten', 'Correct'), 4):
        assert prompt in server.requests[index]['messages'][0]['content']
        assert server.requests[index]['messages'][1]['content'] == 'alpha'
    assert server.requests[9]['messages'][1]['content'] == 'Question:\n文档有什么内容？\n\nCurrent document:\nalpha\r\nA'
    assert '文档有什么内容？' not in server.requests[9]['messages'][0]['content']
    assert server.requests[10]['messages'][1]['content'] == 'old\r\nkeep\r\nsecond'
    assert server.requests[11]['messages'][1]['content'] == 'old\r\n' * 1200
    match = re.search(r'Evidence: (.+workspace-test-[a-f0-9]+)', process.stdout)
    assert match, 'workspace evidence path missing'
    settings = (pathlib.Path(match.group(1).strip()) / 'test.ini').read_text(encoding='utf-8-sig')
    assert 'panel-test-model' in settings and f'127.0.0.1:{server.server_port}/panel' in settings
    assert '[AIProfile00]' in settings and '[AIProfile01]' in settings
    assert 'TimeoutMs=31000' in settings and 'TimeoutMs=32000' in settings
    assert 'unsaved-key-must-not-transfer' not in settings
    assert 'fake-panel-test-key' not in settings, 'credential leaked into INI'
    print('PASS local AI panel requests independently verified. Evidence:', folder)
finally:
    server.shutdown(); server.server_close()
