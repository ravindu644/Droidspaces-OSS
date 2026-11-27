/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_CLI_CLI_H
#define DS_CLI_CLI_H

/* check.c */
int check_ns(int flag, const char *name);
int check_requirements_hw(int hw_access);
int check_requirements_detailed(int json_output);

/* docs.c */
void print_documentation(const char *argv0);

/* options.c */
void ds_resolve_argv_paths(int argc, char **argv);
int parse_and_validate_names(const char *optarg, char *out_buf,
                             size_t out_size);

/* Every short flag. A letter missing here is rejected as an unknown option
 * even though long_options maps it, so keep the two in sync. */
#define DS_OPTSTRING "r:i:n:h:d:fHXPISvVB:C:E:u:"
extern const struct option long_options[];

/* The CLI override pass. Idempotent over the same argv. */
int ds_apply_cli_overrides(int argc, char **argv, struct ds_config *cfg,
                           int strict);
const char *ds_command_word(int argc, char **argv);

/* usage.c */
void print_ds_banner(void);

void print_usage(void);

#endif /* DS_CLI_CLI_H */
