#!/usr/bin/env python3
"""
Custom HTTP Server supporting Range Requests.
Required for serving large WebAssembly Virtual File System (.big) files.
"""

import os
import re
import sys
import http.server

class RangeRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Connection", "close")

        # Enable caching for static VFS archives to prevent duplicate HTTP Range requests
        path = self.translate_path(self.path)
        if path.lower().endswith('.big'):
            self.send_header("Cache-Control", "public, max-age=31536000, immutable")
        else:
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")

        super().end_headers()

    def send_head(self):
        self.range_content_length = None
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            return super().send_head()
            
        ctype = self.guess_type(path)
        try:
            f = open(path, 'rb')
        except OSError:
            self.send_error(http.HTTPStatus.NOT_FOUND, "File not found")
            return None

        # Add WebAssembly headers
        if path.endswith('.wasm'):
            ctype = 'application/wasm'

        range_header = self.headers.get('Range')
        if not range_header:
            return super().send_head()

        # Parse range header (e.g., "bytes=100-200")
        match = re.match(r'bytes=(\d+)-(\d*)', range_header)
        if not match:
            self.send_error(http.HTTPStatus.BAD_REQUEST, "Invalid Range Header")
            f.close()
            return None

        start = int(match.group(1))
        end = match.group(2)
        
        try:
            size = os.path.getsize(path)
        except OSError:
            self.send_error(http.HTTPStatus.NOT_FOUND, "File size lookup failed")
            f.close()
            return None

        if end:
            end = int(end)
        else:
            end = size - 1

        if start >= size:
            self.send_error(http.HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE, "Range not satisfiable")
            f.close()
            return None

        if end >= size:
            end = size - 1

        self.range_content_length = end - start + 1

        self.send_response(http.HTTPStatus.PARTIAL_CONTENT)
        self.send_header('Content-Type', ctype)
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.send_header('Content-Length', str(self.range_content_length))
        self.send_header('Last-Modified', self.date_time_string(os.path.getmtime(path)))
        self.end_headers()
        
        # Seek to starting byte
        f.seek(start)
        return f

    def copyfile(self, source, outputfile):
        # If we have a range content-length, copy only the requested bytes
        if hasattr(self, 'range_content_length') and self.range_content_length is not None:
            remaining = self.range_content_length
            buffer_size = 64 * 1024
            while remaining > 0:
                chunk = source.read(min(remaining, buffer_size))
                if not chunk:
                    break
                outputfile.write(chunk)
                remaining -= len(chunk)
        else:
            super().copyfile(source, outputfile)

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8888
    server_address = ('', port)
    # Enable SO_REUSEADDR
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    httpd = http.server.ThreadingHTTPServer(server_address, RangeRequestHandler)
    print(f"Serving GeneralsX on port {port} with HTTP Range support...")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server.")
        sys.exit(0)
