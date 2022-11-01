#ifndef _STAT_H_
#define _STAT_H_

/**
 * Process CPU usage statisticas
 * https://man7.org/linux/man-pages/man5/proc.5.html
 */
struct pstats {
    /**
     * Amount of time that this process has been scheduled in user mode, measured in clock ticks (divide by
     * sysconf(_SC_CLK_TCK)).  This includes guest time, guest_time (time spent running a virtual CPU, see
     * below), so that applications that are not aware of the guest time field do not lose that time from
     * their calculations.
     */
    long unsigned int utime_ticks;
    /**
     * Amount of time that this process's waited-for children have been scheduled in user mode, measured
     * in clock ticks (divide by sysconf(_SC_CLK_TCK)). (See also times(2).)  This includes guest time,
     * cguest_time (time spent running a virtual CPU, see below).
     */
    long int         cutime_ticks;
    /**
     * Amount of time that this process has been scheduled in kernel mode, measured in clock ticks (divide by
     * sysconf(_SC_CLK_TCK)).
     */
    long unsigned int stime_ticks;
    /**
     * Amount of time that this process's waited-for children have been scheduled in kernel mode, measured in
     * clock ticks (divide by sysconf(_SC_CLK_TCK)).
     */
    long int         cstime_ticks;
    long unsigned int vsize;                         // Virtual memory size in bytes
    long unsigned int rss;                           // Resident set size in bytes
    long unsigned int cpu_total_time;
};

/**
 * NIC statistics in a time interval
 */
struct nstats {
    uint16_t port_id;                                // Port identifier
    struct timeval s_t;                              // Start time
    struct timeval e_t;                              // End time
    double elapsed;                                  // Elapsed time (seconds)
    struct rte_eth_stats stat;                       // Statistics from an Ethernet port
};

/**
 * All statistics
 */
struct stats {
    uint16_t port_id;
    struct timeval base;
    struct timeval pre_t;
    struct timeval cur_t;
    struct rte_eth_stats nstat_pre;
    struct rte_eth_stats nstat_cur;
    struct pstats pstat_pre;
    struct pstats pstat_cur;
    char cpu_path[30];
    char mem_path[30];
};

/**
 * Initilize a nstats instance and reset the statistics of the given port
 *
 * @para port_id
 *   Port identifier
 * @return
 *   A nstats instance
 */
struct nstats new_nstats(uint16_t port_id);
/**
 * Retrieve and print the general I/O statistics of an Ethernet device.
 *
 * @para nic_stats
 *   NIC statistics.
 * @para num_queue
 *   Number of queues needed to be retrieved
 */
void print_nstats(struct nstats stat, uint16_t num_queue);
/**
 * Print the extended statistics of an Ethernet device, e.g., rx_discards_phy and tx_discards_phy.
 *
 * @para port_id
 *   Port identifier
 */
void print_xstats(uint16_t port_id);
/**
 * Retrieve and print the general I/O statistics of an Ethernet device every `interval` second(s).
 * Note: This function will block the running of the main process
 *
 * @para port_id
 *   Port identifier
 * @para interval
 *   time interval
 * @para force_quit
 *   A pointer to the force quit identifer.
 */
void print_stats_with_interval(uint16_t port_id, uint64_t interval);

void init_stat(void);
int update_stat(uint64_t limit);
void exit_stat(void);

#endif