from http.server import HTTPServer, BaseHTTPRequestHandler
import sys

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        data = self.rfile.read(length).decode('utf-8', errors='ignore')
        with open(r'C:\SynapseAISentinel\laptop_report.txt', 'w', encoding='utf-8') as f:
            f.write(data)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")
        print("RECEIVED_REPORT")
        sys.exit(0)

server = HTTPServer(('0.0.0.0', 9999), Handler)
print("READY_TO_RECEIVE")
server.handle_request()
