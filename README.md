# Immich 安装指导文档

> 面向本工作区（omv-plugins）的自托管照片管理服务 **Immich** 安装与部署。
> 安装方式已在测试环境（`${TEST_ENV_HOSTNAME}` / `${TEST_ENV_IP_ADDRESS}`，OMV `${OMV_VERSION}` / Debian `${DEBIAN_VERSION}`）**实测验证通过**，按本文可复现到生产 NAS。
>
> - 实测版本：**Immich v3.2.4**（2026-09-28 发布，撰写时最新稳定版）
> - 实测日期：2026-10-04
> - 测试环境形态：**低配无 ML**（机器 2 核 / 929M 内存，禁用 machine-learning 服务）
> - 本文 `${变量名}` 取值见 `project_rules_ops.md`「全局变量定义表」

## 目录

1. [Immich 是什么](#一immich-是什么)
2. [架构与资源需求](#二架构与资源需求)
3. [本文件夹文件清单](#三本文件夹文件清单)
4. [前置条件](#四前置条件)
5. [路线 A：通用 CLI 安装（测试环境实测路径）](#五路线-a通用-cli-安装测试环境实测路径)
6. [路线 B：生产 NAS 安装（omv-compose 插件）](#六路线-b生产-nas-安装omv-compose-插件)
7. [启用 / 禁用机器学习（ML）](#七启用--禁用机器学习ml)
8. [安装后验证清单](#八安装后验证清单)
9. [日常运维：升级 / 备份 / 卸载](#九日常运维升级--备份--卸载)
10. [常见问题](#十常见问题)
11. [安全注意事项](#十一安全注意事项)
12. [OMV 插件（openmediavault-immich）](#十二omv-插件openmediavault-immich)

## 一、Immich 是什么

Immich 是最流行的开源自托管照片/视频管理方案（Google Photos 替代品，GPL-3.0）：

- 手机 App / 网页端自动备份、时间线浏览
- 人物/事物智能搜索、人脸识别（依赖 ML 服务）
- 时间线回忆、相册、共享、WebDAV/OAuth 集成
- 官方文档：https://docs.immich.app ｜ GitHub：https://github.com/immich-app/immich

## 二、架构与资源需求

### 容器架构（官方 docker-compose，v3.x）

| 服务 | 容器名 | 镜像 | 作用 | 实测内存占用 |
|---|---|---|---|---|
| immich-server | immich_server | `ghcr.io/immich-app/immich-server:v3.2.4` | 主服务（Web/API/缩略图/转码） | 291 MiB |
| redis | immich_redis | `docker.io/valkey/valkey:9`（digest 固定） | 任务队列 | 5 MiB |
| database | immich_postgres | `ghcr.io/immich-app/postgres:14-vectorchord0.4.3-pgvectors0.2.0`（digest 固定） | PostgreSQL + 向量扩展 | 210 MiB |
| immich-machine-learning（可选） | immich_machine_learning | `ghcr.io/immich-app/immich-machine-learning:v3.2.4` | 人脸识别/智能搜索/OCR | 未启用（内存大户） |

> 镜像带 `@sha256:` digest 固定，升级时须从新版官方 compose 拷贝新 digest。

### 资源需求

| 配置 | 最低内存 | 说明 |
|---|---|---|
| 无 ML（本文测试形态） | 2G（推荐）/ 1G（勉强，需 swap） | 实测三容器共约 505 MiB，宿主还需跑 OMV |
| 带 ML | 4G（推荐 6G+） | ML 模型加载（人脸/CLIP/OCR）内存占用大 |

> **生产 NAS 现状（3.7G 内存）**：无 ML 形态可跑；带 ML 需先扩内存（`nas_README.md` 待办已有此记录）。
> **磁盘**：DB 数据目录**禁止放在网络共享**（NFS/SMB/CIFS）上，必须本地文件系统；生产 NAS 的数据阵列（btrfs 本地挂载）满足要求。

## 三、本文件夹文件清单

```
omv-immich/
├── README.md                        # 本文档
├── docker-compose.yml               # 实测可用的 compose（无 ML 版，取自测试环境 /srv/immich/）
├── docker-compose.yml.official      # 官方原版 compose（含 ML 服务，取自 v3.2.4 release）
├── .env.example                     # .env 模板（路线 A 使用）
└── scripts/
    ├── test-env-install-docker.sh   # 测试环境装 Docker CE（已执行）
    └── test-env-deploy-immich.sh    # 测试环境部署 Immich（已执行，含 swap 扩容 + ML 剥离）
```

## 四、前置条件

1. **Docker Engine ≥ 25 + Compose 插件**（官方 healthcheck 用了 `start_interval`，老版本需手工注释）。
   - 生产 NAS：omv-compose 插件已托管 Docker，无需再装。
   - 全新机器：见 [路线 A 步骤 0](#五路线-a通用-cli-安装测试环境实测路径)。
2. **内存**：见上表。低内存机器先加 swap（路线 A 脚本已含）。
3. **端口**：默认 `2283`，确认未占用。生产 NAS 已占用端口：8080（FileBrowser）/ 19798（CloudDrive2）/ 7500（frpc 管理页），2283 无冲突。
4. **外网可达**：拉镜像需访问 ghcr.io 与 docker.io。

## 五、路线 A：通用 CLI 安装（测试环境实测路径）

> 适用：测试环境、任何裸 Debian 机器。生产 NAS 也可用此路线，但生产建议走路线 B 以获得 WebUI 托管。

### 步骤 0：安装 Docker（已有 Docker 跳过）

```bash
apt-get update && apt-get install -y ca-certificates curl gnupg
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/debian trixie stable" \
  > /etc/apt/sources.list.d/docker.list
apt-get update && apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
systemctl enable --now docker
```

> 实测：Docker 29.8.2 + Compose v5.6.0。注意 Debian 13 新装的机器 unattended-upgrades 可能占 dpkg 锁，等待或临时 `systemctl stop unattended-upgrades` 后重试。

### 步骤 1：目录布局

```bash
mkdir -p /srv/immich/upload /srv/immich/db    # 测试环境布局
cd /srv/immich
```

生产 NAS 的布局见路线 B（数据放阵列）。

### 步骤 2：获取 compose 与 .env

```bash
wget -O docker-compose.yml https://github.com/immich-app/immich/releases/latest/download/docker-compose.yml
wget -O .env https://github.com/immich-app/immich/releases/latest/download/example.env
```

编辑 `.env`（模板见 [.env.example](.env.example)）：

```bash
UPLOAD_LOCATION=/srv/immich/upload     # 照片/视频存放目录
DB_DATA_LOCATION=/srv/immich/db        # 数据库目录（禁止网络共享）
TZ=Asia/Shanghai
IMMICH_VERSION=v3.2.4                  # 务必 pin 具体版本，不要用 release 浮动标签
DB_PASSWORD=<openssl rand -hex 16 生成，仅用 A-Za-z0-9>
DB_USERNAME=postgres
DB_DATABASE_NAME=immich
```

### 步骤 3（低内存机器）：剥离 ML 服务

929M 内存机器必须去掉 `immich-machine-learning` 服务块，否则 OOM。处理要点：

1. 删除整个 `immich-machine-learning:` 服务块；
2. 删除文件尾部的 `volumes:`（model-cache）段——**悬挂的空 `volumes:` 段会导致 `volumes must be a mapping` 校验失败**（实测踩坑）。

一键脚本：[scripts/test-env-deploy-immich.sh](scripts/test-env-deploy-immich.sh)（已实测，含下载/剥离/生成 .env/启动全流程）。

### 步骤 4：启动

```bash
docker compose up -d
docker compose ps        # 三个容器均应 healthy
```

### 实测结果（`${TEST_ENV_HOSTNAME}` / `${TEST_ENV_IP_ADDRESS}`，2026-10-04）

```
NAME              STATUS
immich_postgres   Up 32 seconds (healthy)
immich_redis      Up 32 seconds (healthy)
immich_server     Up 32 seconds (healthy)

curl http://localhost:2283/api/server/ping     → {"res":"pong"}
curl http://localhost:2283/api/server/version  → {"major":3,"minor":2,"patch":4,"prerelease":null}
```

- 容器总内存：约 505 MiB；宿主另加 1G swap（/swapfile2，已写入 fstab）
- 磁盘占用（空库）：db 313M（含 WAL 初始化）/ upload 忽略

## 六、路线 B：生产 NAS 安装（omv-compose 插件）

> 生产 NAS（`${PROD_NAS_HOSTNAME}` / `${PROD_NAS_IP_ADDRESS}`）已有 omv-compose 托管 Docker（data-root 在阵列上）。
> **compose 栈必须经数据库注册才在 WebUI 显示**（nas_README 运维要点 1）。
> **本路线已于 2026-10-04 在生产 NAS 实际执行**（脚本：[scripts/prod-deploy-immich.sh](scripts/prod-deploy-immich.sh)）。

### ⚠ 路径冲突注意（实测踩坑）

omv-compose 插件会把栈 yml 渲染到 `<阵列>/data/<栈名>/`——**`data/immich/` 目录名被插件占用**。照片目录不能用 `data/immich/`，实测用 `data/immich-photos/`；DB 放 `data/appdata/immich/db/`。

| 目录 | 用途 | 说明 |
|---|---|---|
| `${PROD_NAS_DATA_ARRAY_MOUNT_PATH}/data/immich/` | **插件栈目录**（compose.yml/immich.yml/immich.env，禁手改） | setFile 自动生成 |
| `${PROD_NAS_DATA_ARRAY_MOUNT_PATH}/data/immich-photos/` | 照片/视频（容器内 `/data`） | 手工创建 |
| `${PROD_NAS_DATA_ARRAY_MOUNT_PATH}/data/appdata/immich/db/` | PostgreSQL 数据 | 手工创建，本地 btrfs 满足「禁网络共享」要求 |

### 步骤 1：备份 config.xml（变更前红线）

```bash
cp -a /etc/openmediavault/config.xml /root/omv-workspace/backups/config.xml.$(date +%F-%H%M%S).bak
```

### 步骤 2：注册栈（CLI 方式，实测）

直接 `omv-rpc Compose setFile`（与 WebUI Add Stack 等价，新建自动 applyChanges 并渲染出栈目录）。参数 schema：`{name, description, body, env, override, showenv, showoverride}`，body = 内联 compose（**无 .env 依赖，变量全写实值**）：

- 镜像 pin `v3.2.4`；redis/database 用官方 digest（见 [docker-compose.yml](docker-compose.yml)）
- 上传目录 `data/immich-photos:/data`、DB `data/appdata/immich/db:/var/lib/postgresql/data`
- `POSTGRES_PASSWORD`：`openssl rand -hex 16`；`POSTGRES_INITDB_ARGS: '--data-checksums'`
- 删掉官方版的 `env_file:` 块、ML 服务块、尾部 `volumes:` 段；server 加 `environment: TZ: Asia/Shanghai`
- payload 存 `/root/omv-workspace/immich-stack.json`（chmod 600，密码留档）

```bash
omv-rpc -u admin Compose setFile "$(cat /root/omv-workspace/immich-stack.json)"
omv-confdbadm read conf.service.compose.file | jq -r '.[].name'   # 应出现 immich
```

> 也可走 WebUI「Compose → 栈 → Add」粘贴同样的渲染版 yml，效果等价。

### 步骤 3：拉镜像启动（栈目录内）

```bash
cd ${PROD_NAS_DATA_ARRAY_MOUNT_PATH}/data/immich
docker compose up -d          # 约 1GB 镜像，带宽慢时耐心等
docker compose ps             # 三容器 healthy
```

> 实测坑：并发跑多个 `docker compose up` 会产生重复 pull 进程互相干扰——中断后重跑前先 `ps aux | grep "compose up"` 清理旧进程，或用 `nohup docker compose up -d > 日志 &` 保证会话断开不中断。

### 步骤 4：验证与初始化

```bash
curl -s http://localhost:2283/api/server/ping        # {"res":"pong"}
```

- 浏览器 `http://${PROD_NAS_IP_ADDRESS}:2283` → **首个注册账号即管理员**，注册后立即设强密码
- WebUI「Compose」页应能看到 immich 栈并可启停/看日志

## 七、启用 / 禁用机器学习（ML）

### 禁用（低内存形态，本文实测）

删除 `immich-machine-learning` 服务块 + 尾部 `volumes:` 段（见路线 A 步骤 3）。

**禁用后失去**：人脸识别、智能搜索（CLIP）、OCR。**保留**：上传/备份/时间线/普通搜索/相册/共享，一切日常功能。

禁用后建议在 Immich 管理页关闭 ML 相关任务：管理界面 → Administration → Settings → Smart Search / Facial Recognition → 关闭或把并发设 0，避免 server 反复重试请求 ML。

### 启用（内存 ≥ 4G 后）

1. 恢复官方 compose 中的 `immich-machine-learning` 服务块与 `volumes: model-cache:` 段（直接用 [docker-compose.yml.official](docker-compose.yml.official)）；
2. `docker compose up -d`（会自动拉起 ML 容器，模型缓存卷 model-cache 存于 Docker 数据目录）；
3. 首次启用后在管理页跑「Smart Search」「Facial Recognition」任务对存量照片建索引（耗时长，属正常）。

## 八、安装后验证清单

| # | 检查项 | 命令/方法 | 预期 |
|---|---|---|---|
| 1 | 三容器 healthy | `docker compose ps` | 全部 `Up (healthy)` |
| 2 | API ping | `curl http://localhost:2283/api/server/ping` | `{"res":"pong"}` |
| 3 | 版本 | `curl http://localhost:2283/api/server/version` | `3.2.4`（与 pin 一致） |
| 4 | WebUI | 浏览器 `http://<IP>:2283` | 注册/登录页正常 |
| 5 | 管理员注册 | 首个账号注册 | 注册成功进入时间线 |
| 6 | 上传测试 | 网页拖入 1-2 张照片 | 缩略图生成、时间线出现 |
| 7 | 手机备份（可选） | App 填服务器地址 `http://<IP>:2283` | 自动备份正常 |
| 8 | 内存水位 | `free -h` + `docker stats` | 无持续 swap 高占用 |

实测状态：1-4 ✅（2026-10-04，测试环境）；5-7 属用户交互项，生产部署后人工过一遍。

## 九、日常运维升级 / 备份 / 卸载

### 升级

```bash
cd /srv/immich        # 或 omv-compose 栈目录（生产走 WebUI 改 tag）
# 1. 读 https://github.com/immich-app/immich/releases 发布说明（大版本升级必读）
# 2. 备份数据库（见下）
# 3. 从新版 release 的 docker-compose.yml 同步：镜像 digest（redis/database）与任何结构变更
sed -i 's/^IMMICH_VERSION=.*/IMMICH_VERSION=v3.x.x/' .env
docker compose pull && docker compose up -d
```

### 备份（建议每周 cron）

```bash
# 数据库全量（停写不是必需，官方推荐 pg_dumpall）
docker exec -t immich_postgres pg_dumpall --clean --if-exists --username=postgres \
  | gzip > /backup/immich-db-$(date +%F).sql.gz
# 上传目录直接整目录备份（UPLOAD_LOCATION）
```

> 生产 NAS：备份文件放阵列 `data/backup/` 之类目录，不落系统盘。

### 卸载

```bash
docker compose down            # 保留数据
docker compose down -v         # 无命名卷（无 ML 时），数据都在绑定目录，删除需手删 /srv/immich 或阵列目录
```

omv-compose 栈卸载：WebUI → 栈 → Down + 删除，再删共享文件夹目录。

## 十、常见问题

| 问题 | 处理 |
|---|---|
| `validating ... volumes must be a mapping` | compose 尾部残留空的 `volumes:` 段（删 ML 块后常见），删除该行（实测踩坑） |
| 容器反复重启 / OOM | 内存不足；确认 ML 已禁用，`docker stats` 看占用，加 swap 或扩内存 |
| `server` 日志刷 ML 请求失败 | 已禁 ML 但智能搜索/人脸识别任务未关，管理页关掉相关任务 |
| DB 目录放 SMB/NFS 后启动崩溃 | 官方明确不支持网络共享存 DB；移回本地文件系统 |
| 2283 被占 | compose 端口改 `'8084:2283'` 形式（左侧宿主端口） |
| 首次缩略图生成慢 | 正常，低配机器 CPU 密集；任务队列在管理页可见进度 |
| 硬盘转码/存储在 HDD | database 环境变量取消注释 `DB_STORAGE_TYPE: 'HDD'` |
| WebUI 打不开但容器 healthy | 检查防火墙/端口映射；生产注意仅内网暴露原则（第十一节） |

## 十一、安全注意事项

1. **首个注册账号即管理员**：部署完立刻注册并设强密码，避免被内网其他设备抢占。
2. **公网暴露原则**（与工作区一致）：Immich 默认仅内网访问；如需外网，走 frpc 隧道按需暴露，且建议启用 Immich 内置 OAuth/强密码 + HTTPS（frp 侧配 TLS 或反代）。
3. `DB_PASSWORD` 用强随机值（仅 A-Za-z0-9，官方要求）；`.env` 权限 600。
4. 升级前必读 release notes 并备份数据库；大版本（v3→v4 等）可能有迁移步骤，不可直接跳。

## 十二、OMV 插件（openmediavault-immich）

本仓库同时是一个 OMV 原生插件源码树（deb 包），把上面的手工部署/升级流程做成 WebUI 托管：
`Services → Immich` 页配置参数、渲染 Compose 栈、按钮部署/启停/升级、实时看容器状态与版本。
插件只做 OMV 侧集成（渲染 + docker compose 生命周期 + 状态展示），**不改 Immich 自身功能**；照片库业务照旧在 Immich 原生界面管理。

### 插件工作原理

- 配置存 `conf.service.immich`（版本标签/Web 端口/时区/ML 开关/数据库凭据 + **三个共享文件夹引用**）。
  存储采用 OMV 官方 sharedfolder 引用模式：`composeDirRef`（栈文件）/ `uploadRef`（照片库）/ `dbRef`（PostgreSQL 数据）
  三个下拉框（`sharedFolderSelect`）直接选择已建好的共享文件夹，**数据天然落在阵列上**；
  配置里只存 UUID 引用，真实路径由 Salt（`omv_conf.get_sharedfolder_path`）与 ctl（`omv_get_sharedfolder_path`）在渲染/执行时解析，
  共享文件夹变更会被引擎模块监听并自动置脏重渲染。
- 「应用」时 Salt state（`srv/salt/omv/deploy/immich/`）把 `.env`（600）与 `docker-compose.yml`（644）渲染到所选共享文件夹；三个引用未配齐时不渲染（Apply 显示提示）。启用状态下文件有变化会自动重建栈，停用则移除容器（数据保留）。
- 首次部署、显式升级、启停、日志走 RPC 后台任务（`omv-immich-ctl` 封装 `docker compose`），任务弹窗实时显示输出，不受 Web 请求超时影响。
- 状态页显示：容器状态（运行/部分/停止/未渲染）、运行版本（Immich API）、最新上游版本（GitHub API）与更新提示。
- 已知限制：测试环境网络下 `api.github.com` 可能被限流（HTTP 403），此时「最新版本」显示为空、更新检测降级停用；`docker pull` 升级不受影响。

### 升级到 8.0.2（存储字段改为共享文件夹引用）

8.0.1 的三个手填路径字段（`composeDir/uploadLocation/dbDataLocation`）在 8.0.2 已替换为三个共享文件夹引用（`composeDirRef/uploadRef/dbRef`）。**插件不会自动搬家数据**，按下列顺序操作：

**顺序铁律**：先停栈 → 再升级 deb → 再搬数据 → 最后配置 Apply。

1. **8.0.1 页面先 Stop 栈**（容器移除、数据保留）。必须用 8.0.1 的按钮停——升级后再停面对的是孤儿容器，要手工 `docker rm` 清理；
2. `apt-get install ./openmediavault-immich_8.0.2_all.deb`（或 purge 后重装，confdb 更干净）。安装时 migration 自动补三个空引用键，旧路径键留在 config.xml 成孤儿（datamodel 忽略，无害）；
3. **WebUI 建三个共享文件夹**（存储 → 共享文件夹，建在数据阵列上）：`immich-stack`（栈文件）/ `immich-photos`（照片库）/ `immich-db`（数据库，本地文件系统，禁网络共享）；
4. **搬移数据**（旧数据与共享文件夹在同一阵列文件系统时 `mv` 是秒级 rename）：

   ```bash
   MNT=<阵列挂载点>   # 如 /srv/dev-disk-by-uuid-…
   # 照片（含隐藏文件，shopt 确保 dotglob）
   (shopt -s dotglob; mv "$MNT"/docker/immich/upload/* "$MNT"/immich-photos/)
   # PostgreSQL 数据（必须容器已停；同样含隐藏文件）
   (shopt -s dotglob; mv "$MNT"/docker/immich/db/* "$MNT"/immich-db/)
   # 旧渲染物留档即可，不搬家
   ```

5. **页面配置**：三个下拉分别选三个共享文件夹 → 版本确认 `v3.2.4` → 应用（渲染到新路径）→ 启动；
6. **验证**：状态 Running、三容器 healthy、运行版本 3.2.4、compose 文件路径指向新共享文件夹、上传一张测试照片确认落在 `immich-photos` 物理路径、重启 NAS 后栈自动拉起；
7. 旧 `docker/immich/` 目录保留观察一周，确认无新增写入后归档删除。

**回滚**：`apt-get install` 降回 8.0.1 deb 即可——旧路径键仍在 config.xml，8.0.1 忽略 `*Ref` 键，在旧路径启动栈即恢复。**前提：升级全程不删旧目录。**

**测试环境（无真实数据）建议 purge 重建**：卸载 8.0.1（数据目录保留）→ 装 8.0.2 → 建共享文件夹 → 全新走一遍配置与部署，顺带完成 8.0.2 真机验证。

### 测试环境实测记录（2026-10-04，`${TEST_ENV_IP_ADDRESS}`）

- `openmediavault-immich 8.0.1`（Architecture: all）本地构建安装，`omv-mkworkbench`/路由/中文 i18n 全部生效。
- 全流程验证通过：设置保存 → 应用渲染（`/srv/docker/immich/` 下 `.env`+compose）→ 栈自动拉起（三容器 running/healthy）→ `api/server/ping` pong、版本 3.2.4 → RPC 状态 Running → 停止/启动往返正常 → 停用+ML 渲染条件块/重建正常。
- 注意：插件栈 `container_name` 固定为 `immich_server/immich_redis/immich_postgres`，与手工部署同名冲突——混用时先 `docker compose down` 旧栈（Compose 项目名同为 `immich` 时互删）。

### 发布流程

push tag `v*` → GitHub Actions（`.github/workflows/build.yml`）lint + 构建 `_all.deb` + 附到 Release；`${GITHUB_USER}` 占位符由 `render-vars.sh` 按 `debian/variables.env` 渲染。
