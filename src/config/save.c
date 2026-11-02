/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* A key written here must also be parsed by ds_config_load() in config/load.c.
 * Otherwise it is read back as an unknown line and saved twice. */
static void ds_config_serialize_known(FILE *f, struct ds_config *cfg) {
  fprintf(f, "# Droidspaces Container Configuration\n");
  fprintf(f, "# Generated automatically - Changes may be overwritten\n\n");

  /* Write managed keys */
  if (cfg->container_name[0])
    fprintf(f, "name=%s\n", cfg->container_name);
  if (cfg->hostname[0])
    fprintf(f, "hostname=%s\n", cfg->hostname);

  if (cfg->is_img_mount && cfg->rootfs_img_path[0])
    fprintf(f, "rootfs_path=%s\n", cfg->rootfs_img_path);
  else if (cfg->rootfs_path[0])
    fprintf(f, "rootfs_path=%s\n", cfg->rootfs_path);

  fprintf(f, "disable_ipv6=%d\n", cfg->disable_ipv6);
  fprintf(f, "enable_x11=%d\n", cfg->x11);
  if (is_android()) {
    fprintf(f, "enable_android_storage=%d\n", cfg->android_storage);
    if (cfg->tx11_extra_flags)
      fprintf(f, "tx11_extra_flags=%s\n", cfg->tx11_extra_flags);
    fprintf(f, "enable_virgl=%d\n", cfg->virgl);
    if (cfg->virgl_extra_flags)
      fprintf(f, "virgl_extra_flags=%s\n", cfg->virgl_extra_flags);
    fprintf(f, "enable_pulseaudio=%d\n", cfg->pulseaudio);
    fprintf(f, "enable_anland=%d\n", cfg->anland);
  }
  fprintf(f, "enable_hw_access=%d\n", cfg->hw_access);
  fprintf(f, "enable_gpu_mode=%d\n", cfg->gpu_mode);
  fprintf(f, "selinux_permissive=%d\n", cfg->selinux_permissive);
  fprintf(f, "allow_sandboxing=%d\n", cfg->sandboxing_allowed);
  fprintf(f, "allow_vts=%d\n", cfg->allow_vts);
  fprintf(f, "allow_swap=%d\n", cfg->allow_swap);
  fprintf(f, "volatile_mode=%d\n", cfg->volatile_mode);
  fprintf(f, "force_cgroupv1=%d\n", cfg->force_cgroupv1);
  if (cfg->memory_limit > 0)
    fprintf(f, "memory_limit=%lld\n", cfg->memory_limit);
  if (cfg->cpu_quota > 0)
    fprintf(f, "cpu_quota=%lld\n", cfg->cpu_quota);
  if (cfg->cpu_period > 0)
    fprintf(f, "cpu_period=%lld\n", cfg->cpu_period);
  if (cfg->pids_limit > 0)
    fprintf(f, "pids_limit=%lld\n", cfg->pids_limit);

  if (cfg->privileged_bad[0]) {
    fprintf(f, "privileged=%s\n", cfg->privileged_bad);
  } else if (cfg->privileged_mask > 0) {
    char tags[64];
    ds_privileged_names(cfg->privileged_mask, tags, sizeof(tags), ",");
    fprintf(f, "privileged=%s\n", tags);
  }

  fprintf(f, "foreground=%d\n", cfg->foreground);

  fprintf(f, "net_mode=%s\n", ds_net_mode_name(cfg->net_mode));

  if (cfg->net_mode == DS_NET_GATEWAY) {
    if (cfg->gateway_container[0])
      fprintf(f, "gateway_container=%s\n", cfg->gateway_container);
    if (cfg->gateway_net[0])
      fprintf(f, "gateway_net=%s\n", cfg->gateway_net);
    if (cfg->gateway_bridge[0])
      fprintf(f, "gateway_bridge=%s\n", cfg->gateway_bridge);
    if (cfg->gateway_lan_ifname[0])
      fprintf(f, "gateway_lan_ifname=%s\n", cfg->gateway_lan_ifname);
  }

  if (cfg->net_mode == DS_NET_MACVLAN) {
    if (cfg->macvlan_parent[0])
      fprintf(f, "macvlan_parent=%s\n", cfg->macvlan_parent);
    if (cfg->macvlan_mode && cfg->macvlan_mode != MACVLAN_MODE_BRIDGE)
      fprintf(f, "macvlan_mode=%s\n", ds_macvlan_mode_name(cfg->macvlan_mode));
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->upstream_iface_count > 0) {
    fprintf(f, "upstream_interfaces=");
    for (int i = 0; i < cfg->upstream_iface_count; i++)
      fprintf(f, "%s%s", cfg->upstream_ifaces[i],
              (i < cfg->upstream_iface_count - 1) ? "," : "");
    fprintf(f, "\n");
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->port_forward_count > 0) {
    fprintf(f, "port_forwards=");
    for (int i = 0; i < cfg->port_forward_count; i++) {
      const struct ds_port_forward *pf = &cfg->port_forwards[i];
      if (pf->host_port_end) {
        fprintf(f, "%u-%u:%u-%u/%s", pf->host_port, pf->host_port_end,
                pf->container_port, pf->container_port_end, pf->proto);
      } else {
        fprintf(f, "%u:%u/%s", pf->host_port, pf->container_port, pf->proto);
      }
      if (i < cfg->port_forward_count - 1)
        fprintf(f, ",");
    }
    fprintf(f, "\n");
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->static_nat_ip[0])
    fprintf(f, "static_nat_ip=%s\n", cfg->static_nat_ip);

  if (cfg->env_file[0])
    fprintf(f, "env_file=%s\n", cfg->env_file);
  if (cfg->uuid[0])
    fprintf(f, "uuid=%s\n", cfg->uuid);

  if (cfg->custom_init[0])
    fprintf(f, "custom_init=%s\n", cfg->custom_init);

  if (cfg->dns_servers[0])
    fprintf(f, "dns_servers=%s\n", cfg->dns_servers);

  if (cfg->bind_count > 0) {
    fprintf(f, "bind_mounts=");
    for (int i = 0; i < cfg->bind_count; i++)
      fprintf(f, "%s:%s%s%s", cfg->binds[i].src, cfg->binds[i].dest,
              cfg->binds[i].ro ? ":ro" : "",
              (i < cfg->bind_count - 1) ? "," : "");
    fprintf(f, "\n");
  }
}

/* A new name is not on disk until its directory is: without this a hard
 * reboot right after the rename can lose container.config and with it the
 * container (#364). Best effort, the file itself is already in place. */
static void sync_dir_of(const char *path) {
  char dir[PATH_MAX];
  safe_strncpy(dir, path, sizeof(dir));
  char *slash = strrchr(dir, '/');
  if (!slash)
    return;
  *slash = '\0';
  int dfd = open(slash == dir ? "/" : dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    fsync(dfd);
    close(dfd);
  }
}

int ds_config_save(const char *config_path, struct ds_config *cfg) {
  /* Sort bind mounts before saving so they are persisted in a sane order. */
  sort_bind_mounts(cfg);

  char *buf = NULL;
  size_t size = 0, known_size = 0;
  FILE *f = open_memstream(&buf, &size);
  if (!f)
    return -1;
  ds_config_serialize_known(f, cfg);
  if (fflush(f) == 0)
    known_size = size;
  /* Keys this build does not know, the Android app's metadata among them,
   * ride along verbatim. */
  for (struct ds_config_line *n = cfg->unknown_head; n; n = n->next)
    fputs(n->line, f);
  if (fclose(f) != 0 || !known_size) {
    free(buf);
    return -1;
  }

  /* The app edits its own keys in this file while a container runs, and the
   * stale copy of them in cfg must not land on top. Only a change in the keys
   * this build manages earns a rewrite. */
  int same = 0;
  struct ds_config disk;
  ds_config_init(&disk);
  if (access(config_path, F_OK) == 0 &&
      ds_config_peek_path(config_path, &disk) == 0) {
    sort_bind_mounts(&disk);
    char *dbuf = NULL;
    size_t dsize = 0;
    FILE *df = open_memstream(&dbuf, &dsize);
    if (df) {
      ds_config_serialize_known(df, &disk);
      same = fclose(df) == 0 && dsize == known_size &&
             memcmp(dbuf, buf, known_size) == 0;
    }
    free(dbuf);
    ds_config_free(&disk);
  }
  int rc = same ? 0 : write_file_atomic(config_path, buf);
  free(buf);
  if (rc < 0)
    return -1;
  if (!same)
    sync_dir_of(config_path);
  cfg->config_file_existed = 1;
  return 0;
}

int ds_config_save_by_name(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;
  if (!validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  char container_dir[PATH_MAX];
  snprintf(container_dir, sizeof(container_dir), "%s/Containers/%s",
           get_workspace_dir(), safe_name);
  mkdir_p(container_dir, 0755);

  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%.3800s/container.config",
           container_dir);

  return ds_config_save(config_path, cfg);
}
