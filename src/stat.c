#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <event2/event.h>

#include <rte_timer.h>
#include <rte_ethdev.h>

#include "util.h"
#include "stat.h"
#include "conf.h"

struct event_base *ev_base = NULL;
struct event *ev_eth = NULL;
struct rte_timer timer;
struct nstats ethstat;
uint64_t prev_tsc = 0;
uint64_t base_tsc = 0;
struct stats tfs = {0};

struct nstats new_nstats(uint16_t port_id) {
    struct nstats ns;

    memset(&ns, 0, sizeof(struct nstats));
    ns.port_id = port_id;
    gettimeofday(&ns.s_t, NULL);
    rte_eth_stats_reset(port_id);

    return ns;
}

#define XSTAT_SIZE 3000
void
print_nstats(struct nstats stat, uint16_t num_queue) {
    gettimeofday(&stat.e_t, NULL);
    stat.elapsed = time_diff(stat.s_t, stat.e_t);
    rte_eth_stats_get(stat.port_id, &stat.stat);

    LOG_LINE(75, '-', "NIC Statistics");
    struct rte_ether_addr addr;
    struct rte_eth_dev_info dev_info;
    rte_eth_macaddr_get(stat.port_id, &addr);
    rte_eth_dev_info_get(stat.port_id, &dev_info);
    LOG_INFO(
        "Statistics of Port %hu [%s, %s, %02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8"]\n",
        stat.port_id,
        dev_info.driver_name,
        dev_info.device->name,
        addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
        addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]
    );

    char s_str[128];
    char e_str[128];
    strftime(s_str, 64, "%Y-%m-%d %H:%M:%S", localtime(&stat.s_t.tv_sec));
    strftime(e_str, 64, "%Y-%m-%d %H:%M:%S", localtime(&stat.e_t.tv_sec));
    LOG_INFO(
        "From %s to %s, %.2f seconds elapsed\n",
        s_str,
        e_str,
        stat.elapsed
    );

    struct rte_eth_xstat_name xstats_names[XSTAT_SIZE];
    int num = rte_eth_xstats_get_names(stat.port_id, xstats_names, XSTAT_SIZE);

    struct rte_eth_xstat xstats[XSTAT_SIZE];
    rte_eth_xstats_get(stat.port_id, xstats, XSTAT_SIZE);

    uint64_t rx_discard = 0xffffffffffffffff;
    uint64_t tx_discard = 0xffffffffffffffff;
    for (int loop = 0; loop < num; loop++) {
        // LOG_INFO("%s: %lu\n", xstats_names[xstats[loop].id].name, xstats[loop].value);
        if (strcmp(xstats_names[xstats[loop].id].name, "rx_phy_discard_packets") == 0)
            rx_discard = xstats[loop].value;
        else if (strcmp(xstats_names[xstats[loop].id].name, "tx_phy_discard_packets") == 0)
            tx_discard = xstats[loop].value;
    }

    LOG_INFO(
        "RX %11lu Pkts %13lu Bytes  %6.2f Mpps  %6.2f Gbps  %lu Drops\n",
        stat.stat.ipackets,
        stat.stat.ibytes,
        (stat.stat.ipackets / 1000000.0) / stat.elapsed,
        (stat.stat.ibytes + SIZE_LINK_OVERHEAD * stat.stat.ipackets) / (125000000 * stat.elapsed),
        rx_discard
    );

    LOG_INFO(
        "TX %11lu Pkts %13lu Bytes  %6.2f Mpps  %6.2f Gbps  %lu Drops\n",
        stat.stat.opackets,
        stat.stat.obytes,
        (stat.stat.opackets / 1000000.0) / stat.elapsed,
        (stat.stat.obytes + SIZE_LINK_OVERHEAD * stat.stat.opackets) / (125000000 * stat.elapsed),
        tx_discard
    );

    LOG_INFO("\n");
    LOG_INFO(
        "Queue ID,  RX Packets,  TX Packets,  RX GBytes,  TX GBytes,  Dropped (RX)\n"
    );

    // struct conf_t* conf = get_conf();
    for (uint16_t loop = 0; loop < RTE_MIN(num_queue, RTE_ETHDEV_QUEUE_STAT_CNTRS); loop++) {
        LOG_INFO(
            "   %02hu      %10lu   %10lu    %8.3f    %8.3f     %10lu\n",
            loop,
            stat.stat.q_ipackets[loop],
            stat.stat.q_opackets[loop],
            stat.stat.q_ibytes[loop] / 1000000000.0,
            stat.stat.q_obytes[loop] / 1000000000.0,
            stat.stat.q_errors[loop]
        );
    }
    if (num_queue > RTE_ETHDEV_QUEUE_STAT_CNTRS)
        LOG_INFO(" ...... \n");
    LOG_LINE(75, '-', NULL);
}

void
print_xstats(uint16_t port_id) {
    // struct nstats stat;
    // rte_eth_stats_get(port_id, &stat.stat);

    struct rte_eth_xstat_name xstats_names[XSTAT_SIZE];
    int num = rte_eth_xstats_get_names(port_id, xstats_names, XSTAT_SIZE);

    struct rte_eth_xstat xstats[XSTAT_SIZE];
    rte_eth_xstats_get(port_id, xstats, XSTAT_SIZE);

    for (int loop = 0; loop < num; loop++) {
        LOG_INFO("%s: %lu\n", xstats_names[xstats[loop].id].name, xstats[loop].value);
    }
}

static inline void
calc_cpu_usage_pct(struct pstats* last_usage, struct pstats* cur_usage, double* ucpu_usage, double* scpu_usage) {
    long unsigned int total_time_diff = cur_usage->cpu_total_time - last_usage->cpu_total_time;
    *ucpu_usage = 100 * (((cur_usage->utime_ticks + cur_usage->cutime_ticks) - (last_usage->utime_ticks + last_usage->cutime_ticks)) / (double) total_time_diff);
    *scpu_usage = 100 * (((cur_usage->stime_ticks + cur_usage->cstime_ticks) - (last_usage->stime_ticks + last_usage->cstime_ticks)) / (double) total_time_diff);
}

static inline void
calc_cpu_usage(struct pstats* last_usage, struct pstats* cur_usage, long unsigned int* ucpu_usage, long unsigned int* scpu_usage) {
    *ucpu_usage = (cur_usage->utime_ticks + cur_usage->cutime_ticks) - (last_usage->utime_ticks + last_usage->cutime_ticks);
    *scpu_usage = (cur_usage->stime_ticks + cur_usage->cstime_ticks) - (last_usage->stime_ticks + last_usage->cstime_ticks);
}

/*
 * read /proc data into the passed struct pstats
 * returns 0 on success, -1 on error
*/
static int
get_cpu_usage(char* path, struct pstats* result) {
    FILE *fpstat = fopen(path, "r");
    if (fpstat == NULL) {
        LOG_ERRO("fopen error: %s\n", path);
        return -1;
    }
    FILE *fstat = fopen("/proc/stat", "r");
    if (fstat == NULL) {
        LOG_ERRO("fopen error: /proc/stat");
        fclose(fpstat);
        return -1;
    }

    // read statisticas from /proc/pid/stat
    bzero(result, sizeof(struct pstats));
    long int rss;
    if (fscanf(fpstat, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
                "%lu %ld %ld %*d %*d %*d %*d %*u %lu %ld",
                &result->utime_ticks, &result->stime_ticks,
                &result->cutime_ticks, &result->cstime_ticks, &result->vsize,
                &rss) == EOF) {
        fclose(fpstat);
        return -1;
    }
    fclose(fpstat);
    result->rss = rss * getpagesize();

    //read+calc cpu total time from /proc/stat
    long unsigned int cpu_time[10];
    bzero(cpu_time, sizeof(cpu_time));
    if (fscanf(fstat, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                &cpu_time[0], &cpu_time[1], &cpu_time[2], &cpu_time[3],
                &cpu_time[4], &cpu_time[5], &cpu_time[6], &cpu_time[7],
                &cpu_time[8], &cpu_time[9]) == EOF) {
        fclose(fstat);
        return -1;
    }
    fclose(fstat);

    for(int i=0; i < 10;i++)
        result->cpu_total_time += cpu_time[i];
    return 0;
}

static int
get_mem_usage(char* path) {
    FILE* fh = fopen(path, "r");
    const char* data_mem = "VmRSS:";
    char buff[64];
    while(fscanf(fh, "%s", buff) != EOF) {
        if (strncmp(buff, data_mem, strlen(data_mem)) == 0) {
            if (fscanf(fh, "%s", buff) != EOF) {
                break;
            }
        }
    }
    fclose(fh);
    return atoi(buff);
}

static void
show_cpumem(void) {
    struct conf_t* conf = get_conf();
    FILE* fh = fopen(conf->path_to_cpumem, "r");
    char buff[100];
    int index = 0;
    double MinCPU = 100.0, MaxCPU = 0, AveCPU = 0;
    int MinMem = 1000000000, MaxMem = 0, AveMem = 0;
    while (fgets(buff, 100, fh) != NULL) {
        index++;
        if (index == 1)
            continue;
        char* in[5];
        int temp_index = 0;
        char* token;
        char* rest = buff;
        while ((token = strtok_r(rest, " \t\n", &rest))) {
            in[temp_index++] = token;
        }
        double cpu_usage = atof(in[3]);
        int mem_usage = atoi(in[4]);

        if (cpu_usage < MinCPU)
            MinCPU = cpu_usage;
        if (cpu_usage > MaxCPU)
            MaxCPU = cpu_usage;
        AveCPU += cpu_usage;
        if (mem_usage < MinMem)
            MinMem = mem_usage;
        if (mem_usage > MaxMem)
            MaxMem = mem_usage;
        AveMem += mem_usage;
    }
    fclose(fh);

    unsigned int ngx_ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    uint64_t mem_size = sysconf (_SC_PHYS_PAGES) * sysconf (_SC_PAGESIZE) / (1024 * 1024 * 1024);
    LOG_INFO("Writing cpu usage statisticas to %s ...\n", conf->path_to_cpumem);
    LOG_LINE(75, '-', "CPU/Mem Statistics");
    LOG_INFO(
        "Cores  Memory  MinCPU  MaxCPU  AveCPU   MinMem(KB)  MaxMem(KB)  AveMem(KB)\n"
    );
    LOG_INFO("%5u  %4luGB  %5.2f%%  %5.2f%%  %5.2f%%   %10d  %10d  %10d\n", ngx_ncpu, mem_size, MinCPU, MaxCPU, AveCPU/(index-1), MinMem, MaxMem, AveMem/(index-1));
    LOG_LINE(75, '-', NULL);
}

#define TIMER_RESOLUTION_CYCLES 22000000ULL
static void
stats_callback(__rte_unused struct rte_timer *timer, void *arg) {
    prev_tsc = rte_rdtsc();

    struct stats* tfs = (struct stats*) arg;
    struct conf_t* conf = get_conf();
    rte_eth_stats_get(tfs->port_id, &(tfs->nstat_cur));
    get_cpu_usage(tfs->cpu_path, &(tfs->pstat_cur));
    int mem_usage = get_mem_usage(tfs->mem_path);
    gettimeofday(&tfs->cur_t, NULL);
    double delta_1 = time_diff(tfs->base, tfs->pre_t);
    double delta_2 = time_diff(tfs->base, tfs->cur_t);
    double delta_3 = time_diff(tfs->pre_t, tfs->cur_t);

    struct rte_eth_stats tsp = tfs->nstat_pre;
    struct rte_eth_stats tsc = tfs->nstat_cur;
    LOG_INFO(
        "%06.2f-%06.2f   %hu  "CO_RED"%6.2f %6.2f %6.2f %6.2f"CO_YELLOW" %6.2f %6.2f %6.2f %6.2f"CO_RESET"\n",
        delta_1,
        delta_2,
        tfs->port_id,
        (tsc.ipackets  - tsp.ipackets) / 1000000.0,
        (tsc.ibytes    - tsp.ibytes)   / 1000000000.0,
        (tsc.ipackets  - tsp.ipackets) / (1000000 * delta_3),
        ((tsc.ipackets - tsp.ipackets) * SIZE_LINK_OVERHEAD + tsc.ibytes - tsp.ibytes) / (125000000 * delta_3),
        (tsc.opackets  - tsp.opackets) / 1000000.0,
        (tsc.obytes    - tsp.obytes)   / 1000000000.0,
        (tsc.opackets  - tsp.opackets) / (1000000 * delta_3),
        ((tsc.opackets - tsp.opackets) * SIZE_LINK_OVERHEAD + tsc.obytes - tsp.obytes) / (125000000 * delta_3)
    );
    memcpy(&tfs->nstat_pre, &tfs->nstat_cur, sizeof(struct rte_eth_stats));

    char temp[100] = {0};

    double a, b;
    calc_cpu_usage_pct(&tfs->pstat_pre, &tfs->pstat_cur, &a, &b);
    sprintf(temp, "%06.2f-%06.2f    %06.2f  %06.2f  %06.2f  %8d\n", delta_1, delta_2, a, b, a+b, mem_usage);
    FILE* fh = fopen(conf->path_to_cpumem, "a");
    fputs(temp, fh);
    fclose(fh);

    memcpy(&tfs->pre_t, &tfs->cur_t, sizeof(struct timeval));
    memcpy(&tfs->pstat_pre, &tfs->pstat_cur, sizeof(struct pstats));
}

// void
// print_stats_with_interval(uint16_t port_id, uint64_t interval) {
//     uint64_t prev_tsc = 0, cur_tsc, diff_tsc;
//     struct stats tfs = {0};
//     struct conf_t* conf = get_conf();

//     sprintf(tfs.cpu_path, "/proc/%u/stat", getpid());
//     sprintf(tfs.mem_path, "/proc/%u/status", getpid());
//     get_cpu_usage(tfs.cpu_path, &tfs.pstat_pre);
//     tfs.port_id = port_id;
//     gettimeofday(&tfs.base,  NULL);
//     gettimeofday(&tfs.pre_t, NULL);

//     FILE* fh = fopen(conf->path_to_cpumem, "w");
//     char header[100] = {0};
//     sprintf(header, "Time Interval     %%User    %%Sys  %%Total  Mem (KB)\n");
//     fputs(header, fh);
//     fclose(fh);

//     // Initializes internal variables (list, locks and so on) for the RTE timer library.
//     rte_timer_subsystem_init();
//     rte_timer_init(&timer);
//     // Get the number of cycles in one second
//     uint64_t hz = rte_get_timer_hz();
//     rte_timer_reset(&timer, interval * hz, PERIODICAL, rte_lcore_id(), stats_callback, &tfs);

//     LOG_INFO(
//         "%s                  %s %s             In            %s %s            Out            %s\n",
//         BG_GREEN, BG_RESET, BG_RED, BG_RESET, BG_YELLOW, BG_RESET
//     );
//     LOG_INFO(
//         "%s   Interval   Port%s %s MPkts GBytes   Mpps   Gbps%s %s MPkts GBytes   Mpps   Gbps%s\n",
//         BG_GREEN, BG_RESET, BG_RED, BG_RESET, BG_YELLOW, BG_RESET
//     );
//     // while (!force_quit) {
//     //     cur_tsc = rte_rdtsc();
//     //     diff_tsc = cur_tsc - prev_tsc;
//     //     if (diff_tsc > TIMER_RESOLUTION_CYCLES) {
//     //         rte_timer_manage();
//     //         prev_tsc = cur_tsc;
//     //     }
//     //     rte_delay_ms(1);
//     // }

// }

void init_stat(void) {
    struct conf_t* conf = get_conf();
    ethstat = new_nstats(conf->port_id);

    // uint64_t prev_tsc = 0, cur_tsc, diff_tsc;
    uint64_t prev_tsc = 0;
    // struct stats tfs = {0};
    sprintf(tfs.cpu_path, "/proc/%u/stat", getpid());
    sprintf(tfs.mem_path, "/proc/%u/status", getpid());
    get_cpu_usage(tfs.cpu_path, &tfs.pstat_pre);
    tfs.port_id = conf->port_id;
    gettimeofday(&tfs.base,  NULL);
    gettimeofday(&tfs.pre_t, NULL);

    FILE* fh = fopen(conf->path_to_cpumem, "w");
    char header[100] = {0};
    sprintf(header, "Time Interval     %%User    %%Sys  %%Total  Mem (KB)\n");
    fputs(header, fh);
    fclose(fh);

    // Initializes internal variables (list, locks and so on) for the RTE timer library.
    rte_timer_subsystem_init();
    rte_timer_init(&timer);
    // Get the number of cycles in one second
    uint64_t hz = rte_get_timer_hz();
    rte_timer_reset(&timer, time_double(conf->interval) * hz, PERIODICAL, rte_lcore_id(), stats_callback, &tfs);

    if (!(conf->is_rtt == true || conf->data_size > 0)) {
        LOG_INFO(
            "%s                  %s %s             In            %s %s            Out            %s\n",
            BG_GREEN, BG_RESET, BG_RED, BG_RESET, BG_YELLOW, BG_RESET
        );
        LOG_INFO(
            "%s   Interval   Port%s %s MPkts GBytes   Mpps   Gbps%s %s MPkts GBytes   Mpps   Gbps%s\n",
            BG_GREEN, BG_RESET, BG_RED, BG_RESET, BG_YELLOW, BG_RESET
        );
    }

    prev_tsc = rte_rdtsc();
    base_tsc = prev_tsc;
}

int update_stat(uint64_t limit) {
    uint64_t cur_tsc = rte_rdtsc();
    uint64_t diff_tsc = cur_tsc - prev_tsc;
    if (diff_tsc > TIMER_RESOLUTION_CYCLES) {
        rte_timer_manage();
        // prev_tsc = cur_tsc;
    }
    if (cur_tsc - base_tsc > limit)
        return 1;
    else
        return 0;
    // rte_delay_ms(1);
}

void exit_stat(void) {
    struct conf_t* conf = get_conf();
    print_nstats(ethstat, conf->total_lcore);
    rte_timer_stop(&timer);
    // Free timer subsystem resources.
    rte_timer_subsystem_finalize();
    if (conf->data_size == 0 && conf->is_rtt == false)
        show_cpumem();
} 