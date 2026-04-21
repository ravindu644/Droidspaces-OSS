/*
 * Droidspaces v5 - Resource Virtualization Layer
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "virtualize.h"
#include <time.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* In-place overwrite for bind-mounted files (rename breaks bind mounts) */
static int write_file_inplace(const char *path, const char *content) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    size_t len = strlen(content);
    if (write_all(fd, content, len) != (ssize_t)len) {
        close(fd);
        return -1;
    }

    if (ftruncate(fd, (off_t)len) < 0) {
        /* Ignore error or handle */
    }

    close(fd);
    return 0;
}

/* Helper to read cgroup v2 memory.stat */
static void get_cgroup_v2_mem_stat(struct ds_config *cfg, long long *anon, long long *file, long long *slab) {
    char path[PATH_MAX * 2];
    char buf[4096];

    /* Try common locations for memory.stat - using explicit snprintf calls
     * to avoid -Werror=format-nonliteral. */
    snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/memory.stat", cfg->container_name);
    if (access(path, R_OK) != 0)
        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/memory.stat", cfg->container_name);
    if (access(path, R_OK) != 0)
        snprintf(path, sizeof(path), "/sys/fs/cgroup/memory/droidspaces/%s/memory.stat", cfg->container_name);

    if (read_file(path, buf, sizeof(buf)) > 0) {
        char *p;
        if ((p = strstr(buf, "anon "))) (void)sscanf(p + 5, "%lld", anon);
        if ((p = strstr(buf, "file "))) (void)sscanf(p + 5, "%lld", file);
        if ((p = strstr(buf, "slab "))) (void)sscanf(p + 5, "%lld", slab);
    }
}

/* Get PID namespace inode for a given PID */
unsigned long ds_get_pid_ns_inode(pid_t pid) {
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/proc/%d/ns/pid", pid);
    if (stat(path, &st) == 0) return (unsigned long)st.st_ino;
    return 0;
}

/*
 * Generate virtualized /proc/meminfo
 */
int ds_virtualize_meminfo(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    long long mem_limit = -1, mem_usage = -1;
    ds_cgroup_get_limits(cfg, &mem_limit, NULL, NULL, NULL);
    ds_cgroup_get_usage(cfg, &mem_usage, NULL, NULL);

    if (mem_usage < 0) mem_usage = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    long long host_total = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lld", &host_total) == 1) break;
    }
    rewind(f);

    /* Fallback to requested limit if cgroup enforcement is not readable */
    if (mem_limit <= 0 && cfg->memory_limit > 0) {
        mem_limit = cfg->memory_limit;
    }

    double ratio = 1.0;
    if (mem_limit > 0 && host_total > 0) {
        ratio = (double)mem_limit / (host_total * 1024.0);
    }

    long long cg_anon = -1, cg_file = -1, cg_slab = -1;
    get_cgroup_v2_mem_stat(cfg, &cg_anon, &cg_file, &cg_slab);

    size_t cap = 16384;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    size_t offset = 0;
    while (fgets(line, sizeof(line), f)) {
        if (offset + 1024 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }

        char key[256];
        long long val;
        int has_kb = strstr(line, " kB") != NULL;
        if (sscanf(line, "%255[^:]: %lld", key, &val) == 2) {
            if (mem_limit > 0) {
                if (strcmp(key, "MemTotal") == 0) {
                    val = mem_limit / 1024;
                } else if (strcmp(key, "MemFree") == 0) {
                    val = (mem_limit - mem_usage) / 1024;
                    if (val < 0) val = 0;
                } else if (strcmp(key, "MemAvailable") == 0) {
                    long long free_kb = (mem_limit - mem_usage) / 1024;
                    if (free_kb < 0) free_kb = 0;
                    if (cg_file >= 0) val = free_kb + (cg_file / 1024);
                    else val = free_kb + (long long)(val * ratio * 0.8);
                    if (val > mem_limit / 1024) val = mem_limit / 1024;
                } else if (strcmp(key, "SwapTotal") == 0 || strcmp(key, "SwapFree") == 0) {
                    val = 0;
                } else if (strcmp(key, "AnonPages") == 0 && cg_anon >= 0) {
                    val = cg_anon / 1024;
                } else if ((strcmp(key, "Cached") == 0 || strcmp(key, "Mapped") == 0) && cg_file >= 0) {
                    val = cg_file / 1024;
                } else if (strcmp(key, "Slab") == 0 && cg_slab >= 0) {
                    val = cg_slab / 1024;
                } else if (has_kb) {
                    val = (long long)(val * ratio);
                }

                if (has_kb && val > mem_limit / 1024) val = mem_limit / 1024;

                /* Restore fixed-width alignment for compatibility */
                strncat(key, ":", sizeof(key) - strlen(key) - 1);
                if (has_kb) {
                    offset += snprintf(buf + offset, cap - offset, "%-16s %10lld kB\n", key, val);
                } else {
                    offset += snprintf(buf + offset, cap - offset, "%-16s %10lld\n", key, val);
                }
                continue;
            }
        }
        size_t len = strlen(line);
        if (offset + len < cap) {
            memcpy(buf + offset, line, len);
            offset += len;
        }
    }
    buf[offset] = '\0';
    fclose(f);
    *buf_out = buf;
    *size_out = offset;
    return 0;
}

/*
 * Generate virtualized /proc/cpuinfo
 */
int ds_virtualize_cpuinfo(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int host_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int max_cpus = host_cpus;
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        max_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (max_cpus < 1) max_cpus = 1;
        if (max_cpus > host_cpus) max_cpus = host_cpus;
    }

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return -1;

    size_t cap = 65536;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    char line[4096];
    size_t offset = 0;
    int current_cpu = -1;
    while (fgets(line, sizeof(line), f)) {
        int cpu_id;
        if (sscanf(line, "processor : %d", &cpu_id) == 1) {
            current_cpu = cpu_id;
        }
        if (current_cpu >= max_cpus) break;

        size_t len = strlen(line);
        if (offset + len + 1 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }
        memcpy(buf + offset, line, len);
        offset += len;
    }
    buf[offset] = '\0';
    fclose(f);
    *buf_out = buf;
    *size_out = offset;
    return 0;
}

/*
 * Generate virtualized /proc/stat
 */
int ds_virtualize_stat(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int host_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int max_cpus = host_cpus;
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        max_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (max_cpus < 1) max_cpus = 1;
        if (max_cpus > host_cpus) max_cpus = host_cpus;
    }

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    size_t cap = 65536;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    char line[2048];
    size_t offset = 0;
    unsigned long long sum_user = 0, sum_nice = 0, sum_system = 0, sum_idle = 0,
                       sum_iowait = 0, sum_irq = 0, sum_softirq = 0, sum_steal = 0,
                       sum_guest = 0, sum_guest_nice = 0;

    while (fgets(line, sizeof(line), f)) {
        int cpu_id;
        if (sscanf(line, "cpu%d", &cpu_id) == 1) {
            if (cpu_id < max_cpus) {
                unsigned long long u = 0, n = 0, s = 0, i = 0, io = 0, ir = 0, si = 0, st = 0, gu = 0, gn = 0;
                sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &u, &n, &s, &i, &io, &ir, &si, &st, &gu, &gn);
                sum_user += u; sum_nice += n; sum_system += s; sum_idle += i;
                sum_iowait += io; sum_irq += ir; sum_softirq += si; sum_steal += st;
                sum_guest += gu; sum_guest_nice += gn;
            }
        }
    }
    rewind(f);

    int aggregate_written = 0;
    while (fgets(line, sizeof(line), f)) {
        if (offset + 1024 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }

        if (strncmp(line, "cpu ", 4) == 0) {
            if (!aggregate_written) {
                offset += snprintf(buf + offset, cap - offset, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                                   sum_user, sum_nice, sum_system, sum_idle, sum_iowait, sum_irq, sum_softirq, sum_steal, sum_guest, sum_guest_nice);
                aggregate_written = 1;
            }
            continue;
        }

        int cpu_id;
        if (sscanf(line, "cpu%d", &cpu_id) == 1) {
            if (cpu_id >= max_cpus) continue;
        }

        size_t len = strlen(line);
        memcpy(buf + offset, line, len);
        offset += len;
    }
    buf[offset] = '\0';
    fclose(f);
    *buf_out = buf;
    *size_out = offset;
    return 0;
}

/*
 * Generate virtualized /proc/uptime
 */
int ds_virtualize_uptime(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double up = (double)(now.tv_sec - cfg->start_time.tv_sec) +
                (double)(now.tv_nsec - cfg->start_time.tv_nsec) / 1e9;
    if (up < 0) up = 0;

    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double host_up, host_idle;
    if (fscanf(f, "%lf %lf", &host_up, &host_idle) != 2) {
        fclose(f);
        return -1;
    }
    fclose(f);

    int host_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int container_cpus = host_cpus;
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        container_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (container_cpus < 1) container_cpus = 1;
    }

    double idle = host_idle * ((double)container_cpus / host_cpus);
    if (idle > up * container_cpus) idle = up * container_cpus;

    char *buf = malloc(128);
    if (!buf) return -1;
    *size_out = snprintf(buf, 128, "%.2f %.2f\n", up, idle);
    *buf_out = buf;
    return 0;
}

/*
 * Generate virtualized CPU range for sysfs (e.g. 0-3)
 */
int ds_virtualize_cpu_sysfs(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int host_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int container_cpus = host_cpus;
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        container_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (container_cpus < 1) container_cpus = 1;
        if (container_cpus > host_cpus) container_cpus = host_cpus;
    }

    char *buf = malloc(64);
    if (!buf) return -1;

    if (container_cpus == 1) {
        *size_out = snprintf(buf, 64, "0\n");
    } else {
        *size_out = snprintf(buf, 64, "0-%d\n", container_cpus - 1);
    }
    *buf_out = buf;
    return 0;
}

/*
 * Generate virtualized /proc/loadavg
 */
int ds_virtualize_loadavg(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int host_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int container_cpus = host_cpus;
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        container_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (container_cpus < 1) container_cpus = 1;
    }

    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    double l1, l5, l15;
    int runnable, total;
    if (fscanf(f, "%lf %lf %lf %d/%d %*d", &l1, &l5, &l15, &runnable, &total) != 5) {
        fclose(f);
        return -1;
    }
    fclose(f);

    double ratio = (double)container_cpus / host_cpus;
    char *buf = malloc(256);
    if (!buf) return -1;

    *size_out = snprintf(buf, 256, "%.2f %.2f %.2f %d/%d 0\n",
                         l1 * ratio, l5 * ratio, l15 * ratio,
                         (int)(runnable * ratio) > 0 ? (int)(runnable * ratio) : (runnable > 0 ? 1 : 0),
                         (int)(total * ratio) > 0 ? (int)(total * ratio) : 1);
    *buf_out = buf;
    return 0;
}

int ds_virtualize_init(struct ds_config *cfg) {
    char vproc_path[PATH_MAX * 2] = "/run/droidspaces/vproc";

    if (mkdir_p(vproc_path, 0755) < 0) {
        ds_warn("Failed to create vproc directory: %s", strerror(errno));
        return -1;
    }
    if (domount("none", vproc_path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755,size=1M") < 0) {
        ds_warn("Failed to mount vproc tmpfs: %s", strerror(errno));
        return -1;
    }

    const char *files[] = {"meminfo", "cpuinfo", "stat", "uptime", "loadavg"};
    int (*funcs[])(struct ds_config *, char **, size_t *) = {
        ds_virtualize_meminfo, ds_virtualize_cpuinfo, ds_virtualize_stat, ds_virtualize_uptime, ds_virtualize_loadavg
    };

    for (size_t i = 0; i < ARRAY_SIZE(files); i++) {
        char *vbuf = NULL;
        size_t vsz = 0;
        if (funcs[i](cfg, &vbuf, &vsz) == 0) {
            char path[PATH_MAX * 2];
            snprintf(path, sizeof(path), "%s/%s", vproc_path, files[i]);
            if (write_file(path, vbuf) < 0) {
                free(vbuf);
                continue;
            }
            free(vbuf);

            char target[PATH_MAX];
            snprintf(target, sizeof(target), "/proc/%s", files[i]);
            if (bind_mount(path, target) < 0) {
                ds_warn("Failed to bind mount virtual %s over %s (continuing)", path, target);
            }
        } else {
            ds_warn("Failed to generate virtual content for %s (continuing)", files[i]);
        }
    }

    /* CPU sysfs virtualization for nproc */
    char cpu_sys_base[PATH_MAX * 2];
    snprintf(cpu_sys_base, sizeof(cpu_sys_base), "%s/sys/devices/system/cpu", vproc_path);
    if (mkdir_p(cpu_sys_base, 0755) == 0) {
        const char *cpu_files[] = {"online", "possible", "present"};
        for (size_t i = 0; i < ARRAY_SIZE(cpu_files); i++) {
            char *vbuf = NULL;
            size_t vsz = 0;
            if (ds_virtualize_cpu_sysfs(cfg, &vbuf, &vsz) == 0) {
                char path[PATH_MAX * 2];
                snprintf(path, sizeof(path), "%s/%s", cpu_sys_base, cpu_files[i]);
                if (write_file(path, vbuf) == 0) {
                    char target[PATH_MAX];
                    snprintf(target, sizeof(target), "/sys/devices/system/cpu/%s", cpu_files[i]);
                    if (bind_mount(path, target) < 0) {
                        ds_warn("Failed to bind mount virtual %s over %s (continuing)", path, target);
                    }
                }
                free(vbuf);
            }
        }
    }

    return 0;
}

void ds_virtualize_update(struct ds_config *cfg) {
    /* ASSUMPTION: Monitor is in the HOST mount namespace.
     * If this changes, path resolution via /proc/<pid>/root will fail. */

    char *vbuf = NULL;
    size_t vsz = 0;
    char path[PATH_MAX];

    /* Verify container identity before update to avoid PID recycling race */
    if (ds_get_pid_ns_inode(cfg->container_pid) != cfg->ns_inode) {
        return;
    }

    const char *files[] = {"meminfo", "stat", "uptime", "loadavg"};
    int (*funcs[])(struct ds_config *, char **, size_t *) = {
        ds_virtualize_meminfo, ds_virtualize_stat, ds_virtualize_uptime, ds_virtualize_loadavg
    };

    for (size_t i = 0; i < ARRAY_SIZE(files); i++) {
        if (funcs[i](cfg, &vbuf, &vsz) == 0) {
            snprintf(path, sizeof(path), "/proc/%d/root/run/droidspaces/vproc/%s", cfg->container_pid, files[i]);
            struct stat st;
            if (stat(path, &st) == 0) {
                write_file_inplace(path, vbuf);
            }
            free(vbuf);
        }
    }
}
