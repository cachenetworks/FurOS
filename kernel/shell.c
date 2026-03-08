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
    {"base",            1, 1},
    {"bash",            1, 1},
    {"coreutils",       1, 1},
    {"sudo",            1, 1},
    {"nano",            1, 0},
    {"less",            1, 0},
    {"network-manager", 1, 0},
    {"net-tools",       0, 0},
    {"curl",            0, 0},
    {"openssh-client",  0, 0},
    {"vim",             0, 0},
    {"git",             0, 0}
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

static void shell_print_uint(uint32_t value) {
    char buf[12];
    int i = 0;
    int j;
    if (value == 0) {
        vga_putchar('0');
        return;
    }
    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (j = i - 1; j >= 0; j--) {
        vga_putchar(buf[j]);
    }
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
    vga_write_string("@furos:");
    vga_write_string(path);
    vga_write_string(shell_uid == SHELL_UID_ROOT ? " # " : " $ ");
}

static void cmd_help(void) {
    shell_println("FurOS 1.0 (Debian-based) -- available commands:");
    shell_println("");
    shell_println("  File system:");
    shell_println("    pwd                     print working directory");
    shell_println("    ls [path]               list directory contents");
    shell_println("    cd <path>               change directory");
    shell_println("    mkdir <path>            create directory");
    shell_println("    touch <path>            create empty file");
    shell_println("    cat <file>              print file contents");
    shell_println("    write <file> <text>     write text to file");
    shell_println("    nano <file>             edit file (Ctrl+S save, Ctrl+X exit)");
    shell_println("    rm <path>               remove file or empty directory");
    shell_println("    chmod <mode> <path>     change permissions (octal, e.g. 755)");
    shell_println("    chown <user> <path>     change file owner (root only)");
    shell_println("");
    shell_println("  Users:");
    shell_println("    whoami                  print current user name");
    shell_println("    id                      print uid/gid/groups");
    shell_println("    sudo <command>          run command as root");
    shell_println("");
    shell_println("  System:");
    shell_println("    help                    show this help");
    shell_println("    clear                   clear the screen");
    shell_println("    install                 install FurOS to disk");
    shell_println("    disk info|load|save|format");
    shell_println("    sync                    save filesystem to disk");
    shell_println("");
    shell_println("  Packages:");
    shell_println("    apt update              refresh package index");
    shell_println("    apt list                list all packages");
    shell_println("    apt install <pkg>       install a package");
    shell_println("    apt remove <pkg>        remove a package");
    shell_println("");
    shell_println("  Network:");
    shell_println("    networkmanager status|start|stop|restart");
    shell_println("    nmcli general|device|connection|radio|networking ...");
    shell_println("    ifconfig                show network interfaces");
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

static void cmd_whoami(void) {
    shell_println(uid_to_name(shell_uid));
}

static void cmd_id(void) {
    uint16_t uid = shell_uid;
    const char *name = uid_to_name(uid);
    vga_write_string("uid=");
    shell_print_uint(uid);
    vga_write_string("(");
    vga_write_string(name);
    vga_write_string(") gid=");
    shell_print_uint(uid);
    vga_write_string("(");
    vga_write_string(name);
    vga_write_string(") groups=");
    shell_print_uint(uid);
    vga_write_string("(");
    vga_write_string(name);
    vga_write_string(")");
    if (uid != SHELL_UID_ROOT) {
        vga_write_string(",27(sudo)");
    }
    vga_write_string("\n");
}

static void cmd_chmod(int argc, char *argv[], uint16_t uid) {
    uint16_t mode;
    int node;

    if (argc < 3) {
        shell_println("chmod: usage chmod <mode> <path>");
        return;
    }
    if (parse_mode_octal(argv[1], &mode) != 0) {
        shell_println("chmod: invalid mode (use octal, e.g. 755)");
        return;
    }
    node = fs_resolve(shell_cwd, argv[2]);
    if (node < 0) {
        shell_println("chmod: path not found");
        return;
    }
    if (uid != SHELL_UID_ROOT && (uint16_t)fs_get_owner(node) != uid) {
        shell_println("chmod: permission denied");
        return;
    }
    fs_set_mode(node, mode);
}

static void cmd_chown(int argc, char *argv[], uint16_t uid) {
    uint16_t new_owner;
    int node;

    if (argc < 3) {
        shell_println("chown: usage chown <user> <path>");
        return;
    }
    if (require_root(uid) != 0) {
        return;
    }
    if (parse_uid(argv[1], &new_owner) != 0) {
        shell_println("chown: invalid user (use root, debian, or numeric uid)");
        return;
    }
    node = fs_resolve(shell_cwd, argv[2]);
    if (node < 0) {
        shell_println("chown: path not found");
        return;
    }
    fs_set_owner(node, new_owner);
}

/* Forward declaration so cmd_sudo can call it */
static void shell_dispatch(int argc, char *argv[]);

static void cmd_sudo(int argc, char *argv[]) {
    uint16_t saved_uid;
    if (argc < 2) {
        shell_println("sudo: usage sudo <command> [args]");
        return;
    }
    saved_uid = shell_uid;
    shell_uid = SHELL_UID_ROOT;
    shell_dispatch(argc - 1, argv + 1);
    shell_uid = saved_uid;
}

static void cmd_networkmanager(int argc, char *argv[]) {
    if (argc < 2) {
        shell_println("networkmanager: usage networkmanager status|start|stop|restart");
        return;
    }
    if (!package_is_installed("network-manager")) {
        shell_println("networkmanager: not installed (try: sudo apt install network-manager)");
        return;
    }
    if (kstrcmp(argv[1], "status") == 0) {
        vga_write_string("NetworkManager: ");
        shell_println(network_manager_running ? "active (running)" : "inactive (stopped)");
    } else if (kstrcmp(argv[1], "start") == 0) {
        network_manager_running = 1;
        shell_println("Started NetworkManager");
    } else if (kstrcmp(argv[1], "stop") == 0) {
        network_manager_running = 0;
        wifi_connected = 0;
        shell_println("Stopped NetworkManager");
    } else if (kstrcmp(argv[1], "restart") == 0) {
        network_manager_running = 1;
        shell_println("Restarted NetworkManager");
    } else {
        shell_println("networkmanager: unknown subcommand (status|start|stop|restart)");
    }
}

static void cmd_nmcli(int argc, char *argv[]) {
    size_t i;

    if (argc < 2) {
        shell_println("nmcli: usage nmcli general|device|connection|radio|networking ...");
        return;
    }
    if (!package_is_installed("network-manager")) {
        shell_println("nmcli: NetworkManager not installed");
        return;
    }
    if (!network_manager_running) {
        shell_println("Error: NetworkManager is not running.");
        return;
    }

    if (kstrcmp(argv[1], "general") == 0) {
        shell_println("STATE      CONNECTIVITY  WIFI-HW  WIFI     WWAN-HW  WWAN");
        if (wifi_connected) {
            vga_write_string("connected  full          enabled  enabled  enabled  enabled\n");
        } else {
            vga_write_string("disconnected  none       enabled  ");
            vga_write_string(wifi_enabled ? "enabled  " : "disabled ");
            vga_write_string("enabled  enabled\n");
        }
    } else if (kstrcmp(argv[1], "device") == 0) {
        if (argc < 3 || kstrcmp(argv[2], "status") == 0) {
            shell_println("DEVICE  TYPE      STATE         CONNECTION");
            vga_write_string("wlan0   wifi      ");
            if (wifi_connected) {
                vga_write_string("connected     ");
                vga_write_string(wifi_ssid);
            } else if (wifi_enabled) {
                vga_write_string("disconnected  --");
            } else {
                vga_write_string("unavailable   --");
            }
            vga_write_string("\n");
            shell_println("lo      loopback  unmanaged     --");
        } else if (kstrcmp(argv[2], "wifi") == 0) {
            if (argc < 4 || kstrcmp(argv[3], "list") == 0) {
                shell_println("IN-USE  SSID         MODE   CHAN  RATE       SIGNAL  BARS  SECURITY");
                for (i = 0; i < sizeof(wifi_aps) / sizeof(wifi_aps[0]); i++) {
                    int active = wifi_connected && kstrcmp(wifi_ssid, wifi_aps[i].ssid) == 0;
                    vga_write_string(active ? "*  " : "   ");
                    vga_write_string(wifi_aps[i].ssid);
                    vga_write_string("  Infra  6     54 Mbit/s  75      ***   ");
                    vga_write_string(wifi_aps[i].password != 0 ? "WPA2" : "--");
                    vga_write_string("\n");
                }
            } else if (kstrcmp(argv[3], "connect") == 0) {
                const struct wifi_ap *ap;
                if (argc < 5) {
                    shell_println("nmcli: usage nmcli device wifi connect <ssid> [password <pw>]");
                    return;
                }
                ap = find_wifi_ap(argv[4]);
                if (ap == 0) {
                    shell_println("Error: No network with SSID found.");
                    return;
                }
                if (!wifi_enabled) {
                    shell_println("Error: wifi radio is disabled.");
                    return;
                }
                if (ap->password != 0) {
                    if (argc < 7 || kstrcmp(argv[5], "password") != 0) {
                        shell_println("Error: 802-11-wireless-security.psk: password required.");
                        return;
                    }
                    if (kstrcmp(argv[6], ap->password) != 0) {
                        shell_println("Error: Secrets were required, but not provided.");
                        return;
                    }
                }
                wifi_connected = 1;
                kstrncpy(wifi_ssid, argv[4], sizeof(wifi_ssid) - 1);
                wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
                vga_write_string("Device 'wlan0' successfully activated with network '");
                vga_write_string(wifi_ssid);
                vga_write_string("'\n");
            } else {
                shell_println("nmcli device wifi: unknown subcommand (list|connect)");
            }
        } else {
            shell_println("nmcli device: unknown subcommand (status|wifi)");
        }
    } else if (kstrcmp(argv[1], "connection") == 0) {
        if (wifi_connected) {
            shell_println("NAME     UUID                                  TYPE  DEVICE");
            vga_write_string(wifi_ssid);
            vga_write_string("  a1b2c3d4-e5f6-7890-abcd-ef1234567890  wifi  wlan0\n");
        } else {
            shell_println("(no active connections)");
        }
    } else if (kstrcmp(argv[1], "radio") == 0) {
        if (argc < 3) {
            shell_println("WIFI-HW  WIFI     WWAN-HW  WWAN");
            vga_write_string("enabled  ");
            vga_write_string(wifi_enabled ? "enabled  " : "disabled ");
            vga_write_string("enabled  enabled\n");
        } else if (kstrcmp(argv[2], "wifi") == 0) {
            if (argc < 4) {
                vga_write_string("wifi: ");
                shell_println(wifi_enabled ? "enabled" : "disabled");
            } else if (kstrcmp(argv[3], "on") == 0) {
                wifi_enabled = 1;
                shell_println("wifi radio enabled");
            } else if (kstrcmp(argv[3], "off") == 0) {
                wifi_enabled = 0;
                wifi_connected = 0;
                shell_println("wifi radio disabled");
            } else {
                shell_println("nmcli radio wifi: usage on|off");
            }
        } else {
            shell_println("nmcli radio: unknown subcommand (wifi)");
        }
    } else if (kstrcmp(argv[1], "networking") == 0) {
        if (argc < 3) {
            vga_write_string("networking: ");
            shell_println(network_manager_running ? "enabled" : "disabled");
        } else if (kstrcmp(argv[2], "on") == 0) {
            network_manager_running = 1;
            shell_println("Networking enabled");
        } else if (kstrcmp(argv[2], "off") == 0) {
            network_manager_running = 0;
            wifi_connected = 0;
            shell_println("Networking disabled");
        } else {
            shell_println("nmcli networking: usage on|off");
        }
    } else {
        shell_println("nmcli: unknown subcommand (general|device|connection|radio|networking)");
    }
}

static void cmd_ifconfig(void) {
    if (!package_is_installed("net-tools")) {
        shell_println("ifconfig: command not found (try: sudo apt install net-tools)");
        return;
    }
    shell_println("lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536");
    shell_println("        inet 127.0.0.1  netmask 255.0.0.0");
    shell_println("        loop  txqueuelen 1000  (Local Loopback)");
    shell_println("");
    if (network_ready()) {
        shell_println("wlan0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500");
        shell_println("        inet 192.168.1.100  netmask 255.255.255.0  broadcast 192.168.1.255");
        shell_println("        ether 02:00:00:00:00:01  txqueuelen 1000  (Ethernet)");
    } else {
        shell_println("wlan0: flags=4099<UP,BROADCAST,MULTICAST>  mtu 1500");
        shell_println("        ether 02:00:00:00:00:01  txqueuelen 1000  (Ethernet)");
    }
    shell_println("");
}

static void cmd_install(void) {
    int root;
    int home_node;

    shell_println("FurOS 1.0 installer (terminal mode)");
    if (!disk_available()) {
        shell_println("install: no supported ATA disk detected");
        shell_println("  Use QEMU with -hda <disk.img> to attach an IDE disk.");
        return;
    }

    shell_println("Formatting disk filesystem...");
    if (disk_format_fs() != 0) {
        shell_println("install: disk format failed");
        shell_println(disk_last_error());
        return;
    }

    shell_println("Creating directory hierarchy...");
    root = fs_get_root();
    fs_mkdir_as(root, "bin",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "etc",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "home", SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "var",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "tmp",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "usr",  SHELL_UID_ROOT, 0755);

    home_node = fs_resolve(root, "home");
    if (home_node >= 0) {
        fs_mkdir_as(home_node, "debian", SHELL_UID_DEBIAN, 0755);
    }

    shell_println("Writing configuration files...");
    {
        const char *os_release =
            "PRETTY_NAME=\"FurOS 1.0 (Bookworm)\"\n"
            "NAME=\"FurOS\"\n"
            "VERSION_ID=\"1.0\"\n"
            "VERSION=\"1.0 (Bookworm)\"\n"
            "VERSION_CODENAME=bookworm\n"
            "ID=furos\n"
            "ID_LIKE=debian\n";
        fs_write_file(root, "etc/os-release", os_release, kstrlen(os_release));
    }
    {
        const char *hostname = "furos\n";
        fs_write_file(root, "etc/hostname", hostname, kstrlen(hostname));
    }
    {
        const char *passwd =
            "root:x:0:0:root:/root:/bin/sh\n"
            "debian:x:1000:1000:Debian User,,,:/home/debian:/bin/sh\n";
        fs_write_file(root, "etc/passwd", passwd, kstrlen(passwd));
    }
    {
        const char *group =
            "root:x:0:\n"
            "sudo:x:27:debian\n"
            "debian:x:1000:\n";
        fs_write_file(root, "etc/group", group, kstrlen(group));
    }

    if (disk_save_fs() != 0) {
        shell_println("install: write failed");
        shell_println(disk_last_error());
        return;
    }

    shell_cwd = fs_get_root();
    shell_println("Install complete. Reboot to start FurOS from disk.");
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
    size_t repo_len = sizeof(package_repo) / sizeof(package_repo[0]);

    if (argc < 2) {
        shell_println("apt: usage apt update|list|install|remove <pkg>");
        return;
    }

    if (kstrcmp(argv[1], "update") == 0) {
        shell_println("Get:1 http://deb.furos.org/furos bookworm InRelease");
        shell_println("Get:2 http://deb.debian.org/debian bookworm InRelease");
        shell_println("Get:3 http://deb.debian.org/debian bookworm-updates InRelease");
        shell_println("Get:4 http://security.debian.org bookworm-security InRelease");
        shell_println("Reading package lists... Done");
        shell_println("Building dependency tree... Done");
        apt_index_ready = 1;
        return;
    }

    if (kstrcmp(argv[1], "list") == 0) {
        shell_println("Listing packages:");
        for (i = 0; i < repo_len; i++) {
            vga_write_string("  ");
            vga_write_string(package_repo[i].name);
            vga_write_string("/bookworm  ");
            vga_write_string(package_repo[i].installed ? "[installed]" : "[available]");
            if (package_repo[i].essential) {
                vga_write_string(" [essential]");
            }
            vga_write_string("\n");
        }
        return;
    }

    if (argc < 3) {
        shell_println("apt: package name required");
        return;
    }

    if (kstrcmp(argv[1], "install") == 0) {
        struct package *pkg = package_find(argv[2]);
        if (pkg == 0) {
            vga_write_string("apt: package '");
            vga_write_string(argv[2]);
            vga_write_string("' not found in sources\n");
            return;
        }
        if (pkg->installed) {
            vga_write_string(pkg->name);
            shell_println(" is already the newest version.");
            return;
        }
        vga_write_string("Selecting previously unselected package ");
        vga_write_string(pkg->name);
        vga_write_string(".\n");
        shell_println("Unpacking ... Setting up ... Done.");
        pkg->installed = 1;
        sync_state_with_packages();
        return;
    }

    if (kstrcmp(argv[1], "remove") == 0) {
        struct package *pkg = package_find(argv[2]);
        if (pkg == 0) {
            vga_write_string("apt: package '");
            vga_write_string(argv[2]);
            vga_write_string("' not found\n");
            return;
        }
        if (pkg->essential) {
            vga_write_string("apt: refusing to remove essential package '");
            vga_write_string(pkg->name);
            vga_write_string("'\n");
            return;
        }
        if (!pkg->installed) {
            vga_write_string(pkg->name);
            shell_println(" is not installed.");
            return;
        }
        vga_write_string("Removing ");
        vga_write_string(pkg->name);
        vga_write_string(" ... Done.\n");
        pkg->installed = 0;
        sync_state_with_packages();
        return;
    }

    vga_write_string("apt: unknown subcommand '");
    vga_write_string(argv[1]);
    vga_write_string("' (update|list|install|remove)\n");
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
    int root = fs_get_root();
    int home_node;

    /* Standard Debian-like directory structure */
    fs_mkdir_as(root, "bin",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "etc",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "home", SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "var",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "tmp",  SHELL_UID_ROOT, 0755);
    fs_mkdir_as(root, "usr",  SHELL_UID_ROOT, 0755);

    /* /home/debian */
    home_node = fs_resolve(root, "home");
    if (home_node >= 0) {
        fs_mkdir_as(home_node, "debian", SHELL_UID_DEBIAN, 0755);
        {
            const char *bashrc =
                "# ~/.bashrc: executed by bash for non-login shells\n"
                "PS1='\\u@furos:\\w\\$ '\n"
                "alias ls='ls --color=auto'\n"
                "alias ll='ls -la'\n";
            int deb = fs_resolve(home_node, "debian");
            if (deb >= 0) {
                fs_write_file(deb, ".bashrc", bashrc, kstrlen(bashrc));
            }
        }
    }

    /* /etc files */
    {
        const char *os_release =
            "PRETTY_NAME=\"FurOS 1.0 (Bookworm)\"\n"
            "NAME=\"FurOS\"\n"
            "VERSION_ID=\"1.0\"\n"
            "VERSION=\"1.0 (Bookworm)\"\n"
            "VERSION_CODENAME=bookworm\n"
            "ID=furos\n"
            "ID_LIKE=debian\n"
            "HOME_URL=\"https://furos.org/\"\n"
            "SUPPORT_URL=\"https://furos.org/support\"\n"
            "BUG_REPORT_URL=\"https://furos.org/bugs\"\n";
        fs_write_file(root, "etc/os-release", os_release, kstrlen(os_release));
    }
    {
        const char *hostname = "furos\n";
        fs_write_file(root, "etc/hostname", hostname, kstrlen(hostname));
    }
    {
        const char *motd =
            "\n"
            "  ______           ____  ____\n"
            " |  ____|         / __ \\/ ___|\n"
            " | |__ _   _ _ __| |  | \\___ \\\n"
            " |  __| | | | '__| |  | |___) |\n"
            " | |  | |_| | |  | |__| |____/\n"
            " |_|   \\__,_|_|   \\____/\n"
            "\n"
            " FurOS 1.0 (based on Debian GNU/Linux 12 Bookworm)\n"
            " Type 'help' for available commands.\n"
            "\n";
        fs_write_file(root, "etc/motd", motd, kstrlen(motd));
    }
    {
        const char *passwd =
            "root:x:0:0:root:/root:/bin/sh\n"
            "debian:x:1000:1000:Debian User,,,:/home/debian:/bin/sh\n";
        fs_write_file(root, "etc/passwd", passwd, kstrlen(passwd));
    }
    {
        const char *group =
            "root:x:0:\n"
            "sudo:x:27:debian\n"
            "debian:x:1000:\n";
        fs_write_file(root, "etc/group", group, kstrlen(group));
    }
}

static void shell_dispatch(int argc, char *argv[]) {
    if (argc == 0) {
        return;
    }
    if (kstrcmp(argv[0], "help") == 0) {
        cmd_help();
    } else if (kstrcmp(argv[0], "clear") == 0) {
        vga_clear();
    } else if (kstrcmp(argv[0], "whoami") == 0) {
        cmd_whoami();
    } else if (kstrcmp(argv[0], "id") == 0) {
        cmd_id();
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
    } else if (kstrcmp(argv[0], "chmod") == 0) {
        cmd_chmod(argc, argv, shell_uid);
    } else if (kstrcmp(argv[0], "chown") == 0) {
        cmd_chown(argc, argv, shell_uid);
    } else if (kstrcmp(argv[0], "sudo") == 0) {
        cmd_sudo(argc, argv);
    } else if (kstrcmp(argv[0], "install") == 0) {
        cmd_install();
    } else if (kstrcmp(argv[0], "disk") == 0) {
        cmd_disk(argc, argv);
    } else if (kstrcmp(argv[0], "sync") == 0) {
        cmd_sync();
    } else if (kstrcmp(argv[0], "apt") == 0) {
        cmd_apt(argc, argv);
    } else if (kstrcmp(argv[0], "networkmanager") == 0) {
        cmd_networkmanager(argc, argv);
    } else if (kstrcmp(argv[0], "nmcli") == 0) {
        cmd_nmcli(argc, argv);
    } else if (kstrcmp(argv[0], "ifconfig") == 0) {
        cmd_ifconfig();
    } else {
        vga_write_string(argv[0]);
        shell_println(": command not found (type 'help' for available commands)");
    }
}

__attribute__((noreturn))
void shell_run(void) {
    char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARGV_MAX];
    int argc;
    const char *motd_data = 0;
    size_t motd_size = 0;

    keyboard_init();
    fs_init();
    disk_init();

    if (disk_available() && disk_load_fs() == 0) {
        /* Loaded from disk - filesystem already set up */
    } else {
        shell_seed_filesystem();
        if (!disk_available()) {
            vga_write_string("[  OK  ] No disk found - running in live mode\n");
        } else {
            vga_write_string("[  OK  ] No installed filesystem - running in live mode\n");
        }
    }

    shell_cwd = fs_get_root();
    shell_uid = SHELL_UID_DEBIAN;

    /* Show /etc/motd if it exists */
    if (fs_read_file(fs_get_root(), "etc/motd", &motd_data, &motd_size) == 0 && motd_data != 0 && motd_size > 0) {
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        vga_write_string(motd_data);
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    while (1) {
        if (shell_uid == SHELL_UID_ROOT) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        } else {
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        }
        shell_prompt();
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        shell_read_line(line, sizeof(line));
        argc = shell_parse(line, argv, SHELL_ARGV_MAX);
        if (argc == 0) {
            continue;
        }
        shell_dispatch(argc, argv);
    }
}
