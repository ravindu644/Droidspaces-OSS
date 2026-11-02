/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_TTY_PTY_H
#define DS_TTY_PTY_H

int ds_openpty(const char *ptmx, int *master, int *slave, char *name);
int ds_terminal_create(struct ds_tty_info *tty);
int ds_terminal_set_stdfds(int fd);
int ds_terminal_make_controlling(int fd);
int ds_setup_tios(int fd, struct termios *old);

#endif /* DS_TTY_PTY_H */
