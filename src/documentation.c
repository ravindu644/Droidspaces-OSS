/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <stdarg.h>

/* Get binary name from argv[0], handling paths */
static const char *get_binary_name(const char *argv0) {
  if (!argv0)
    return "droidspaces";
  const char *name = strrchr(argv0, '/');
  return name ? name + 1 : argv0;
}

/* Terminal control sequences */
#define CLEAR_SCREEN                                                           \
  "\033[2J\033[H"                /* Clear entire screen and move to home       \
                                  */
#define CLEAR_LINE "\033[2K"     /* Clear entire line */
#define RESET_TERMINAL "\033[0m" /* Reset all attributes */
#define BOLD "\033[1m"
#define REVERSE "\033[7m"
#define CURSOR_HOME "\033[H" /* Move cursor to home position */
#define HIDE_CURSOR "\033[?25l"
#define SHOW_CURSOR "\033[?25h"

/* Page titles */
static const char *page_titles[] = {"CLI Basics",
                                    "Quick Start",
                                    "Management",
                                    "Networking",
                                    "Hardware & Integration",
                                    "Advanced Features"};

/* Total number of pages */
#define TOTAL_PAGES 6

/* Pager state */
static int g_current_line = 0;
static int g_scroll_offset = 0;
static int g_visible_height = 0;
static int g_total_lines = 0;
static int g_dry_run = 0;

/* Pager-aware printf */
static void p_printf(const char *fmt, ...) {
  va_list args;
  char buf[4096];

  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len < 0)
    return;

  /* Count lines in the output */
  char *line_start = buf;
  char *newline;
  while ((newline = strchr(line_start, '\n')) != NULL) {
    if (!g_dry_run) {
      if (g_current_line >= g_scroll_offset &&
          g_current_line < g_scroll_offset + g_visible_height) {
        /* Print the line including its newline */
        fwrite(line_start, 1, (newline - line_start) + 1, stdout);
      }
    }
    g_current_line++;
    g_total_lines++;
    line_start = newline + 1;
  }

  /* Handle trailing content without newline */
  if (*line_start != '\0') {
    if (!g_dry_run) {
      if (g_current_line >= g_scroll_offset &&
          g_current_line < g_scroll_offset + g_visible_height) {
        printf("%s", line_start);
      }
    }
    /* Note: We don't increment line count for partial lines in this simple
     * pager */
  }
}

/* Clear screen completely - more aggressive clearing */
static void clear_screen_completely(void) {
  /* Reset all terminal attributes first */
  printf("%s", RESET_TERMINAL);
  fflush(stdout);

  /* Move cursor to home position first */
  printf("\033[H");
  fflush(stdout);

  /* Clear entire screen from cursor to end */
  printf("\033[2J");
  fflush(stdout);

  /* Try to clear scrollback buffer (not all terminals support this) */
  printf("\033[3J");
  fflush(stdout);

  /* Get terminal size and clear each line explicitly to ensure clean state */
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
    /* Clear each visible line by moving to it and clearing */
    for (int i = 0; i < ws.ws_row; i++) {
      printf("\033[%d;1H\033[2K", i + 1); /* Move to line, clear it */
    }
    /* Return cursor to home position */
    printf("\033[H");
  } else {
    /* Fallback: just ensure we're at home */
    printf("\033[H");
  }

  fflush(stdout);
}

/* Print header with page number and title (like GNU nano) */
static void print_header(int page, int total_pages, const char *title) {
  /* Get actual terminal width */
  struct winsize ws;
  int width = 80; /* Default fallback width */
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    width = ws.ws_col;
  }

  /* Build page string */
  char page_str[256];
  int page_str_len = snprintf(page_str, sizeof(page_str), "Page %d/%d: %s",
                              page + 1, total_pages, title);
  if (page_str_len < 0 || page_str_len >= (int)sizeof(page_str)) {
    page_str_len = 0;
    page_str[0] = '\0';
  }

  const char *doc_title = DS_PROJECT_NAME " v" DS_VERSION " Documentation";
  int doc_title_len = strlen(doc_title);

  /* Calculate where "Droidspaces Documentation" should start to be centered in
   * the entire line */
  int center_pos = width / 2;
  int doc_start_pos = center_pos - (doc_title_len / 2);

  /* Ensure doc title doesn't overlap with page info */
  if (doc_start_pos < page_str_len) {
    doc_start_pos = page_str_len + 2; /* Add some spacing if too close */
  }

  printf("%s", REVERSE);

  /* Print page info on the left */
  printf("%s", page_str);

  /* Pad from end of page info to where doc title should start */
  int padding_before_doc = doc_start_pos - page_str_len;
  if (padding_before_doc > 0) {
    for (int i = 0; i < padding_before_doc; i++)
      printf(" ");
  }

  /* Print centered "Droidspaces Documentation" */
  printf("%s", doc_title);

  /* Fill remaining space to end of line */
  int doc_end_pos = doc_start_pos + doc_title_len;
  int padding_after_doc = width - doc_end_pos;
  if (padding_after_doc > 0) {
    for (int i = 0; i < padding_after_doc; i++)
      printf(" ");
  }

  printf("%s", RESET_TERMINAL);
}

/* Print page content */
static void print_page(int page, const char *bin) {
  const char *bold = BOLD;
  const char *reset = RESET_TERMINAL;

#define printf p_printf

  switch (page) {
  case 0: /* CLI Basics */
    printf("\n");
    printf("CLI BASICS & ARGUMENT FLEXIBILITY\n");
    printf("---------------------------------\n\n");
    printf("Droidspaces features a robust multi-pass argument parser.\n");
    printf("You can combine arguments like a \"salad\":\n\n");

    printf("%sInterchangeable Flags:%s\n", bold, reset);
    printf("  Short and long flags are identical:\n");
    printf("  %s -r /path/to/rootfs -n mycontainer start\n", bin);
    printf("  %s --rootfs /path/to/rootfs --name mycontainer start\n", bin);
    printf("  %s --rootfs=/path/to/rootfs --name=mycontainer start\n\n", bin);
    printf("  (Run %s help to study all available short flags)\n\n", bin);

    printf("%sFlexible Order:%s\n", bold, reset);
    printf("  Command and flags can be mixed in any order:\n");
    printf("  %s start --name mycontainer --rootfs /path\n", bin);
    printf("  %s --rootfs /path/to/rootfs -n mycontainer start\n\n", bin);

    printf("%sMixing Styles:%s\n", bold, reset);
    printf("  Feel free to mix short and long flags with or without '=':\n");
    printf("  %s -r /path/to/rootfs --name=mycontainer --hostname myserver "
           "start\n",
           bin);
    break;

  case 1: /* Quick Start */
    printf("\n");
    printf("QUICK START\n");
    printf("-----------\n\n");
    printf("%sStarting for the first time:%s\n", bold, reset);
    printf("  (Define name and rootfs/img path. Flags are persisted to "
           "config)\n");
    printf("  # From a rootfs directory\n");
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs start\n\n", bin);
    printf("  # From an ext4 image\n");
    printf("  %s --name=mycontainer --rootfs-img=/path/to/rootfs.img start\n\n",
           bin);

    printf("%sSubsequent starts:%s\n", bold, reset);
    printf("  (Settings like rootfs path are loaded from the container's "
           "config file)\n");
    printf("  %s --name=mycontainer start\n\n", bin);

    printf("%sEntering the container:%s\n", bold, reset);
    printf("  %s --name=mycontainer enter\n", bin);
    printf("  %s --name=mycontainer enter username\n\n", bin);

    printf("%sRunning Commands:%s\n", bold, reset);
    printf("  %s --name=mycontainer run 'uname -a'\n", bin);
    printf("  %s --name=mycontainer --user=myuser run whoami\n\n", bin);

    printf("%sStopping:%s\n", bold, reset);
    printf("  %s --name=mycontainer stop\n", bin);
    printf("  %s stop --name=c1,c2,c3 (Multi-stop)\n\n", bin);

    printf("%sUsing Configuration Files:%s\n", bold, reset);
    printf("  %s --config=./container.config start\n", bin);
    break;

  case 2: /* Management */
    printf("\n");
    printf("MANAGEMENT & LIFECYCLE\n");
    printf("----------------------\n\n");

    printf("%sRestarting:%s\n", bold, reset);
    printf("  %s --name=mycontainer restart\n", bin);
    printf("  Note: You can also reboot by running `reboot` inside the\n");
    printf("  container shell or remotely (e.g. via SSH).\n\n");

    printf("%sListing Containers:%s\n", bold, reset);
    printf("  %s show\n", bin);
    printf("  (Displays name and PID inside a table)\n\n");

    printf("%sTechnical Information:%s\n", bold, reset);
    printf("  %s --name=mycontainer info\n", bin);
    printf("  (Shows current features, metadata, and container state)\n\n");

    printf("%sTechnical Information (machine-parseable):%s\n", bold, reset);
    printf("  %s --name=mycontainer --format info\n\n", bin);

    printf("%sResource Usage:%s\n", bold, reset);
    printf("  %s --name=mycontainer usage\n", bin);
    printf("  (Shows uptime, CPU%%, and RAM usage)\n\n");

    printf("%sMetadata Recovery:%s\n", bold, reset);
    printf("  %s scan\n", bin);
    printf("  (Detects containers even if host-side config/PIDs are lost)\n\n");
    break;

  case 3: /* Networking */
    printf("\n");
    printf("NETWORKING\n");
    printf("----------\n\n");

    printf("%sIsolation Modes (--net):%s\n", bold, reset);
    printf("  --net=host   Shared with host (default)\n");
    printf("  --net=none   No network access (air-gapped)\n");
    printf("  --net=nat    Isolated namespace with internet access\n\n");

    printf("%sNAT Mode Configuration:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --net=nat "
           "--upstream=wlan0 "
           "start\n",
           bin);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --net=nat "
           "--upstream=rmnet*,wlan0 "
           "start\n\n",
           bin);

    printf("%sPort Forwarding (NAT only):%s\n", bold, reset);
    printf("  --port=8080:80          Single port\n");
    printf("  --port=1000-2000:1000-2000  Symmetric range\n");
    printf("  --port=8080:80/udp      Specific protocol\n\n");

    printf("%sStatic IP assignment:%s\n", bold, reset);
    printf("  --nat-ip=172.28.5.10\n\n");

    printf("%sCustom DNS & IPv6:%s\n", bold, reset);
    printf("  %s --name=mycontainer --dns=1.1.1.1,8.8.4.4 start\n", bin);
    printf("  %s --name=mycontainer --disable-ipv6 start\n", bin);
    break;

  case 4: /* Hardware & Integration */
    printf("\n");
    printf("HARDWARE & INTEGRATION\n");
    printf("----------------------\n\n");

    printf("%sFull Hardware Access (--hw-access):%s\n", bold, reset);
    printf(
        "  %s --name=mycontainer --rootfs=/path/to/rootfs --hw-access start\n",
        bin);
    printf("  (Exposes host /dev nodes, maps GPU groups, setups X11 in "
           "Linux)\n\n");

    printf("%sSecure GPU-only Mode (--gpu):%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --gpu start\n",
           bin);
    printf("  (Maps ONLY GPU nodes into an isolated tmpfs /dev)\n\n");

    printf("%sAndroid-specific Features:%s\n", bold, reset);
    printf("  --enable-android-storage  Mounts /storage/emulated/0\n");
    printf("  --termux-x11              Setups Termux:X11 socket\n\n");

    printf("%sSystem Integration:%s\n", bold, reset);
    printf("  --selinux-permissive      Set host SELinux to permissive\n");
    printf("  --force-cgroupv1          Force legacy cgroup hierarchy\n");
    printf("  --block-nested-namespaces  Shield against VFS deadlocks\n");
    break;

  case 5: /* Advanced Features */
    printf("\n");
    printf("ADVANCED FEATURES\n");
    printf("-----------------\n\n");

    printf("%sPrivileged Mode (--privileged):%s\n", bold, reset);
    printf("  Relax security with comma-separated tags:\n");
    printf("  nomask        Allow write access to /proc and /sys\n");
    printf("  nocaps        Keep all Linux Capabilities (full root)\n");
    printf("  noseccomp     Disable syscall filtering (useful for "
           "flatpak,bwrap and other unprivileged sandboxes)\n");
    printf("  shared        Enable MS_SHARED mount propagation\n");
    printf("  unfiltered-dev Bypass device filtering (all host /dev)\n");
    printf("  full          Enable all above tags\n\n");

    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --privileged=full "
           "start\n\n",
           bin);

    printf("%sEphemeral Mode (--volatile):%s\n", bold, reset);
    printf(
        "  %s --name=mycontainer --rootfs=/path/to/rootfs --volatile start\n",
        bin);
    printf("  (All changes are stored in RAM and lost on exit)\n\n");

    printf("%sBind Mounts & Environment:%s\n", bold, reset);
    printf("  --bind /host:/cont    Bind mount host path\n");
    printf("  --env /path/to/env.list  Load environment variables\n\n");

    printf("%sConfig Management:%s\n", bold, reset);
    printf("  --config=PATH         Load specific config file\n");
    printf("  --reset               Reset config to defaults\n");
    printf("  --help                Show summary of all flags\n");
    break;
  }
#undef printf
}

/* Read arrow key (escape sequence) */
static int read_arrow_key(void) {
  char seq[3];
  ssize_t n = read(STDIN_FILENO, &seq[0], 1);
  if (n != 1)
    return 0;

  if (seq[0] == '\033') {
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return 0;
    if (seq[1] == '[') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1)
        return 0;
      if (seq[2] == 'A')
        return -2; /* Up arrow */
      if (seq[2] == 'B')
        return 2; /* Down arrow */
      if (seq[2] == 'D')
        return -1; /* Left arrow */
      if (seq[2] == 'C')
        return 1; /* Right arrow */
    }
  } else if (seq[0] == 'q' || seq[0] == 'Q') {
    return 'q';
  }
  return 0;
}

/* Global flag for terminal resize */
static volatile int g_terminal_resized = 0;

/* Signal handler for terminal resize */
static void handle_sigwinch(int sig) {
  (void)sig;
  g_terminal_resized = 1;
}

static struct termios *g_old_tios_ptr = NULL;
static void handle_sigint(int sig) {
  (void)sig;
  if (g_old_tios_ptr)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, g_old_tios_ptr);
  printf("\033[?25h"); /* SHOW_CURSOR */
  write(STDOUT_FILENO, "\033[2J\033[H", 7); /* clear screen */
  _exit(0);
}

/* Print documentation with interactive navigation */
void print_documentation(const char *argv0) {
  const char *bin = get_binary_name(argv0);

  /* Check if both stdin and stdout are TTYs for interactive mode */
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    /* Not a TTY - print all documentation non-interactively */
    for (int i = 0; i < TOTAL_PAGES; i++) {
      printf("\n");
      /* Non-interactive: just use regular printf through a wrapper */
      g_dry_run = 0;
      g_scroll_offset = 0;
      g_visible_height = 9999;
      print_page(i, bin);
    }
    return;
  }

  /* Save original terminal settings */
  static struct termios old_tios;
  struct termios new_tios;
  if (tcgetattr(STDIN_FILENO, &old_tios) < 0) {
    ds_error("Failed to get terminal attributes: %s", strerror(errno));
    return;
  }

  new_tios = old_tios;
  new_tios.c_lflag &= ~(ICANON | ECHO);
  new_tios.c_cc[VMIN] = 1;
  new_tios.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_tios) < 0) {
    ds_error("Failed to set terminal attributes: %s", strerror(errno));
    return;
  }

  struct sigaction sa;
  sa.sa_handler = handle_sigwinch;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGWINCH, &sa, NULL);

  sa.sa_handler = handle_sigint;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  g_old_tios_ptr = &old_tios;

  setvbuf(stdout, NULL, _IONBF, 0);

  int current_page = 0;
  int running = 1;
  g_scroll_offset = 0;

  while (running) {
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    int width = ws.ws_col > 0 ? ws.ws_col : 80;
    int height = ws.ws_row > 0 ? ws.ws_row : 24;

    /* Leave room for header (1) and navigation hint (2) */
    g_visible_height = height - 3;
    if (g_visible_height < 1)
      g_visible_height = 1;

    /* 1. Dry run to calculate total lines */
    g_dry_run = 1;
    g_total_lines = 0;
    g_current_line = 0;
    print_page(current_page, bin);

    /* Clamp scroll offset */
    if (g_scroll_offset > g_total_lines - g_visible_height)
      g_scroll_offset = g_total_lines - g_visible_height;
    if (g_scroll_offset < 0)
      g_scroll_offset = 0;

    /* 2. Actual render */
    clear_screen_completely();
    printf("%s", HIDE_CURSOR);

    /* Draw Header at row 1 */
    printf("\033[1;1H");
    print_header(current_page, TOTAL_PAGES, page_titles[current_page]);

    /* Draw content from row 2 */
    printf("\033[2;1H");
    g_dry_run = 0;
    g_current_line = 0;
    print_page(current_page, bin);

    /* Draw Navigation Hint at the very bottom */
    printf("\033[%d;1H%s", height, REVERSE);
    char hint[256];
    snprintf(hint, sizeof(hint),
             " [←/→] Prev/Next   [↑/↓] Scroll (%d/%d)   [q] Quit",
             g_scroll_offset + 1, g_total_lines);
    printf("%s", hint);
    for (int i = strlen(hint) - 8; i < width; i++)
      printf(" ");
    printf("%s", RESET_TERMINAL);

    fflush(stdout);

    int key = read_arrow_key();
    switch (key) {
    case -1: /* Left */
      if (current_page > 0) {
        current_page--;
        g_scroll_offset = 0;
      }
      break;
    case 1: /* Right */
      if (current_page < TOTAL_PAGES - 1) {
        current_page++;
        g_scroll_offset = 0;
      }
      break;
    case -2: /* Up */
      if (g_scroll_offset > 0)
        g_scroll_offset--;
      break;
    case 2: /* Down */
      if (g_scroll_offset < g_total_lines - g_visible_height)
        g_scroll_offset++;
      break;
    case 'q':
      running = 0;
      break;
    }
  }

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tios);
  printf("%s", SHOW_CURSOR);
  clear_screen_completely();
}
