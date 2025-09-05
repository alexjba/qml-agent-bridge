#!/usr/bin/env python3
import argparse
import json
import sys
import threading
import time
from typing import Any, Dict, Optional

try:
    import websocket  # type: ignore
except ImportError:
    print("Missing dependency websocket-client. Install with: pip install -r tools/python/requirements.txt", file=sys.stderr)
    sys.exit(1)


def send_request(ws: "websocket.WebSocket", method: str, params: Optional[Dict[str, Any]] = None, id_value: Optional[str] = None) -> Dict[str, Any]:
    if id_value is None:
        id_value = str(int(time.time() * 1000))
    payload = {"id": id_value, "method": method}
    if params is not None:
        payload["params"] = params
    ws.send(json.dumps(payload))
    # Wait for matching id response
    while True:
        msg = ws.recv()
        if not msg:
            continue
        data = json.loads(msg)
        if isinstance(data, dict) and data.get("id") == id_value:
            return data
        # Print async events
        if isinstance(data, dict) and data.get("method") == "event":
            print(json.dumps(data), flush=True)


def watch_events(ws: "websocket.WebSocket") -> None:
    try:
        while True:
            msg = ws.recv()
            if not msg:
                continue
            try:
                data = json.loads(msg)
            except Exception:
                print(msg)
                continue
            if isinstance(data, dict) and data.get("method") == "event":
                print(json.dumps(data), flush=True)
            else:
                # Non-event messages are ignored in watcher mode
                pass
    except Exception:
        return


def main() -> int:
    parser = argparse.ArgumentParser(description="qab.py - minimal Python client for qml-agent-bridge")
    parser.add_argument("--url", default="ws://127.0.0.1:7777", help="WebSocket URL")
    parser.add_argument("--method", required=True, help="Method to call (hello, list_roots, find_by_name, inspect, list_children, set_property, call_method, evaluate, subscribe_signal, subscribe_property, unsubscribe)")
    parser.add_argument("--params", default="{}", help="JSON-encoded params object")
    parser.add_argument("--watch", action="store_true", help="Keep connection open and print incoming events")
    args = parser.parse_args()

    try:
        params: Dict[str, Any] = json.loads(args.params)
    except Exception as e:
        print(f"Invalid --params JSON: {e}", file=sys.stderr)
        return 2

    ws = websocket.create_connection(args.url)
    try:
        if args.watch:
            # Start watcher thread
            t = threading.Thread(target=watch_events, args=(ws,), daemon=True)
            t.start()

        resp = send_request(ws, args.method, params)
        print(json.dumps(resp))

        if args.watch:
            # Keep process alive
            while True:
                time.sleep(1)
    finally:
        if not args.watch:
            ws.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


