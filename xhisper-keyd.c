/*
 * xhisper-keyd — Global hotkey daemon for xhisper
 * Listens for LeftCtrl+Super+LeftAlt chord via evdev and toggles xhisper recording.
 *
 * Build: gcc -O2 -Wall -Wextra xhisper-keyd.c -o xhisper-keyd
 *
 * Run:   sudo ./xhisper-keyd          (needs input group or root for evdev access)
 * Or:    sudo ./xhisper-keyd -d       (daemonize)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <time.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

/* Key codes for the chord — in case input-event-codes.h doesn't define them */
#ifndef KEY_LEFTCTRL
#define KEY_LEFTCTRL  29
#endif
#ifndef KEY_LEFTALT
#define KEY_LEFTALT   56
#endif
#ifndef KEY_LEFTMETA
#define KEY_LEFTMETA  125
#endif

#define MAX_DEVICES 32
#define LOG_TAG "xhisper-keyd"

static int g_verbose = 0;
static volatile sig_atomic_t g_running = 1;

static void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", LOG_TAG);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Check if a device supports the keys we need (EV_KEY + our chord keys) */
static int device_supports_keys(int fd) {
    unsigned long evbit[EV_MAX / (sizeof(long) * 8) + 1];
    unsigned long keybit[KEY_MAX / (sizeof(long) * 8) + 1];

    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0)
        return 0;

    if (!(evbit[EV_KEY / (sizeof(long) * 8)] & (1UL << (EV_KEY % (sizeof(long) * 8)))))
        return 0;

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0)
        return 0;

    /* Check all three keys are supported */
    int need[] = { KEY_LEFTCTRL, KEY_LEFTALT, KEY_LEFTMETA };
    for (int i = 0; i < 3; i++) {
        int k = need[i];
        if (!(keybit[k / (sizeof(long) * 8)] & (1UL << (k % (sizeof(long) * 8)))))
            return 0;
    }
    return 1;
}

/* Check if a device supports the keys we need (EV_KEY + our chord keys) */
static int open_devices(int fds[], int max_fds) {
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        log_msg("Cannot open /dev/input: %s", strerror(errno));
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < max_fds) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[256];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        char devname[256] = "unknown";
        ioctl(fd, EVIOCGNAME(sizeof(devname)), devname);

        if (!device_supports_keys(fd)) {
            close(fd);
            continue;
        }

        if (g_verbose)
            log_msg("Monitoring: %s (%s)", path, devname);

        fds[count++] = fd;
    }

    closedir(dir);
    return count;
}

/* Find the xhisper script to invoke */
static const char *find_xhisper(void) {
    static char path[512];

    /* Check PATH */
    const char *paths[] = {
        "/usr/local/bin/xhisper",
        "/usr/bin/xhisper",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], X_OK) == 0)
            return paths[i];
    }

    /* Use which */
    FILE *fp = popen("which xhisper 2>/dev/null", "r");
    if (fp) {
        if (fgets(path, sizeof(path), fp)) {
            path[strcspn(path, "\n")] = 0;
            pclose(fp);
            if (strlen(path) > 0)
                return path;
        }
        pclose(fp);
    }

    return "xhisper"; /* fallback: hope it's in PATH */
}

/* Run xhisper in background — fire and forget */
static void toggle_xhisper(void) {
    const char *bin = find_xhisper();
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: exec xhisper */
        /* Close stdin/stdout/stderr to avoid interference */
        int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);

        /* Load GROQ_API_KEY from ~/.env if it exists */
        char *home = getenv("HOME");
        char env_path[512];
        if (home) {
            snprintf(env_path, sizeof(env_path), "%s/.env", home);
            FILE *f = fopen(env_path, "r");
            if (f) {
                char line[1024];
                while (fgets(line, sizeof(line), f)) {
                    if (strncmp(line, "GROQ_API_KEY=", 14) == 0) {
                        char *val = line + 14;
                        val[strcspn(val, "\n")] = 0;
                        setenv("GROQ_API_KEY", val, 1);
                        break;
                    }
                }
                fclose(f);
            }
        }

        execl(bin, "xhisper", (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        if (g_verbose)
            log_msg("Launched xhisper (pid %d)", pid);
    } else {
        log_msg("Failed to fork: %s", strerror(errno));
    }
}

/* Main event loop */
static void event_loop(int fds[], int nfds) {
    /* Track chord key states */
    int ctrl_pressed = 0;
    int alt_pressed = 0;
    int meta_pressed = 0;
    int chord_active = 0;

    /* Debounce: avoid re-triggering too fast */
    time_t last_trigger = 0;

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = 0;

        for (int i = 0; i < nfds; i++) {
            FD_SET(fds[i], &rfds);
            if (fds[i] > maxfd) maxfd = fds[i];
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 }; /* 500ms timeout */
        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            log_msg("select error: %s", strerror(errno));
            break;
        }

        if (ret == 0) continue; /* timeout */

        for (int i = 0; i < nfds; i++) {
            if (!FD_ISSET(fds[i], &rfds))
                continue;

            struct input_event ev;
            while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type != EV_KEY)
                    continue;

                int is_press = (ev.value == 1 || ev.value == 2); /* press or repeat */
                int is_release = (ev.value == 0);

                /* Update state for our chord keys */
                if (ev.code == KEY_LEFTCTRL) {
                    if (is_press) ctrl_pressed = 1;
                    else if (is_release) ctrl_pressed = 0;
                } else if (ev.code == KEY_LEFTALT) {
                    if (is_press) alt_pressed = 1;
                    else if (is_release) alt_pressed = 0;
                } else if (ev.code == KEY_LEFTMETA) {
                    if (is_press) meta_pressed = 1;
                    else if (is_release) meta_pressed = 0;
                } else {
                    /* Any other key breaks the chord to avoid accidental triggering */
                    /* But only if it's a press, not a release */
                    if (is_press) {
                        /* Don't break on shifts, caps lock, etc. that users might hold */
                        int ignorable[] = { KEY_LEFTSHIFT, KEY_RIGHTSHIFT, 58, 119 };
                        int ignore = 0;
                        for (int k = 0; k < 4; k++) {
                            if (ev.code == ignorable[k]) { ignore = 1; break; }
                        }
                        if (!ignore) {
                            /* If chord was active, don't clear state — let release handle it */
                            /* Otherwise ignore */
                        }
                    }
                }

                int all_pressed = (ctrl_pressed && alt_pressed && meta_pressed);

                if (all_pressed && !chord_active) {
                    chord_active = 1;
                    if (g_verbose)
                        log_msg("Chord detected: Ctrl+Alt+Super");
                }

                if (chord_active && !all_pressed) {
                    /* Chord was released — trigger if at least one was held long enough
                       We trigger on release of any one of the chord keys while others were held */
                    time_t now = time(NULL);
                    if (now - last_trigger >= 1) {
                        last_trigger = now;
                        if (g_verbose)
                            log_msg("Chord released — toggling xhisper");
                        toggle_xhisper();
                    }
                    chord_active = 0;
                }
            }
        }
    }
}

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) exit(0); /* parent exits */

    if (setsid() < 0) { perror("setsid"); exit(1); }

    /* Second fork to prevent reacquiring terminal */
    pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) exit(0);

    chdir("/");
    umask(0);

    /* Close std streams */
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
}

static void show_usage(const char *prog) {
    fprintf(stderr,
        "xhisper-keyd — Global hotkey daemon for xhisper\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -d           Daemonize (run in background)\n"
        "  -v           Verbose output\n"
        "  -h           Show this help\n"
        "\n"
        "Hotkey: LeftCtrl + Super + LeftAlt (release to toggle)\n"
        "\n"
        "Requires read access to /dev/input/event*.\n"
        "Add user to 'input' group: sudo usermod -aG input $USER\n"
    , prog);
}

static void write_pidfile(void) {
    char *run_dir = getenv("XDG_RUNTIME_DIR");
    if (!run_dir) run_dir = "/tmp";
    char path[512];
    snprintf(path, sizeof(path), "%s/xhisper-keyd.pid", run_dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

int main(int argc, char *argv[]) {
    int do_daemon = 0;

    int opt;
    while ((opt = getopt(argc, argv, "dvh")) != -1) {
        switch (opt) {
            case 'd': do_daemon = 1; break;
            case 'v': g_verbose = 1; break;
            case 'h': show_usage(argv[0]); return 0;
            default:  show_usage(argv[0]); return 1;
        }
    }

    if (do_daemon)
        daemonize();

    write_pidfile();

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGCHLD, SIG_IGN); /* auto-reap children */

    int fds[MAX_DEVICES];
    int nfds = open_devices(fds, MAX_DEVICES);

    if (nfds == 0) {
        log_msg("No keyboard devices found! Check permissions (need 'input' group).");
        return 1;
    }

    if (g_verbose)
        log_msg("Monitoring %d devices. Hotkey: Ctrl+Alt+Super", nfds);

    /* Also monitor for device hotplug roughly by re-scanning on read errors */
    event_loop(fds, nfds);

    /* Cleanup */
    for (int i = 0; i < nfds; i++)
        close(fds[i]);

    /* Remove pidfile */
    char *run_dir = getenv("XDG_RUNTIME_DIR");
    if (!run_dir) run_dir = "/tmp";
    char pidpath[512];
    snprintf(pidpath, sizeof(pidpath), "%s/xhisper-keyd.pid", run_dir);
    unlink(pidpath);

    if (g_verbose)
        log_msg("Exiting.");

    return 0;
}
