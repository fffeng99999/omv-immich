#!/usr/bin/env bash
python3 -c "
import json
d = json.load(open('/usr/share/openmediavault/datamodels/rpc.compose.json'))
print(json.dumps(d[0], indent=1)[:800])
print('===')
for x in d:
    n = x.get('methodname') or x.get('rpc') or list(x.keys())
    print(n)
" 2>&1 | head -40
grep -n "setFile" /usr/share/openmediavault/engined/rpc/compose.inc | head -5
