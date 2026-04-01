# Droidspaces System Integration (init.rc)

Integrating Droidspaces natively into Android's `init.rc` allows for **Daemon Mode** at the deepest level. This enables containers to start independently of the userspace app, facilitates auto-start on boot, and ensures your containers remain persistent (unkillable by Android's OOM killer - auto-spawn if killed).

## Prerequisites

- **Tooling**: You must use [Android_Image_Tools](https://github.com/ravindu644/Android_Image_Tools) to unpack/repack your `vendor.img`.
- **AVB/dm-verity**: You must [disable AVB (Android Verified Boot)](https://ravindu644.medium.com/how-i-edited-the-android-vendor-boot-img-to-remove-avb-and-add-ext4-support-ae16ef98ef46) to boot a modified vendor image. This typically involves removing `avb` flags from your `fstab` (in `boot` or `vendor_boot` ramdisk).
- **Preparation**: Ensure a pure, repacked `vendor.img` (unpacked and repacked without changes) boots successfully before attempting modifications.

---

## Integration Methods

Choose **one** of the following methods based on your preference for flexibility vs. self-containment.

### Method A: Symlink Integration (Recommended)
This method creates a symlink at `/vendor/bin/droidspaces` pointing to `/data/local/Droidspaces/bin/droidspaces`. This is highly recommended as it allows you to update the `droidspaces` binary via the app without re-flashing your `vendor.img`.

1. **Unpack `vendor.img`** using the image tools.
2. **Configure Symlink**:
    - Navigate to the `.repack_info` hidden folder in your unpacked output.
    - Append the contents of [symlink-configuration/symlink_info.txt](./android-service/symlink-configuration/symlink_info.txt) to the existing `symlink_info.txt`.
    - Append the contents of [symlink-configuration/fs-config.txt](./android-service/symlink-configuration/fs-config.txt) to the existing `fs-config.txt`.
    - Append the contents of [symlink-configuration/file_contexts.txt](./android-service/symlink-configuration/file_contexts.txt) to the existing `file_contexts.txt`.
3. **Apply SELinux Policy**:
    - Append the contents of [symlink-configuration/droidspaces_symlink.cil](./android-service/symlink-configuration/droidspaces_symlink.cil) to your `<Unpacked folder>/etc/selinux/vendor_sepolicy.cil`.

---

### Method B: Binary Integration (Standalone)
This method places the raw `droidspaces` binary directly into the `/vendor` partition. It is more self-contained but requires a re-flash for binary updates.

1. **Unpack `vendor.img`**.
2. **Move Binary**: Copy the `droidspaces` binary (arm64 static) to `<Unpacked folder>/vendor/bin/droidspaces`.
3. **Configure Permissions**:
    - Navigate to the `.repack_info` hidden folder.
    - Append the contents of [binary-configuration/fs-config.txt](./android-service/binary-configuration/fs-config.txt) to `fs-config.txt`.
    - Append the contents of [binary-configuration/file_contexts.txt](./android-service/binary-configuration/file_contexts.txt) to `file_contexts.txt`.
4. **Apply SELinux Policy**:
    - Append the contents of [binary-configuration/droidspaces_binary.cil](./android-service/binary-configuration/droidspaces_binary.cil) to your `<Unpacked folder>/etc/selinux/vendor_sepolicy.cil`.

---

## Shared Service Configuration

Regardless of the method chosen above, you must perform these steps to register the service:

1. **Register Init Script**:
   Copy [init.droidspaces.rc](./android-service/vendor/etc/init/init.droidspaces.rc) to `<Unpacked folder>/etc/init/`.
2. **Add Autoboot Script**:
   Copy [droidspaces_autoboot.sh](./android-service/vendor/bin/droidspaces_autoboot.sh) to `<Unpacked folder>/vendor/bin/`.
3. **Repack & Flash**:
   Repack the `vendor.img` using the image tool and flash it via fastboot:
   ```bash
   fastboot flash vendor vendor.img
   ```

---

## Troubleshooting & Notes

> [!IMPORTANT]
> **Filesystem Consistency**: Always repack using the same filesystem type as the original image (check using `file vendor.img`).

- **Silencing SELinux Spam (Highly Recommended)**:
    While not strictly necessary for functionality, we strongly recommend applying the rules in [selinux-testing/ds_log_spam_fix.cil](./android-service/selinux-testing/ds_log_spam_fix.cil).
    - **Benefit**: This reduces approximately **90% of AVC denials** caused by Droidspaces in your `dmesg`, keeping your system logs clean.
    - **Risk**: As detailed below, applying this file is the most common cause of bootloops if your host SELinux policy lacks certain domains.

- **Verification**: Once booted, verify that the Droidspaces service is active:
    ```bash
    getprop init.svc.droidspacesd
    ```

- **Bootloops**: If the device bootloops after modification, there is a **90% probability** it is caused by the `ds_log_spam_fix.cil` file. This occurs when a domain defined in our CIL is not found in your host's SELinux policy, triggering a kernel panic during boot.
    - **How to Fix**: Capture the boot logs. The `secilc` (SELinux CIL Compiler) binary will typically point to the exact line number causing the error. Remove the faulty line from the CIL file, repack, and try again until the device boots successfully.
    - **Logs**: If you can't get a live serial log, pull `/proc/last_kmesg` or `console-ramoops` after the crash to inspect the panic.

