/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "droidspaces.h"
#include <assert.h>

static int fixture;
static int reads;
int uname(struct utsname *out) {
  memset(out, 0, sizeof(*out));
  strcpy(out->machine, fixture >= 10 ? "aarch64" : "x86_64");
  return 0;
}
void ds_log_internal(const char *prefix, const char *color, int error,
                     const char *fmt, ...) {
  (void)prefix;
  (void)color;
  (void)error;
  (void)fmt;
}
int read_file(const char *path, char *buf, size_t size) {
  reads++;
  if (strstr(path, "/present"))
    return snprintf(buf, size, "0-7\n");
  if (strstr(path, "/online"))
    return snprintf(buf, size, "%s\n", fixture == 1 ? "0-1,4-7" : "0-7");
  if (strstr(path, "cpu_core/cpus")) {
    if (fixture >= 10 || fixture == 3)
      return -1;
    return snprintf(buf, size, "%s\n", fixture == 1 ? "0-1" : "0-3");
  }
  if (strstr(path, "cpu_atom/cpus")) {
    if (fixture >= 10 || fixture == 3)
      return -1;
    return snprintf(buf, size, "%s\n", fixture == 2 ? "4-6" : "4-7");
  }
  int cpu;
  if (sscanf(path, "/sys/devices/system/cpu/cpu%d/", &cpu) != 1 || cpu < 0 ||
      cpu > 7)
    return -1;
  if (strstr(path, "physical_package_id"))
    return snprintf(buf, size, "0\n");
  if (strstr(path, "core_id"))
    return snprintf(buf, size, "%d\n", cpu < 4 ? (cpu % 2 ? 1 : 10) : cpu);
  if (strstr(path, "thread_siblings_list")) {
    if (fixture == 4)
      return -1;
    if (cpu < 4)
      return snprintf(buf, size, "%s\n",
                      fixture == 1 ? (cpu % 2 ? "1" : "0")
                                   : (cpu % 2 ? "1,3" : "0,2"));
    return snprintf(buf, size, "%d\n", cpu);
  }
  if (strstr(path, "cpu_capacity"))
    return snprintf(
        buf, size, "%d\n",
        fixture == 12
            ? 1024
            : (fixture == 11 && cpu == 4 ? 768 : (cpu < 4 ? 512 : 1024)));
  return -1;
}
int main(void) {
  struct ds_config cfg = {0};
  const char *bad[] = {NULL, "-1P",      "1P,",         "1P,0E", "1P,2P",
                       "1",  "1P,2E,3P", "2147483648E", "1P 2E"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    cfg.cpu_p_cores = 2;
    cfg.cpu_e_cores = 3;
    cfg.cpuset_invalid = 1;
    CPU_SET(7, &cfg.cpuset_cpus);
    assert(ds_cpu_parse_cores(bad[i], &cfg) == -1);
    assert(cfg.cpu_p_cores == 2 && cfg.cpu_e_cores == 3 &&
           cfg.cpuset_invalid == 1 && CPU_ISSET(7, &cfg.cpuset_cpus));
  }
  assert(ds_cpu_parse_cores("", &cfg) == 0);
  assert(!cfg.cpu_p_cores && !cfg.cpu_e_cores && !cfg.cpuset_invalid &&
         !CPU_COUNT(&cfg.cpuset_cpus));
  reads = 0;
  assert(ds_cpu_resolve_cores(&cfg) == 0 && reads == 0);
  for (fixture = 0; fixture <= 1; fixture++) {
    assert(ds_cpu_parse_cores("1p,2e", &cfg) == 0);
    assert(ds_cpu_resolve_cores(&cfg) == 0);
    assert(CPU_COUNT(&cfg.cpuset_cpus) == 4);
    assert(CPU_ISSET(1, &cfg.cpuset_cpus) && CPU_ISSET(3, &cfg.cpuset_cpus) &&
           CPU_ISSET(4, &cfg.cpuset_cpus) && CPU_ISSET(5, &cfg.cpuset_cpus));
  }
  fixture = 0;
  assert(ds_cpu_parse_cores("5E", &cfg) == 0);
  CPU_SET(7, &cfg.cpuset_cpus);
  assert(ds_cpu_resolve_cores(&cfg) == -1 && CPU_COUNT(&cfg.cpuset_cpus) == 1 &&
         CPU_ISSET(7, &cfg.cpuset_cpus));
  fixture = 2;
  assert(ds_cpu_parse_cores("4E", &cfg) == 0);
  assert(ds_cpu_resolve_cores(&cfg) == -1);
  fixture = 3;
  assert(ds_cpu_parse_cores("1E", &cfg) == 0);
  assert(ds_cpu_resolve_cores(&cfg) == -1);
  fixture = 4;
  assert(ds_cpu_parse_cores("1P,2E", &cfg) == 0);
  assert(ds_cpu_resolve_cores(&cfg) == 0 && CPU_COUNT(&cfg.cpuset_cpus) == 4);
  fixture = 10;
  assert(ds_cpu_parse_cores("1P,1E", &cfg) == 0);
  assert(ds_cpu_resolve_cores(&cfg) == 0 && CPU_COUNT(&cfg.cpuset_cpus) == 3);
  fixture = 11;
  assert(ds_cpu_parse_cores("1P,2E", &cfg) == 0);
  assert(ds_cpu_resolve_cores(&cfg) == 0);
  assert(CPU_COUNT(&cfg.cpuset_cpus) == 5 && CPU_ISSET(5, &cfg.cpuset_cpus) &&
         !CPU_ISSET(4, &cfg.cpuset_cpus));
  fixture = 12;
  assert(ds_cpu_parse_cores("1E", &cfg) == 0);
  assert(ds_cpu_resolve_cores(&cfg) == -1);
  cfg.cpuset_invalid = 1;
  assert(ds_cpu_resolve_cores(&cfg) == -1 && cfg.cpuset_invalid);
  puts("CPU core parser, Intel SMT/offline/order, ARM capacity and rejection "
       "fixtures passed");
}
