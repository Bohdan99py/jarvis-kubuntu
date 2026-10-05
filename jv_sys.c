#define _POSIX_C_SOURCE 200809L

#include "jv_sys.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* Reads up to cap-1 bytes into buf (NUL-terminated). Returns length or -errno.
 * Uses raw read(): procfs/sysfs files are tiny and stdio buffering is waste. */
static ssize_t read_small(const char *path, char *buf, size_t cap)
{
    if (cap < 2)
        return -EINVAL;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;

    size_t len = 0;
    for (;;) {
        ssize_t n = read(fd, buf + len, cap - 1 - len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            int e = errno;
            close(fd);
            return -e;
        }
        if (n == 0)
            break;
        len += (size_t)n;
        if (len >= cap - 1)
            break;
    }
    close(fd);
    buf[len] = '\0';
    return (ssize_t)len;
}

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

/* Reads a file that holds one unsigned number. */
static int read_u64(const char *path, uint64_t *out)
{
    char buf[64];
    ssize_t n = read_small(path, buf, sizeof buf);
    if (n < 0)
        return (int)n;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(buf, &end, 10);
    if (end == buf || errno != 0)
        return -EINVAL;
    *out = (uint64_t)v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* memory                                                             */
/* ------------------------------------------------------------------ */

int jv_mem_read(jv_meminfo *m)
{
    if (!m)
        return -EINVAL;

    char buf[8192];
    ssize_t n = read_small("/proc/meminfo", buf, sizeof buf);
    if (n < 0)
        return (int)n;

    memset(m, 0, sizeof *m);

    const struct {
        const char *key;
        size_t      klen;
        uint64_t   *dst;
    } table[] = {
        {"MemTotal:",     9,  &m->total_kb},
        {"MemFree:",      8,  &m->free_kb},
        {"MemAvailable:", 13, &m->available_kb},
        {"Buffers:",      8,  &m->buffers_kb},
        {"Cached:",       7,  &m->cached_kb},
        {"SwapTotal:",    10, &m->swap_total_kb},
        {"SwapFree:",     9,  &m->swap_free_kb},
    };

    for (char *p = buf; *p;) {
        for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i) {
            if (strncmp(p, table[i].key, table[i].klen) == 0) {
                *table[i].dst = strtoull(p + table[i].klen, NULL, 10);
                break;
            }
        }
        char *nl = strchr(p, '\n');
        if (!nl)
            break;
        p = nl + 1;
    }

    if (m->total_kb == 0)
        return -EIO;
    if (m->available_kb == 0) /* kernels < 3.14 */
        m->available_kb = m->free_kb + m->buffers_kb + m->cached_kb;
    return 0;
}

/* ------------------------------------------------------------------ */
/* CPU                                                                */
/* ------------------------------------------------------------------ */

int jv_cpu_times_read(jv_cpu_times *t)
{
    if (!t)
        return -EINVAL;

    char buf[512]; /* only the aggregate first line is needed */
    ssize_t n = read_small("/proc/stat", buf, sizeof buf);
    if (n < 0)
        return (int)n;
    if (strncmp(buf, "cpu ", 4) != 0)
        return -EIO;

    memset(t, 0, sizeof *t);
    int got = sscanf(buf + 4,
                     "%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                     " %" SCNu64 " %" SCNu64 " %" SCNu64,
                     &t->user, &t->nice, &t->system, &t->idle, &t->iowait,
                     &t->irq, &t->softirq, &t->steal);
    return got >= 4 ? 0 : -EIO;
}

static uint64_t cpu_total(const jv_cpu_times *t)
{
    return t->user + t->nice + t->system + t->idle + t->iowait + t->irq +
           t->softirq + t->steal;
}

double jv_cpu_usage_between(const jv_cpu_times *a, const jv_cpu_times *b)
{
    if (!a || !b)
        return 0.0;

    const uint64_t ta = cpu_total(a);
    const uint64_t tb = cpu_total(b);
    if (tb <= ta)
        return 0.0;

    const uint64_t dt = tb - ta;
    const uint64_t ia = a->idle + a->iowait;
    const uint64_t ib = b->idle + b->iowait;
    const uint64_t di = ib > ia ? ib - ia : 0;
    if (di >= dt)
        return 0.0;

    return 100.0 * (double)(dt - di) / (double)dt;
}

int jv_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

int jv_loadavg_read(jv_loadavg *l)
{
    if (!l)
        return -EINVAL;
    char buf[128];
    ssize_t n = read_small("/proc/loadavg", buf, sizeof buf);
    if (n < 0)
        return (int)n;
    return sscanf(buf, "%lf %lf %lf", &l->l1, &l->l5, &l->l15) == 3 ? 0 : -EIO;
}

int jv_uptime_read(double *seconds)
{
    if (!seconds)
        return -EINVAL;
    char buf[128];
    ssize_t n = read_small("/proc/uptime", buf, sizeof buf);
    if (n < 0)
        return (int)n;
    return sscanf(buf, "%lf", seconds) == 1 ? 0 : -EIO;
}

/* ------------------------------------------------------------------ */
/* battery                                                            */
/* ------------------------------------------------------------------ */

#define PS_DIR "/sys/class/power_supply"

int jv_battery_read(jv_battery *b)
{
    if (!b)
        return -EINVAL;
    memset(b, 0, sizeof *b);
    b->percent = -1;

    DIR *d = opendir(PS_DIR);
    if (!d)
        return errno == ENOENT ? -ENODEV : -errno;

    int rc = -ENODEV;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;

        char path[384];
        char val[64];

        snprintf(path, sizeof path, PS_DIR "/%s/type", e->d_name);
        if (read_small(path, val, sizeof val) < 0)
            continue;
        rtrim(val);
        if (strcmp(val, "Battery") != 0)
            continue;

        /* skip peripherals (wireless mouse, headset...) */
        snprintf(path, sizeof path, PS_DIR "/%s/scope", e->d_name);
        if (read_small(path, val, sizeof val) > 0) {
            rtrim(val);
            if (strcmp(val, "Device") == 0)
                continue;
        }

        uint64_t cap = 0;
        snprintf(path, sizeof path, PS_DIR "/%s/capacity", e->d_name);
        if (read_u64(path, &cap) == 0) {
            b->percent = cap > 100 ? 100 : (int)cap;
        } else {
            uint64_t now = 0, full = 0;
            char p1[384], p2[384];
            snprintf(p1, sizeof p1, PS_DIR "/%s/energy_now", e->d_name);
            snprintf(p2, sizeof p2, PS_DIR "/%s/energy_full", e->d_name);
            if (read_u64(p1, &now) != 0 || read_u64(p2, &full) != 0) {
                snprintf(p1, sizeof p1, PS_DIR "/%s/charge_now", e->d_name);
                snprintf(p2, sizeof p2, PS_DIR "/%s/charge_full", e->d_name);
                if (read_u64(p1, &now) != 0 || read_u64(p2, &full) != 0)
                    full = 0;
            }
            if (full > 0) {
                uint64_t pct = now * 100 / full;
                b->percent = pct > 100 ? 100 : (int)pct;
            }
        }

        snprintf(path, sizeof path, PS_DIR "/%s/status", e->d_name);
        if (read_small(path, val, sizeof val) > 0) {
            rtrim(val);
            snprintf(b->status, sizeof b->status, "%.*s", (int)sizeof b->status - 1, val);
        } else {
            snprintf(b->status, sizeof b->status, "Unknown");
        }

        b->charging = strcmp(b->status, "Charging") == 0;
        b->present = 1;
        rc = 0;
        break;
    }
    closedir(d);
    return rc;
}

/* ------------------------------------------------------------------ */
/* temperature                                                        */
/* ------------------------------------------------------------------ */

static void consider_temp(const char *path, double *best, int *found)
{
    uint64_t raw = 0;
    if (read_u64(path, &raw) != 0)
        return;
    /* millidegrees; ignore 0 and absurd values (broken sensors) */
    if (raw == 0 || raw > 150000)
        return;
    double c = (double)raw / 1000.0;
    if (!*found || c > *best)
        *best = c;
    *found = 1;
}

int jv_thermal_max_c(double *celsius)
{
    if (!celsius)
        return -EINVAL;

    double best = 0.0;
    int found = 0;
    char path[384];
    struct dirent *e;

    DIR *d = opendir("/sys/class/thermal");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0)
                continue;
            snprintf(path, sizeof path, "/sys/class/thermal/%s/temp", e->d_name);
            consider_temp(path, &best, &found);
        }
        closedir(d);
    }

    d = opendir("/sys/class/hwmon");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (strncmp(e->d_name, "hwmon", 5) != 0)
                continue;
            for (int i = 1; i <= 16; ++i) {
                snprintf(path, sizeof path, "/sys/class/hwmon/%s/temp%d_input",
                         e->d_name, i);
                consider_temp(path, &best, &found);
            }
        }
        closedir(d);
    }

    if (!found)
        return -ENODEV;
    *celsius = best;
    return 0;
}

/* ------------------------------------------------------------------ */
/* disk, OS                                                           */
/* ------------------------------------------------------------------ */

int jv_disk_read(const char *path, jv_disk *out)
{
    if (!path || !out)
        return -EINVAL;
    struct statvfs s;
    if (statvfs(path, &s) != 0)
        return -errno;
    out->total_bytes = (uint64_t)s.f_blocks * (uint64_t)s.f_frsize;
    out->avail_bytes = (uint64_t)s.f_bavail * (uint64_t)s.f_frsize;
    return 0;
}

static void parse_pretty_name(char *dst, size_t cap)
{
    static const char *const files[] = {"/etc/os-release", "/usr/lib/os-release"};
    char buf[2048];

    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        if (read_small(files[i], buf, sizeof buf) < 0)
            continue;
        for (char *p = buf; *p;) {
            if (strncmp(p, "PRETTY_NAME=", 12) == 0) {
                char *v = p + 12;
                char *nl = strchr(v, '\n');
                if (nl)
                    *nl = '\0';
                if (*v == '"' || *v == '\'') {
                    char q = *v++;
                    char *close_q = strchr(v, q);
                    if (close_q)
                        *close_q = '\0';
                }
                snprintf(dst, cap, "%s", v);
                return;
            }
            char *nl = strchr(p, '\n');
            if (!nl)
                break;
            p = nl + 1;
        }
    }
    snprintf(dst, cap, "Linux");
}

int jv_osinfo_read(jv_osinfo *o)
{
    if (!o)
        return -EINVAL;
    struct utsname u;
    if (uname(&u) != 0)
        return -errno;
    snprintf(o->hostname, sizeof o->hostname, "%s", u.nodename);
    snprintf(o->kernel, sizeof o->kernel, "%s", u.release);
    snprintf(o->machine, sizeof o->machine, "%s", u.machine);
    parse_pretty_name(o->pretty, sizeof o->pretty);
    return 0;
}

/* ------------------------------------------------------------------ */
/* processes                                                          */
/* ------------------------------------------------------------------ */

int jv_proc_top_mem(jv_proc *out, size_t max)
{
    if (!out || max == 0 || max > JV_PROC_MAX)
        return -EINVAL;

    DIR *d = opendir("/proc");
    if (!d)
        return -errno;

    long page_kb = sysconf(_SC_PAGESIZE) / 1024;
    if (page_kb <= 0)
        page_kb = 4;

    size_t count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char *end = NULL;
        long pid = strtol(e->d_name, &end, 10);
        if (*end != '\0' || pid <= 0)
            continue;

        char path[64];
        char buf[128];
        snprintf(path, sizeof path, "/proc/%ld/statm", pid);
        if (read_small(path, buf, sizeof buf) < 0)
            continue;

        unsigned long long vsize = 0, resident = 0;
        if (sscanf(buf, "%llu %llu", &vsize, &resident) != 2)
            continue;

        uint64_t rss = (uint64_t)resident * (uint64_t)page_kb;
        if (rss == 0) /* kernel threads */
            continue;
        /* cheap reject before touching /proc/<pid>/comm */
        if (count == max && rss <= out[count - 1].rss_kb)
            continue;

        char comm[32];
        snprintf(path, sizeof path, "/proc/%ld/comm", pid);
        if (read_small(path, comm, sizeof comm) < 0)
            continue;
        rtrim(comm);

        size_t pos;
        if (count < max)
            pos = count++;
        else
            pos = max - 1;
        while (pos > 0 && out[pos - 1].rss_kb < rss) {
            out[pos] = out[pos - 1];
            --pos;
        }
        out[pos].pid = (int32_t)pid;
        out[pos].rss_kb = rss;
        snprintf(out[pos].comm, sizeof out[pos].comm, "%s", comm);
    }
    closedir(d);
    return (int)count;
}
