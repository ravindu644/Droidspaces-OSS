# Linux Installation Guide

This guide covers how to install Droidspaces on a Linux desktop or server.

## Prerequisites

Most modern Linux distributions already include everything Droidspaces needs. No special kernel configuration or additional packages are required.

**Requirements:**
- Root privileges (`sudo`)

## Step 1: Download the Release

Go to the [latest release page](https://github.com/ravindu644/Droidspaces-OSS/releases/latest) and download the **Droidspaces Backend Tarball**.

Alternatively, download it from the command line:

```bash
# Replace VERSION with the actual version (e.g., v4.3.0)
wget https://github.com/ravindu644/Droidspaces-OSS/releases/download/VERSION/droidspaces-vVERSION-DATE.tar.gz
```

## Step 2: Extract and Install

### Identify Your Architecture

```bash
uname -m
```

This will output one of: `x86_64`, `aarch64`, `armv7l` (armhf), or `i686` (x86).

### Install the Binary

```bash
# Extract the tarball
tar xzf droidspaces-v*.tar.gz

# Navigate into the extracted directory
cd droidspaces-v*/

# Copy the binary for your architecture to a directory in your PATH
sudo cp x86_64/droidspaces /usr/local/bin/droidspaces

# Make it executable
sudo chmod +x /usr/local/bin/droidspaces
```

## Step 3: Verify Installation

Run the system requirements checker:

```bash
sudo droidspaces check
```

All checks should pass with green checkmarks. On a modern Linux desktop, everything should be supported out of the box.

## Step 4: Get a Rootfs

You need a Linux root filesystem to create your first container. We recommend using the official **Linux Containers image repository**, which provides clean, pre-built rootfs tarballs for dozens of distributions.

**Download URL**: [images.linuxcontainers.org/images/](https://images.linuxcontainers.org/images/)

Navigate to your desired distribution (e.g., `ubuntu/noble/amd64/default/`) and download the `rootfs.tar.xz` file.

> [!IMPORTANT]
> You **must** use `sudo` when extracting the rootfs tarball in both methods below. This ensures that file ownership (`UID 0`) and special setuid permissions are preserved. Failure to do so will result in a broken container where commands like `sudo` won't function (because the binaries won't be owned by root inside the environment), and system services may fail to start.

---

### Option A: Use a Directory Rootfs
Simply extract the tarball to a directory:

```bash
mkdir my-container
sudo tar -xvf rootfs.tar.xz -C my-container
```

### Option B: Create an ext4 Image (Recommended)
Encapsulating your rootfs in a single `.img` file provides better portability and avoids host filesystem conflicts.

1. **Create a sparse image file** (e.g., 16GB):
   ```bash
   truncate -s 16G rootfs.img
   ```

2. **Format it as ext4**:
   ```bash
   mkfs.ext4 -L Droidspaces rootfs.img
   ```

3. **Mount the image**:
   ```bash
   mkdir -p rootfs_mount
   sudo mount rootfs.img rootfs_mount
   ```

4. **Extract the rootfs tarball to the mountpoint**:
   ```bash
   sudo tar -xvf /path/to/rootfs.tar.xz -C rootfs_mount
   ```

5. **Unmount and clean up**:
   ```bash
   sudo umount rootfs_mount
   rmdir rootfs_mount
   ```

Now you can boot it instantly using:
`sudo droidspaces --name=my-container --rootfs-img=rootfs.img start`

## Next Steps

- [Linux CLI Usage Guide](Usage-Linux-CLI.md) for getting started with containers
- [CLI Reference](CLI-Reference.md) for a complete list of commands and flags
- [Feature Deep Dives](Features.md) for detailed explanations of each feature
