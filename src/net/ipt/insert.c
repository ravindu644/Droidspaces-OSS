/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <arpa/inet.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_nat.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_TCPMSS.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

#include "net/ipt/internal.h"

/* Internal: fixup_jump_targets
 *
 * After inserting new_rule_sz bytes at insert_off, any xt_standard_target
 * with a positive verdict (= absolute byte offset = chain jump) that pointed
 * to an entry AT OR AFTER insert_off must be incremented by new_rule_sz. */

static void fixup_jump_targets(const struct xt_family *f, unsigned char *blob,
                               unsigned int blob_sz, unsigned int insert_off,
                               unsigned int delta) {
  unsigned int off = 0;

  while (off < blob_sz) {
    unsigned char *e = blob + off;
    unsigned int next = ent_next(f, e);

    if (next < f->entry_sz || off + next > blob_sz)
      break;

    struct xt_standard_target *st = entry_standard_target(f, e, next);
    if (st && st->verdict >= (int)insert_off)
      st->verdict += (int)delta;

    off += next;
  }
}

/* Internal: insert_rule_at_hook
 *
 * Inserts new_rule at the very beginning of the given hook's chain. */

int insert_rule_at_hook(const struct xt_family *f, int fd,
                        const char *table_name, struct ipt_getinfo *info_in,
                        unsigned char *blob_in, unsigned int hook_id,
                        const void *new_rule, unsigned int new_rule_sz) {
  /*
   * cur_info / cur_blob track the table state we are working from.
   * They start as the caller-supplied values.  On EAGAIN we refetch the table
   * and update these so the next attempt uses a fresh snapshot.
   *
   * cur_base: the allocation returned by get_table() on a refetch.
   *           NULL means we are still using the caller-owned blob_in.
   *           Non-NULL means we own it and must free it on exit.
   */
  struct ipt_getinfo cur_info = *info_in;
  unsigned char *cur_base = NULL; /* NULL → caller owns initial blob */
  unsigned char *cur_blob = blob_in;

  int max_retries = 5; /* generous: handles bursts of netd activity */
  int ret = -1, err = EAGAIN;

  ds_log("[IPT] insert_rule_at_hook: %s table='%s' hook=%u rule_sz=%u", f->bin,
         table_name, hook_id, new_rule_sz);

  while (max_retries-- > 0) {
    unsigned int insert_off = cur_info.hook_entry[hook_id];
    unsigned int old_sz = cur_info.size;
    unsigned int new_sz = old_sz + new_rule_sz;

    ds_log("[IPT] insert_rule_at_hook: table='%s' hook=%u insert_off=%u "
           "old_sz=%u new_sz=%u",
           table_name, hook_id, insert_off, old_sz, new_sz);

    size_t replace_sz = sizeof(struct ipt_replace) + new_sz;
    struct ipt_replace *repl = calloc(1, replace_sz);
    if (!repl) {
      err = ENOMEM;
      break;
    }

    safe_strncpy(repl->name, table_name, sizeof(repl->name));
    repl->valid_hooks = cur_info.valid_hooks;
    repl->num_entries = cur_info.num_entries + 1;
    repl->size = new_sz;

    /* num_counters = OLD count */
    repl->num_counters = cur_info.num_entries;

    /* Counters buffer: kernel writes OLD entry counters back into this array.
     * Must hold cur_info.num_entries (the live count) values, not
     * num_entries+1. */
    repl->counters = calloc(cur_info.num_entries ? cur_info.num_entries : 1,
                            sizeof(struct xt_counters));
    if (!repl->counters) {
      free(repl);
      err = ENOMEM;
      break;
    }

    /* hook_entry/underflow offset adjustment */
    for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
      if (!(cur_info.valid_hooks & (1u << h)))
        continue;

      repl->hook_entry[h] = cur_info.hook_entry[h];
      repl->underflow[h] = cur_info.underflow[h];

      /* hook_entry: strictly greater - the chain we insert INTO keeps its start
       */
      if (cur_info.hook_entry[h] > insert_off)
        repl->hook_entry[h] += new_rule_sz;

      /* underflow: at-or-after - terminal entries always shift */
      if (cur_info.underflow[h] >= insert_off)
        repl->underflow[h] += new_rule_sz;

      ds_log("[IPT]   hook[%d]: entry %u→%u  underflow %u→%u", h,
             cur_info.hook_entry[h], repl->hook_entry[h], cur_info.underflow[h],
             repl->underflow[h]);
    }

    /* Build new blob: prefix | new_rule | suffix */
    unsigned char *nb = (unsigned char *)(repl + 1);
    if (insert_off > 0)
      memcpy(nb, cur_blob, insert_off);
    memcpy(nb + insert_off, new_rule, new_rule_sz);
    if (old_sz > insert_off)
      memcpy(nb + insert_off + new_rule_sz, cur_blob + insert_off,
             old_sz - insert_off);

    /* Patch stale jump verdicts in the shifted suffix */
    fixup_jump_targets(f, nb, new_sz, insert_off, new_rule_sz);

    ret = setsockopt(fd, f->level, IPT_SO_SET_REPLACE, repl,
                     (socklen_t)replace_sz);
    err = errno;

    free(repl->counters);
    free(repl);

    if (ret == 0)
      break;

    if (err == EAGAIN && max_retries > 0) {
      ds_log("[IPT]   EAGAIN (attempt remaining=%d), refetching '%s' table",
             max_retries, table_name);
      usleep(5000 + 10000 * (4 - max_retries)); /* 5 / 15 / 25 / 35 ms */

      /* Free any previously refetched base and get a fresh snapshot. */
      if (cur_base) {
        free(cur_base);
        cur_base = NULL;
      }

      struct ipt_getinfo new_info;
      unsigned char *new_base = NULL;
      if (get_table(f, fd, table_name, &new_info, &new_base) < 0) {
        ds_log("[IPT]   refetch of '%s' failed, giving up", table_name);
        ret = -1;
        err = EAGAIN;
        break;
      }

      cur_base = new_base;
      cur_blob = ENTRIES_BLOB(new_base);
      cur_info = new_info;
      continue;
    }

    break; /* non-EAGAIN error, or last attempt exhausted */
  }

  if (cur_base)
    free(cur_base);

  if (ret < 0) {
    if (err != ENOENT && err != EAGAIN)
      ds_log("[IPT] insert_rule_at_hook: kernel rejected blob (errno=%d %s)",
             err, strerror(err));
    return -err;
  }
  return 0;
}
