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

/* Emit page content through the pager (p_printf), honouring the current scroll
 * window / dry-run counting held in the g_* pager state. */
static void print_page(int page, const char *bin) {
  const char *bold = BOLD;
  const char *reset = RESET_TERMINAL;

  switch (page) {
  case 0: /* CLI Basics */
    p_printf("\n");
    p_printf("CLI BASICS & ARGUMENT FLEXIBILITY\n");
    p_printf("---------------------------------\n\n");
    p_printf("Droidspaces features a robust multi-pass argument parser.\n");
    p_printf("You can combine arguments like a \"salad\":\n\n");

    p_printf("%sInterchangeable Flags:%s\n", bold, reset);
    p_printf("  Short and long flags are identical:\n");
    p_printf("  %s -r /path/to/rootfs -n mycontainer start\n", bin);
    p_printf("  %s --rootfs /path/to/rootfs --name mycontainer start\n", bin);
    p_printf("  %s --rootfs=/path/to/rootfs --name=mycontainer start\n\n", bin);
    p_printf("  (Run %s help to study all available short flags)\n\n", bin);

    p_printf("%sFlexible Order:%s\n", bold, reset);
    p_printf("  Command and flags can be mixed in any order:\n");
    p_printf("  %s start --name mycontainer --rootfs /path\n", bin);
    p_printf("  %s --rootfs /path/to/rootfs -n mycontainer start\n\n", bin);

    p_printf("%sMixing Styles:%s\n", bold, reset);
    p_printf("  Feel free to mix short and long flags with or without '=':\n");
    p_printf("  %s -r /path/to/rootfs --name=mycontainer --hostname myserver "
             "start\n",
             bin);
    break;

  case 1: /* Quick Start */
    p_printf("\n");
    p_printf("QUICK START\n");
    p_printf("-----------\n\n");
    p_printf("%sStarting for the first time:%s\n", bold, reset);
    p_printf("  (Define name and rootfs/img path. Flags are persisted to "
             "config)\n");
    p_printf("  # From a rootfs directory\n");
    p_printf("  %s --name=mycontainer --rootfs=/path/to/rootfs start\n\n", bin);
    p_printf("  # From an ext4 image\n");
    p_printf(
        "  %s --name=mycontainer --rootfs-img=/path/to/rootfs.img start\n\n",
        bin);

    p_printf("%sSubsequent starts:%s\n", bold, reset);
    p_printf("  (Settings like rootfs path are loaded from the container's "
             "config file)\n");
    p_printf("  %s --name=mycontainer start\n\n", bin);

    p_printf("%sEntering the container:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer enter\n", bin);
    p_printf("  %s --name=mycontainer enter username\n\n", bin);

    p_printf("%sRunning Commands:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer run 'uname -a'\n", bin);
    p_printf("  %s --name=mycontainer --user=myuser run whoami\n\n", bin);

    p_printf("%sStopping:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer stop\n", bin);
    p_printf("  %s stop --name=c1,c2,c3 (Multi-stop)\n\n", bin);

    p_printf("%sUsing Configuration Files:%s\n", bold, reset);
    p_printf("  %s --config=./container.config start\n", bin);
    break;

  case 2: /* Management */
    p_printf("\n");
    p_printf("MANAGEMENT & LIFECYCLE\n");
    p_printf("----------------------\n\n");

    p_printf("%sRestarting:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer restart\n", bin);
    p_printf("  Note: You can also reboot by running `reboot` inside the\n");
    p_printf("  container shell or remotely (e.g. via SSH).\n\n");

    p_printf("%sListing Containers:%s\n", bold, reset);
    p_printf("  %s show\n", bin);
    p_printf("  (Displays name and PID inside a table)\n\n");

    p_printf("%sTechnical Information:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer info\n", bin);
    p_printf("  (Shows current features, metadata, and container state)\n\n");

    p_printf("%sTechnical Information (machine-parseable):%s\n", bold, reset);
    p_printf("  %s --name=mycontainer --format info\n\n", bin);

    p_printf("%sResource Usage:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer usage\n", bin);
    p_printf("  (Shows uptime, CPU%%, and RAM usage)\n\n");

    p_printf("%sMetadata Recovery:%s\n", bold, reset);
    p_printf("  %s scan\n", bin);
    p_printf(
        "  (Detects containers even if host-side config/PIDs are lost)\n\n");
    break;

  case 3: /* Networking */
    p_printf("\n");
    p_printf("NETWORKING\n");
    p_printf("----------\n\n");

    p_printf("%sIsolation Modes (--net):%s\n", bold, reset);
    p_printf("  --net=host     Shared with host (default)\n");
    p_printf("  --net=none     No network access (air-gapped)\n");
    p_printf("  --net=nat      Isolated namespace with internet access\n");
    p_printf("  --net=gateway  LAN delegated to another container "
             "(e.g. OpenWRT)\n\n");

    p_printf("%sNAT Mode Configuration:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --net=nat "
             "start\n",
             bin);
    p_printf("  The internet uplink is detected automatically and tracked "
             "in real time\n");
    p_printf("  as the host switches networks (Wi-Fi <-> mobile data, "
             "ethernet, etc.).\n\n");

    p_printf("%sManual Uplink Pinning (--upstream, NAT only):%s\n", bold,
             reset);
    p_printf("  Pin WAN to specific interface(s) and disable auto-detect. "
             "The list is\n");
    p_printf("  comma-separated, priority-ordered, and supports wildcards.\n");
    p_printf("  --upstream=wlan0          Always use Wi-Fi\n");
    p_printf("  --upstream=wlan0,rmnet*   Prefer Wi-Fi, fall back to mobile "
             "data\n");
    p_printf("  --upstream=tun0           Route only through a phone VPN "
             "(killswitch)\n");
    p_printf("  --upstream=rmnet*         Stay on mobile data while the phone "
             "uses Wi-Fi\n");
    p_printf("  (Use rmnet* - the mobile-data interface number is not stable "
             "across reconnects.)\n\n");

    p_printf("%sGateway Mode Configuration:%s\n", bold, reset);
    p_printf("  Delegate the LAN to another running container (e.g. OpenWRT), "
             "which owns\n");
    p_printf("  DHCP, DNS, firewall and routing. Start the gateway first, "
             "then the client.\n");
    p_printf("  %s --name=openwrt --rootfs=/path/to/openwrt --net=nat start\n",
             bin);
    p_printf("  %s --name=kali --rootfs=/path/to/kali --net=gateway "
             "--gateway=openwrt start\n",
             bin);
    p_printf("  --gateway=NAME         Gateway container (required)\n");
    p_printf("  --gateway-net=NAME     LAN segment / bridge suffix "
             "(default: lan)\n");
    p_printf("  --gateway-iface=IFACE  Interface name inside the gateway "
             "(default: eth1)\n");
    p_printf("  --gateway-bridge=BR    Override host bridge name "
             "(default: ds-NAME)\n\n");

    p_printf("%sPort Forwarding (NAT only):%s\n", bold, reset);
    p_printf("  --port=8080:80          Single port\n");
    p_printf("  --port=1000-2000:1000-2000  Symmetric range\n");
    p_printf("  --port=8080:80/udp      Specific protocol\n\n");

    p_printf("%sStatic IP assignment:%s\n", bold, reset);
    p_printf("  --nat-ip=172.28.5.10\n\n");

    p_printf("%sCustom DNS & IPv6:%s\n", bold, reset);
    p_printf("  %s --name=mycontainer --dns=1.1.1.1,8.8.4.4 start\n", bin);
    p_printf("  %s --name=mycontainer --disable-ipv6 start\n", bin);
    break;

  case 4: /* Hardware & Integration */
    p_printf("\n");
    p_printf("HARDWARE & INTEGRATION\n");
    p_printf("----------------------\n\n");

    p_printf("%sFull Hardware Access (--hw-access):%s\n", bold, reset);
    p_printf(
        "  %s --name=mycontainer --rootfs=/path/to/rootfs --hw-access start\n",
        bin);
    p_printf("  (Exposes host /dev nodes, maps GPU groups, setups X11 in "
             "Linux)\n\n");

    p_printf("%sSecure GPU-only Mode (--gpu):%s\n", bold, reset);
    p_printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --gpu start\n",
             bin);
    p_printf("  (Maps ONLY GPU nodes into an isolated tmpfs /dev)\n\n");

    p_printf("%sAndroid-specific Features:%s\n", bold, reset);
    p_printf("  --enable-android-storage  Mounts /storage/emulated/0\n");
    p_printf("  --termux-x11              Setups Termux:X11 socket\n\n");

    p_printf("%sSystem Integration:%s\n", bold, reset);
    p_printf("  --selinux-permissive      Set host SELinux to permissive\n");
    p_printf("  --force-cgroupv1          Force legacy cgroup hierarchy\n");
    p_printf("  --block-nested-namespaces  Shield against VFS deadlocks\n");
    break;

  case 5: /* Advanced Features */
    p_printf("\n");
    p_printf("ADVANCED FEATURES\n");
    p_printf("-----------------\n\n");

    p_printf("%sPrivileged Mode (--privileged):%s\n", bold, reset);
    p_printf("  Relax security with comma-separated tags:\n");
    p_printf("  nomask        Allow write access to /proc and /sys\n");
    p_printf("  nocaps        Keep all Linux Capabilities (full root)\n");
    p_printf("  noseccomp     Disable syscall filtering (useful for "
             "flatpak,bwrap and other unprivileged sandboxes)\n");
    p_printf("  shared        Enable MS_SHARED mount propagation\n");
    p_printf("  unfiltered-dev Bypass device filtering (all host /dev)\n");
    p_printf("  full          Enable all above tags\n\n");

    p_printf(
        "  %s --name=mycontainer --rootfs=/path/to/rootfs --privileged=full "
        "start\n\n",
        bin);

    p_printf("%sEphemeral Mode (--volatile):%s\n", bold, reset);
    p_printf(
        "  %s --name=mycontainer --rootfs=/path/to/rootfs --volatile start\n",
        bin);
    p_printf("  (All changes are stored in RAM and lost on exit)\n\n");

    p_printf("%sCustom Init (--init):%s\n", bold, reset);
    p_printf("  Override the default init binary (/sbin/init).\n");
    p_printf("  Useful for distros with non-standard init paths, or for\n");
    p_printf("  running a shell directly as PID 1 (e.g. for debugging).\n\n");
    p_printf("  %s --name=mycontainer --rootfs=/path/to/rootfs "
             "--init=/bin/bash start\n\n",
             bin);

    p_printf("%sBind Mounts & Environment:%s\n", bold, reset);
    p_printf("  --bind /host:/cont[:ro]  Bind mount host path (append :ro for "
             "read-only)\n");
    p_printf("  --env /path/to/env.list  Load environment variables\n\n");

    p_printf("%sConfig Management:%s\n", bold, reset);
    p_printf("  --config=PATH         Load specific config file\n");
    p_printf("  --reset               Reset config to defaults\n");
    p_printf("  --help                Show summary of all flags\n");
    break;
  }
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
  /* Async-signal-safe: printf is not, so write the escape directly. */
  if (write(STDOUT_FILENO, "\033[?25h", 6) < 0) { /* SHOW_CURSOR; ignore */
  }
  if (write(STDOUT_FILENO, "\033[2J\033[H", 7) < 0) {
    /* ignore */
  }
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
    /* Pad to the terminal width.  hint contains multi-byte arrow glyphs, so
     * count display columns (bytes that are not UTF-8 continuation bytes)
     * rather than strlen, which would over-count and need a magic fudge. */
    int hint_cols = 0;
    for (const char *p = hint; *p; p++)
      if (((unsigned char)*p & 0xC0) != 0x80)
        hint_cols++;
    for (int i = hint_cols; i < width; i++)
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
