[English](../../CONTRIBUTING.md) | 简体中文

# 参与 Droidspaces 贡献

## 理念

> 一个不存在的功能，好过一个有 Bug 的实现。

Droidspaces 运行在两种截然不同的平台上：Android（硬件范围从古老的厂商冻结 3.10 内核到
现代 GKI 设备，涵盖数十种 SoC 和 OEM）以及 Linux 桌面环境。一个在你的环境上能正常工作、
却在别人的环境上会出问题的补丁，不是贡献，而是倒退。每一个被合入核心的改动，都必须毫无
例外地遵守这份契约。

这份理念的另一半，是关于代码库本身的。十行能完成工作的代码，胜过一百行做同样事情的代码。
复用胜过重写。本项目经历过的最大一次清理，正是因为总在既有代码旁边另写新代码、而不是扩展
它，代价是删掉 1,550 行代码，以及修复一整类主机 root 命令注入漏洞。下面的
[复用清单](#复用清单)存在的意义，就是让你永远不必猜测某个东西是否已经存在。

## 平台范围

Droidspaces 核心必须同时在 Android 和 Linux 上正确工作。这两个环境不可互换，必须作为独立
的目标平台对待。

### 平台检测

代码库提供 `is_android()` 用于运行时平台检测：

```c
int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  /* 优先级 1：检查是否处于 Recovery（如 TWRP） */
  if (access("/system/bin/recovery", F_OK) == 0) {
    cached_result = 0;
  }
  /* 优先级 2：检查核心 Android 系统标记 */
  else if (access("/system/build.prop", F_OK) == 0 ||
           access("/system/bin/app_process", F_OK) == 0) {
    cached_result = 1;
  }
  /* 回退：非标准 Android 环境 */
  else {
    cached_result = 0;
  }

  return cached_result;
}
```

**任何 Android 独有的功能或行为必须用 `is_android()` 进行防护。**
**任何 Linux 桌面独有的功能或行为同样必须进行防护。**

不要假设运行时环境。不要让 Android 专用代码在 Linux 上执行，反之亦然。未做平台守卫的假设
将导致 PR 被拒绝。

## 内核兼容性

**最低支持内核：3.10**

这是一个硬性底线，不是建议。如果你的实现依赖某个不存在于 3.10 内核的 syscall、`/proc`
接口、namespace 特性或内核配置，它将不会被合入核心。

- 不要使用 `openat2(2)`，5.6 之前不可用。
- 不要只依赖 cgroup v2，cgroup v1 必须保持可用。
- 不要假设 `clone3(2)`、`pidfd_*` 或任何 5.x 之后才有的 API。
- 如果存在回退路径，就实现它。如果没有，那这个功能不属于核心。

这同时适用于 Android 和 Linux 目标平台。现代桌面内核并不能豁免你的补丁。

请在运行旧内核的真实硬件上测试你的改动。模拟器和现代原生内核不算充分验证。

## SoC 与 OEM 覆盖（Android）

Droidspaces 运行在高通、Exynos、联发科和紫光展锐的芯片上，运行在与主线差异极大的 OEM
内核之下。你的补丁在提交前必须在具有代表性的一批环境上测试过。

请在 PR 中明确写出你在哪些设备和内核版本上测试过。未经测试的兼容性声明一律视为未测试。

针对某一个 SoC 系列或 OEM 内核特有行为的补丁，**只有在 Droidspaces 能够在运行时适配该
行为的前提下**才可接受，也就是通过检测、条件代码路径或优雅回退来处理，并且不能让未受影响
的硬件出现回归。如果修复无法这样泛化，它属于下游分支，不属于核心。

## Android App 改动

App 的最低要求是 **Android 8（API 26）**。对 Android App 的改动不得引入任何在 Android 8
上会出问题的依赖、API 调用或行为。

在发起 PR 之前，请在 Android 8 上测试。仅在较新的 Android 版本上测试是不够的。

## 构建

### C 后端

```
make native            # 为当前主机架构构建
make aarch64           # 交叉构建：aarch64、x86_64、armhf、x86、riscv64
make debug-hardened    # 带 ASan、UBSan、LSan 的构建，用于排查内存问题
make all-build         # 构建全部架构，并同步进 APK 资源目录
make all-tarball       # all-build 再加上发布用 tarball
make format            # 用仓库的 .clang-format 对 src/ 执行 clang-format
make clean
```

不带目标的 `make` 只会打印帮助，不会构建任何东西。`make native` 需要 musl 工具链；如果
缺失，按错误提示运行 `./install-musl.sh <架构>`。

常用选项：`V=1` 显示完整编译命令行，`ENABLE_SOCKETD_BACKEND=0` 构建不含私有 API 桥接的
精简版本。

构建启用了 `-Wall -Wextra -Wpedantic -Werror`，以及格式化、变量遮蔽和截断相关的警告。一个
警告就是一次构建失败，而不是一条评审意见。

新增 `.c` 文件时必须把它加进 Makefile 的 `SRCS`。这里没有通配符。

版本号从 `src/include/droidspace.h` 中的 `DS_VERSION` 提取。要改版本号请改那里，不要改
Makefile。

### Android App

在 `Android/` 目录下：

```
./build.sh             # debug APK
./build.sh release     # 签名的 release APK
```

请使用脚本，而不要直接调用 gradle。脚本会准备 wrapper、清理产物，并把 APK 放到其余工具
期望的位置。

### CI

`.github/workflows/ci.yml` 会执行 `make all-tarball`、`scripts/mkdeb.sh` 以及 gradle
release 构建。它**不会**检查代码格式，所以运行 `make format` 是你的责任。

## 代码风格

### 不要使用长破折号

代码里不要，注释里不要，提交信息里不要，文档里也不要。用逗号、句号，或者把句子重写一遍。

### 不要使用 ASCII 分隔线注释

不要一整行的 `-----`，不要 `=====`，不要方框式的段落标题。每个文件顶部的 SPDX 许可证声明
保留。`src/` 中已有的分隔线早于这条规则，正在单独清理中。不要再新增。

### 注释要像人话

说明代码为什么这么做，或者不这么做会出什么问题。只是把下一行代码重复一遍的注释是噪音。

```c
/* 差 */
/* 计数加一 */
count++;

/* 好 */
/* netd 每次重启都会清掉我们的规则，所以每个周期都要重新写入 */
install_policy_rules(cfg);
```

后端有些地方已经做得很好了。`src/include/droidspace.h` 记录了调用顺序约束（
`ds_ksu_neutralize_root_escape` 必须在 `ds_seccomp_apply_minimal` 之前运行，并写明了
原因），`ds_bind_mount_socket` 用它所堵住的竞态来解释为什么要用 `O_NOFOLLOW`。这就是标准。

### 十行胜过一百行

先想能不能删，再想要不要加。目标是在正确的位置做最小的改动。在错误位置的最小改动，只是
第二个 Bug。

### C 语言约定

- 使用 C89 块注释（`/* ... */`）。`src/` 中没有 `//` 注释。
- 默认 `static`。只有当函数被加进 `src/include/droidspace.h` 时，才去掉 `static`。
- 新的跨模块函数使用 `ds_` 前缀。遗留的无前缀名称（`read_file`、`mkdir_p`、`run_command`、
  `domount`）保持原样，不要重命名，也不要在新代码中模仿它们。
- `is_*` 用于判定函数，`check_*` 用于可能打印输出的探测函数，`setup_*` 与 `cleanup_*`
  成对表示生命周期，`print_*` 用于纯输出，`free_*` 用于释放。
- 常量用 `DS_` 加全大写下划线。结构体用 `struct ds_*`。`_t` 后缀只用于 typedef。
- 头文件保护宏形如 `#ifndef DROIDSPACE_H`，并以带名字的 `#endif /* DROIDSPACE_H */` 结束。
  不使用 `#pragma once`。
- 没有 `xmalloc` 之类的封装。使用普通的 `malloc` 并显式检查 NULL；只要大小有上界，就优先
  使用定长缓冲区。`struct ds_config` 几乎全是定长字段，原因正在于此。
- 每个持有堆内存的 API 都记录了它的释放函数。请沿用这个做法。

### Kotlin 约定

- 状态放在 ViewModel 里，不要放在有状态的 Composable 里，也不要放在 `util` 单例里。
- 列表使用带稳定 key 的 `LazyColumn` 渲染。
- 颜色取自 `MaterialTheme.colorScheme`，字体取自 `MaterialTheme.typography`，圆角取自
  `ShapeUtils`，动画时长取自 `AnimationUtils`。不要写死这些值。
- 任何进入 root shell 的值都必须经过 `ContainerCommandBuilder.quote()` 或白名单校验。
  没有例外。

## 提交规范

- 改动任何 `.c` 或 `.h` 文件后，提交前先运行 `make format`。
- 每个提交都要签名：`git commit -s`。
- 不要为 AI Agent 添加 `Co-Authored-By:` 尾注。人类共同作者是可以的。
- 标题用祈使语气；当改动不是一目了然时，在正文里说明为什么。

| 前缀 | 用于 |
| --- | --- |
| `app:` | Android App 改动，具体场景用 `app: fix:` 和 `app: refactor:` |
| `fix:` | 后端 Bug 修复 |
| `refactor:` | 不改变行为的后端重构 |
| `feat:` | 新的后端功能 |
| `docs:` | 文档 |
| `net:` `mount:` `seccomp:` `daemon:` `socketd:` | 限定子系统的后端改动 |
| `fix(security):` | 任何安全相关改动，两端都适用 |

## 复用清单

在写任何新东西之前先查这份清单。如果已经有相近的东西，就扩展它，而不是另加一个同类。
Android 路径相对于 `Android/app/src/main/java/com/droidspaces/app/`，C 路径相对于仓库根目录。

### Android：表单与可复用界面

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `ContainerConfigForm(state, onStateChange, ...)` | `ui/component/ContainerConfigForm.kt` | 任何编辑容器设置的界面。新选项加进这个文件，绝不要另写一个表单 |
| `ContainerConfigState` | `util/ContainerConfigState.kt` | 可编辑配置的唯一真相来源。新字段加在这里，不要另建平行的状态类 |
| `ContainerInfo.toConfigState()` / `.withConfig(state)` | `util/ContainerConfigState.kt` | 用容器数据预填表单，或把编辑结果写回去 |
| `InitServiceScreen(containerName, titleRes, isAvailable, fetchRows, filters, ...)` | `ui/screen/InitServiceScreen.kt` | 支持一个新的 init 系统。提供这几个 lambda 即可，不要另写界面 |
| `InitServiceRow`、`InitServiceUiStatus`、`InitCommandResult`、`InitServiceMenuAction`、`InitServiceFilterChip` | `ui/screen/InitServiceScreen.kt` | 把某个 manager 的服务列表映射进共享界面。状态颜色和筛选计数都由它们推导，因此不会出现偏差 |
| `GatewaySettingsSection(visible, config, onConfigChange, ...)` + `GatewayConfig` | `ui/component/GatewaySettingsSection.kt` | 网关网络配置区块，已嵌入配置表单 |
| `PortForwardingList(portForwards, onPortForwardsChange)` | `ui/component/PortForwardingList.kt` | 可编辑的端口转发列表，自带添加对话框 |
| `UpstreamInterfaceList(upstreamInterfaces, onInterfacesChange)` | `ui/component/UpstreamInterfaceList.kt` | 可编辑的上行网卡标签列表 |
| `DsDropdown(label, selected, options, displayName, onSelect, ...)` | `ui/component/DsDropdown.kt` | 任何下拉选择框。不要自己手写 `ExposedDropdownMenuBox` |
| `DsTextFieldDefaults.colors()` / `.surfaceColors()` | `ui/component/DsTextFieldDefaults.kt` | 每一个 `OutlinedTextField`。界面用 `colors()`，对话框内用 `surfaceColors()` |
| `FocusUtils`、`rememberClearFocus()`、`ClearFocusOnClickOutside` | `ui/util/FocusUtils.kt` | 输入法动作，以及点击空白处收起键盘 |

### Android：对话框

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `DialogFooterRow(dismissLabel, confirmLabel, onDismiss, onConfirm, ...)` | `ui/component/DialogFooterRow.kt` | 每个对话框的取消与确认按钮行。已有十处调用 |
| `FilePickerDialog(onDismiss, onConfirm, title, showFiles)` | `ui/component/FilePickerDialog.kt` | 选择主机路径或文件 |
| `EnvironmentVariablesDialog(initialContent, onConfirm, onDismiss, ...)` | `ui/component/EnvironmentVariablesDialog.kt` | 键值形式的环境变量编辑器 |
| `PrivilegedModeDialog`、`HardwareAccessDialog` | `ui/component/` | 需要手动输入确认短语的开启流程 |
| `DangerousWarningCard(title, text)` + `ConfirmPhraseField(value, onValueChange, isError)` | `ui/component/DangerousActionConfirm.kt` | 构建破坏性操作确认框。把这两个和 `DialogFooterRow` 组合起来 |
| `TerminalDialog(title, logs, onDismiss, onClear, isBlocking)` | `ui/component/TerminalDialog.kt` | 展示实时或流式的命令输出 |
| `ProgressDialog(message)` / `ErrorLogsDialog(logs)` | `ui/util/DialogUtils.kt` | 阻塞式加载框，或失败命令的日志 |
| `RootfsRepoSheet(onDismiss, onInstall)` | `ui/component/RootfsRepoSheet.kt` | rootfs 仓库浏览器，含搜索与仓库管理 |
| `BugReportDialog(onDismiss)` | `ui/component/BugReportDialog.kt` | 收集问题报告 |

有些对话框是某个界面私有的（`ui/screen/ContainersScreen.kt` 中的
`UninstallConfirmationDialog` 与 `SparseSizeDialog`，`ui/screen/ContainerTerminalScreen.kt`
中的 `UserPickerDialog`，`ui/screen/SettingsScreen.kt` 中的语言和关于对话框）。不要导入，
也不要复制它们。

### Android：底栏、脚手架与反馈

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `PrimaryActionBottomBar(label, icon, onClick, ...)` | `ui/component/PrimaryActionBottomBar.kt` | 任何向导或全屏页的"下一步""安装""继续"底栏。已有六个界面在用 |
| `PullToRefreshWrapper(onRefresh) { ... }` | `ui/component/PullToRefreshWrapper.kt` | 任何下拉刷新的列表或标签页内容 |
| `showSuccess/showError/showInfo(snackbarHostState, message)` | `ui/util/SnackbarUtils.kt` | 所有 Snackbar。不要直接调用 `showSnackbar` |

### Android：卡片与列表项

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `SettingsCard(title, onClick, icon, subtitleContent, trailing, ...)` | `ui/component/SettingsCard.kt` | 所有设置项和选项行的基础组件。新变体在它之上构建 |
| `SettingsRowCard`、`ToggleCard` | `ui/component/` | 可点击行，或开关行。两者都是 `SettingsCard` 的薄封装 |
| `SwitchItem` | `ui/component/SwitchItem.kt` | 扁平的 `ListItem` 开关行，用于设置界面。见下方重复项说明 |
| `ContainerCard(container, actions, ...)` + `ContainerCardActions` | `ui/component/ContainerCard.kt` | 可展开的容器行。新增操作加进 `ContainerCardActions`，不要新增参数 |
| `RunningContainerCard(container, onEnter, onTerminalClick, osInfo)` | `ui/component/RunningContainerCard.kt` | 控制面板上的紧凑运行中容器卡片 |
| `DroidspacesStatusCard(status, version, ...)` + `DroidspacesStatus` | `ui/component/DroidspacesStatusCard.kt` | 后端或模块状态主卡片 |
| `SystemInfoCard`、`ContainerUsersCard`、`HelpCard` | `ui/component/` | 主机资源卡片、容器用户管理、新手引导区块 |
| `EmptyState(icon, title, description)`、`ErrorState`、`RootUnavailableState` | `ui/component/EmptyState.kt` | 任何空列表、后端不可用状态或无 root 状态 |

### Android：状态与指示器

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `StatusPill(label, color)` | `ui/component/StatusPill.kt` | 任何小型状态标签 |
| `LoadingIndicator(size, color)` + `LoadingSize` | `ui/util/LoadingIndicator.kt` | 行内加载圈。选一个 `LoadingSize`，不要写裸的 `.size(n.dp)` |
| `FullScreenLoading(message)` | `ui/util/LoadingIndicator.kt` | 全屏加载状态 |
| `ContainedLoadingIndicator`、`LoadingIndicatorDefaults`、`MaterialShapes` | `ui/util/LoadingIndicator.kt` | 确定进度和形变指示器及其样式常量 |
| `TerminalConsole(logs, isProcessing, maxHeight)` | `ui/component/TerminalConsole.kt` | 行内可滚动日志视图 |
| `ShimmerAnimation(enabled) { ... }` | `ui/component/TerminalConsole.kt` | 骨架加载效果 |
| `PercentCircle(percent, size, strokeWidth, ...)` | `ui/component/PercentCircle.kt` | 圆形百分比仪表。目前没有调用点，请先复用它再考虑另写 |

### Android：主题

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `DroidspacesTheme(darkTheme, dynamicColor, amoledMode, themePalette)` | `ui/theme/Theme.kt` | 唯一的主题根，在 `MainActivity` 中应用 |
| `rememberThemeState()` + `ThemeState` | `ui/theme/ThemeStateHolder.kt` | 读取实时主题偏好 |
| `ThemePalette` | `ui/theme/Color.kt` | 新增强调色方案。只在这里加 |
| `MaterialTheme.colorScheme.*` | | 所有颜色。`ui/theme/Color.kt` 里裸的 `PRIMARY`、`GREEN`、`RED` 属于遗留常量，新代码不要用 |
| `MaterialTheme.typography.*`、`JetBrainsMono` | `ui/theme/Type.kt` | 所有文字样式，以及终端、日志和代码文本用的等宽字体 |
| `ShapeUtils` | `ui/util/DialogUtils.kt` | 圆角半径。`DIALOG_SHAPE`、`CARD_SHAPE`、`BUTTON_SHAPE` 等 |
| `AnimationUtils` | `util/AnimationUtils.kt` | 时长、缓动和 tween 规格。不要写字面量 `tween(300)` |
| `AccentColorPicker`、`ColorPaletteSwatch` | `ui/component/` | 设置里的配色选择器 |

目前没有间距常量对象。内边距直接写 dp 字面量，遵循现有约定：对话框和界面水平内边距 24.dp，
卡片内部内边距 16.dp，行间距 8.dp 或 12.dp。

### Android：导航

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `Screen` 密封类 | `ui/navigation/DroidspacesNavigation.kt` | 新增目的地。绝不要写裸的路由字符串 |
| `Screen.X.createRoute(...)` | 同上 | 构造路由。参数会自动做 URI 编码 |
| `DroidspacesNavigation(navController, ...)` | 同上 | 唯一的 `NavHost`，新界面在这里注册 |

### Android：Shell 与 root 执行

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `ContainerCommandBuilder.quote(value)` | `util/ContainerCommandBuilder.kt` | **每一个**进入 root 命令的动态值。POSIX 单引号包裹并转义内部引号 |
| `ContainerCommandBuilder.buildStart/Stop/Restart/Usage/GetIpCommand(...)` | 同上 | 调用后端二进制。不要手工拼这些命令 |
| `ContainerCommandBuilder.getConfigPath(container)` | 同上 | 容器配置文件的规范路径 |
| `ContainerOperationExecutor.executeCommand(command, operation, logger, ...)` | `util/ContainerOperationExecutor.kt` | 任何输出需要逐行流入界面的长时 root 命令 |
| `ContainerOperationExecutor.checkCommandSuccess(command)` | 同上 | 执行并只看结果，不记录日志 |
| `ContainerLogger` / `ViewModelLogger(onLog)` | `util/ContainerLogger.kt` | 所有安装器和执行器都接受的日志接收端 |
| `ContainerRuntime.scan()` | `util/ContainerRuntime.kt` | 与磁盘上的容器对账。这是访问后端二进制的既定入口 |
| `Constants.getDroidspacesCommand()` | `util/Constants.kt` | 解析 PATH 中的二进制还是完整路径 |

libsu 的全局配置在 `DroidspacesApplication.kt` 中，那是唯一应该设置它的地方。

### Android：校验与安全

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `ServiceManagerBase.isSafeServiceName(name)` | `util/ServiceManagerBase.kt` | 任何来自容器内部的服务名或单元名，在进入 shell 之前。失败即拒绝 |
| `ValidationUtils.validateContainerName` / `isSafeContainerName` / `normalizeContainerName` | `util/ValidationUtils.kt` | 容器名。先归一化再校验，两者都失败即拒绝 |
| `ValidationUtils.validateHostname` / `sanitizeHostname` | 同上 | 主机名 |
| `ValidationUtils.validateConfigValues(config)` | 同上 | 写配置之前。拒绝单行值中的控制字符 |
| `ValidationUtils.validateGatewayConfig(...)` + `GatewayErrors` | 同上 | 跨容器的网关冲突规则 |
| `ValidationUtils.effGatewayNet/Iface/Bridge` | 同上 | 推导网关默认值。它们与 C 运行时保持一致，不要自己重新推导 |
| `ValidationResult` | 同上 | 共享的成功与错误结果类型 |
| `ContainerManager.sanitizeContainerName(name)` | `util/ContainerManager.kt` | 仅用于构造路径。它**不是**安全校验函数，必须搭配 `isSafeContainerName` |

`ContainerInstaller.validateRootfsTarball` 在校验脚本缺失时失败即拒绝。
`FilePickerUtils.isValidTarball` 只检查文件扩展名，不是安全边界。

### Android：数据、仓库与偏好设置

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `ContainerManager` | `util/ContainerManager.kt` | 容器发现、配置解析、状态、更新与卸载 |
| `ContainerInfo`、`BindMount`、`PortForward`、`ContainerStatus` | 同上 | 容器数据模型。`ContainerInfo.toConfigContent()` 是唯一的配置序列化入口，绝不要手写配置行 |
| `DaemonModeRepository` | `util/DaemonModeRepository.kt` | 读写守护进程模式标志 |
| `RootfsRepository.fetchAllAssets(context)` + `RootfsAsset` | `util/RootfsRepository.kt` | 拉取官方与自定义 rootfs 仓库 |
| `PreferencesManager.getInstance(context)` | `util/PreferencesManager.kt` | 所有设置持久化。请收集 `daemonModeFlow` 和 `symlinkEnabledFlow`，不要自己注册偏好监听器 |
| `Constants` | `util/Constants.kt` | 所有路径、偏好键和默认值。绝不要重复声明字面量 |
| `ContributorManager`、`Contributor`、`Language` | `util/` | 贡献者列表与语言模型 |

### Android：设备与平台

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `DeviceArch.suffix()` / `.displayName()` | `util/DeviceArch.kt` | 任何 ABI 映射。它同时对应二进制后缀和 rootfs 仓库的架构字段 |
| `SystemInfoManager` | `util/SystemInfoManager.kt` | 内核版本、架构、Android 版本、SELinux 状态、root 方案版本、后端版本与模式，全部带缓存 |
| `RootChecker` / `RootStatus` | `util/RootChecker.kt` | root 可用性 |
| `StorageChecker` | `util/StorageChecker.kt` | 剩余空间检查 |
| `DroidspacesChecker` / `DroidspacesBackendStatus` | `util/DroidspacesChecker.kt` | 后端安装状态与更新可用性 |
| `LocaleHelper` | `util/LocaleHelper.kt` | 语言列表与切换 |
| `SELinuxChecker` | `util/SELinuxChecker.kt` | 与 `SystemInfoManager.getSELinuxStatus()` 功能重叠，优先用后者，它有缓存 |

### Android：init 系统管理器与容器信息

三个 init 管理器结构相同。容器名始终经过 `ContainerCommandBuilder` 加引号，服务名始终经过
`ServiceManagerBase` 白名单校验。

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `ContainerSystemdManager` | `util/ContainerSystemdManager.kt` | systemd 单元。同时负责单元详情与 drop-in override 的读写删。`setOverrideConf` 中的 base64 流式写入是传递任意文本的范例 |
| `ContainerOpenRCManager` | `util/ContainerOpenRCManager.kt` | OpenRC 服务 |
| `ContainerProcdManager` | `util/ContainerProcdManager.kt` | procd 服务。唯一一个连动作也做白名单校验的管理器 |
| `ContainerProcessManager` | `util/ContainerProcessManager.kt` | 容器内进程列表与结束进程 |
| `ContainerUsersManager` | `util/ContainerUsersManager.kt` | 容器用户列表，带缓存 |
| `ContainerOSInfoManager` | `util/ContainerOSInfoManager.kt` | 发行版名称、版本与图标 |
| `ContainerUsageCollector` | `util/ContainerUsageCollector.kt` | 一次调用拿到 CPU、内存、运行时长和 IP |
| `ContainerDiskUsageManager` | `util/ContainerDiskUsageManager.kt` | 稀疏镜像磁盘占用 |

原始透传入口（`executeSystemctlCommand`、`executeRCCommand`）不做名称校验，请优先使用封装
方法。

### Android：ViewModel

| 符号 | 路径 | 负责 |
| --- | --- | --- |
| `AppStateViewModel` | `ui/viewmodel/AppStateViewModel.kt` | 后端状态、root 状态、后端与模块安装 |
| `ContainerViewModel` | `ui/viewmodel/ContainerViewModel.kt` | 容器列表、计数、刷新与扫描 |
| `ContainerOperationsViewModel` | `ui/viewmodel/ContainerOperationsViewModel.kt` | 启动、停止、重启、卸载、导出、稀疏镜像迁移与扩容，以及它们的日志 |
| `ContainerInstallationViewModel` | `ui/viewmodel/ContainerInstallationViewModel.kt` | 安装向导状态，在返回栈条目间共享 |
| `ContainerUsageViewModel` | `ui/viewmodel/ContainerUsageViewModel.kt` | 实时资源占用轮询 |
| `SystemStatsViewModel` | `ui/viewmodel/SystemStatsViewModel.kt` | 各容器的系统信息 |
| `RootfsRepoViewModel` | `ui/viewmodel/RootfsRepoViewModel.kt` | 仓库列表与每个资源的下载状态 |

### Android：终端

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `TerminalSessionService` + `SessionBinder` | `service/TerminalSessionService.kt` | 创建、获取和终止终端会话。界面销毁时调用 `detachAllClients()`，避免持有 Activity |
| `TerminalSessionService.globalSessionList` | 同上 | 进程级、可被 Compose 观察的会话注册表 |
| `DroidspacesTerminalSession.create(client, containerName, containerUser)` | `ui/terminal/DroidspacesTerminalSession.kt` | 唯一会拉起容器 shell 的地方，它会重新校验容器名和用户名 |
| `TerminalBackEnd`、`TerminalScreenState`、`NoOpTerminalSessionClient` | `ui/terminal/` | 终端视图相关的胶水代码 |
| `AnsiColorParser.parseAnsi/stripAnsi` | `util/AnsiColorParser.kt` | 渲染或清理 ANSI 输出 |

### Android：rootfs 下载与安装

| 符号 | 路径 | 何时使用 |
| --- | --- | --- |
| `RootfsDownloadManager` | `util/RootfsDownloadManager.kt` | rootfs 下载的入队、轮询和取消，以及电池优化豁免 |
| `ContainerInstaller.installContainer(...)` | `util/ContainerInstaller.kt` | 从 tarball 安装。校验、检查空间、解压、写配置，失败时清理 |
| `SparseImageInstaller.extract(...)` | `util/SparseImageInstaller.kt` | 稀疏镜像的创建、格式化、挂载、解压、卸载全流程 |
| `BinaryInstaller` / `InstallationStep` | `util/BinaryInstaller.kt` | 安装后端二进制并通知守护进程 |
| `ModuleInstaller` / `ModuleInstallationStep` | `util/ModuleInstaller.kt` | 安装 Magisk 模块 |
| `SymlinkInstaller` | `util/SymlinkInstaller.kt` | 启用与关闭二进制软链接 |
| `FilePickerUtils`、`IconUtils` | `util/` | 文件名解析、发行版图标查找 |

### C 后端：日志

| 符号 | 何时使用 |
| --- | --- |
| `ds_log(fmt, ...)` | 正常进度输出 |
| `ds_warn(fmt, ...)` | 可恢复的问题。输出到 stderr，永不被静音 |
| `ds_error(fmt, ...)` | 由调用方处理的失败 |
| `ds_die(fmt, ...)` | 致命错误。记录后退出。绝不要手写 `fprintf` 加 `exit` |
| `ds_log_silent` | 在噪音大的调用前后临时静音非错误输出。记得保存并恢复 |
| `C_RED`、`C_GREEN`、`C_YELLOW`、`C_RESET` 等 | 颜色。绝不要写死转义序列 |
| `rotate_log(path, max_size)` | 按大小轮转日志 |
| `write_monitor_debug_log(name, fmt, ...)` | 从分离的 monitor 进程记录日志，只写文件 |
| `ds_open_container_log` / `ds_close_container_log` | 把 `ds_log` 输出同时写进容器日志 |
| `ds_spawn_log_relay(fd, log_file, tag)` | 给子进程输出打时间戳并写入日志目录 |

调试通道是方括号前缀，不是命令行开关。以 `[DEBUG]`、`[NET]`、`[CGROUP]`、`[SEC]`、`[IPT]`、
`[VIRT]`、`[GPU]`、`[FW]`、`[DHCP]`、`[X11]`、`[VirGL]`、`[PulseAudio]` 开头的消息会写进
日志文件但不打印到终端。请用它，不要新增 verbose 开关。

### C 后端：字符串

| 符号 | 何时使用 |
| --- | --- |
| `safe_strncpy(dst, src, size)` | `strcpy` 和 `strncpy` 的替代。NULL 安全，总是补终止符，截断时告警 |
| 带返回值检查的 `snprintf` | `sprintf` 和 `strcat` 的替代。没有封装，而 `-Wformat-truncation=2 -Werror` 意味着你必须处理截断 |
| `ds_split_flags` / `ds_free_split_flags` | 切分用户传入的 flag 字符串，会拒绝 shell 元字符 |
| `ds_parse_iface_csv` | 解析逗号分隔的网卡名列表 |
| `ds_format_uptime`、`ds_parse_size`、`ds_format_size` | 运行时长字符串，以及 `1G`、`512M` 这类字节数 |
| `sanitize_container_name`、`validate_container_name`、`reject_container_name`、`parse_and_validate_names` | 处理用户输入的容器名。`reject_container_name` 是参数解析时的一步到位版本 |
| `validate_bind_destination` | 拒绝会覆盖容器内部结构的挂载目标 |
| `ds_shell_metachars`（`src/utils.c`） | 被拒绝元字符集合的唯一来源。引用它，不要重新敲一遍 |

### C 后端：文件与路径

| 符号 | 何时使用 |
| --- | --- |
| `write_file_atomic(path, content)` | 任何需要持久化的写入。`mkstemp` 加 `fsync` 加 `rename`，这样预先埋在可预测临时名上的软链接无法得逞 |
| `write_file`、`read_file`、`write_all` | 一次性写入、读入缓冲区、EINTR 安全的完整写入。绝不要自己循环调 `write()` |
| `grep_file(path, pattern)` | 文件内子串查找 |
| `mkdir_p`、`remove_recursive`、`copy_file` | 等价于 `mkdir -p`、`rm -rf` 和流式复制 |
| `is_subpath(parent, child)` | 路径逃逸校验。基于 realpath，用它而不是 `strncmp` |
| `is_ramfs`、`is_mountpoint` | 文件系统类型与挂载点判断 |
| `build_proc_root_path(pid, suffix, buf, size)` | 构造 `/proc/<pid>/root/...` 并检查截断 |
| `ds_resolve_path_arg`、`ds_resolve_argv_paths` | 在守护进程化改变工作目录之前，把用户传入的相对路径转成绝对路径 |
| `access(path, F_OK)` | 存在性检查。没有封装，也不需要 |

新代码中每个 `open()` 都应带 `O_CLOEXEC`。当路径可能被攻击者影响时，照搬
`ds_bind_mount_socket` 的做法：用 `O_NOFOLLOW|O_CLOEXEC` 打开，然后对已打开的 inode 执行
`fchown` 和 `fchmod`，这样就没有第二次路径解析可供竞争。

### C 后端：进程与守护进程

| 符号 | 何时使用 |
| --- | --- |
| `run_command`、`run_command_quiet`、`run_command_log` | 执行外部程序。这是唯一被认可的方式。本仓库没有 `system()` |
| `ds_spawn_daemon(child_fn, user_data, log_file, tag, label)` | fork 一个长期存活的辅助进程。通过 ready 管道确认 `execv` 成功，并挂上日志中继 |
| `ds_daemon_child_preamble()` | 在这类子进程里第一个调用，此时还是 root |
| `ds_oom_protect()` | 尽力而为的 OOM 分数保护 |
| `ds_daemon_read_pid` / `write_pid` / `remove_pid`、`ds_resolve_daemon_pid` | pid 文件生命周期。读取时会检查进程是否存活 |
| `ds_global_daemon_stop(...)` | 统一的 SIGTERM、轮询、SIGKILL、回收、删除文件流程 |
| `wait_for_socket_or_death(pid, path, timeout_ms, interval_us)` | 等待套接字出现，服务端一死就提前返回。用它代替 sleep 轮询 |
| `ds_send_fd` / `ds_recv_fd` | SCM_RIGHTS 文件描述符传递 |
| `collect_pids`、`read_and_validate_pid` | 快照 `/proc`，以及读取 pid 文件并检查存活 |
| `is_external_lock_active(name)` | 检查锁文件，持有者已死的陈旧锁会被自动清除 |
| `DS_SIG_STOP`、`ds_init_type_t`、`detect_container_init()` | 优雅停止。每种 init 都有自己的停止和重启信号，在 procd 下 `SIGTERM` 表示重启 |

### C 后端：平台判定

| 符号 | 何时使用 |
| --- | --- |
| `is_android()` | 每个 Android 专用或 Linux 专用路径的强制守卫，两个方向都要 |
| `is_running_in_termux()` | Termux 环境 |
| `get_kernel_version`、`check_kernel_recommendation` | 基于 `DS_MIN_KERNEL_MAJOR` 与 `DS_MIN_KERNEL_MINOR` 的版本判定 |
| `check_ns(flag, name)` | 探测某个 `CLONE_NEW*` 命名空间是否可用 |
| `ds_cgroup_v2_usable`、`ds_cgroup_kernel_supports_v2`、`ds_cgroup_host_is_v2` | cgroup 版本判定 |
| `ds_nl_probe_nat_capability(reason, size)` | 内核网桥、veth 与 NAT 能力探测，不 fork。任何 NAT 配置前先跑它 |
| `ds_get_selinux_status()`、`is_systemd_rootfs(path)` | SELinux 模式，以及 rootfs 类型 |

### C 后端：配置

`struct ds_config` 是运行时状态的唯一持有者，以指针形式贯穿整棵调用树。它几乎全是定长字段，
另有三个堆分配的列表。

| 符号 | 何时使用 |
| --- | --- |
| `ds_config_load` / `ds_config_load_by_name` | 加载配置。优先用按名字的版本，不要自己拼路径 |
| `ds_config_save` / `ds_config_save_by_name` | 保存 |
| `ds_config_validate` | 在使用已加载的配置之前 |
| `ds_config_add_bind(cfg, src, dest, ro)` | 添加挂载点的唯一方式。会自动扩容、去重和校验 |
| `ds_config_free` | 释放。它会调用三个按列表的释放函数 |
| `sort_bind_mounts(cfg)` | 应用挂载前调用，保证父目录先于子目录挂载 |
| `parse_privileged(value, cfg)` | 把 `--privileged` 解析成 `DS_PRIV_*` 位掩码 |
| `ds_config_auto_path`、`apply_reset_config` | 推导默认配置路径，以及重置 |
| `load_etc_environment`、`ds_env_boot_setup`、`ds_env_save`、`parse_env_file_to_config` | 环境变量文件处理 |

新增配置键必须**同时**加进 `src/config.c` 的 `ds_config_load()` 和
`ds_config_serialize_known()`。漏掉后者，这个键会被当成未知行原样保留。

### C 后端：挂载、cgroup、工作目录与 seccomp

| 符号 | 何时使用 |
| --- | --- |
| `domount`、`domount_silent`、`bind_mount` | 挂载。用它们代替裸的 `mount(2)` |
| `ds_apply_jail_mask(hw_access, privileged_mask)` | 遮蔽 `/proc` 和 `/sys` 条目 |
| `setup_dev`、`create_devices`、`setup_devpts`、`ds_fix_host_ptys` | 设备节点配置 |
| `setup_custom_binds(cfg, rootfs)` | 应用 `cfg->binds` |
| `setup_volatile_overlay`、`cleanup_volatile_overlay`、`check_volatile_mode` | 易失模式 |
| `mount_rootfs_img`、`unmount_rootfs_img` | 稀疏镜像的 loop 设备生命周期 |
| `setup_cgroups`、`ds_cgroup_host_bootstrap` | cgroup 初始化 |
| `ds_cgroup_attach`、`ds_cgroup_detach`、`ds_cgroup_cleanup_container` | 把进程移入，以及清理 |
| `ds_cgroup_apply_limits`、`ds_cgroup_get_usage`、`print_cgroup_status` | 资源限制与用量 |
| `ds_cg_word_in_list(list, name)` | 判断控制器名是否在列表中。不要对控制器列表用 `strstr` |
| `get_workspace_dir`、`get_pids_dir`、`get_net_dir`、`get_logs_dir` | 构造工作目录路径的唯一认可方式。它们会在 Android 与 Linux 根目录之间切换 |
| `ensure_workspace` | 创建目录树 |
| `is_container_running`、`find_container_by_name`、`find_container_init_pid`、`is_container_init` | 容器查找 |
| `ds_feature_needs(offsetof(struct ds_config, 字段))` | 通用的特性扫描。新增全局守护进程时在这里加一个两行的封装，而不是另写扫描器 |
| `ds_seccomp_apply_minimal`、`android_seccomp_setup` | seccomp 过滤器 |
| `ds_ksu_neutralize_root_escape` | KernelSU 加固。必须在 `ds_seccomp_apply_minimal` **之前**运行，否则后者的 magic reboot 拦截会挡掉它需要的那次 reboot 调用 |
| `ds_apply_capability_hardening` | capability 丢弃 |

### C 后端：网络

`src/net/` 中的一切都直接与内核通信。主路径上没有 `ip` 或 `iptables` 命令调用。

| 符号 | 何时使用 |
| --- | --- |
| `ds_nl_open` / `ds_nl_close` | 打开所有链路、地址、路由和规则调用都需要的 netlink 上下文 |
| `ds_nl_create_bridge`、`ds_nl_create_veth`、`ds_nl_set_master`、`ds_nl_link_up/down`、`ds_nl_del_link`、`ds_nl_rename`、`ds_nl_set_mac` | 链路操作 |
| `ds_nl_add_addr4`、`ds_nl_add_route4` | 地址与路由 |
| `ds_nl_move_to_netns`、`ds_nl_move_to_netns_named` | 把网卡移入命名空间 |
| `ds_nl_add_rule4`、`ds_nl_del_rule4` | FIB 策略路由规则。优先级取自 `DS_RULE_PRIO_TO_SUBNET`、`DS_RULE_PRIO_TETHER`、`DS_RULE_PRIO_FROM_SUBNET`，必须高于 OEM 保留区间、低于 Android 的 VPN 区间 |
| `ds_nl_get_iface_table`、`ds_nl_get_table_default_oif`、`ds_nl_get_android_default` | 路由表信息读取 |
| `ds_nl_flush_stale_veths`、`ds_nl_list_ifaces`、`ds_nl_count_ifaces_with_prefix` | 枚举与回收 |
| `ds_ipt_ensure_masquerade`、`ds_ipt_ensure_forward_accept`、`ds_ipt_ensure_input_accept`、`ds_ipt_ensure_mss_clamp` | 写入 netfilter 规则 |
| `ds_ipt_host_rules_present(iface, src_cidr, expect_dnat)` | 对整套主机规则的不 fork 探测。路由监视器据此决定是否重新写入 |
| `ds_ipt_remove_iface_rules`、`ds_ipt_remove_ds_rules` | 清理 |
| `ds_ipt_add_portforwards`、`ds_ipt_remove_portforwards` | 端口转发 |
| `parse_cidr(cidr, ip_out, mask_out)` | 共享的 CIDR 拆分函数 |
| `fix_networking_host`、`fix_networking_rootfs`、`setup_veth_host_side`、`setup_veth_child_side_named`、`setup_gateway_veth_side` | 网络建立 |
| `ds_net_start_route_monitor`、`ds_net_mark_local_forward_active` | 在 netd 清掉规则后重新写回的对账器 |
| `ds_net_cleanup`、`ds_net_gateway_teardown`、`ds_net_rewire_gateway_clients` | 拆除与网关客户端重连 |
| `ds_net_validate_static_ip`、`ds_net_check_ip_collision`、`ds_net_resolve_static_ip` | 静态 NAT IP 处理。调用 resolve 之后必须保存配置才能持久化结果 |
| `ds_dhcp_server_start`、`ds_dhcp_server_stop` | 单租约 DHCP 服务。拆除 veth 之前先停它，否则接收会阻塞 |
| `ds_get_dns_servers`、`detect_ipv6_in_container`、`ds_net_disable_tx_checksum` | DNS、IPv6 检测、校验和卸载 |

### C 后端：安全守卫

| 符号 | 契约 |
| --- | --- |
| `ds_peer_authorized(fd, group_name)` | 全仓库唯一的授权入口。允许 root 或指定组的成员，但前提是对端与我们处于同一 PID 命名空间。每一条失败路径都拒绝。调用方是 `src/daemon.c` 和 `src/socketd_bridge.c` |
| `ds_peer_in_pidns(peer_pid)` | 必须失败即拒绝。pid 为 0 表示对端无法映射进我们的命名空间，readlink 失败表示我们无法证明其归属，两种情况都拒绝。这里一旦失败即放行，调用方就能复用已死的 pid 逃逸到主机 root |
| `ds_bind_mount_socket(src, dst, uid, label)` | 向容器可控目录写入时防软链接竞态的标准做法 |
| `is_dangerous_node(name)` | 设备节点黑名单 |
| `set_selinux_context`、`get_selinux_context`、`ds_selinux_dyntransition`、`ds_selinux_enter_domain`、`ds_drop_privileges`、`ds_resolve_termux_uid` | SELinux 与权限处理 |

任何安全检查上写着"无法确定，那就放行"的注释都是 Bug。在信任边界上无法证明，就意味着拒绝。

## 已知重复：不要再加第三份

以下问题目前确实存在，已列入清理计划。请扩展共享版本，不要再加一份。

- `JetBrainsMono` 被声明了四次：`ui/theme/Type.kt` 里的规范版本，加上 `InitServiceScreen.kt`、
  `UnitDetailScreen.kt` 和 `OverrideEditorScreen.kt` 里的私有副本。请导入主题里的那个。
- 没有共享的 `DsDialog`。同样的 `Dialog { Surface { ... } }` 结构在大约十六处被手工重写。
  如果你需要对话框，参照现有实现并在 PR 中说明，更好的做法是把共享组件抽出来。
- 四处局部的 `RoundedCornerShape` 对话框常量应该改成 `ShapeUtils.DIALOG_SHAPE`。
- `ToggleCard` 和 `SwitchItem` 是同一种开关行的两种形态。
- `ContainersScreen` 里内联了一份输入确认短语的逻辑，而 `ConfirmPhraseField` 已经提供了。
- `SummaryItem` 在 `InstallationSummaryScreen.kt` 中以三个私有重载存在。在第二个界面需要它
  之前，先把它提升到 `ui/component/`。
- 若干安装器和检查器仍然用字面引号甚至不加引号把路径拼进 shell 字符串（`BinaryInstaller`、
  `ContainerInstaller`、`SparseImageInstaller`、`ModuleInstaller`、`SymlinkInstaller`）。
  它们已在清理列表上。不要复制这种写法，请用 `ContainerCommandBuilder.quote()`。
- `AppStateViewModel`、`ContainerOperationsViewModel` 和 `FilePickerDialog` 直接执行 root
  命令，这是错误的层次。新的 root 调用应经由仓库层或 `ContainerOperationExecutor`。
- 若干 `util` 单例持有可变缓存和 Compose 状态，这些本应属于 ViewModel
  （`ContainerOSInfoManager`、`ContainerUsersManager`、`ContainerDiskUsageManager`、
  `SystemInfoManager`、`TerminalScreenState`）。

## 新增东西之前

1. 先查上面的清单，再 grep 一遍代码。通常已经有相近的东西。
2. 扩展共享组件。给一个组件加一个参数，好过再加一个同类组件。
3. 如果你准备复制一段代码只改两个字段，那就把它参数化。
4. 在所有调用方汇聚的那个点修 Bug，而不是在问题报告恰好提到的那一处。
5. 绝不要再造第二种拼 shell 命令的方式。
6. 新状态放进 ViewModel，不要放进 Composable，也不要放进 `util` 对象。

## PR 要求

每个功能 PR 必须包含：

1. **对所解决真实问题的清晰描述。** "我就想要这个"不是有效的问题陈述。请说明在真实硬件上
   对真实用户来说，什么会出问题、什么会失败或缺少什么。

2. **截图或终端输出**，展示功能按预期运行。

3. **明确的已测试环境列表。** 对于 Android：设备名称、SoC、内核版本以及 OEM 或 Android
   版本。对于 Linux：发行版、内核版本以及架构。

4. **没有回归。** 用你的改动跑一遍现有行为。如果之前能用的功能现在不行了，先修好再发 PR。

## 代码所有权

如果你的功能被合入，你从此就要对它负责。

当新的内核版本、新的 SoC 特有行为或平台行为变更导致你的贡献出问题时，你应当负责解决。如果
你提交的功能开始引发问题，而你无法联系到或不愿维护它，该功能将被移除。

用户不知道是谁写的某个功能。当事情出问题时，他们只会责怪项目。在提交代码之前，请确保你理解
你的代码在做什么。如果你无法解释为什么要做某个特定的实现选择，那么这个选择就不应该进入
生产环境。

## 哪些会被合入

- 解决真实问题、能在内核 3.10+ 上工作、正确做了平台守卫、并在多种环境下得到验证的功能。
- 有清晰复现用例和已验证解决方案的 Bug 修复。
- 安全改进。这类改动永远欢迎。
- 有可衡量且不引入回归的性能改进。
- 删除。移除重复代码或无用的灵活性同样是贡献。
- 文档修正。

## 哪些会被拒绝

- 仅能在内核 5.x+ 上工作且无回退方案的补丁。
- 解决的问题没有真实用户报告过，或无法在狭隘的硬件或平台配置之外复现。
- 未用 `is_android()` 守卫的 Android 专用或 Linux 专用代码。
- 作者在评审中无法解释或辩护的代码。
- 破坏 Android 8 兼容性的 App 改动。
- 与上面清单中已有条目重复的新组件或新辅助函数。
- 任何未加引号、也未过白名单就进入 root shell 的动态值。
- 任何引入回归的改动，无论新行为多么有用。

## 重复被拒

如果一位贡献者提交的多个 PR 因为同样的原因被拒绝，比如功能解决不了真实问题、不满足通用性
要求、或者给代码库增加不必要的复杂度，他将被禁止继续贡献。

没有固定的次数上限。判断标准是模式识别：如果明显能看出贡献者没有阅读反馈、没有认真测试，
或者在故意堆砌代码，是否封禁由维护者裁量，且为最终决定。

## 安全漏洞

安全修复和加固补丁永远欢迎，并会被优先评审。

如果你发现了漏洞，尤其是**在非硬件访问模式下、不带特权标志即可复现的容器逃逸**，请不要
公开提 issue。

请私下报告：

- **邮箱：** droidcasts@protonmail.com
- **Telegram：** [t.me/ravindu](https://t.me/ravindu)

请附上复现用例、受影响的配置，以及相关的内核或 SoC 细节。公开披露应等到修复发布之后。

## 流程

1. Fork 仓库，并在专门的分支上工作。
2. 带着上述信息向 `main` 发起 PR。
3. 评审期间请保持响应。长期无响应的 PR 会被关闭。
4. 直接在原 PR 上处理评审意见，不要为同一个改动另开新 PR。

本项目没有正式的 CLA。提交 PR 即表示你同意你的贡献可以按项目现有许可证分发。

在本仓库工作的 AI Agent 请阅读 [AGENTS.md](../../AGENTS.md)，那是上述规则的精简版。
