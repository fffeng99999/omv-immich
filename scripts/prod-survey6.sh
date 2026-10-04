#!/usr/bin/env bash
python3 -c "
import json
d = json.load(open('/usr/share/openmediavault/datamodels/rpc.compose.json'))
for x in d:
    if x.get('id') in ('rpc.compose.setFile','rpc.compose.doCommand','rpc.compose.set'):
        print(json.dumps(x, indent=1)[:3000]); print('===')
"
echo '=== setFile php (param extraction) ==='
sed -n '1171,1240p' /usr/share/openmediavault/engined/rpc/compose.inc
