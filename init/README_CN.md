# Droidspaces 系统集成（init.rc）

将 Droidspaces 原生集成到 Android 的 `init.rc` 中，可以在最底层实现**守护进程模式**。这使容器能够独立于用户空间 App 启动，支持开机自启，并确保容器持久运行（不会被 Android OOM killer 杀死——若被杀会自动重启）。

## 前提条件

- **工具**：你必须使用 [Android_Image_Tools](https://github.com/ravindu644/Android_Image_Tools) 来解包/重新打包你的 `vendor.img`。
- **AVB/dm-verity**：你必须[禁用 AVB（Android 验证启动）](https://ravindu644.medium.com/how-i-edited-the-android-vendor-boot-img-to-remove-avb-and-add-ext4-support-ae16ef98ef46)才能启动修改过的 vendor 镜像。这通常涉及从你的 `fstab`（在 `boot` 或 `vendor_boot` ramdisk 中）中移除 `avb` 标志。
- **准备**：在尝试修改之前，请确保一个纯净、重新打包的 `vendor.img`（解包后不做任何修改再重新打包）能够成功启动。

---

## 集成方法

根据你对灵活性与独立性的偏好，选择以下方法中的**一种**。

### 方法 A：符号链接集成（推荐）
此方法在 `/vendor/bin/droidspaces` 处创建一个指向 `/data/local/Droidspaces/bin/droidspaces` 的符号链接。强烈推荐此方式，因为它允许你通过 App 更新 `droidspaces` 二进制文件，而无需重新刷写 `vendor.img`。

1. 使用镜像工具**解包 `vendor.img`**。
2. **配置符号链接**：
    - 进入解包输出目录中的 `.repack_info` 隐藏文件夹。
    - 将 [symlink-configuration/symlink_info.txt](./android-service/symlink-configuration/symlink_info.txt) 的内容追加到现有的 `symlink_info.txt`。
    - 将 [symlink-configuration/fs-config.txt](./android-service/symlink-configuration/fs-config.txt) 的内容追加到现有的 `fs-config.txt`。
    - 将 [symlink-configuration/file_contexts.txt](./android-service/symlink-configuration/file_contexts.txt) 的内容追加到现有的 `file_contexts.txt`。
3. **应用 SELinux 策略**：
    - 将 [symlink-configuration/droidspaces_symlink.cil](./android-service/symlink-configuration/droidspaces_symlink.cil) 的内容追加到你的 `<解包文件夹>/etc/selinux/vendor_sepolicy.cil`。

---

### 方法 B：二进制文件集成（独立式）
此方法直接将原始的 `droidspaces` 二进制文件放入 `/vendor` 分区。它更加自包含，但更新二进制文件时需要重新刷写。

1. **解包 `vendor.img`**。
2. **移入二进制文件**：将 `droidspaces` 二进制文件（arm64 静态编译）复制到 `<解包文件夹>/vendor/bin/droidspaces`。
3. **配置权限**：
    - 进入 `.repack_info` 隐藏文件夹。
    - 将 [binary-configuration/fs-config.txt](./android-service/binary-configuration/fs-config.txt) 的内容追加到 `fs-config.txt`。
    - 将 [binary-configuration/file_contexts.txt](./android-service/binary-configuration/file_contexts.txt) 的内容追加到 `file_contexts.txt`。
4. **应用 SELinux 策略**：
    - 将 [binary-configuration/droidspaces_binary.cil](./android-service/binary-configuration/droidspaces_binary.cil) 的内容追加到你的 `<解包文件夹>/etc/selinux/vendor_sepolicy.cil`。

---

## 共享的服务配置

无论选择哪种方法，你都必须执行以下步骤来注册服务：

1. **注册 init 脚本**：
   将 [init.droidspaces.rc](./android-service/vendor/etc/init/init.droidspaces.rc) 复制到 `<解包文件夹>/etc/init/`。
2. **添加自启脚本**：
   将 [droidspaces_autoboot.sh](./android-service/vendor/bin/droidspaces_autoboot.sh) 复制到 `<解包文件夹>/vendor/bin/`。
3. **重新打包并刷写**：
   使用镜像工具重新打包 `vendor.img` 并通过 fastboot 刷写：
   ```bash
   fastboot flash vendor vendor.img
   ```

---

## 故障排除与注意事项

> [!IMPORTANT]
> **文件系统一致性**：始终使用与原始镜像相同的文件系统类型进行重新打包（使用 `file vendor.img` 检查）。

- **静默 SELinux 日志刷屏（强烈推荐）**：
    虽然不是功能实现所必需，但我们强烈建议应用 [selinux-testing/ds_log_spam_fix.cil](./android-service/selinux-testing/ds_log_spam_fix.cil) 中的规则。
    - **好处**：这可以减少 Droidspaces 在你的 `dmesg` 中产生的约 **90% 的 AVC 拒绝记录**，保持系统日志干净整洁。
    - **风险**：如下文所述，如果你的宿主机 SELinux 策略缺少某些域，应用此文件是引发 bootloop 的最常见原因。

- **验证**：设备启动后，验证 Droidspaces 服务是否处于活跃状态：
    ```bash
    getprop init.svc.droidspacesd
    ```

- **Bootloop 问题**：如果修改后设备进入 bootloop，有 **90% 的可能性** 是由 `ds_log_spam_fix.cil` 文件导致的。当我们的 CIL 中定义的某个域在你的宿主机 SELinux 策略中找不到时，就会在启动过程中触发内核 panic。
    - **如何修复**：抓取启动日志。`secilc`（SELinux CIL 编译器）二进制文件通常会指出导致错误的准确行号。从 CIL 文件中移除该错误行，重新打包，然后再次尝试，直到设备成功启动。
    - **日志**：如果你无法获取实时串行日志，请在崩溃后拉取 `/proc/last_kmesg` 或 `console-ramoops` 来查看 panic 信息。
