/*
 * jv_sys — read-only Linux system introspection for JARVIS (pure C11).
 *
 * Design rules:
 *   - no heap allocation, no global state  -> thread-safe, daemon-friendly
 *   - talks directly to /proc, /sys, statvfs(), uname()
 *   - every function returns 0 / a count on success or a negative errno
 *   - never writes to the system; privileged actions belong to jarvis-helper
 */
#ifndef JV_SYS_H
#define JV_SYS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- memory (/proc/meminfo), values in KiB ---- */
typedef struct {
    uint64_t total_kb;
    uint64_t available_kb;
    uint64_t free_kb;
    uint64_t buffers_kb;
    uint64_t cached_kb;
    uint64_t swap_total_kb;
    uint64_t swap_free_kb;
} jv_meminfo;

int jv_mem_read(jv_meminfo *out);

/* ---- CPU (/proc/stat) ---- */
typedef struct {
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
} jv_cpu_times;

int    jv_cpu_times_read(jv_cpu_times *out);
/* Busy percentage (0..100) between two snapshots, a taken before b. */
double jv_cpu_usage_between(const jv_cpu_times *a, const jv_cpu_times *b);
int    jv_cpu_count(void);

typedef struct {
    double l1, l5, l15;
} jv_loadavg;

int jv_loadavg_read(jv_loadavg *out);
int jv_uptime_read(double *seconds);

/* ---- battery (/sys/class/power_supply); -ENODEV when there is none ---- */
typedef struct {
    int  present;
    int  percent;      /* 0..100, -1 if unknown */
    int  charging;     /* 1 while status == "Charging" */
    char status[24];   /* "Charging", "Discharging", "Full", ... */
} jv_battery;

int jv_battery_read(jv_battery *out);

/* ---- temperature: hottest sensor from thermal zones and hwmon, deg C ---- */
int jv_thermal_max_c(double *celsius);

/* ---- filesystem usage for a mount point ---- */
typedef struct {
    uint64_t total_bytes;
    uint64_t avail_bytes;  /* available to unprivileged users */
} jv_disk;

int jv_disk_read(const char *path, jv_disk *out);

/* ---- OS identity ---- */
typedef struct {
    char hostname[65];
    char kernel[65];   /* uname release */
    char machine[65];  /* x86_64, aarch64... */
    char pretty[128];  /* PRETTY_NAME from os-release, e.g. "Kubuntu 26.04 LTS" */
} jv_osinfo;

int jv_osinfo_read(jv_osinfo *out);

/* ---- processes ---- */
#define JV_PROC_MAX 64

typedef struct {
    int32_t  pid;
    uint64_t rss_kb;
    char     comm[32];
} jv_proc;

/* Fills `out` (capacity `max` <= JV_PROC_MAX) with the top processes by
 * resident memory, descending. Returns the number of entries, or -errno. */
int jv_proc_top_mem(jv_proc *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* JV_SYS_H */
