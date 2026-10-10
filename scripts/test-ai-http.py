import http.server, json, pathlib, subprocess, sys, threading, time, ssl, datetime, ipaddress
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_POST(self):
        try:
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            assert body['model'] == 'test-model' and body['stream'] is False
            assert body['messages'][1] == {'role': 'user', 'content': '中文 input'}
            assert self.headers.get('Authorization') == 'Bearer fake-local-test-key'
            assert self.headers.get('Cookie') is None
            with self.server.task_lock: self.server.task_requests.append(self.path)
            if self.path == '/slow': time.sleep(3)
            if self.path == '/denied':
                self.send_response(401); self.end_headers(); return
            if self.path == '/redirect':
                self.send_response(302); self.send_header('Location', '/redirect-target'); self.end_headers(); return
            if self.path == '/redirect-target': raise AssertionError('redirect followed')
            if self.path == '/broken': data = b'{broken'
            elif self.path == '/oversize': data = b'x' * (2 * 1024 * 1024 + 1)
            else: data = json.dumps({'choices': [{'message': {'content': '模拟中文\n😀'}, 'finish_reason': 'length' if self.path == '/incomplete' else 'stop'}]}).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(data))); self.end_headers()
            if self.path == '/drip':
                for byte in data: self.wfile.write(bytes([byte])); self.wfile.flush(); time.sleep(.025)
            else: self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError): pass
        except Exception as exc:
            with self.server.task_lock: self.server.task_errors.append(str(exc))
            try: self.send_response(500); self.end_headers()
            except OSError: pass
root = pathlib.Path(__file__).resolve().parents[1]
folder = root / 'artifacts' / ('ai-http-test-' + str(time.time_ns()))
folder.mkdir()
server = Server(('127.0.0.1', 0), Handler)
server.task_lock = threading.Lock(); server.task_requests = []; server.task_errors = []
thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Notepad3 isolated test')])
now = datetime.datetime.now(datetime.timezone.utc)
cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=1)).not_valid_after(now + datetime.timedelta(days=1))
        .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), critical=False).sign(key, hashes.SHA256()))
(folder / 'test-cert.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM))
(folder / 'test-key.pem').write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
tls = Server(('127.0.0.1', 0), Handler); tls.task_lock = threading.Lock(); tls.task_requests = []; tls.task_errors = []
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(folder / 'test-cert.pem', folder / 'test-key.pem')
tls.socket = context.wrap_socket(tls.socket, server_side=True)
threading.Thread(target=tls.serve_forever, daemon=True).start()
try:
    result = subprocess.run([str(root / 'artifacts' / 'ai_http_test.exe'), f'http://127.0.0.1:{server.server_port}', f'https://127.0.0.1:{tls.server_port}'], capture_output=True, timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    text = (result.stdout + result.stderr).decode('utf-8', errors='replace')
    (folder / 'native-test.log').write_text(text, encoding='utf-8')
    print(text, end='')
    with server.task_lock:
        evidence = {'exit_code': result.returncode, 'requests': server.task_requests.copy(), 'errors': server.task_errors.copy(), 'tls_requests': tls.task_requests.copy(), 'tls_errors': tls.task_errors.copy()}
    (folder / 'server-verification.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('Evidence:', folder)
    if result.returncode or evidence['errors'] or evidence['tls_requests'] or evidence['tls_errors'] or '/redirect-target' in evidence['requests']: sys.exit(1)
finally: server.shutdown(); server.server_close(); tls.shutdown(); tls.server_close()
