#ifndef _CONF_H_
#define _CONF_H_

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#include <rte_ether.h>
/**
 * Interval to print NIC statistics
 */
#define STAT_INTERVAL         1
/**
 * Default time to run test
 */
#define STAT_ALLTIME          10
/**
 * SIZE_LINK_OVERHEAD =
 *    12 bytes (Inter-Packet Gap)
 *  +  7 bytes (Preamble)
 *  +  1 byte  (Start Frame Delimiter)
 *  +  4 bytes (CRC)
 */
#define SIZE_LINK_OVERHEAD    20
/**
 * The number of elements in the mbuf pool
 */
#define SIZE_MBUF_POOL        8192 // > MAX_WND
/**
 * Size of the per-core object cache
 */
#define SIZE_MCACHE           512
/**
 * The number of transmit descriptors to allocate for the transmit ring.
 *
 * See  struct rte_eth_desc_lim for the HW descriptor ring limitations
 */
#define SIZE_RING_TX          2048
/**
 * The number of receive descriptors to allocate for the receive ring.
 */
#define SIZE_RING_RX          2048
/**
 * The maximum number of packets to transmit
 */
#define CLIENT_SIZE_BURST_TX  32
#define SERVER_SIZE_BURST_TX  32
/**
 * The maximum number of packets to receive
 */
#define CLIENT_SIZE_BURST_RX  32
#define SERVER_SIZE_BURST_RX  32
// Max length of path
#define LEN_PATH              200
// Max length of ip address
#define LEN_IP_ADDR           20
// Max length of an argument
#define LEN_ARGV              32
// Max number of lcores allocated to DPDK
#define MAX_LCORE             64
// Max number of items in my_argv
#define MAX_ARGC              64
// Max size of sliding window
#define MAX_WND               512
#define DEFAULT_PORT          5000
// Task size (bytes)
#define BUFSIZE               "2M"  // ??? ??? ??? ???
#define MTU                   1500
// RTT TEST
/**
 * Number of ping packet for rtt measurement
 */
#define NUM_PING              10000
/**
 * Retransmission timeout (ms)
 */
 #define RTO                  200
/**
 * Max retry
 */
#define MAX_RETRY             3

struct conn_t {
    bool is_rtt;
    uint16_t port_id;
    uint16_t queue_id;
    uint16_t pkt_size;
    /* TCP/UDP Layer */        
    uint16_t src_port;
    uint16_t dst_port;  

    unsigned ID;
    unsigned lcore_id;

    struct rte_mempool* mbuf_pool;

    /* IP Layer */
    uint32_t src_addr;                               
    uint32_t dst_addr;    
    /* Ethernet Layer */
    struct rte_ether_addr src_mac;                   
    struct rte_ether_addr dst_mac;                 
} __rte_cache_aligned;

struct conf_t {
    char src_ip_str[LEN_IP_ADDR];
    char dst_ip_str[LEN_IP_ADDR];  
    char path_to_cpumem[LEN_PATH];
    char rtt_path[LEN_PATH];

    bool is_rtt;               // By default, we measure bandwidth rather than rtt
    bool is_server;
    bool is_client;
    bool is_udp;

    uint16_t port_id;
    uint16_t num_thread;       // Number of DPDK slave threads 
    uint16_t total_lcore;      // num_thread + 1
    uint16_t port_base;        // Base port, thread i's port = port_base + i
    uint16_t win_size;         // Max sliding window size
    uint16_t pkt_size;         // Packet size, Ethernet + IP + TCP/UDP + payload

    uint32_t src_ip;           // Local IP    
    uint32_t dst_ip;           // Server's IP
    uint32_t num_ping;
    uint64_t data_size;        // Number of bytes to transmit
    uint64_t bufsize;          // Size of the sending task

    struct timeval all_time;   // Time to transmit for 
    struct timeval interval;   // Time between periodic bandwidth reports
    struct conn_t conn[MAX_LCORE];
} __rte_cache_aligned;

/**
 * Get the address of the global configuration variable `conf`
 *
 * @return
 *   A pointer to the `conf`
 */
struct conf_t* get_conf(void);

/**
 * Generate arguments for eal_init()
 * 
 * @para app
 *   Application name (dperf)
 * @para my_argv
 *   An array will be filled (must be allocated from heap)
 * @return 
 *   length of my_argv
 */
int opt_genargv(char* app, char** my_argv);

/**
 * Initialize connection configuration
 */
void init_conn(void);

/**
 * Capture the signal and force quit the application
 *
 * @para signum
 *   Either SIGINT or SIGTERM
 */
void signal_handler(int signum);

/**
 * Get the force quit flag `volatile bool force_quit`
 *
 * @preturn
 *   A pointer to the `force_quit` flag
 */

volatile bool* get_quit(void);
/**
 * Force quit the program
 */
void set_quit(void);

/**
 * Parse arguments
 */
int opt_parser(int argc, char** argv);

#endif
