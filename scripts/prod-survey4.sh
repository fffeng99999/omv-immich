#!/usr/bin/env bash
python3 -c "
import json
d = json.load(open('/usr/share/openmediavault/datamodels/rpc.compose.json'))
print(type(d), len(d) if hasattr(d,'__len__') else '')
if isinstance(d, list):
    print([x.get('name') if isinstance(x,dict) else type(x) for x in d][:80])
elif isinstance(d, dict):
    print(list(d.keys()))
"
