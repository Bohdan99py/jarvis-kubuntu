#!/usr/bin/env python3
"""A stand-in for llama-server in the agent tests (0.22.1).

It answers /health and /v1/chat/completions on 127.0.0.1 like llama.cpp does.
The plan it returns comes from JARVIS_FAKE_PLANS, a JSON file mapping a piece
of the user's request to the model's answer ({"steps", "clarify", "say"}); the
key "*" is the answer for anything else. It reproduces what a small model
really did in 0.22: an empty plan that says "I can't change the volume", or
steps planned for a question.
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

if "--list-devices" in sys.argv:
    print("Available devices:")
    sys.exit(0)

port = int(sys.argv[sys.argv.index("--port") + 1])
plans = json.load(open(os.environ["JARVIS_FAKE_PLANS"], encoding="utf-8"))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, body):
        data = json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.reply({"status": "ok"})

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        user = request["messages"][-1]["content"]
        asked = user.split("Просьба пользователя:")[-1]
        plan = plans.get("*", {"steps": [], "clarify": "", "say": ""})
        for key, value in plans.items():
            if key != "*" and key in asked:
                plan = value
        self.reply({"choices": [{"message": {"role": "assistant", "content": json.dumps(plan, ensure_ascii=False)}}]})


HTTPServer(("127.0.0.1", port), Handler).serve_forever()
