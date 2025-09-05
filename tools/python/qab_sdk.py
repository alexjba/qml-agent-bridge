import json
import time
from typing import Any, Dict, List, Optional

try:
    import websocket  # type: ignore
except ImportError as e:
    raise RuntimeError("Please install websocket-client (see tools/python/requirements.txt)") from e


class QmlAgentBridgeClient:
    def __init__(self, url: str = "ws://127.0.0.1:7777") -> None:
        self._url = url
        self._ws: Optional["websocket.WebSocket"] = None

    def connect(self) -> None:
        if self._ws is not None:
            return
        self._ws = websocket.create_connection(self._url)

    def close(self) -> None:
        if self._ws is not None:
            try:
                self._ws.close()
            finally:
                self._ws = None

    def __enter__(self) -> "QmlAgentBridgeClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        assert self._ws is not None, "Client not connected"
        req_id = str(int(time.time() * 1000))
        payload: Dict[str, Any] = {"id": req_id, "method": method}
        if params is not None:
            payload["params"] = params
        self._ws.send(json.dumps(payload))
        while True:
            msg = self._ws.recv()
            data = json.loads(msg)
            if isinstance(data, dict) and data.get("id") == req_id:
                if "error" in data:
                    raise RuntimeError(f"{method} error: {data['error']}")
                return data["result"] if "result" in data else data
            # ignore events here

    # Convenience API
    def hello(self) -> Dict[str, Any]:
        return self._request("hello")

    def list_roots(self) -> List[Dict[str, Any]]:
        res = self._request("list_roots")
        return res.get("roots", [])

    def find_by_name(self, name: str) -> List[Dict[str, Any]]:
        res = self._request("find_by_name", {"name": name})
        return res.get("matches", [])

    def inspect(self, object_id: str) -> Dict[str, Any]:
        return self._request("inspect", {"objectId": object_id})

    def list_children(self, object_id: str) -> List[Dict[str, Any]]:
        res = self._request("list_children", {"objectId": object_id})
        return res.get("children", [])

    def set_property(self, object_id: str, name: str, value: Any) -> bool:
        res = self._request("set_property", {"objectId": object_id, "name": name, "value": value})
        return bool(res.get("ok", False))

    def call_method(self, object_id: str, name: str, *args: Any) -> Any:
        res = self._request("call_method", {"objectId": object_id, "name": name, "args": list(args)})
        return res.get("result")

    def evaluate(self, object_id: str, expression: str) -> Any:
        res = self._request("evaluate", {"objectId": object_id, "expression": expression})
        return res.get("result")

    def subscribe_signal(self, object_id: str, signal: str) -> str:
        res = self._request("subscribe_signal", {"objectId": object_id, "signal": signal})
        return res.get("subscriptionId")

    def subscribe_property(self, object_id: str, name: str) -> str:
        res = self._request("subscribe_property", {"objectId": object_id, "name": name})
        return res.get("subscriptionId")

    def unsubscribe(self, subscription_id: str) -> bool:
        res = self._request("unsubscribe", {"subscriptionId": subscription_id})
        return bool(res.get("ok", False))

    # Convenience
    def first_by_name(self, name: str) -> Optional[Dict[str, Any]]:
        matches = self.find_by_name(name)
        return matches[0] if matches else None
