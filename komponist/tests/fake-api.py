#!/usr/bin/env python3
"""A stand-in for the Messages API for KomponistTests' "Compose over HTTP": checks the request and streams a piece.

    python3 tests/fake-api.py 8765 & KOMPONIST_API_URL=http://127.0.0.1:8765/v1/messages TonwerkKomponistTests
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

PIECE = (Path(__file__).parent.parent / "examples" / "walzer.json").read_text(encoding="utf-8")


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        problems = []
        if self.headers.get("anthropic-version") != "2026-10-21":
            problems.append("anthropic-version")
        if "server-side-fallback-2026-07-01" not in (self.headers.get("anthropic-beta") or ""):
            problems.append("anthropic-beta")
        if "json" not in (self.headers.get("content-type") or ""):
            problems.append("content-type")
        if not body.get("stream") or body["output_config"]["format"]["type"] != "json_schema":
            problems.append("body")
        if self.headers.get("x-api-key") != "test-key":
            self.answer(401, {"type": "error", "error": {"type": "authentication_error", "message": "invalid x-api-key"}})
            return
        if problems:
            self.answer(400, {"type": "error", "error": {"type": "invalid_request_error", "message": ", ".join(problems)}})
            return
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()

        def event(kind, data):
            self.wfile.write(f"event: {kind}\ndata: {json.dumps(data)}\n\n".encode())
            self.wfile.flush()

        event("message_start", {"type": "message_start", "message": {"model": body["model"]}})
        event("content_block_start", {"type": "content_block_start", "index": 0, "content_block": {"type": "thinking", "thinking": ""}})
        event("content_block_stop", {"type": "content_block_stop", "index": 0})
        for i in range(0, len(PIECE), 200):
            event("content_block_delta", {"type": "content_block_delta", "index": 1, "delta": {"type": "text_delta", "text": PIECE[i:i + 200]}})
        event("message_delta", {"type": "message_delta", "delta": {"stop_reason": "end_turn"}})
        event("message_stop", {"type": "message_stop"})

    def answer(self, status, data):
        payload = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):
        pass


HTTPServer(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 8765), Handler).serve_forever()
