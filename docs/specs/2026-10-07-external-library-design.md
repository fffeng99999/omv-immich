# Immich 外部库（External Library）插件支持 — 设计

- 日期：2026-10-07
- 仓库/插件：`omv-immich` / openmediavault-immich
- 目标版本：**8.0.9**
- 状态：**设计已批准，待实现**

## 1. 背景与目标

Immich 官方 [External Library](https://docs.immich.app/guides/external-library/) 用于**原地读取**宿主机上已有的照片目录（不复制进 Immich 上传库）。官方做法：

1. 在 `immich-server` 的 `volumes:` 里再加一条挂载，官方示例是"宿主路径 = 容器内路径"，例如
   `- /home/user/photos1:/home/user/photos1:ro`（可多条）；
2. `:ro` = 只读（Immich 不能删除文件、不写 XMP sidecar）；**去掉 `:ro`** 才允许删除/写元数据；
3. 再到 Immich 管理界面 → **External Libraries** → 创建库，把**容器内那个路径**填进 Folder → Scan。

关键事实：
- 官方**没有**"标准的固定容器目录"（示例用同路径，也可自定义）；
- 官方**没有**用于外部库的环境变量（`.env` 里只有 `UPLOAD_LOCATION` 等）。不存在"给外部库配环境变量"这条路——本质就是**多一条卷挂载 + 界面里填路径**。

**目标**：把这套能力做进插件——在 Immich 设置页之外，提供一个"外部库"子页，用 **OMV 共享文件夹引用** 增删多条挂载，并控制只读/可写；保存后由插件重新发布 Compose 栈，用户在 Apply 后到 Immich 界面建库即可。

**用户已确认的范围**：多条挂载 + 每行可自定义容器路径 + 只读开关（即"文档里有的功能都做"）。

## 2. 术语与约定

- **宿主侧**：OMV 共享文件夹引用（存 UUID）。渲染进 compose 用 Compose 插件占位符 `${{ sf:"<共享文件夹名>" }}`，由 Compose 插件在部署时解析成真实路径（挂载点 + reldirpath）。**不手填原始路径**（保持 OMV "路径即引用" 规范，随共享文件夹移动自动跟随）。
- **容器内路径**：用户在 Immich 界面建库时要填的路径；`/data` 是 Immich 上传库，禁止占用。
- **单写入者**：stack 文件由 Compose 插件落盘（`<Compose 共享文件夹>/immich/immich.yml`），但**内容由 Immich 插件生成**并通过 `Compose.setFile` 提交（现有 `syncComposeStack()` 机制）。

## 3. 数据模型（新增 iterable）

新增 `conf.service.immich.library`（xpath `.//services/immich/libraries/library`，`iterable: true`，`idproperty: uuid`）：

| 属性 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `uuid` | string, `format: uuidv4` | | 行 ID |
| `sharedfolderref` | string（uuidv4 或空串） | `""` | 共享文件夹引用（表单用 `sharedFolderSelect`） |
| `containerPath` | string | `"/mnt/extlib"` | 容器内路径（= 在 Immich 建库时填的路径） |
| `readonly` | boolean | `true` | 只读（渲染 `:ro`）；关掉才允许 Immich 删除/写 XMP sidecar |

不设 `name`/`comment`（YAGNI；列表用 `containerPath` 作人类可读标识，`rowEnumFmt: "{{ containerPath }}"`）。

**节点创建**（`/config/services/immich` 下加 `<libraries>` 容器节点）：
- 新装：在 `confdb/create.d/conf.service.immich.sh` 末尾追加**幂等**块（`omv_config_exists "/config/services/immich" && ! omv_config_exists "/config/services/immich/libraries"` → `omv_config_add_node`）。
- 已装：迁移脚本 `confdb/migrations.d/conf.service.immich_8.0.9.sh`（同样幂等）。

## 4. RPC（Immich 服务新增 4 个方法）

| 方法 | 作用 |
|---|---|
| `getLibraries($params,$context)` | 列表（供 datatable `store.proxy.get`）。每行除数据模型字段外，额外返回只读展示字段 `sharedFolderName`（由 `sharedfolderref` 解析；解析不到时给占位文本），列表列用它显示共享文件夹。 |
| `getLibrary($params,$context)` | 按 `uuid` 取单条（供编辑表单 `request.get`）。 |
| `setLibrary($params,$context)` | 新增/更新单条 → 保存后调用 `syncComposeStack()` **重新发布栈**。 |
| `deleteLibrary($params,$context)` | 删除单条 → 同样 `syncComposeStack()`。 |

**校验（`setLibrary`，服务端必做）**：
- `sharedfolderref` 必选且可解析为共享文件夹；
- `containerPath` 必填、以 `/` 开头的绝对路径、**不含 `:`**（会破坏卷语法）、不等于 `/`、不等于 `/data`；
- `containerPath` **不得与其它行重复**（跨行唯一）；
- 全部通过后才落库。

（表单侧也加基础校验：required、`^/[^:]*$`、长度上限，给出中文错误文案。）

## 5. Compose 渲染（`buildComposeBody()`）

在 `immich-server.volumes` 中、`/data` 挂载之后，按**稳定顺序**（按 uuid 排序，避免每次重排）追加每个库行：

```yaml
- ${{ sf:"<共享文件夹名>" }}:<containerPath>:ro    # readonly = true
- ${{ sf:"<共享文件夹名>" }}:<containerPath>       # readonly = false
```

- 宿主侧用共享文件夹名占位符（Compose 插件部署时解析真实路径；共享文件夹挪位置自动跟随）。
- 解析不到共享文件夹名时**报错拒绝**（与现有 uploadRef/dbRef 的处理一致），避免生成坏 yml。
- 与现有行为一致：body 交给 `Compose.setFile`，由 Compose 插件渲染落盘。

## 6. UI（新增子页，方案 A：独立子页）

| 文件 | 内容 |
|---|---|
| `workbench/navigation.d/services.immich.libraries.yaml` | navigation-item：`path: services.immich.libraries`，`text: _("External libraries")`，`position: 20`，icon（如 `mdi:folder-multiple-image`） |
| `workbench/route.d/services.immich.libraries.yaml` | route：`/services/immich/libraries` → datatable 组件 |
| `workbench/route.d/services.immich.libraries.create.yaml` | route：`/services/immich/libraries/create`，`editing: false`，`notificationTitle` |
| `workbench/route.d/services.immich.libraries.edit.yaml` | route：`/services/immich/libraries/edit/:uuid`，`editing: true` |
| `workbench/component.d/omv-services-immich-library-datatable-page.yaml` | datatablePage：`stateId`(新 UUID)、`rowId: uuid`、`rowEnumFmt`、`store.proxy.{service: Immich, get.method: getLibraries}`、列（共享文件夹 / 容器内路径 / 只读）、`actions: create/edit/delete`（create/edit→url，delete→request `deleteLibrary`） |
| `workbench/component.d/omv-services-immich-library-form-page.yaml` | formPage：`request.get.method: getLibrary`（`params.uuid: {{ _routeParams.uuid }}`）、`post.method: setLibrary`；字段 `confObjUuid` + `sharedFolderSelect` + `textInput(containerPath)` + `checkbox(readonly)`；hint 提示"创建后到 Immich 管理界面 → 外部库 → 创建库，把「容器内路径」填入 Folder，然后 Scan"；按钮 cancel(→列表) + submit |

同时：在现有 Immich **设置页**顶部提示补一句指向"外部库"子页。

## 7. 生效链路

外部库页"新建/编辑/删除" → `setLibrary/deleteLibrary` 落库并**立即重新发布栈**（`Compose.setFile`）→ OMV 顶部出现"未应用更改" → 用户点**应用** → Compose 渲染 `immich.yml` + Immich 的 Salt 状态执行 `omv-compose-run immich up -d` → **容器按新挂载重建** → 用户到 Immich 界面建库并 Scan。

（不额外弹任务框；沿用 OMV 标准"改配置 → 应用"流程。）

## 8. i18n

新增所有 `_("…")` msgid 补进 `locale/openmediavault-immich.pot` + `zh_CN` + `zh_TW`（列表列名、表单标签/提示、导航文案、通知"Updated library."等）。复用已有词条（如 `Storage`）。

## 9. 版本与迁移

- 版本升到 **8.0.9**（`debian/changelog` 顶部新增条目；tag `v8.0.9`）。
- 迁移 `conf.service.immich_8.0.9.sh`：幂等创建 `<libraries>` 节点。无其它数据变更。
- `.install` 用 `usr/share/openmediavault/*` glob，新增文件自动收录，无需改。

## 10. 文件清单

**新增**
- `usr/share/openmediavault/datamodels/conf.service.immich.library.json`
- `usr/share/openmediavault/confdb/migrations.d/conf.service.immich_8.0.9.sh`
- `usr/share/openmediavault/workbench/navigation.d/services.immich.libraries.yaml`
- `usr/share/openmediavault/workbench/route.d/services.immich.libraries.yaml`
- `usr/share/openmediavault/workbench/route.d/services.immich.libraries.create.yaml`
- `usr/share/openmediavault/workbench/route.d/services.immich.libraries.edit.yaml`
- `usr/share/openmediavault/workbench/component.d/omv-services-immich-library-datatable-page.yaml`
- `usr/share/openmediavault/workbench/component.d/omv-services-immich-library-form-page.yaml`

**修改**
- `usr/share/openmediavault/engined/rpc/immich.inc`（+4 RPC 方法；`buildComposeBody()` 加挂载；set/delete 后重发布）
- `usr/share/openmediavault/confdb/create.d/conf.service.immich.sh`（幂等创建 `<libraries>`）
- `usr/share/openmediavault/locale/openmediavault-immich.pot` / `zh_CN/…po` / `zh_TW/…po`
- `debian/changelog`（8.0.9）
- `README.md`（功能说明）

## 11. 测试计划（先在测试环境）

1. 新建一行 → 保存 → 应用 → `immich.yml` 出现对应 `- <解析路径>:<containerPath>:ro`，容器重建且 `docker inspect` 可见该挂载。
2. 编辑容器路径/共享文件夹/只读开关 → 应用 → yml 与容器挂载随之变化。
3. 删除一行 → 应用 → 挂载消失，容器重建；数据未动。
4. Immich 界面建库指向该容器路径 → Scan 能扫到文件。
5. 只读：Immich 无法删除库内文件；切可写后可删/写 XMP。
6. 校验：容器路径填 `/data`、相对路径、含 `:`、重复 → 被拒并有中文提示。
7. 共享文件夹改名 → 应用后 yml 自动跟随（Compose 占位符解析）。
8. 页面中文文案与"未应用更改"流程正常。

## 12. 非目标（YAGNI）

- **不**把外部库纳入现有"测试目录 / 迁移数据"（外部库是原地引用，无迁移语义）。
- **不**做任意原始路径手填（只走共享文件夹引用）。
- **不**做每行的自定义挂载参数（如 `:rw` 之外的选项、`nosuid` 等）——留待将来。
- **不**在插件里读/写外部库内容（只在 Immich 原生界面建库与扫描）。

## 13. 风险与备选

- **风险**：datatable 子页与现有设置页的导航共存（新增 nav position 20）——与 frpc 代理页同构，风险低。
- **风险**：`${{ sf:"…" }}` 占位符能否带 `:ro` 后缀——需在测试环境实测（预期可行，占位符只是路径替换）。若不行，退化为在 ctl 侧解析路径后写实路径。
- **备选**：若"独立子页"因故不可行，回退到设计阶段评估过的"设置页内嵌 datatable"或极简 textarea（本次不采用）。
