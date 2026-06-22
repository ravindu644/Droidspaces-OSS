/*
 * DroidSpaces Image Creation
 *
 * Implements the "create" command for generating an ext4 rootfs image
 * from a rootfs archive.
 *
 * Workflow:
 *   Create image -> Format ext4 -> Mount -> Extract archive
 *   -> Sync -> Unmount -> Cleanup
 */
#include "droidspace.h"

int ds_create_image(const char *archive, const char *image, const char *size) {
    off_t   bytes;
    char    loop_dev[64]   = {0};
    char    mount_pt[256]  = {0};
    int     mounted        = 0;
    int     ret            = -1;

    /* ------------------------------------------------------------------ */
    /* Validations                                                         */
    /* ------------------------------------------------------------------ */
    if (getuid() != 0) {
        ds_error("ds_create_image requires root");
        return -1;
    }
    if (!archive || !*archive) {
        ds_error("Missing rootfs archive");
        return -1;
    }
    if (!image || !*image) {
        ds_error("Missing image path");
        return -1;
    }
    if (!size || !*size) {
        ds_error("Missing image size");
        return -1;
    }
    if (access(archive, F_OK) != 0) {
        ds_error("Archive not found: %s", archive);
        return -1;
    }
    if (!is_valid_archive(archive)) {
        ds_error("Not a valid tar archive: %s", archive);
        return -1;
    }
    if (parse_size(size, &bytes) < 0) {
        ds_error("Invalid size: %s", size);
        return -1;
    }
    if (access(image, F_OK) == 0) {
        ds_error("Image already exists: %s", image);
        return -1;
    }

    /* ------------------------------------------------------------------ */
    /* Step 1: Create sparse image file                                    */
    /* ------------------------------------------------------------------ */
    ds_log("Creating image: %s (%s)", image, size);
    if (create_sparse_image(image, bytes) < 0)
        return -1;

    /* ------------------------------------------------------------------ */
    /* Step 2: Format ext4                                                 */
    /* ------------------------------------------------------------------ */
    ds_log("Formatting ext4 filesystem");
    const char *mkfs[] = {
        "mkfs.ext4",
        "-L " DS_PROJECT_NAME,
        "-F",
        image,
        NULL
    };
    if (run_cmd(mkfs) != 0) {
        ds_error("mkfs.ext4 failed");
        goto err_unlink;
    }

    /* ------------------------------------------------------------------ */
    /* Step 3: Attach loop device                                         */
    /* ------------------------------------------------------------------ */
    ds_log("Attaching loop device");
    if (attach_loop(image, loop_dev, sizeof(loop_dev)) < 0)
        goto err_unlink;

    /* ------------------------------------------------------------------ */
    /* Step 4: Create mount point and mount                               */
    /* ------------------------------------------------------------------ */
    if (make_mount_point(mount_pt, sizeof(mount_pt)) < 0)
        goto err_detach;

    ds_log("Mounting %s -> %s", loop_dev, mount_pt);
    if (mount(loop_dev, mount_pt, "ext4", 0, NULL) != 0) {
        ds_error("mount(%s, %s): %s", loop_dev, mount_pt, strerror(errno));
        rmdir(mount_pt);
        goto err_detach;
    }
    mounted = 1;

    /* ------------------------------------------------------------------ */
    /* Step 5: Extract archive                                            */
    /* ------------------------------------------------------------------ */
    ds_log("Extracting archive: %s", archive);
    if (extract_archive(archive, mount_pt) != 0) {
        ds_error("Archive extraction failed");
        goto err_unmount;
    }

    /* ------------------------------------------------------------------ */
    /* Step 6: Sync                                                       */
    /* ------------------------------------------------------------------ */
    ds_log("Syncing filesystem");
    sync();

    ds_log("Image created successfully: %s", image);
    ret = 0;

    /* ------------------------------------------------------------------ */
    /* Cleanup                                                            */
    /* ------------------------------------------------------------------ */
err_unmount:
    if (mounted) {
        if (umount2(mount_pt, 0) != 0)
            ds_warn("umount2(%s): %s", mount_pt, strerror(errno));
        rmdir(mount_pt);
    }
err_detach:
    detach_loop(loop_dev);
err_unlink:
    if (ret != 0)
        unlink(image);
    return ret;
}

/* --------------------------------------------------------------------------
 * Archive validation: check magic bytes, not filename extension.
 *
 * Recognised magic:
 *   xz:    FD 37 7A 58 5A 00
 *   gzip:  1F 8B
 *   bzip2: 42 5A 68
 *   tar:   "ustar" at offset 257 (POSIX) or offset 265 (GNU)
 * -------------------------------------------------------------------------- */

int is_valid_archive(const char *path) {
    unsigned char buf[512] = {0};
    int  fd;
    ssize_t n;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n < 6)
        return 0;

    /* xz */
    if (buf[0] == 0xFD && buf[1] == '7' && buf[2] == 'z' &&
        buf[3] == 'X'  && buf[4] == 'Z' && buf[5] == 0x00)
        return 1;

    /* gzip */
    if (buf[0] == 0x1F && buf[1] == 0x8B)
        return 1;

    /* bzip2 */
    if (buf[0] == 'B' && buf[1] == 'Z' && buf[2] == 'h')
        return 1;

    /* bare tar: "ustar" at offset 257 */
    if (n >= 262 && memcmp(buf + 257, "ustar", 5) == 0)
        return 1;

    return 0;
}

/* --------------------------------------------------------------------------
 * Archive extraction
 *
 * Linux:   system tar handles xz natively via -J flag.
 * Android: toybox tar has no xz support and no system xz binary.
 *          Use bundled busybox
 * -------------------------------------------------------------------------- */
int extract_archive(const char *archive, const char *dest) {
    const char *args[8];
    int i = 0;

    if (is_android()) {
        args[i++] = DS_BUSYBOX;
        args[i++] = "tar";
    } else {
        args[i++] = "tar";
    }

    args[i++] = "-xpJf";
    args[i++] = archive;
    args[i++] = "-C";
    args[i++] = dest;
    args[i++] = "--numeric-owner";
    args[i++] = NULL;

    return run_cmd(args);
}

/* --------------------------------------------------------------------------
 * Loop device helpers
 * -------------------------------------------------------------------------- */

int attach_loop(const char *image, char *loop_out, size_t loop_sz) {
    int  ctrl_fd  = -1;
    int  img_fd   = -1;
    int  loop_fd  = -1;
    int  free_num = -1;

    ctrl_fd = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (ctrl_fd < 0) {
        ds_error("open(/dev/loop-control): %s", strerror(errno));
        return -1;
    }

    free_num = ioctl(ctrl_fd, LOOP_CTL_GET_FREE);
    close(ctrl_fd);
    if (free_num < 0) {
        ds_error("LOOP_CTL_GET_FREE: %s", strerror(errno));
        return -1;
    }

    snprintf(loop_out, loop_sz,
         is_android() ? "/dev/block/loop%d" : "/dev/loop%d",
         free_num);

    img_fd = open(image, O_RDWR | O_CLOEXEC);
    if (img_fd < 0) {
        ds_error("open(%s): %s", image, strerror(errno));
        return -1;
    }

    loop_fd = open(loop_out, O_RDWR | O_CLOEXEC);
    if (loop_fd < 0) {
        ds_error("open(%s): %s", loop_out, strerror(errno));
        close(img_fd);
        return -1;
    }

    if (ioctl(loop_fd, LOOP_SET_FD, img_fd) < 0) {
        ds_error("LOOP_SET_FD(%s): %s", loop_out, strerror(errno));
        close(loop_fd);
        close(img_fd);
        return -1;
    }

    /* Optional but good practice: set loop info */
    struct loop_info64 info;
    memset(&info, 0, sizeof(info));
    strncpy((char *)info.lo_file_name,
            image,
            sizeof(info.lo_file_name) - 1);
    ioctl(loop_fd, LOOP_SET_STATUS64, &info); /* non-fatal if it fails */

    close(loop_fd);
    close(img_fd);
    return 0;
}

void detach_loop(const char *loop_dev) {
    if (!loop_dev || !*loop_dev)
        return;
    int fd = open(loop_dev, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return;
    if (ioctl(fd, LOOP_CLR_FD, 0) < 0)
        ds_warn("LOOP_CLR_FD(%s): %s", loop_dev, strerror(errno));
    close(fd);
}

/* --------------------------------------------------------------------------
 * Mount point: Based on OS
 * -------------------------------------------------------------------------- */

int make_mount_point(char *path_out, size_t size) {
    const char *base = is_android() ? "/data/local/tmp/container-XXXXXX"
                                    : "/tmp/container-XXXXXX";
    snprintf(path_out, size, "%s", base);

    if (!mkdtemp(path_out)) {
        ds_error("mkdtemp(%s): %s", base, strerror(errno));
        return -1;
    }
    return 0;
}


/* --------------------------------------------------------------------------
 * Sparse image creation
 * -------------------------------------------------------------------------- */

int create_sparse_image(const char *image, off_t bytes) {
    int fd = open(image, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        ds_error("open(%s): %s", image, strerror(errno));
        return -1;
    }
    if (ftruncate(fd, bytes) < 0) {
        ds_error("ftruncate(%s): %s", image, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}


