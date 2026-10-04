#!/usr/bin/env bash
# 测试环境安装 Docker CE（与 omv-compose 插件同源的官方 docker 仓库）
set -euo pipefail

# 等待/绕过 unattended-upgrades 的 dpkg 锁（装完恢复）
if fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1; then
  systemctl stop unattended-upgrades.service || true
  sleep 3
fi

apt-get update -qq
apt-get install -y -qq ca-certificates curl gnupg >/dev/null
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/debian trixie stable" \
  > /etc/apt/sources.list.d/docker.list
apt-get update -qq
apt-get install -y -qq docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin >/dev/null

systemctl enable --now docker
systemctl start unattended-upgrades.service 2>/dev/null || true
docker --version
docker compose version
