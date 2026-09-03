#!/usr/bin/env python3

import argparse
import http.server
import subprocess
import threading
from urllib.parse import parse_qs, urlparse

STREAMS = {
    "/streamA": (7, 8, 9, 10, 11, 12),
    "/streamB": (5,),  # Leave the following poll open until client.stop().
    "/streamC": (5, 6, 7, 8, 9, 10),
}
PROBE_INDICES = {("/streamA", 12), ("/streamC", 10)}
STREAM_C_BATCHES = {5: (5, 6), 7: (7, 8, 9), 10: (10,)}

def payload(index):
    return "{}:{}".format(index, "".join(str(i) for i in range(100))).encode()

def batch_payload(path, indices, boundary):
    body = bytearray()
    for index in indices:
        data = payload(index)
        body.extend("--{}\r\n".format(boundary).encode())
        body.extend("x-opencmw-long-polling-idx: {}\r\n".format(index).encode())
        body.extend("x-opencmw-topic: {}?sample={}\r\n".format(path, index).encode())
        body.extend("x-opencmw-service-name: {}-service-{}\r\n".format(path, index).encode())
        body.extend("content-length: {}\r\n\r\n".format(len(data)).encode())
        body.extend(data)
        body.extend(b"\r\n")
    body.extend("--{}--\r\n".format(boundary).encode())
    return bytes(body)

HOLD_SECONDS = 30.0
PROBE_DELAY_SECONDS = 0.5

stopping = threading.Event()
stream_c_recovery = threading.Event()

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_OPTIONS(self):
        self.send_response(204)
        self._common_headers()
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "accept, content-type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        indices = STREAMS.get(parsed.path)
        if indices is None:
            self._respond(404, b"unknown stream")
            return

        params = parse_qs(parsed.query)
        index = params.get("LongPollingIdx", [""])[0]
        batch = params.get("LongPollingBatch", [None])[0]
        if index == "Next":
            if parsed.path == "/streamC":
                # Recovery must request Next; starting with Next would miss messages 5 and 6.
                stream_c_recovery.set()
                self._redirect(parsed.path, 7, batch)
            else:
                self._redirect(parsed.path, min(indices), batch)
            return
        if not index.isdigit():
            self._respond(400, b"malformed LongPollingIdx")
            return

        numeric_index = int(index)
        if parsed.path == "/streamC" and batch == "AllAvailable":
            # Request 3 starts at the oldest buffered message, 5.
            numeric_index = max(numeric_index, min(indices))
        if numeric_index not in indices:
            stopping.wait(HOLD_SECONDS)
            self._respond(504, b"")
            return
        if (parsed.path, numeric_index) in PROBE_INDICES:
            stopping.wait(PROBE_DELAY_SECONDS)

        if batch is not None:
            selected = STREAM_C_BATCHES.get(numeric_index) if parsed.path == "/streamC" and batch == "AllAvailable" else None
            if selected is None:
                self._respond(400, b"unexpected batch request")
                return
            if parsed.path == "/streamC" and numeric_index == 7 and not stream_c_recovery.is_set():
                self._respond(200, b"not a multipart response", "multipart/mixed")
                return
            boundary = "opencmw-long-polling-multipart-boundary-{}".format(numeric_index)
            self._respond(200, batch_payload(parsed.path, selected, boundary), "multipart/mixed; boundary={}".format(boundary))
            return

        self._respond(200, payload(numeric_index))

    def _common_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")

    def _redirect(self, path, index, batch):
        # Absolute, because xhr2 does not resolve a relative Location against the request URL.
        location = "http://{}{}?LongPollingIdx={}".format(self.headers["Host"], path, index)
        if batch is not None:
            location += "&LongPollingBatch={}".format(batch)
        try:
            self.send_response(302)
            self._common_headers()
            self.send_header("Location", location)
            self.send_header("Content-Length", "0")
            self.end_headers()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _respond(self, code, body, content_type="application/octet-stream"):
        try:
            self.send_response(code)
            self._common_headers()
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Expected when the client aborts the long poll.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("node", "browser"), required=True)
    parser.add_argument("--binary", required=True, help="generated .js (Node) or .html (browser) test program")
    parser.add_argument("--node", help="node executable (node mode)")
    parser.add_argument("--browser", help="browser executable (browser mode)")
    parser.add_argument("--browser-family", choices=("chromium", "firefox"), help="browser family (browser mode)")
    parser.add_argument("--emrun", help="emrun executable (browser mode)")
    args = parser.parse_args()
    if args.mode == "node" and not args.node:
        parser.error("--node is required in node mode")
    if args.mode == "browser" and not all((args.browser, args.browser_family, args.emrun)):
        parser.error("--browser, --browser-family, and --emrun are required in browser mode")

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()

    if args.mode == "node":
        command = [args.node, args.binary, "--port={}".format(port)]
    else:
        if args.browser_family == "chromium":
            browser_args = "--headless=new --no-sandbox --disable-gpu --disable-dev-shm-usage"
        else:
            browser_args = "--headless"
        command = [
            args.emrun,
            "--browser", args.browser,
            "--browser-args={}".format(browser_args),
            "--port", "0",
            "--kill-exit",
            "--silence-timeout", "60",
        ]
        if args.browser_family == "firefox":
            command.append("--safe-firefox-profile")
        command.extend([args.binary, "--", "--port={}".format(port)])

    try:
        return subprocess.call(command)
    finally:
        stopping.set()
        server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
