#!/usr/bin/env bash
# 生产 NAS 只读调查：Compose RPC 方法、既有栈结构、confdb 注册模式
echo '=== compose RPC methods ==='
grep -oE 'registerMethod\("[a-zA-Z]+"\)' /usr/share/openmediavault/engined/rpc/compose.inc \
  | sed 's/registerMethod("//;s/")//' | sort -u
echo '=== compose datamodels ==='
ls /usr/share/openmediavault/datamodels/ | grep -i compose
echo '=== filebrowser stack dir ==='
ls -la /srv/dev-disk-by-uuid-d84bfe87-da9f-4543-a4b3-77e72870ff21/data/filebrowser/
echo '=== filebrowser yml head ==='
head -40 /srv/dev-disk-by-uuid-d84bfe87-da9f-4543-a4b3-77e72870ff21/data/filebrowser/*.yml
echo '=== confdb compose entries ==='
omv-confdbadm read conf.system.plugin.compose 2>/dev/null || echo '(datamodel path differs)'
