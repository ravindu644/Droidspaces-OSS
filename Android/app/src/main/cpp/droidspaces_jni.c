/*
 * Droidspaces v6 - JNI Bridge
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Thin JNI wrapper around the core Droidspaces C API.
 * Compiles with NDK (bionic) for in-process container management.
 *
 * Rootless mode: pure JNI, no root required (user namespaces).
 * Root mode:     callers use the dispatch binary (runner) via su.
 */

#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Helper: Java String -> C string (UTF)
 * ---------------------------------------------------------------------------*/
static char *jstring_to_cstr(JNIEnv *env, jstring js) {
  if (!js) return NULL;
  const char *utf = (*env)->GetStringUTFChars(env, js, NULL);
  if (!utf) return NULL;
  size_t len = strlen(utf);
  char *c = malloc(len + 1);
  if (c) { memcpy(c, utf, len + 1); }
  (*env)->ReleaseStringUTFChars(env, js, utf);
  return c;
}

/* ---------------------------------------------------------------------------
 * Helper: setup a ds_config from workspace + name
 * ---------------------------------------------------------------------------*/
static int setup_config(const char *name_cstr, const char *ws_cstr,
                        struct ds_config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  ds_set_workspace_dir(ws_cstr);
  safe_strncpy(cfg->container_name, name_cstr, sizeof(cfg->container_name));
  return 0;
}

/* ---------------------------------------------------------------------------
 * Helper: read all data from a pipe fd into a Java String
 * ---------------------------------------------------------------------------*/
static jstring pipe_to_jstring(JNIEnv *env, int fd) {
  char buf[16384];
  size_t total = 0;
  ssize_t n;
  while ((n = read(fd, buf + total, sizeof(buf) - total - 1)) > 0) {
    total += (size_t)n;
    if (total >= sizeof(buf) - 1) break;
  }
  buf[total] = '\0';
  close(fd);
  return (*env)->NewStringUTF(env, buf);
}

/* ---------------------------------------------------------------------------
 * JNI: start container
 *
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)I
 * ---------------------------------------------------------------------------*/
JNIEXPORT jint JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_startContainer(
    JNIEnv *env, jclass clazz,
    jstring config_path, jstring workspace, jboolean rootless) {

  (void)clazz;

  char *c_config = jstring_to_cstr(env, config_path);
  char *c_ws     = jstring_to_cstr(env, workspace);
  if (!c_config || !c_ws) { free(c_config); free(c_ws); return -1; }

  ds_set_workspace_dir(c_ws);

  struct ds_config cfg;
  memset(&cfg, 0, sizeof(cfg));
  safe_strncpy(cfg.config_file, c_config, sizeof(cfg.config_file));

  if (ds_config_load(c_config, &cfg) < 0) {
    free(c_config); free(c_ws);
    return -1;
  }

  if (rootless) {
    cfg.rootless = 1;
    cfg.net_mode = DS_NET_HOST;
  }

  ds_cgroup_host_bootstrap(cfg.rootless);
  int ret = start_rootfs(&cfg);

  free_config_env_vars(&cfg);
  free_config_binds(&cfg);
  free(c_config);
  free(c_ws);
  return (jint)ret;
}

/* ---------------------------------------------------------------------------
 * JNI: stop container
 *
 * Signature: (Ljava/lang/String;Ljava/lang/String;I)I
 * ---------------------------------------------------------------------------*/
JNIEXPORT jint JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_stopContainer(
    JNIEnv *env, jclass clazz,
    jstring name, jstring workspace, jint pid) {

  (void)clazz;
  char *c_name = jstring_to_cstr(env, name);
  char *c_ws   = jstring_to_cstr(env, workspace);
  if (!c_name) { free(c_ws); return -1; }

  ds_set_workspace_dir(c_ws);

  struct ds_config cfg;
  memset(&cfg, 0, sizeof(cfg));
  safe_strncpy(cfg.container_name, c_name, sizeof(cfg.container_name));
  cfg.container_pid = (pid_t)pid;

  int ret = stop_rootfs(&cfg, 0);
  free(c_name);
  free(c_ws);
  return (jint)ret;
}

/* ---------------------------------------------------------------------------
 * JNI: get container PID
 *
 * Signature: (Ljava/lang/String;Ljava/lang/String;)I
 * ---------------------------------------------------------------------------*/
JNIEXPORT jint JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_getContainerPid(
    JNIEnv *env, jclass clazz,
    jstring name, jstring workspace) {

  (void)clazz;
  char *c_name = jstring_to_cstr(env, name);
  char *c_ws   = jstring_to_cstr(env, workspace);
  if (!c_name) { free(c_ws); return -2; }

  ds_set_workspace_dir(c_ws);

  struct ds_config cfg;
  memset(&cfg, 0, sizeof(cfg));
  safe_strncpy(cfg.container_name, c_name, sizeof(cfg.container_name));

  pid_t pid = 0;
  int running = is_container_running(&cfg, &pid);

  free(c_name);
  free(c_ws);
  if (running && pid > 0) return (jint)pid;
  return -1;
}

/* ---------------------------------------------------------------------------
 * JNI: run command inside a running container
 *
 * Captures stdout+stderr output and returns as a String.
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 * ---------------------------------------------------------------------------*/
JNIEXPORT jstring JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_runInContainer(
    JNIEnv *env, jclass clazz,
    jstring name, jstring workspace, jstring command) {

  (void)clazz;
  char *c_name    = jstring_to_cstr(env, name);
  char *c_ws      = jstring_to_cstr(env, workspace);
  char *c_command = jstring_to_cstr(env, command);
  if (!c_name || !c_ws || !c_command) {
    free(c_name); free(c_ws); free(c_command);
    return (*env)->NewStringUTF(env, "");
  }

  struct ds_config cfg;
  setup_config(c_name, c_ws, &cfg);

  pid_t container_pid = 0;
  if (!is_container_running(&cfg, &container_pid) || container_pid <= 0) {
    free(c_name); free(c_ws); free(c_command);
    return (*env)->NewStringUTF(env, "ERROR: Container not running");
  }

  /* Create pipe to capture output */
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    free(c_name); free(c_ws); free(c_command);
    return (*env)->NewStringUTF(env, "ERROR: pipe failed");
  }

  /* Build argv for run_in_rootfs: sh -c "<command>" */
  char *argv[4];
  argv[0] = "sh";
  argv[1] = "-c";
  argv[2] = c_command;
  argv[3] = NULL;

  /* Fork a child that will call run_in_rootfs with captured output */
  pid_t child = fork();
  if (child < 0) {
    close(pipefd[0]); close(pipefd[1]);
    free(c_name); free(c_ws); free(c_command);
    return (*env)->NewStringUTF(env, "ERROR: fork failed");
  }

  if (child == 0) {
    /* Child: redirect stdout+stderr to pipe */
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    if (pipefd[1] > STDERR_FILENO) close(pipefd[1]);

    /* Run the command inside the container */
    int rc = run_in_rootfs(&cfg, 3, argv, NULL);
    _exit(rc < 0 ? 1 : 0);
  }

  /* Parent: close write end, read output */
  close(pipefd[1]);
  jstring result = pipe_to_jstring(env, pipefd[0]);

  /* Wait for child */
  int status;
  waitpid(child, &status, 0);

  free(c_name); free(c_ws); free(c_command);
  return result;
}

/* ---------------------------------------------------------------------------
 * JNI: get container resource usage
 *
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 * ---------------------------------------------------------------------------*/
JNIEXPORT jstring JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_getContainerUsage(
    JNIEnv *env, jclass clazz,
    jstring name, jstring workspace) {

  (void)clazz;
  char *c_name = jstring_to_cstr(env, name);
  char *c_ws   = jstring_to_cstr(env, workspace);
  if (!c_name || !c_ws) {
    free(c_name); free(c_ws);
    return (*env)->NewStringUTF(env, "");
  }

  struct ds_config cfg;
  setup_config(c_name, c_ws, &cfg);

  int pipefd[2];
  if (pipe(pipefd) < 0) {
    free(c_name); free(c_ws);
    return (*env)->NewStringUTF(env, "");
  }

  pid_t child = fork();
  if (child < 0) {
    close(pipefd[0]); close(pipefd[1]);
    free(c_name); free(c_ws);
    return (*env)->NewStringUTF(env, "");
  }

  if (child == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    if (pipefd[1] > STDOUT_FILENO) close(pipefd[1]);
    show_container_usage(&cfg);
    _exit(0);
  }

  close(pipefd[1]);
  jstring result = pipe_to_jstring(env, pipefd[0]);

  int status;
  waitpid(child, &status, 0);

  free(c_name); free(c_ws);
  return result;
}

/* ---------------------------------------------------------------------------
 * JNI: restart container
 *
 * Signature: (Ljava/lang/String;Ljava/lang/String;Z)I
 * ---------------------------------------------------------------------------*/
JNIEXPORT jint JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_restartContainer(
    JNIEnv *env, jclass clazz,
    jstring config_path, jstring workspace, jboolean rootless) {

  (void)clazz;
  char *c_config = jstring_to_cstr(env, config_path);
  char *c_ws     = jstring_to_cstr(env, workspace);
  if (!c_config || !c_ws) { free(c_config); free(c_ws); return -1; }

  ds_set_workspace_dir(c_ws);

  struct ds_config cfg;
  memset(&cfg, 0, sizeof(cfg));
  safe_strncpy(cfg.config_file, c_config, sizeof(cfg.config_file));

  if (ds_config_load(c_config, &cfg) < 0) {
    free(c_config); free(c_ws);
    return -1;
  }

  if (rootless) {
    cfg.rootless = 1;
    cfg.net_mode = DS_NET_HOST;
  }

  int ret = restart_rootfs(&cfg);

  free_config_env_vars(&cfg);
  free_config_binds(&cfg);
  free(c_config);
  free(c_ws);
  return (jint)ret;
}

/* ---------------------------------------------------------------------------
 * JNI: get Droidspaces version string
 *
 * Signature: ()Ljava/lang/String;
 * ---------------------------------------------------------------------------*/
JNIEXPORT jstring JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_getVersion(
    JNIEnv *env, jclass clazz) {

  (void)clazz;
  return (*env)->NewStringUTF(env, DS_VERSION);
}

/* ---------------------------------------------------------------------------
 * JNI: get backend mode (daemon or direct)
 *
 * Signature: ()Ljava/lang/String;
 * ---------------------------------------------------------------------------*/
JNIEXPORT jstring JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_getBackendMode(
    JNIEnv *env, jclass clazz) {

  (void)clazz;
  const char *mode = ds_daemon_probe() ? "daemon" : "direct";
  return (*env)->NewStringUTF(env, mode);
}

/* ---------------------------------------------------------------------------
 * JNI: enter container namespace and exec a shell (rootless mode)
 *
 * Forks a child that enters the container namespace and execs a shell.
 * Returns child PID. Java side connects to the child for terminal I/O.
 *
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I
 * ---------------------------------------------------------------------------*/
JNIEXPORT jint JNICALL
Java_com_droidspaces_app_nativebridge_NativeBridge_enterContainer(
    JNIEnv *env, jclass clazz,
    jstring name, jstring workspace, jstring user) {

  (void)clazz;
  char *c_name = jstring_to_cstr(env, name);
  char *c_ws   = jstring_to_cstr(env, workspace);
  char *c_user = jstring_to_cstr(env, user);
  if (!c_name || !c_ws) {
    free(c_name); free(c_ws); free(c_user);
    return -1;
  }

  if (!c_user || strlen(c_user) == 0) c_user = strdup("root");

  struct ds_config cfg;
  setup_config(c_name, c_ws, &cfg);

  pid_t child = fork();
  if (child < 0) {
    free(c_name); free(c_ws); free(c_user);
    return -1;
  }

  if (child == 0) {
    /* Child: enter the container namespace and exec a shell */
    enter_rootfs(&cfg, c_user);
    _exit(1);
  }

  free(c_name); free(c_ws); free(c_user);
  return (jint)child;
}

/* ---------------------------------------------------------------------------
 * Native entry point for root-mode dispatch (called by runner via dlopen)
 *
 * This function acts as a simplified main() for the runner binary.
 * It reads a ds_config from the workspace + container name or config path.
 *
 * Signature: int ds_runner_main(int argc, char **argv, const char *workspace)
 *
 * Commands:
 *   start <config_path>   — start container
 *   stop <name> [pid]     — stop container
 *   restart <config_path> — restart container
 *   pid <name>            — get PID
 *   usage <name>          — show usage
 *   enter <name> [user]   — enter container
 * ---------------------------------------------------------------------------*/
int ds_runner_main(int argc, char **argv, const char *workspace) {
  if (argc < 2) {
    fprintf(stderr, "Usage: runner <start|stop|restart|pid|usage|enter|version> [args...]\n");
    return 1;
  }

  ds_set_workspace_dir(workspace);

  const char *cmd = argv[1];

  /* version */
  if (strcmp(cmd, "version") == 0) {
    printf("%s\n", DS_VERSION);
    return 0;
  }

  /* build a ds_config for commands that need a container name */
  struct ds_config cfg;
  memset(&cfg, 0, sizeof(cfg));

  if (strcmp(cmd, "start") == 0 || strcmp(cmd, "restart") == 0) {
    if (argc < 3) { fprintf(stderr, "missing config path\n"); return 1; }
    const char *config_path = argv[2];
    safe_strncpy(cfg.config_file, config_path, sizeof(cfg.config_file));
    if (ds_config_load(config_path, &cfg) < 0) return 1;

    if (strcmp(cmd, "start") == 0) {
      ds_cgroup_host_bootstrap(0);
      int ret = start_rootfs(&cfg);
      free_config_env_vars(&cfg);
      free_config_binds(&cfg);
      return ret;
    } else {
      int ret = restart_rootfs(&cfg);
      free_config_env_vars(&cfg);
      free_config_binds(&cfg);
      return ret;
    }
  }

  if (strcmp(cmd, "stop") == 0) {
    if (argc < 3) { fprintf(stderr, "missing container name\n"); return 1; }
    safe_strncpy(cfg.container_name, argv[2], sizeof(cfg.container_name));
    if (argc > 3) cfg.container_pid = (pid_t)atoi(argv[3]);
    return stop_rootfs(&cfg, 0);
  }

  if (strcmp(cmd, "pid") == 0) {
    if (argc < 3) { fprintf(stderr, "missing container name\n"); return 1; }
    safe_strncpy(cfg.container_name, argv[2], sizeof(cfg.container_name));
    pid_t pid = 0;
    if (is_container_running(&cfg, &pid) && pid > 0) {
      printf("%d\n", (int)pid);
      return 0;
    }
    printf("NONE\n");
    return 1;
  }

  if (strcmp(cmd, "usage") == 0) {
    if (argc < 3) { fprintf(stderr, "missing container name\n"); return 1; }
    safe_strncpy(cfg.container_name, argv[2], sizeof(cfg.container_name));
    return show_container_usage(&cfg);
  }

  if (strcmp(cmd, "enter") == 0) {
    if (argc < 3) { fprintf(stderr, "missing container name\n"); return 1; }
    safe_strncpy(cfg.container_name, argv[2], sizeof(cfg.container_name));
    const char *user = (argc > 3) ? argv[3] : "root";
    return enter_rootfs(&cfg, user);
  }

  fprintf(stderr, "Unknown command: %s\n", cmd);
  return 1;
}
