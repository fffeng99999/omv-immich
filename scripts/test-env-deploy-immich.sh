#!/usr/bin/env bash
# 测试环境部署 Immich（低配方案：禁用 machine-learning 服务）
set -euo pipefail

# ---------- 1. swap 扩容到 2G（低内存保险） ----------
SWAP_KB=$(awk '/^SwapTotal/{print $2}' /proc/meminfo)
if [ "${SWAP_KB:-0}" -lt 2000000 ] && [ ! -e /swapfile2 ]; then
  fallocate -l 1G /swapfile2
  chmod 600 /swapfile2
  mkswap /swapfile2 >/dev/null
  swapon /swapfile2
  grep -q '^/swapfile2' /etc/fstab || echo '/swapfile2 none swap sw 0 0' >> /etc/fstab
  echo "swap +1G enabled"
fi

# ---------- 2. 目录布局 ----------
IMMICH_DIR=/srv/immich
mkdir -p "$IMMICH_DIR/upload" "$IMMICH_DIR/db"
cd "$IMMICH_DIR"

# ---------- 3. 下载官方 compose 并移除 machine-learning 服务 ----------
wget -q -O docker-compose.yml.official \
  https://github.com/immich-app/immich/releases/latest/download/docker-compose.yml
python3 - <<'EOF'
import re
lines = open('docker-compose.yml.official').read().splitlines(True)
out, skip_ml, skip_vol = [], False, False
for ln in lines:
    if re.match(r'^  immich-machine-learning:', ln):
        skip_ml = True; continue
    if skip_ml:
        if re.match(r'^  \S', ln):
            skip_ml = False
        else:
            continue
    if re.match(r'^  model-cache:', ln):
        skip_vol = True; continue
    if skip_vol:
        if re.match(r'^(\S|  \S)', ln):
            skip_vol = False
        else:
            continue
    out.append(ln)
# ML 删除后顶层 volumes: 段只剩 model-cache 一个条目，整段移除（悬挂空段会导致校验失败）
open('docker-compose.yml', 'w').writelines(out)
EOF
# 兜底：移除残留的空 volumes: 段（若脚本逻辑未覆盖）
python3 - <<'EOF'
import re
t = open('/srv/immich/docker-compose.yml').read()
t = re.sub(r'\nvolumes:\s*$', '\n', t)
open('/srv/immich/docker-compose.yml', 'w').write(t)
EOF
echo "--- compose after strip ---"
grep -E '^  \S+:' docker-compose.yml

# ---------- 4. 生成 .env ----------
if [ ! -f .env ]; then
  DB_PASSWORD=$(openssl rand -hex 16)
  cat > .env <<ENVEOF
# Immich 测试环境（omv-test-env / 192.168.217.128）— 生成于 $(date +%F)
UPLOAD_LOCATION=/srv/immich/upload
DB_DATA_LOCATION=/srv/immich/db
TZ=Asia/Shanghai
IMMICH_VERSION=v3.2.4
DB_PASSWORD=${DB_PASSWORD}
DB_USERNAME=postgres
DB_DATABASE_NAME=immich
ENVEOF
  chmod 600 .env
fi

# ---------- 5. 启动 ----------
docker compose up -d
sleep 5
docker compose ps
