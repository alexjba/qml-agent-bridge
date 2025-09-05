# qml-agent-bridge

Tiny WebSocket JSON bridge for AI-driven Qt/QML automation.

Usage

Build
```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

Run example app
```bash
./examples/minimal-cpp/example-minimal
```

CLI client (qab-cli)
```bash
# Hello / handshake
./tools/cli/qab-cli --url ws://127.0.0.1:7777 --method hello

# List engine roots
./tools/cli/qab-cli --method list_roots

# Find by objectName
./tools/cli/qab-cli --method find_by_name --params '{"name":"helloButton"}'

# Inspect an object
OID=$(./tools/cli/qab-cli --method find_by_name --params '{"name":"helloButton"}' | grep -o 'qobj:[0-9a-f]\+' | head -1)
./tools/cli/qab-cli --method inspect --params '{"objectId":"'"$OID"'"}'

# Set a property
./tools/cli/qab-cli --method set_property --params '{"objectId":"'"$OID"'","name":"visible","value":false}'

# Call a zero-arg method (with QML fallback)
./tools/cli/qab-cli --method call_method --params '{"objectId":"'"$OID"'","name":"forceActiveFocus","args":[]}'

# Evaluate a QML expression in the object context
./tools/cli/qab-cli --method evaluate --params '{"objectId":"'"$OID"'","expression":"forceActiveFocus()"}'

# Inspect custom signals on customEmitter
OID_EMIT=$(./tools/cli/qab-cli --method find_by_name --params '{"name":"customEmitter"}' | grep -o 'qobj:[0-9a-f]\+' | head -1)
./tools/cli/qab-cli --method inspect --params '{"objectId":"'"$OID_EMIT"'"}' | sed -n '1,200p'

# Subscribe to a custom signal and trigger via button click
OID_BTN=$(./tools/cli/qab-cli --method find_by_name --params '{"name":"helloButton"}' | grep -o 'qobj:[0-9a-f]\+' | head -1)
./tools/cli/qab-cli --method subscribe_signal --params '{"objectId":"'"$OID_EMIT"'","signal":"ping()"}'
./tools/cli/qab-cli --method call_method --params '{"objectId":"'"$OID_BTN"'","name":"clicked","args":[]}'
```

Python client
```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r tools/python/requirements.txt

# Hello / handshake
python tools/python/qab.py --method hello

# List roots
python tools/python/qab.py --method list_roots

# Subscribe to button clicks and watch events
OID=$(./tools/cli/qab-cli --method find_by_name --params '{"name":"helloButton"}' | grep -o 'qobj:[0-9a-f]\+' | head -1)
python tools/python/qab.py --method subscribe_signal --params '{"objectId":"'"$OID"'","signal":"clicked()"}' --watch

# High-level SDK usage
python - <<'PY'
from tools.python.qab_sdk import QmlAgentBridgeClient

with QmlAgentBridgeClient() as client:
    print(client.hello())
    roots = client.list_roots()
    btn = client.find_by_name('helloButton')[0]
    result = client.call_method(btn['objectId'], 'forceActiveFocus')
    print('forceActiveFocus result:', result)
PY
```

Integration tests
```bash
# Ensure the project is built first (see Build section)
python tools/python/integration_test.py
```

API (JSON over WebSocket)
- hello: `{ "id":"1", "method":"hello" }` → `{ protocol, version, capabilities[] }`
- list_roots: returns `roots[{objectId,type,objectName}]`
- find_by_name: `{ name }` → `matches[]`
- inspect: `{ objectId }` → `type,objectName,properties,methods,signals,childrenCount,model?`
- list_children: `{ objectId }` → `children[{objectId,type,objectName}]`
- set_property: `{ objectId,name,value }` → `{ ok:true }`
- call_method: `{ objectId,name,args[] }` → `{ ok:true, result:any }`
- evaluate: `{ objectId,expression }` → `{ result:any }`
- subscribe_signal: `{ objectId, signal, snapshot?: string|string[] }` → `{ subscriptionId }` (events sent as `{ method:"event", params:{ subscriptionId, objectId, kind:"signal", name, snapshot? } }`)
- subscribe_property: `{ objectId, name }` → `{ subscriptionId }` (events emitted on property notify as `{ method:"event", params:{ subscriptionId, objectId, kind:"property", name, value } }`)
- unsubscribe: `{ subscriptionId }` → `{ ok:true }`

Models
- model_info: `{ objectId }` → `{ rowCount, columnCount, roles[] }`
- model_fetch: `{ objectId, start?:0, count?:20, roles?:string[] }` → `{ rowCount, columnCount, items[] | rows[] }`

Security
- Binds to 127.0.0.1 only.
- Add token requirement in future versions.
