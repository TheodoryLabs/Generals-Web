#!/usr/bin/env python3
import http.server
import webbrowser
import os
import sys
import urllib.parse
from socketserver import ThreadingMixIn

os.chdir(os.path.dirname(os.path.abspath(__file__)))

class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cross-Origin-Opener-Policy",  "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy","require-corp")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200, "ok")
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header("Access-Control-Allow-Headers", "Range")
        self.end_headers()

    def guess_type(self, path):
        if path.endswith('.wasm'):
            return 'application/wasm'
        return super().guess_type(path)

    def log_message(self, fmt, *args):
        print(f"  {self.address_string()}  {fmt % args}", flush=True)

    def send_head(self):
        path = self.translate_path(self.path)
        f = None
        if os.path.isdir(path):
            parts = urllib.parse.urlsplit(self.path)
            if not parts.path.endswith('/'):
                self.send_response(301)
                new_parts = (parts[0], parts[1], parts[2] + '/',
                             parts[3], parts[4])
                new_url = urllib.parse.urlunsplit(new_parts)
                self.send_header("Location", new_url)
                self.end_headers()
                return None
            for index in "index.html", "index.htm":
                index = os.path.join(path, index)
                if os.path.exists(index):
                    path = index
                    break
            else:
                return self.list_directory(path)
        ctype = self.guess_type(path)
        try:
            f = open(path, 'rb')
        except OSError:
            self.send_error(404, "File not found")
            return None

        fs = os.fstat(f.fileno())
        size = fs.st_size

        if "Range" in self.headers:
            range_match = self.headers["Range"].replace("bytes=", "").split("-")
            start = int(range_match[0]) if range_match[0] else 0
            end = int(range_match[1]) if len(range_match) > 1 and range_match[1] else size - 1

            if start >= size or end >= size or start > end:
                self.send_error(416, "Requested Range Not Satisfiable")
                return None

            length = end - start + 1
            self.send_response(206, "Partial Content")
            self.send_header("Content-type", ctype)
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.send_header("Content-Length", str(length))
            self.end_headers()

            f.seek(start)
            # Create a wrapper so copyfile only reads `length` bytes
            class RangeFile:
                def __init__(self, f, length):
                    self.f = f
                    self.length = length
                def read(self, amt=None):
                    if self.length <= 0: return b""
                    if amt is None or amt > self.length: amt = self.length
                    data = self.f.read(amt)
                    self.length -= len(data)
                    return data
                def close(self):
                    self.f.close()
            return RangeFile(f, length)

        self.send_response(200)
        self.send_header("Content-type", ctype)
        self.send_header("Content-Length", str(size))
        self.end_headers()
        return f

class ThreadedHTTPServer(ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True

PORT = 8888
print(f"Starting THREADED RANGE server on http://localhost:{PORT}")
print("Press Ctrl+C to stop.\n")
ThreadedHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
