#!/usr/bin/env bash
echo '=== setFile param schema ==='
python3 -c "
import json
d = json.load(open('/usr/share/openmediavault/datamodels/rpc.compose.json'))
for m in d.get('methods', []):
    if m.get('name') in ('setFile','set','deleteFile','doCommand'):
        print(json.dumps(m, indent=1)[:2000])
        print('---')
"
echo '=== conf.service.compose.file entries ==='
omv-confdbadm read conf.service.compose.file
echo '=== filebrowser .env content ==='
cat /srv/dev-disk-by-uuid-d84bfe87-da9f-4543-a4b3-77e72870ff21/data/filebrowser/filebrowser.env
