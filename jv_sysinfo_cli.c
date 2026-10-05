/* Tiny CLI used to verify the C layer on its own: ./jv_sysinfo */
#define _POSIX_C_SOURCE 200809L

#include "jv_sys.h"

#include <stdio.h>
#include <time.h>

int main(void)
{
    jv_osinfo os;
    if (jv_osinfo_read(&os) == 0)
        printf("OS      : %s | kernel %s | %s | host %s\n", os.pretty, os.kernel,
               os.machine, os.hostname);

    jv_meminfo m;
    if (jv_mem_read(&m) == 0)
        printf("Memory  : %.1f / %.1f GiB used, swap %.1f / %.1f GiB\n",
               (double)(m.total_kb - m.available_kb) / 1048576.0,
               (double)m.total_kb / 1048576.0,
               (double)(m.swap_total_kb - m.swap_free_kb) / 1048576.0,
               (double)m.swap_total_kb / 1048576.0);

    jv_cpu_times a, b;
    if (jv_cpu_times_read(&a) == 0) {
        nanosleep(&(struct timespec){0, 300 * 1000 * 1000}, NULL);
        if (jv_cpu_times_read(&b) == 0)
            printf("CPU     : %.1f%% over 300 ms, %d cores\n",
                   jv_cpu_usage_between(&a, &b), jv_cpu_count());
    }

    jv_loadavg l;
    if (jv_loadavg_read(&l) == 0)
        printf("Load    : %.2f %.2f %.2f\n", l.l1, l.l5, l.l15);

    double up;
    if (jv_uptime_read(&up) == 0)
        printf("Uptime  : %.0f s\n", up);

    jv_battery bat;
    int rc = jv_battery_read(&bat);
    if (rc == 0)
        printf("Battery : %d%% (%s)\n", bat.percent, bat.status);
    else
        printf("Battery : none (%d)\n", rc);

    double t;
    rc = jv_thermal_max_c(&t);
    if (rc == 0)
        printf("Temp    : %.1f C\n", t);
    else
        printf("Temp    : no sensors (%d)\n", rc);

    jv_disk dk;
    if (jv_disk_read("/", &dk) == 0)
        printf("Disk /  : %.1f free of %.1f GiB\n",
               (double)dk.avail_bytes / 1073741824.0,
               (double)dk.total_bytes / 1073741824.0);

    jv_proc top[5];
    int n = jv_proc_top_mem(top, 5);
    for (int i = 0; i < n; ++i)
        printf("Top %d   : %-16s pid %-7d %.0f MiB\n", i + 1, top[i].comm,
               (int)top[i].pid, (double)top[i].rss_kb / 1024.0);
    return 0;
}
