#!/usr/bin/env python3

import argparse
import http.server
import subprocess
import threading
from urllib.parse import parse_qs, urlparse

# The delayed sixth stream-A response makes a failed unsubscribe observable.
STREAMS = {
    "/streamA": (7, 8, 9, 10, 11, 12),
    "/streamB": (5,),
}
PROBE_INDEX = ("/streamA", 12)

def payload(index):
    return "{}:{}".format(index, "".join(str(i) for i in range(100))).encode()

HOLD_SECONDS = 30.0
PROBE_DELAY_SECONDS = 0.5

stopping = threading.Event()

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

        index = parse_qs(parsed.query).get("LongPollingIdx", [""])[0]
        if index == "Next":
            self._redirect(parsed.path, min(indices))
            return
        if not index.isdigit():
            self._respond(400, b"malformed LongPollingIdx")
            return

        if int(index) not in indices:
            stopping.wait(HOLD_SECONDS)
            self._respond(504, b"")
            return
        if (parsed.path, int(index)) == PROBE_INDEX:
            stopping.wait(PROBE_DELAY_SECONDS)
        self._respond(200, payload(int(index)))

    def _common_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")

    def _redirect(self, path, index):
        # Absolute, because xhr2 does not resolve a relative Location against the request URL.
        location = "http://{}{}?LongPollingIdx={}".format(self.headers["Host"], path, index)
        try:
            self.send_response(302)
            self._common_headers()
            self.send_header("Location", location)
            self.send_header("Content-Length", "0")
            self.end_headers()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _respond(self, code, body):
        try:
            self.send_response(code)
            self._common_headers()
            self.send_header("Content-Type", "application/octet-stream")
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
