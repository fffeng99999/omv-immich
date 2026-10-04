#!/usr/bin/env bash
python3 -c "
import json
d = json.load(open('/usr/share/openmediavault/datamodels/rpc.compose.json'))
for m in d:
    if isinstance(m, dict) and m.get('name') in ('setFile','doCommand'):
        print(json.dumps(m, indent=1)[:2500])
        print('===')
"
