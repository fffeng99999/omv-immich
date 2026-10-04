#!/usr/bin/env bash
# 生产 NAS 部署 Immich v3.2.4（无 ML 低内存形态，omv-compose 数据库注册）
set -euo pipefail
ARRAY=/srv/dev-disk-by-uuid-d84bfe87-da9f-4543-a4b3-77e72870ff21
WS=/root/omv-workspace

# ---------- 1. 备份 config.xml（红线要求） ----------
mkdir -p "$WS/backups"
cp -a /etc/openmediavault/config.xml "$WS/backups/config.xml.$(date +%F-%H%M%S).bak"
ls -t "$WS/backups/" | head -1

# ---------- 2. 数据目录（栈目录 data/immich 归插件，照片/DB 另放） ----------
mkdir -p "$ARRAY/data/immich-photos" "$ARRAY/data/appdata/immich/db"

# ---------- 3. 生成栈配置（随机 DB 密码） ----------
DBPW=$(openssl rand -hex 16)
python3 - "$DBPW" "$ARRAY" <<'PYEOF' > "$WS/immich-stack.json"
import json, sys
pw, array = sys.argv[1], sys.argv[2]
body = """services:
  immich-server:
    container_name: immich_server
    image: ghcr.io/immich-app/immich-server:v3.2.4
    volumes:
      - %s/data/immich-photos:/data
      - /etc/localtime:/etc/localtime:ro
    environment:
      TZ: Asia/Shanghai
    ports:
      - '2283:2283'
    depends_on:
      - redis
      - database
    restart: always
    healthcheck:
      disable: false

  redis:
    container_name: immich_redis
    image: docker.io/valkey/valkey:9@sha256:70739f85ad2ee01a726a965584a0f94895f01b0c60b3cc8b0aeef11eaa6888cf
    healthcheck:
      test: redis-cli ping | grep -q PONG || exit 1
    restart: always

  database:
    container_name: immich_postgres
    image: ghcr.io/immich-app/postgres:14-vectorchord0.4.3-pgvectors0.2.0@sha256:bcf63357191b76a916ae5eb93464d65c07511da41e3bf7a8416db519b40b1c23
    environment:
      POSTGRES_PASSWORD: %s
      POSTGRES_USER: postgres
      POSTGRES_DB: immich
      POSTGRES_INITDB_ARGS: '--data-checksums'
    volumes:
      - %s/data/appdata/immich/db:/var/lib/postgresql/data
    shm_size: 128mb
    restart: always
    healthcheck:
      disable: false
""" % (array, pw, array)
print(json.dumps({
  "name": "immich",
  "description": "Immich v3.2.4 自托管照片管理（无 ML 低内存形态）",
  "body": body,
  "showenv": False,
  "env": "",
  "showoverride": False,
  "override": ""
}, ensure_ascii=False))
PYEOF
chmod 600 "$WS/immich-stack.json"

# ---------- 4. 注册栈（新建自动 applyChanges 渲染） ----------
omv-rpc -u admin Compose setFile "$(cat "$WS/immich-stack.json")" >/dev/null && echo "setFile OK"
omv-confdbadm read conf.service.compose.file | python3 -c "import json,sys; print([e['name'] for e in json.load(sys.stdin)])"
ls "$ARRAY/data/immich/"

# ---------- 5. 拉镜像并启动 ----------
cd "$ARRAY/data/immich"
docker compose up -d
