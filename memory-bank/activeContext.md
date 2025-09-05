# Active Context

Current focus:
- MVP WS server with hello, list_roots, find_by_name, inspect, list_children.
- Subscriptions: signal and property change events.
 - Subscriptions: property events include current value; signal events can include property snapshots.
- Example app to validate end-to-end.

Next:
- Evaluate improvements (complex types), subscription payloads.
- Python client tests and examples.

Decisions:
- JSON over WebSocket; localhost by default; optional token.
