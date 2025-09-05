#!/usr/bin/env python3
import json
import sys
import time

import websocket  # type: ignore


def send(ws, method, params=None, id_value=None):
    if id_value is None:
        id_value = str(int(time.time() * 1000))
    payload = {"id": id_value, "method": method}
    if params is not None:
        payload["params"] = params
    ws.send(json.dumps(payload))
    return id_value


def recv_until_id(ws, id_value, timeout_s=5.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            data = json.loads(ws.recv())
        except Exception:
            time.sleep(0.05)
            continue
        if data.get("id") == id_value:
            return data
    raise TimeoutError(f"Timed out waiting for id={id_value}")


def recv_event(ws, timeout_s=5.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            data = json.loads(ws.recv())
        except Exception:
            time.sleep(0.05)
            continue
        if data.get("method") == "event":
            return data
    raise TimeoutError("Timed out waiting for event")


def main():
    url = "ws://127.0.0.1:7777"
    ws = websocket.create_connection(url, timeout=2.0)
    try:
        # Find toggleBox
        rid = send(ws, "find_by_name", {"name": "toggleBox"}, id_value="1")
        resp = recv_until_id(ws, rid)
        matches = resp.get("result", {}).get("matches", [])
        if not matches:
            print("No toggleBox found", file=sys.stderr)
            return 2
        oid = matches[0]["objectId"]

        # Subscribe to 'checked'
        rid = send(ws, "subscribe_property", {"objectId": oid, "name": "checked"}, id_value="2")
        _ = recv_until_id(ws, rid)

        # Toggle property to trigger event
        rid = send(ws, "set_property", {"objectId": oid, "name": "checked", "value": False}, id_value="3")
        _ = recv_until_id(ws, rid)

        evt = recv_event(ws, timeout_s=5.0)
        print(json.dumps(evt))
        return 0
    finally:
        try:
            ws.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())


