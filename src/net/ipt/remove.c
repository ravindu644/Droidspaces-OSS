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

/* Internal: fixup_jump_targets_removed
 *
 * Removal twin of fixup_jump_targets.  After rules are deleted the surviving
 * entries shift to lower offsets, but every xt_standard_target with a
 * non-negative verdict (= absolute byte offset = chain jump) still holds its
 * OLD offset.  Re-base each by the number of bytes removed before that offset:
 * old_offsets[k] gives an entry's old start, removed_before[k] the bytes
 * removed before it.  Without this the kernel's mark_source_chains() validator
 * can no longer resolve the shifted jump (xt_find_jump_offset fails) and
 * rejects the whole table replace with ELOOP. */

static void fixup_jump_targets_removed(const struct xt_family *f,
                                       unsigned char *blob,
                                       unsigned int blob_sz,
                                       const unsigned int *old_offsets,
                                       const unsigned int *removed_before,
                                       unsigned int nents) {
  unsigned int off = 0;

  while (off < blob_sz) {
    unsigned char *e = blob + off;
    unsigned int next = ent_next(f, e);

    if (next < f->entry_sz || off + next > blob_sz)
      break;

    struct xt_standard_target *st = entry_standard_target(f, e, next);
    if (st && st->verdict >= 0) {
      /* Jump target: find the tracked entry that started at this old
       * offset and subtract the bytes removed before it. */
      for (unsigned int k = 0; k < nents; k++) {
        if (old_offsets[k] == (unsigned int)st->verdict) {
          st->verdict -= (int)removed_before[k];
          break;
        }
      }
    }

    off += next;
  }
}

/* Internal: remove_matching_rules
 *
 * Builds a new blob that omits every entry matching any of our fingerprints,
 * then submits it atomically via IPT_SO_SET_REPLACE.
 *
 * Two-pass algorithm avoids in-place mutation of the blob being walked.
 *
 * match_src / match_mask point at addr_len bytes, or are NULL when only the
 * interface rules are wanted.
 *
 * Returns the number of rules removed (0 when nothing matched), or a negative
 * errno when the table replace failed. */

int remove_matching_rules(const struct xt_family *f, int fd,
                          const char *table_name, struct ipt_getinfo *info_in,
                          unsigned char *blob_in, unsigned int hook_id,
                          const void *match_src, const void *match_mask,
                          const char *match_iface, unsigned int iface_flags) {
  if (!info_in || !blob_in || hook_id >= NF_INET_NUMHOOKS ||
      !(info_in->valid_hooks & (1u << hook_id)))
    return 0;

  /*
   * cur_info / cur_blob track the table state we are working from.
   * They start as the caller-supplied values.  On EAGAIN we refetch the table
   * and update these so the next attempt uses a fresh snapshot of the rules.
   *
   * cur_base: the allocation returned by get_table() on a refetch.
   *           NULL means we are still using the caller-owned blob_in.
   *           Non-NULL means we own it and must free it on exit.
   */
  struct ipt_getinfo cur_info = *info_in;
  unsigned char *cur_base = NULL; /* NULL → caller owns initial blob */
  unsigned char *cur_blob = blob_in;

  int max_retries = 5;
  int ret = 0, err = 0;
  int removed = 0;

  while (max_retries-- > 0) {
    unsigned char *new_blob = malloc(cur_info.size);
    if (!new_blob) {
      /* On an EAGAIN refetch iteration cur_base owns a get_table() blob; free
       * it before bailing so the early return does not leak it. */
      if (cur_base)
        free(cur_base);
      return -ENOMEM;
    }

    /* Allocate per-entry tracking arrays */
    unsigned int *old_offsets =
        calloc(cur_info.num_entries + 1, sizeof(unsigned int));
    unsigned int *removed_before =
        calloc(cur_info.num_entries + 1, sizeof(unsigned int));

    if (!old_offsets || !removed_before) {
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = -1;
      err = ENOMEM;
      break;
    }

    unsigned int new_sz = 0;
    unsigned int removed_count = 0;
    unsigned int cumulative_gone = 0;
    unsigned int offset = 0;
    unsigned int ei = 0;

    /* Pass 1: walk, classify, build new_blob */
    while (offset + f->entry_sz <= cur_info.size && ei < cur_info.num_entries) {
      const unsigned char *e = cur_blob + offset;
      unsigned int next = ent_next(f, e);
      unsigned int toff = ent_target_off(f, e);
      if (next < f->entry_sz || next > cur_info.size - offset ||
          toff + sizeof(struct xt_entry_target) > next)
        break;

      old_offsets[ei] = offset;
      removed_before[ei] = cumulative_gone;

      const struct xt_entry_target *t =
          (const struct xt_entry_target *)(e + toff);
      const char *tname = t->u.user.name;
      struct xt_ent_view v;
      entry_view_ro(f, e, &v);

      int is_ours = 0;

      if (!hook_contains_offset(&cur_info, hook_id, offset))
        goto keep_rule;

      /* MASQUERADE for our subnet */
      if (match_src && strcmp(tname, "MASQUERADE") == 0 &&
          memcmp(v.src, match_src, f->addr_len) == 0 &&
          memcmp(v.smsk, match_mask, f->addr_len) == 0 &&
          memcmp(v.dst, match_src, f->addr_len) == 0 &&
          memcmp(v.dmsk, match_mask, f->addr_len) == 0 &&
          (*v.invflags & IPT_INV_DSTIP))
        is_ours = 1;

      /* Our MSS clamp, and only ours: a foreign TCPMSS rule stays. */
      if (!is_ours && (iface_flags & DS_IPT_MATCH_CLAMP) && is_our_clamp(f, e))
        is_ours = 1;

      /* ACCEPT on the exact interface/direction we inserted. */
      if (!is_ours && match_iface && match_iface[0] && target_is_accept(t)) {
        if ((iface_flags & DS_IPT_MATCH_IN) &&
            iface_exact_match(v.iniface, v.in_mask, match_iface) &&
            iface_empty(v.outiface, v.out_mask))
          is_ours = 1;
        if ((iface_flags & DS_IPT_MATCH_OUT) &&
            iface_exact_match(v.outiface, v.out_mask, match_iface) &&
            iface_empty(v.iniface, v.in_mask))
          is_ours = 1;
      }

    keep_rule:
      if (is_ours) {
        /* Safety: never remove an underflow (chain policy) entry. */
        int is_underflow = 0;
        for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
          if ((cur_info.valid_hooks & (1u << h)) &&
              offset == cur_info.underflow[h]) {
            is_underflow = 1;
            break;
          }
        }
        if (is_underflow) {
          ds_warn("[IPT] remove: would remove underflow entry at offset %u - "
                  "skipping",
                  offset);
          memcpy(new_blob + new_sz, e, next);
          new_sz += next;
          offset += next;
          ei++;
          continue;
        }
        ds_log("[IPT] remove: matched %s '%s' rule at offset %u", f->bin,
               target_label(t), offset);
        cumulative_gone += next;
        removed_count++;
      } else {
        memcpy(new_blob + new_sz, e, next);
        new_sz += next;
      }

      offset += next;
      ei++;
    }
    old_offsets[ei] = offset;
    removed_before[ei] = cumulative_gone;

    if (removed_count == 0) {
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = 0; /* nothing to do */
      break;
    }

    /* Re-base surviving jump verdicts for the bytes just removed; without this
     * the kernel rejects the table replace with ELOOP (see
     * fixup_jump_targets_removed). */
    fixup_jump_targets_removed(f, new_blob, new_sz, old_offsets, removed_before,
                               ei + 1);

    /* Build ipt_replace */
    size_t replace_sz = sizeof(struct ipt_replace) + new_sz;
    struct ipt_replace *repl = calloc(1, replace_sz);
    if (!repl) {
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = -1;
      err = ENOMEM;
      break;
    }

    safe_strncpy(repl->name, table_name, sizeof(repl->name));
    repl->valid_hooks = cur_info.valid_hooks;
    repl->num_entries = cur_info.num_entries - removed_count;
    repl->size = new_sz;

    /* num_counters: must be the OLD count */
    repl->num_counters = cur_info.num_entries;
    repl->counters = calloc(cur_info.num_entries ? cur_info.num_entries : 1,
                            sizeof(struct xt_counters));

    if (!repl->counters) {
      free(repl);
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = -1;
      err = ENOMEM;
      break;
    }

    /* Pass 2: fix up hook_entry / underflow offsets */
    for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
      if (!(cur_info.valid_hooks & (1u << h)))
        continue;

      unsigned int adj = 0;
      for (unsigned int k = 0; k <= ei; k++) {
        if (old_offsets[k] >= cur_info.hook_entry[h]) {
          adj = removed_before[k];
          break;
        }
      }
      repl->hook_entry[h] = cur_info.hook_entry[h] - adj;

      adj = 0;
      for (unsigned int k = 0; k <= ei; k++) {
        if (old_offsets[k] >= cur_info.underflow[h]) {
          adj = removed_before[k];
          break;
        }
      }
      repl->underflow[h] = cur_info.underflow[h] - adj;
    }

    memcpy(repl + 1, new_blob, new_sz);

    ret = setsockopt(fd, f->level, IPT_SO_SET_REPLACE, repl,
                     (socklen_t)replace_sz);
    err = errno;

    free(repl->counters);
    free(repl);
    free(new_blob);
    free(old_offsets);
    free(removed_before);

    if (ret == 0) {
      removed = (int)removed_count;
      break;
    }

    if (err == EAGAIN && max_retries > 0) {
      ds_log("[IPT] remove: EAGAIN (attempt remaining=%d), refetching '%s'",
             max_retries, table_name);
      usleep(5000 + 10000 * (4 - max_retries));

      if (cur_base) {
        free(cur_base);
        cur_base = NULL;
      }

      struct ipt_getinfo new_info;
      unsigned char *new_base = NULL;
      if (get_table(f, fd, table_name, &new_info, &new_base) < 0) {
        ds_log("[IPT] remove: refetch of '%s' failed, giving up", table_name);
        ret = -1;
        err = EAGAIN;
        break;
      }

      cur_base = new_base;
      cur_blob = ENTRIES_BLOB(new_base);
      cur_info = new_info;
      continue;
    }

    ds_warn("[IPT] remove: table replace on '%s' failed (%s) - falling back "
            "to the %s binary",
            table_name, strerror(err), f->bin);
    break;
  }

  if (cur_base)
    free(cur_base);

  return (ret < 0) ? -err : removed;
}
