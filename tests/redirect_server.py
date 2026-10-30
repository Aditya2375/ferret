import http.server, threading
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path == '/start':
            self.send_response(302); self.send_header('Location', 'rel/page'); self.end_headers()
        elif self.path == '/rel/page':
            self.send_response(302); self.send_header('Location', '/abs'); self.end_headers()
        elif self.path == '/abs':
            self.send_response(301); self.send_header('Location', 'http://127.0.0.1:8799/final'); self.end_headers()
        elif self.path == '/final':
            self.send_response(200); self.send_header('Content-Type','text/html'); self.end_headers()
            self.wfile.write(b'<html><body><h1>made it</h1></body></html>')
        elif self.path == '/loop1':
            self.send_response(302); self.send_header('Location', 'http://127.0.0.1:8799/loop2'); self.end_headers()
        elif self.path == '/loop2':
            self.send_response(302); self.send_header('Location', '/loop1'); self.end_headers()
        else:
            self.send_response(404); self.end_headers()
srv = http.server.HTTPServer(('127.0.0.1', 8799), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
import time
while True: time.sleep(1)
