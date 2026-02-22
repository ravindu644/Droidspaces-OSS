/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

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
static const char *page_titles[] = {
    "Basic Usage", "Medium Usage", "Advanced Usage", "Gigachad Usage", "Notes"};

/* Total number of pages */
#define TOTAL_PAGES 5

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

  const char *doc_title = "Droidspaces v3 Documentation";
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

  printf("%s\n", RESET_TERMINAL);
}

/* Print page content */
static void print_page(int page, const char *bin) {
  const char *bold = BOLD;
  const char *reset = RESET_TERMINAL;

  switch (page) {
  case 0: /* Basic Usage */
    printf("\n");
    printf("BASIC USAGE\n");
    printf("-----------\n\n");
    printf("%sStarting a container:%s\n", bold, reset);
    printf("  %s --rootfs=/path/to/rootfs start\n", bin);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs start\n\n", bin);
    printf("  %s --rootfs-img=/path/to/rootfs.img start\n", bin);
    printf("  %s --name=mycontainer --rootfs-img=/path/to/rootfs.img start\n\n",
           bin);
    printf("  (If --name isn't defined, it will be auto-generated)\n\n");

    printf("%sListing running containers:%s\n", bold, reset);
    printf("  %s show\n\n", bin);

    printf("%sEntering a container:%s\n", bold, reset);
    printf("  %s --name=mycontainer enter\n", bin);
    printf("  %s --name=mycontainer enter username\n\n", bin);

    printf("%sStopping a container:%s\n", bold, reset);
    printf("  %s --name=mycontainer stop\n\n", bin);

    printf("%sRestarting a container:%s\n", bold, reset);
    printf("  %s --name=mycontainer restart\n\n", bin);

    printf("%sRunning a command:%s\n", bold, reset);
    printf("  %s --name=mycontainer run echo hello\n", bin);
    printf("  %s --name=mycontainer run ls -la /tmp\n\n", bin);

    printf("%sFetch info about a container:%s\n", bold, reset);
    printf("  %s --name=mycontainer info\n\n", bin);

    printf("%sChecking container status:%s\n", bold, reset);
    printf("  %s --name=mycontainer status\n\n", bin);

    printf("%sChecking system requirements:%s\n", bold, reset);
    printf("  %s check\n\n", bin);
    break;

  case 1: /* Medium Usage */
    printf("\n");
    printf("MEDIUM USAGE\n");
    printf("------------\n\n");

    printf("%sContainer with custom hostname:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs "
           "--hostname=myserver start\n\n",
           bin);

    printf("%sContainer with hardware access:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --hw-access "
           "start\n\n",
           bin);

    printf("%sContainer with Android storage:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs "
           "--enable-android-storage start\n\n",
           bin);

    printf("%sContainer with IPv6 enabled:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --enable-ipv6 "
           "start\n\n",
           bin);

    printf("%sContainer with SELinux permissive:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs "
           "--selinux-permissive start\n\n",
           bin);

    printf("%sForeground mode (attach to console):%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --foreground "
           "start\n\n",
           bin);

    printf("%sRestarting a container:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs restart\n\n", bin);

    printf("%sGetting container information:%s\n", bold, reset);
    printf("  %s --name=mycontainer info\n\n", bin);

    printf("%sScanning for untracked containers:%s\n", bold, reset);
    printf("  %s scan\n\n", bin);

    printf("%sStopping multiple containers:%s\n", bold, reset);
    printf("  %s stop --name=container1,container2,container3\n\n", bin);
    break;

  case 2: /* Advanced Usage */
    printf("\n");
    printf("ADVANCED USAGE\n");
    printf("--------------\n\n");

    printf("%sMultiple flags combined:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --hw-access "
           "--enable-ipv6 --hostname=myserver start\n\n",
           bin);

    printf("%sContainer with all Android features:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs --hw-access "
           "--enable-android-storage --selinux-permissive start\n\n",
           bin);

    printf("%sRootfs image with auto-generated name:%s\n", bold, reset);
    printf("  %s --rootfs-img=/path/to/rootfs.img start\n", bin);
    printf("  (Name auto-generated from /etc/os-release)\n\n");

    printf("%sRootfs image with custom name:%s\n", bold, reset);
    printf("  %s --name=myimage --rootfs-img=/path/to/rootfs.img start\n\n",
           bin);

    printf("%sUsing PID file:%s\n", bold, reset);
    printf("  %s --pidfile=/custom/path/container.pid --rootfs=/path/to/rootfs "
           "start\n",
           bin);
    printf("  %s --pidfile=./mycontainer.pid status\n", bin);
    printf("  %s --pidfile=/custom/path/container.pid stop\n\n", bin);

    printf("%sEntering as different user:%s\n", bold, reset);
    printf("  %s --name=mycontainer enter myuser\n\n", bin);

    printf("%sStopping containers with spaces in names:%s\n", bold, reset);
    printf("  %s stop --name=\"container 1,container 2,container 3\"\n\n", bin);

    printf("%sContainer management workflow:%s\n", bold, reset);
    printf("  %s --name=mycontainer --rootfs=/path/to/rootfs start\n", bin);
    printf("  %s --name=mycontainer status\n", bin);
    printf("  %s --name=mycontainer enter\n", bin);
    printf("  %s --name=mycontainer run uname -a\n", bin);
    printf("  %s --name=mycontainer info\n", bin);
    printf("  %s --name=mycontainer stop\n\n", bin);
    break;

  case 3: /* Gigachad Usage */
    printf("\n");
    printf("GIGACHAD USAGE\n");
    printf("--------------\n\n");

    printf("%sEphemeral container (Volatile Mode):%s\n", bold, reset);
    printf("  %s -r /path/to/rootfs --volatile start\n", bin);
    printf("  (All changes are stored in RAM and lost on exit)\n\n");

    printf("%sMultiple bind mounts (Comma-separated or Chained):%s\n", bold,
           reset);
    printf("  %s -r rootfs/ start -B /src1:/dst1,/src2:/dst2\n", bin);
    printf("  %s -r rootfs/ start -B /src1:/dst1 -B /src2:/dst2\n", bin);
    printf("  (Mix and match supported, up to 16 mounts)\n\n");

    printf("%sMulti-DNS Configuration (Comma-separated):%s\n", bold, reset);
    printf("  %s -r rootfs/ start -d 1.1.1.1,8.8.4.4,9.9.9.9\n", bin);
    printf("  (Bypasses default Android/host DNS lookup)\n\n");

    printf("%sContainer with maximum features (not secure):%s\n", bold, reset);
    printf("  %s --name=featureful --rootfs-img=/path/to/rootfs.img \\\n", bin);
    printf("      --hw-access --enable-android-storage --enable-ipv6 \\\n");
    printf("      --selinux-permissive --hostname=feature-box \\\n");
    printf("      --volatile --foreground start\n\n");

    printf("%sComplex command execution with pipes and redirection:%s\n", bold,
           reset);
    printf("  %s --name=mycontainer run sh -c \"ps aux | grep init > "
           "/tmp/init.log\"\n",
           bin);
    printf("  %s --name=mycontainer run sh -c \"cat /etc/os-release | grep "
           "ID\"\n\n",
           bin);

    printf("%sContainer lifecycle with all operations:%s\n", bold, reset);
    printf("  %s --name=test --rootfs=/path/to/rootfs --hw-access "
           "--enable-ipv6 start\n",
           bin);
    printf("  %s --name=test status\n", bin);
    printf("  %s --name=test enter developer\n", bin);
    printf("  %s --name=test run systemctl status\n", bin);
    printf("  %s --name=test run journalctl -n 50\n", bin);
    printf("  %s --name=test info\n", bin);
    printf("  %s --name=test restart\n", bin);
    printf("  %s --name=test stop\n\n", bin);

    printf("%sRootfs image management with mount tracking:%s\n", bold, reset);
    printf("  %s --name=imgtest --rootfs-img=/path/to/rootfs.img start\n", bin);
    printf("  %s --name=imgtest status\n", bin);
    printf("  %s --name=imgtest stop\n", bin);
    printf("  (Mount automatically cleaned up)\n\n");

    printf("%sMulti-container orchestration:%s\n", bold, reset);
    printf("  for name in web db cache; do\n");
    printf("    %s --name=$name --rootfs=/path/to/$name-rootfs start\n", bin);
    printf("  done\n");
    printf("  %s show\n", bin);
    printf("  %s stop --name=web,db,cache\n\n", bin);

    printf("%sMultiple containers with different configurations:%s\n", bold,
           reset);
    printf(
        "  %s --name=web --rootfs=/path/to/web-rootfs --hostname=web start\n",
        bin);
    printf("  %s --name=db --rootfs=/path/to/db-rootfs --hostname=db "
           "--hw-access start\n",
           bin);
    printf(
        "  %s --name=app --rootfs-img=/path/to/app.img --enable-ipv6 start\n",
        bin);
    printf("  %s show\n", bin);
    printf("  %s stop --name=web,db,app\n\n", bin);

    printf("%sError recovery and troubleshooting:%s\n", bold, reset);
    printf("  %s --name=broken status\n", bin);
    printf("  %s scan\n", bin);
    printf("  %s --name=broken info\n", bin);
    printf("  %s --name=broken stop\n", bin);
    printf("  %s --name=broken --rootfs=/path/to/rootfs restart\n\n", bin);
    break;

  case 4: /* Notes */
    printf("\n");
    printf("NOTES\n");
    printf("-----\n\n");
    printf("1. --name and --pidfile are mutually exclusive\n");
    printf("2. --rootfs and --rootfs-img are mutually exclusive\n");
    printf("3. Only one command can be specified at a time\n");
    printf(
        "4. Multi-stop (comma-separated names) only works with stop command\n");
    printf("5. Container names are auto-generated from /etc/os-release if "
           "--name is not provided\n");
    printf("6. PID files are stored in workspace directory\n");
    printf("7. Rootfs images are automatically mounted and unmounted\n");
    printf(
        "8. The scan command can detect containers started outside the tool\n");
    printf("9. All commands require root privileges except: check, show, docs, "
           "version\n");
    printf("10. Foreground mode attaches terminal to container console\n\n");
    break;
  }

  /* Print navigation hint */
  printf("\n");
  printf("%s[←] Previous  [→] Next  [q] Quit%s\n", bold, reset);
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
  (void)sig; /* Unused */
  g_terminal_resized = 1;
}

/* Print documentation with interactive navigation */
void print_documentation(const char *argv0) {
  const char *bin = get_binary_name(argv0);

  /* Check if both stdin and stdout are TTYs for interactive mode */
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    /* Not a TTY - print all documentation non-interactively */
    for (int i = 0; i < TOTAL_PAGES; i++) {
      printf("\n");
      print_page(i, bin);
    }
    return;
  }

  /* Save original terminal settings */
  struct termios old_tios, new_tios;
  if (tcgetattr(STDIN_FILENO, &old_tios) < 0) {
    ds_error("Failed to get terminal attributes: %s", strerror(errno));
    return;
  }

  new_tios = old_tios;
  /* Set raw mode: disable echo, canonical mode, but allow signals for SIGWINCH
   */
  new_tios.c_lflag &= ~(ICANON | ECHO);
  /* Don't disable ISIG - we need SIGWINCH for terminal resize */
  new_tios.c_cc[VMIN] = 1;
  new_tios.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_tios) < 0) {
    ds_error("Failed to set terminal attributes: %s", strerror(errno));
    return;
  }

  /* Setup SIGWINCH handler for terminal resize */
  struct sigaction sa;
  sa.sa_handler = handle_sigwinch;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGWINCH, &sa, NULL) < 0) {
    ds_warn("Failed to setup SIGWINCH handler: %s", strerror(errno));
  }

  /* Set unbuffered output for immediate display */
  setvbuf(stdout, NULL, _IONBF, 0);

  /* Clear screen before starting */
  clear_screen_completely();
  printf("%s", HIDE_CURSOR);
  fflush(stdout);

  int current_page = 0;
  int running = 1;
  g_terminal_resized = 0;

  while (running) {
    /* Check if terminal was resized */
    if (g_terminal_resized) {
      g_terminal_resized = 0;
    }

    /* Clear screen and redraw */
    clear_screen_completely();

    /* Print header */
    print_header(current_page, TOTAL_PAGES, page_titles[current_page]);

    /* Print page content */
    print_page(current_page, bin);

    /* Flush output */
    fflush(stdout);

    /* Read user input */
    int key = read_arrow_key();

    switch (key) {
    case -1: /* Left arrow - previous page */
      if (current_page > 0) {
        current_page--;
      }
      break;
    case 1: /* Right arrow - next page */
      if (current_page < TOTAL_PAGES - 1) {
        current_page++;
      }
      break;
    case 'q': /* Quit */
      running = 0;
      break;
    default:
      /* Ignore other keys, but check for resize on next iteration */
      break;
    }
  }

  /* Restore terminal settings */
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tios);
  printf("%s", SHOW_CURSOR);
  fflush(stdout);

  /* Clear screen completely one final time */
  clear_screen_completely();
}
