import sys
from http.server import HTTPServer, BaseHTTPRequestHandler

def read_file(fname):
  with open(fname, "r") as fh:
    return fh.read()

class SimpleHTTPRequestHandler(BaseHTTPRequestHandler):

    def do_GET(self):
        lib_js_data = read_file(f"{js_dir}/lib.js")
        app_js_data = read_file(f"{js_dir}/app.js")
        self.send_response(200)
        self.end_headers()
        payload = f"""
        <html>
        <head>
          <base href="http://{frf_ip}:{frf_port}/">
        </head>
        <body>
        {lib_js_data}
        {app_js_data}
        </body>
        </html>
        """
        self.wfile.write(payload.encode('utf8'))


frf_ip = sys.argv[1]
frf_port = 8000
js_dir = "c-html/"
lib_js_data = read_file(f"{js_dir}/lib.js")
app_js_data = read_file(f"{js_dir}/app.js")

httpd = HTTPServer(('localhost', 8001), SimpleHTTPRequestHandler)
httpd.serve_forever()
