#!/usr/bin/env bash
python3 -c "
import json
d = json.load(open('/usr/share/openmediavault/datamodels/rpc.compose.json'))
for x in d:
    if x.get('id') == 'rpc.compose.setfile':
        print(json.dumps(x, indent=1))
"
