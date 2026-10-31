/*
 * android-bridge: one binary, the daemon on Android and the client in a
 * container
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bridge.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/un.h>

static void usage(FILE *f) {
  fprintf(f,
          "Usage: android-bridge [-t|-T] [-s SOCKET] [COMMAND...]\n"
          "       android-bridge --daemon [--foreground] [--dir DIR]\n"
          "\n"
          "Runs COMMAND, or an interactive shell, on the Android side as the\n"
          "shell user, the same account adb shell uses. Only real root may\n"
          "connect. Use su in that shell for root, if the manager allows it.\n"
          "\n"
          "  -t             always allocate a terminal\n"
          "  -T             never allocate a terminal\n"
          "  -s SOCKET      bridge socket, default " AB_CONTAINER_DIR
          "/" AB_SOCK_NAME "\n"
          "                 or " AB_HOST_DIR "/" AB_SOCK_NAME
          " when that is missing\n"
          "  --daemon       serve the bridge, as root on the Android side\n"
          "  --foreground   with --daemon, do not detach\n"
          "  --dir DIR      with --daemon, socket directory, "
          "default " AB_HOST_DIR "\n"
          "  -h, --help     show this help\n"
          "  -V, --version  show the version\n");
}

int main(int argc, char **argv) {
  static const struct option opts[] = {
      {"daemon", no_argument, NULL, 'D'},
      {"foreground", no_argument, NULL, 'f'},
      {"dir", required_argument, NULL, 'd'},
      {"socket", required_argument, NULL, 's'},
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'V'},
      {NULL, 0, NULL, 0},
  };
  int daemon = 0, foreground = 0, pty = -1, o;
  const char *dir = AB_HOST_DIR, *sock = NULL;

  /* "+" stops at the first command word, so its own flags reach it. */
  while ((o = getopt_long(argc, argv, "+tTs:hV", opts, NULL)) != -1) {
    switch (o) {
    case 't':
      pty = 1;
      break;
    case 'T':
      pty = 0;
      break;
    case 's':
      sock = optarg;
      break;
    case 'D':
      daemon = 1;
      break;
    case 'f':
      foreground = 1;
      break;
    case 'd':
      dir = optarg;
      break;
    case 'h':
      usage(stdout);
      return 0;
    case 'V':
      puts("android-bridge " AB_VERSION);
      return 0;
    default:
      usage(stderr);
      return 255;
    }
  }

  if (daemon) {
    if (optind < argc) {
      usage(stderr);
      return 255;
    }
    return run_daemon(dir, foreground);
  }

  /* ENOENT means this is not a container with the bridge mounted. Any other
   * error, EACCES for a non-root user above all, keeps the container path so
   * the connect reports the real reason. */
  char def[sizeof(((struct sockaddr_un *)0)->sun_path)];
  if (!sock) {
    struct stat st;
    sock_path(def, sizeof(def), AB_CONTAINER_DIR);
    if (lstat(def, &st) < 0 && errno == ENOENT)
      sock_path(def, sizeof(def), AB_HOST_DIR);
    sock = def;
  }
  return run_client(sock, pty, argc - optind, argv + optind);
}
