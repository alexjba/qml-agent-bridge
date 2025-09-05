#!/usr/bin/env python3
import json
import os
import signal
import subprocess
import sys
import time
from typing import Any

from qab_sdk import QmlAgentBridgeClient


def wait_for_port(url: str, timeout_s: float = 5.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with QmlAgentBridgeClient(url) as c:
                c.hello()
                return
        except Exception:
            time.sleep(0.1)
    raise TimeoutError("Bridge did not respond in time")


def assert_true(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    build_dir = os.path.join(root, "build")
    app_path = os.path.join(build_dir, "examples", "minimal-cpp", "example-minimal")
    if not os.path.exists(app_path):
        print("Example app not found; build the project first.", file=sys.stderr)
        return 2

    app = subprocess.Popen([app_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        wait_for_port("ws://127.0.0.1:7777", timeout_s=5.0)

        with QmlAgentBridgeClient() as client:
            hello = client.hello()
            assert_true(hello.get("protocol") == "qml-agent-bridge", "hello protocol mismatch")

            roots = client.list_roots()
            assert_true(len(roots) >= 1, "no roots")

            btn = client.first_by_name("helloButton")
            assert_true(btn is not None, "helloButton not found")

            # Inspect
            info = client.inspect(btn["objectId"])
            assert_true(info.get("objectName") == "helloButton", "inspect mismatch")

            # Call method with args (TextField.select)
            tf = client.first_by_name("nameField")
            assert_true(tf is not None, "nameField not found")
            result: Any = client.call_method(tf["objectId"], "select", 0, 0)
            # no strong assertion on result value; ensure call returned without exception

            # Property change + event value
            toggle = client.first_by_name("toggleBox")
            assert_true(toggle is not None, "toggleBox not found")

            # Don't subscribe via SDK here to avoid cross-connection event noise

            # Use low-level ws to read an event quickly without consuming it inadvertently
            import websocket  # type: ignore

            def ws_send(ws, ident: str, method: str, params: dict | None = None) -> None:
                payload = {"id": ident, "method": method}
                if params is not None:
                    payload["params"] = params
                ws.send(json.dumps(payload))

            def ws_recv_until_id(ws, ident: str, timeout_s: float = 5.0) -> dict:
                deadline = time.time() + timeout_s
                while time.time() < deadline:
                    try:
                        msg = ws.recv()
                    except Exception:
                        continue
                    data = json.loads(msg)
                    if data.get("id") == ident:
                        return data
                raise TimeoutError(f"no response for id={ident}")

            ws = websocket.create_connection("ws://127.0.0.1:7777", timeout=2.0)
            try:
                ws_send(ws, "h1", "hello")
                _ = ws_recv_until_id(ws, "h1")
                # Ensure a change will occur: set to false first (no subscription yet)
                ws_send(ws, "prep", "set_property", {"objectId": toggle["objectId"], "name": "checked", "value": False})
                _ = ws_recv_until_id(ws, "prep")
                # subscribe for property events
                ws_send(ws, "s1", "subscribe_property", {"objectId": toggle["objectId"], "name": "checked"})
                sub_resp = ws_recv_until_id(ws, "s1")
                sid = sub_resp.get("result", {}).get("subscriptionId")
                assert_true(bool(sid), "subscribe_property sid missing")
                # trigger event by toggling to true
                ws_send(ws, "p1", "set_property", {"objectId": toggle["objectId"], "name": "checked", "value": True})
                # Wait for an event with value; accept by subscriptionId OR matching objectId/name.
                # Note: do not drain the event by waiting for the p1 ack first.
                deadline = time.time() + 5.0
                got_value = False
                while time.time() < deadline:
                    try:
                        ws.settimeout(0.5)
                        msg = ws.recv()
                    except Exception:
                        continue
                    data = json.loads(msg)
                    if data.get("method") == "event":
                        params = data.get("params", {})
                        if params.get("subscriptionId") == sid or (
                            params.get("objectId") == toggle["objectId"] and params.get("name") == "checked"
                        ):
                            val = params.get("value", None)
                            if val is not None:
                                got_value = True
                                break
                    # else ignore non-event frames or unrelated events
                assert_true(got_value, "did not receive property value in event")

                # Verify customEmitter signals and event via button click
                emitter = client.first_by_name("customEmitter")
                assert_true(emitter is not None, "customEmitter not found")
                em_info = client.inspect(emitter["objectId"])
                sigs = em_info.get("signals", [])
                assert_true("ping()" in sigs and any(s.startswith("message(") for s in sigs), "customEmitter signals missing")
                # Subscribe to ping()
                ws_send(ws, "s_custom", "subscribe_signal", {"objectId": emitter["objectId"], "signal": "ping()"})
                _ = ws_recv_until_id(ws, "s_custom")
                # Trigger via button click: call onClicked handler via method name 'clicked' or expression fallback
                ws_send(ws, "click1", "call_method", {"objectId": btn["objectId"], "name": "clicked", "args": []})
                deadline3 = time.time() + 5.0
                got_ping = False
                while time.time() < deadline3:
                    try:
                        ws.settimeout(0.5)
                        m3 = ws.recv()
                    except Exception:
                        continue
                    d3 = json.loads(m3)
                    if d3.get("method") == "event":
                        p3 = d3.get("params", {})
                        if p3.get("kind") == "signal" and p3.get("name").startswith("ping"):
                            got_ping = True
                            break
                assert_true(got_ping, "did not receive customEmitter ping event")

                # Model info and fetch snapshot
                fm = client.first_by_name("fruitsModel")
                assert_true(fm is not None, "fruitsModel not found")
                ws_send(ws, "mi", "model_info", {"objectId": fm["objectId"]})
                mi = ws_recv_until_id(ws, "mi")
                roles = mi.get("result", {}).get("roles", [])
                assert_true("name" in roles and "color" in roles, "model roles missing")
                ws_send(ws, "mf", "model_fetch", {"objectId": fm["objectId"], "start": 0, "count": 3, "roles": ["name", "color"]})
                mf = ws_recv_until_id(ws, "mf")
                items = mf.get("result", {}).get("items", [])
                assert_true(len(items) == 3 and items[0]["name"] == "Apple" and items[1]["color"] == "yellow", "model snapshot unexpected")

                # Test signal snapshot on CheckBox checkedChanged(bool)
                cb = client.first_by_name("toggleBox")
                assert_true(cb is not None, "toggleBox not found")
                cb_oid = cb["objectId"]
                ws_send(ws, "s2", "subscribe_signal", {"objectId": cb_oid, "signal": "checkedChanged", "snapshot": ["checked"]})
                _ = ws_recv_until_id(ws, "s2")
                # trigger the signal by toggling checked
                ws_send(ws, "tprep", "set_property", {"objectId": cb_oid, "name": "checked", "value": False})
                _ = ws_recv_until_id(ws, "tprep")
                ws_send(ws, "t1", "set_property", {"objectId": cb_oid, "name": "checked", "value": True})
                # Wait for event
                deadline2 = time.time() + 5.0
                got_snapshot = False
                while time.time() < deadline2:
                    try:
                        ws.settimeout(0.5)
                        msg2 = ws.recv()
                    except Exception:
                        continue
                    data2 = json.loads(msg2)
                    if data2.get("method") == "event":
                        params2 = data2.get("params", {})
                        if params2.get("kind") == "signal" and params2.get("name").startswith("checkedChanged"):
                            snap = params2.get("snapshot", {})
                            if isinstance(snap, dict) and snap.get("checked") is True:
                                got_snapshot = True
                                break
                assert_true(got_snapshot, "did not receive signal snapshot")
            finally:
                try:
                    ws.close()
                except Exception:
                    pass

        print("integration_test: OK")
        return 0
    finally:
        try:
            if app and app.poll() is None:
                if sys.platform == "win32":
                    app.terminate()
                else:
                    os.kill(app.pid, signal.SIGTERM)
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
