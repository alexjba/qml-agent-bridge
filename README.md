# qml-agent-bridge

Tiny WebSocket JSON bridge for AI-driven Qt/QML automation.

Build:

Run example:



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
```

API (JSON over WebSocket)
- hello: `{ "id":"1", "method":"hello" }`
- list_roots: returns `roots[{objectId,type,objectName}]`
- find_by_name: `{ name }` → `matches[]`
- inspect: `{ objectId }` → `type,objectName,properties,methods,childrenCount,model?`
- set_property: `{ objectId,name,value }` → `{ ok:true }`
- call_method: `{ objectId,name,args[] }` → `{ ok:true }` (zero-arg only for now)
- evaluate: `{ objectId,expression }` → `{ result:any }`

Security
- Binds to 127.0.0.1 only.
- Add token requirement in future versions.
