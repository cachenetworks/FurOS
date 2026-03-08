#include "shell.h"

#include "disk.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

#define SHELL_LINE_MAX 256
#define SHELL_ARGV_MAX 16
#define SHELL_UID_ROOT 0u
#define SHELL_UID_DEBIAN 1000u

static int shell_cwd = 0;
static uint16_t shell_uid = SHELL_UID_DEBIAN;
static int apt_index_ready = 0;
static int network_manager_running = 1;
static int wifi_enabled = 1;
static int wifi_connected = 0;
static char wifi_ssid[FS_MAX_NAME + 1];

struct package {
    const char *name;
    int installed;
    int essential;
};

static struct package package_repo[] = {
    {"base", 1, 1},
    {"nano", 1, 0},
    {"coreutils", 1, 1},
    {"sudo", 1, 1},
    {"network-manager", 1, 0},
    {"net-tools", 0, 0},
    {"curl", 0, 0}
};

struct wifi_ap {
    const char *ssid;
    const char *password;
};

static const struct wifi_ap wifi_aps[] = {
    {"FurOS-Lab", "furos123"},
    {"DebianCafe", "linuxmint"},
    {"OpenGuest", 0}
};

static void shell_println(const char *text) {
    vga_write_string(text);
    vga_write_string("\n");
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int is_printable(char c) {
    return c >= 32 && c <= 126;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_octal_digit(char c) {
    return c >= '0' && c <= '7';
}

static const char *uid_to_name(uint16_t uid) {
    if (uid == SHELL_UID_ROOT) {
        return "root";
    }
    if (uid == SHELL_UID_DEBIAN) {
        return "debian";
    }
    return "user";
}

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t value = 0;
    size_t i = 0;

    if (s == 0 || out == 0 || s[0] == '\0') {
        return -1;
    }

    while (s[i] != '\0') {
        if (!is_digit(s[i])) {
            return -1;
        }
        value = value * 10u + (uint32_t)(s[i] - '0');
        i++;
    }

    *out = value;
    return 0;
}

static int parse_mode_octal(const char *s, uint16_t *out) {
    uint32_t value = 0;
    size_t i = 0;

    if (s == 0 || out == 0 || s[0] == '\0') {
        return -1;
    }

    while (s[i] != '\0') {
        if (!is_octal_digit(s[i])) {
            return -1;
        }
        value = (value << 3) + (uint32_t)(s[i] - '0');
        if (value > 0777u) {
            return -1;
        }
        i++;
    }

    *out = (uint16_t)value;
    return 0;
}

static int parse_uid(const char *name, uint16_t *out) {
    uint32_t numeric;

    if (name == 0 || out == 0) {
        return -1;
    }

    if (kstrcmp(name, "root") == 0) {
        *out = SHELL_UID_ROOT;
        return 0;
    }
    if (kstrcmp(name, "debian") == 0) {
        *out = SHELL_UID_DEBIAN;
        return 0;
    }

    if (parse_u32(name, &numeric) != 0 || numeric > 65535u) {
        return -1;
    }

    *out = (uint16_t)numeric;
    return 0;
}

static int require_root(uint16_t uid) {
    if (uid != SHELL_UID_ROOT) {
        shell_println("permission denied (requires root, use sudo)");
        return -1;
    }
    return 0;
}

static int package_is_installed(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(package_repo) / sizeof(package_repo[0]); i++) {
        if (kstrcmp(package_repo[i].name, name) == 0) {
            return package_repo[i].installed;
        }
    }
    return 0;
}

static struct package *package_find(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(package_repo) / sizeof(package_repo[0]); i++) {
        if (kstrcmp(package_repo[i].name, name) == 0) {
            return &package_repo[i];
        }
    }
    return 0;
}

static const struct wifi_ap *find_wifi_ap(const char *ssid) {
    size_t i;
    for (i = 0; i < sizeof(wifi_aps) / sizeof(wifi_aps[0]); i++) {
        if (kstrcmp(wifi_aps[i].ssid, ssid) == 0) {
            return &wifi_aps[i];
        }
    }
    return 0;
}

static int network_ready(void) {
    return package_is_installed("network-manager") && network_manager_running && wifi_enabled && wifi_connected;
}

static void sync_state_with_packages(void) {
    if (!package_is_installed("network-manager")) {
        network_manager_running = 0;
        wifi_enabled = 0;
        wifi_connected = 0;
        wifi_ssid[0] = '\0';
    }
}

static int resolve_parent_path(const char *path, int *parent_out) {
    size_t len;
    size_t i;
    size_t slash = (size_t)-1;

    if (path == 0 || parent_out == 0) {
        return -1;
    }

    len = kstrlen(path);
    if (len == 0) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    if (slash == (size_t)-1) {
        *parent_out = shell_cwd;
        return 0;
    }

    if (slash == len - 1) {
        return -1;
    }

    if (slash == 0) {
        *parent_out = fs_get_root();
        return 0;
    }

    {
        char parent_path[128];
        if (slash >= sizeof(parent_path)) {
            return -1;
        }
        kmemcpy(parent_path, path, slash);
        parent_path[slash] = '\0';
        *parent_out = fs_resolve(shell_cwd, parent_path);
    }

    return (*parent_out >= 0 && fs_is_dir(*parent_out)) ? 0 : -1;
}

static int ensure_parent_writable(const char *path, uint16_t uid) {
    int parent;

    if (resolve_parent_path(path, &parent) != 0) {
        shell_println("path: invalid parent");
        return -1;
    }

    if (!fs_check_access(parent, uid, FS_ACCESS_WRITE | FS_ACCESS_EXEC)) {
        shell_println("permission denied");
        return -1;
    }

    return 0;
}

static size_t shell_read_line(char *buf, size_t max_len) {
    size_t len = 0;

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            vga_putchar('\n');
            return len;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                vga_backspace();
            }
            continue;
        }

        if (is_printable(c) && len + 1 < max_len) {
            buf[len++] = c;
            vga_putchar(c);
        }
    }
}

static int shell_parse(char *line, char *argv[], int max_argv) {
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max_argv) {
        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        argv[argc++] = p;

        while (*p != '\0' && !is_space(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        *p = '\0';
        p++;
    }

    return argc;
}

static int shell_build_path(int node, char *out, size_t out_size) {
    if (fs_make_path(node, out, out_size) != 0) {
        return -1;
    }
    return 0;
}

static void shell_prompt(void) {
    char path[128];
    if (shell_build_path(shell_cwd, path, sizeof(path)) != 0) {
        kstrcpy(path, "/");
    }
    vga_write_string(uid_to_name(shell_uid));
    vga_write_string("@fursos:");
    vga_write_string(path);
    vga_write_string(shell_uid == SHELL_UID_ROOT ? " # " : " $ ");
}

static void cmd_help(void) {
    shell_println("Commands:");
    shell_println("  help");
    shell_println("  clear");
    shell_println("  whoami");
    shell_println("  id");
    shell_println("  pwd");
    shell_println("  ls [path]");
    shell_println("  cd <path>");
    shell_println("  mkdir <path>");
    shell_println("  touch <path>");
    shell_println("  cat <file>");
    shell_println("  write <file> <text>");
    shell_println("  nano <file>");
    shell_println("  rm <path>");
    shell_println("  chmod <mode> <path>");
    shell_println("  chown <user> <path>");
    shell_println("  sudo <command>");
    shell_println("  install");
    shell_println("  disk info|load|save|format");
    shell_println("  sync");
    shell_println("  apt update|list|install|remove <pkg>");
    shell_println("  networkmanager status|start|stop|restart");
    shell_println("  nmcli general|device|connection|radio|networking ...");
    shell_println("  ifconfig");
}

static void cmd_ls(int argc, char *argv[], uint16_t uid) {
    int target = shell_cwd;
    int entries[FS_MAX_NODES];
    int count;
    int i;

    if (argc > 1) {
        target = fs_resolve(shell_cwd, argv[1]);
    }

    if (target < 0) {
        shell_println("ls: path not found");
        return;
    }
    if (!fs_is_dir(target)) {
        shell_println("ls: not a directory");
        return;
    }
    if (!fs_check_access(target, uid, FS_ACCESS_READ | FS_ACCESS_EXEC)) {
        shell_println("ls: permission denied");
        return;
    }

    count = fs_list(target, entries, FS_MAX_NODES);
    if (count <= 0) {
        shell_println("(empty)");
        return;
    }

    for (i = 0; i < count; i++) {
        const char *name = fs_get_name(entries[i]);
        if (name == 0) {
            continue;
        }
        vga_write_string(name);
        if (fs_is_dir(entries[i])) {
            vga_write_string("/");
        }
        vga_write_string("\n");
    }
}

static void cmd_pwd(void) {
    char path[128];
    if (shell_build_path(shell_cwd, path, sizeof(path)) != 0) {
        shell_println("/");
        return;
    }
    shell_println(path);
}

static void cmd_cd(int argc, char *argv[], uint16_t uid) {
    int target;

    if (argc < 2) {
        shell_cwd = fs_get_root();
        return;
    }

    target = fs_resolve(shell_cwd, argv[1]);
    if (target < 0 || !fs_is_dir(target)) {
        shell_println("cd: directory not found");
        return;
    }
    if (!fs_check_access(target, uid, FS_ACCESS_EXEC)) {
        shell_println("cd: permission denied");
        return;
    }

    shell_cwd = target;
}

static void cmd_mkdir(int argc, char *argv[], uint16_t uid) {
    int existing;

    if (argc < 2) {
        shell_println("mkdir: missing operand");
        return;
    }

    existing = fs_resolve(shell_cwd, argv[1]);
    if (existing >= 0) {
        if (fs_is_dir(existing)) {
            return;
        }
        shell_println("mkdir: path exists as file");
        return;
    }

    if (ensure_parent_writable(argv[1], uid) != 0) {
        return;
    }

    if (fs_mkdir_as(shell_cwd, argv[1], uid, FS_DEFAULT_DIR_MODE) != 0) {
        shell_println("mkdir: failed");
    }
}

static void cmd_touch(int argc, char *argv[], uint16_t uid) {
    int node;

    if (argc < 2) {
        shell_println("touch: missing operand");
        return;
    }

    node = fs_resolve(shell_cwd, argv[1]);
    if (node >= 0) {
        if (fs_is_dir(node)) {
            shell_println("touch: is a directory");
            return;
        }
        if (!fs_check_access(node, uid, FS_ACCESS_WRITE)) {
            shell_println("touch: permission denied");
        }
        return;
    }

    if (ensure_parent_writable(argv[1], uid) != 0) {
        return;
    }

    if (fs_touch_as(shell_cwd, argv[1], uid, FS_DEFAULT_FILE_MODE) != 0) {
        shell_println("touch: failed");
    }
}

static void cmd_cat(int argc, char *argv[], uint16_t uid) {
    const char *data;
    size_t size;
    int node;

    if (argc < 2) {
        shell_println("cat: missing operand");
        return;
    }
    node = fs_resolve(shell_cwd, argv[1]);
    if (node < 0 || fs_is_dir(node)) {
        shell_println("cat: file not found");
        return;
    }
    if (!fs_check_access(node, uid, FS_ACCESS_READ)) {
        shell_println("cat: permission denied");
        return;
    }
    if (fs_read_file(shell_cwd, argv[1], &data, &size) != 0) {
        shell_println("cat: file not found");
        return;
    }
    if (size == 0) {
        shell_println("(empty file)");
        return;
    }
    vga_write_string(data);
    if (data[size - 1] != '\n') {
        vga_write_string("\n");
    }
}

static void cmd_write(int argc, char *argv[]) {
    char buffer[FS_MAX_FILE_SIZE];
    size_t pos = 0;
    int i;

    if (argc < 3) {
        shell_println("write: usage write <file> <text>");
        return;
    }

    for (i = 2; i < argc; i++) {
        size_t len = kstrlen(argv[i]);
        if (pos + len + 2 >= sizeof(buffer)) {
            break;
        }
        kmemcpy(buffer + pos, argv[i], len);
        pos += len;
        if (i + 1 < argc) {
            buffer[pos++] = ' ';
        }
    }
    buffer[pos] = '\0';

    if (fs_write_file(shell_cwd, argv[1], buffer, pos) != 0) {
        shell_println("write: failed");
    }
}

static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2) {
        shell_println("rm: missing operand");
        return;
    }
    if (fs_remove(shell_cwd, argv[1]) != 0) {
        shell_println("rm: failed (path missing or directory not empty)");
    }
}

static void cmd_install(void) {
    shell_println("FurOS installer (terminal mode)");
    if (!disk_available()) {
        shell_println("No supported ATA disk detected.");
        shell_println("Use QEMU with an IDE disk or attach an IDE disk in your VM.");
        return;
    }

    shell_println("Formatting target disk layout...");
    if (disk_format_fs() != 0) {
        shell_println("install: disk format failed");
        shell_println(disk_last_error());
        return;
    }

    fs_mkdir(fs_get_root(), "bin");
    fs_mkdir(fs_get_root(), "etc");
    fs_mkdir(fs_get_root(), "home");
    {
        const char *release_text = "FurOS installed system\n";
        fs_write_file(fs_get_root(), "etc/release", release_text, kstrlen(release_text));
    }

    if (disk_save_fs() != 0) {
        shell_println("install: write failed");
        shell_println(disk_last_error());
        return;
    }

    shell_println("Install complete.");
    shell_println("Reboot and run 'disk load' to restore installed state.");
}

static void cmd_sync(void) {
    if (disk_save_fs() != 0) {
        shell_println("sync: failed");
        shell_println(disk_last_error());
        return;
    }
    shell_println("sync: saved to disk");
}

static void cmd_disk(int argc, char *argv[]) {
    if (argc < 2) {
        shell_println("disk: usage disk info|load|save|format");
        return;
    }

    if (kstrcmp(argv[1], "info") == 0) {
        shell_println(disk_available() ? "disk: available" : "disk: unavailable");
        if (disk_last_error() != 0) {
            vga_write_string("status: ");
            vga_write_string(disk_last_error());
            vga_write_string("\n");
        }
        return;
    }

    if (kstrcmp(argv[1], "load") == 0) {
        if (disk_load_fs() != 0) {
            shell_println("disk load: failed");
            shell_println(disk_last_error());
            return;
        }
        shell_cwd = fs_get_root();
        shell_println("disk load: ok");
        return;
    }

    if (kstrcmp(argv[1], "save") == 0) {
        cmd_sync();
        return;
    }

    if (kstrcmp(argv[1], "format") == 0) {
        if (disk_format_fs() != 0) {
            shell_println("disk format: failed");
            shell_println(disk_last_error());
            return;
        }
        shell_cwd = fs_get_root();
        shell_println("disk format: ok");
        return;
    }

    shell_println("disk: unknown subcommand");
}

static void cmd_apt(int argc, char *argv[]) {
    size_t i;

    if (argc < 2) {
        shell_println("apt: usage apt list|install|remove <pkg>");
        return;
    }

    if (kstrcmp(argv[1], "list") == 0) {
        for (i = 0; i < sizeof(package_repo) / sizeof(package_repo[0]); i++) {
            vga_write_string(package_repo[i].name);
            vga_write_string(" - ");
            vga_write_string(package_repo[i].installed ? "installed" : "available");
            vga_write_string("\n");
        }
        return;
    }

    if (argc < 3) {
        shell_println("apt: package name required");
        return;
    }

    for (i = 0; i < sizeof(package_repo) / sizeof(package_repo[0]); i++) {
        if (kstrcmp(argv[2], package_repo[i].name) == 0) {
            if (kstrcmp(argv[1], "install") == 0) {
                package_repo[i].installed = 1;
                shell_println("apt: package installed");
                return;
            }
            if (kstrcmp(argv[1], "remove") == 0) {
                if (kstrcmp(package_repo[i].name, "base") == 0) {
                    shell_println("apt: refusing to remove base");
                    return;
                }
                package_repo[i].installed = 0;
                shell_println("apt: package removed");
                return;
            }
        }
    }

    shell_println("apt: package not found");
}

static void cmd_nano(int argc, char *argv[]) {
    char buffer[FS_MAX_FILE_SIZE];
    size_t len = 0;
    const char *existing = 0;
    size_t existing_len = 0;
    int dirty = 0;
    char c;

    if (argc < 2) {
        shell_println("nano: usage nano <file>");
        return;
    }

    if (fs_touch(shell_cwd, argv[1]) != 0 || fs_read_file(shell_cwd, argv[1], &existing, &existing_len) != 0) {
        shell_println("nano: unable to open file");
        return;
    }

    if (fs_read_file(shell_cwd, argv[1], &existing, &existing_len) == 0 && existing != 0) {
        if (existing_len >= sizeof(buffer)) {
            existing_len = sizeof(buffer) - 1;
        }
        kmemcpy(buffer, existing, existing_len);
        len = existing_len;
    }
    buffer[len] = '\0';

    shell_println("--- FurNano ---");
    shell_println("Ctrl+S save, Ctrl+X save+exit");
    shell_println("---------------");
    if (len > 0) {
        vga_write_string(buffer);
    }

    while (1) {
        c = keyboard_getchar();

        if (c == 19) {
            fs_write_file(shell_cwd, argv[1], buffer, len);
            dirty = 0;
            shell_println("\n[saved]");
            continue;
        }

        if (c == 24) {
            if (dirty) {
                fs_write_file(shell_cwd, argv[1], buffer, len);
            }
            shell_println("\n[exit nano]");
            return;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                buffer[len] = '\0';
                vga_backspace();
                dirty = 1;
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (len + 1 < sizeof(buffer)) {
                buffer[len++] = '\n';
                buffer[len] = '\0';
                vga_putchar('\n');
                dirty = 1;
            }
            continue;
        }

        if (is_printable(c) && len + 1 < sizeof(buffer)) {
            buffer[len++] = c;
            buffer[len] = '\0';
            vga_putchar(c);
            dirty = 1;
        }
    }
}

static void shell_seed_filesystem(void) {
    const char *motd = "Welcome to FurOS terminal mode.\n";
    fs_mkdir(fs_get_root(), "home");
    fs_mkdir(fs_get_root(), "etc");
    fs_write_file(fs_get_root(), "etc/motd", motd, kstrlen(motd));
}

__attribute__((noreturn))
void shell_run(void) {
    char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARGV_MAX];
    int argc;

    keyboard_init();
    fs_init();
    disk_init();

    if (disk_available() && disk_load_fs() == 0) {
        shell_println("Loaded filesystem from disk.");
    } else {
        shell_seed_filesystem();
        if (!disk_available()) {
            shell_println("No ATA disk found. Running in live mode.");
        } else {
            shell_println("No installed filesystem found. Running in live mode.");
        }
    }

    shell_cwd = fs_get_root();

    shell_println("Entering FurOS terminal installer shell.");
    shell_println("Type 'help' for commands.");

    while (1) {
        shell_prompt();
        shell_read_line(line, sizeof(line));
        argc = shell_parse(line, argv, SHELL_ARGV_MAX);
        if (argc == 0) {
            continue;
        }

        if (kstrcmp(argv[0], "help") == 0) {
            cmd_help();
        } else if (kstrcmp(argv[0], "clear") == 0) {
            vga_clear();
        } else if (kstrcmp(argv[0], "pwd") == 0) {
            cmd_pwd();
        } else if (kstrcmp(argv[0], "ls") == 0) {
            cmd_ls(argc, argv, shell_uid);
        } else if (kstrcmp(argv[0], "cd") == 0) {
            cmd_cd(argc, argv, shell_uid);
        } else if (kstrcmp(argv[0], "mkdir") == 0) {
            cmd_mkdir(argc, argv, shell_uid);
        } else if (kstrcmp(argv[0], "touch") == 0) {
            cmd_touch(argc, argv, shell_uid);
        } else if (kstrcmp(argv[0], "cat") == 0) {
            cmd_cat(argc, argv, shell_uid);
        } else if (kstrcmp(argv[0], "write") == 0) {
            cmd_write(argc, argv);
        } else if (kstrcmp(argv[0], "nano") == 0) {
            cmd_nano(argc, argv);
        } else if (kstrcmp(argv[0], "rm") == 0) {
            cmd_rm(argc, argv);
        } else if (kstrcmp(argv[0], "install") == 0) {
            cmd_install();
        } else if (kstrcmp(argv[0], "disk") == 0) {
            cmd_disk(argc, argv);
        } else if (kstrcmp(argv[0], "sync") == 0) {
            cmd_sync();
        } else if (kstrcmp(argv[0], "apt") == 0) {
            cmd_apt(argc, argv);
        } else {
            shell_println("unknown command");
        }
    }
}
