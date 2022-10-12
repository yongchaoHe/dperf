#ifndef _UTIL_H_
#define _UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <numa.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <signal.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_kni.h>
#include <rte_mbuf_core.h>

#define CO_RED                "\033[1;31m"
#define CO_BLACK              "\033[1;30m"
#define CO_RED                "\033[1;31m"
#define CO_GREEN              "\033[1;32m"
#define CO_YELLOW             "\033[1;33m"
#define CO_BLUE               "\033[1;34m"
#define CO_PURPLE             "\033[1;35m"
#define CO_SKYBLUE            "\033[1;36m"
#define CO_WHITE              "\033[1;37m"
#define CO_RESET              "\033[0m"
#define BG_BLACK              "\033[40m"
#define BG_RED                "\033[41m"
#define BG_GREEN              "\033[42m"
#define BG_YELLOW             "\033[43m"
#define BG_BLUE               "\033[44m"
#define BG_PURPLE             "\033[45m"
#define BG_SKYBLUE            "\033[46m"
#define BG_WHITE              "\033[47m"
#define BG_RESET              "\033[0m"
#define LOG_INFO(format, ...)  printf("[%sINFO%s] "format, CO_GREEN,  CO_RESET, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  printf("[%sWARN%s] "format, CO_YELLOW, CO_RESET, ##__VA_ARGS__)
#define LOG_ERRO(format, ...)  printf("[%sERRO%s] "format, CO_RED,    CO_RESET, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) printf("[%sDEBUG%s] "format, CO_PURPLE, CO_RESET, ##__VA_ARGS__)
void LOG_LINE(int len, char ch, char* info);

/**
 * Get NIC name by IP address
 * 
 * @para ip_addr
 *   IP address, e.g., 10.0.0.1
 * @para nic_name
 *   (OUT) NIC name, will be dynamically allocated 
 * @return
 *   0: success 
 *  -1: cannot find a device for ip_addr
 */
int nic_getname_by_ip(char* ip_addr, char** nic_name);

/**
 * Get the IP address for the given NIC
 *
 * @para nic_name
 *   (IN) NIC name
 * @para ip_addr
 *   (OUT) IP address, xxx.xxx.xxx.xxx
 *
 * @return
 *   0: Sucess
 *  -1: Otherwise
 */
int nic_getip_by_name(const char *nic_name, char *ip_addr);

/**
 * Get the PCIe address for the given NIC
 *
 * @para nic_name
 *   NIC name
 * @para businfo
 *   PCIe address, xxxx:xx:xx.x
 *
 * @return
 *   0: Sucess
 *  -1: Otherwise
 */
int nic_getbusinfo_by_name(const char* nic_name, char** businfo);

/**
 * Find out on which NUMA node the given NIC is 
 *
 * @para businfo
 *   PCIe address
 * @return
 *   NUMA node index, if success
 *  -1, otherwise
 */
int nic_getnumanode_by_businfo(const char* businfo);

/**
 * Execute 'lscpu' and return the cpu list for the given numa_node
 * 
 * @para numa_node
 *   Index of numa node 
 * @para cpu_list
 *   core list, e.g., 14-27,42-55
 * @return 
 *   0, success
 *  -1, otherwise
 *
 */
int nic_getcpus_by_numa(int numa_node, char** cpu_list);

/**
 * Read arp cache from /proc/net/arp and return MAC for the given ip
 * 
 * @para ip_addr
 *   IP address
 * @return
 *   MAC address : if a record exists in the arp cache
 *   broadcase addr : otherwise
 */
struct rte_ether_addr nic_getarp_by_ip(char* ip_addr);

/**
 * Generate cpu core mask. For example, (cpu_list=14-27,42-55 & num_core=5) -> 0x7c000  
 * 
 * @pare cpu_list
 *   Available cpu cores 
 * @para num_core 
 *   Number of cores applied 
 * @para core_mask
 *   CPU core mask
 * @return
 *   On success, return the number of cores can be used (max(|cpu_list|, num_core))
 */
int cpu_getmask(char* cpu_list, int num_core, char** core_mask);

/**
 * Convert a string to struct timeval tv
 *
 * @para time
 *   xxxus/ms/s/m/h
 * @para tv
 *   return value
 * @return 
 *   0, success
 *  -1, otherwise
 */
int time_strto_tv(char* time, struct timeval* tv);

/**
 * Check if a string (suf) is suffix of another (str)
 * 
 * @para str
 *   source string
 * @para suf
 *   suffix
 * @return
 *   true, if suf is the suffix of str
 */
bool has_suffix(char* str, char* suf);

inline double
time_diff(struct timeval s_t, struct timeval e_t) {
    return e_t.tv_sec - s_t.tv_sec + (e_t.tv_usec - s_t.tv_usec) / 1000000.0;
}

inline double
time_double(struct timeval t) {
    return t.tv_sec + t.tv_usec / 1000000.0;
}

uint64_t time_to_hz_tv(struct timeval t);
uint64_t time_to_hz_s(uint32_t t);
uint64_t time_to_hz_ms(uint32_t t);
uint64_t time_to_hz_us(uint32_t t);
uint64_t hz_to_us(uint64_t t);
uint64_t hz_to_ms(uint64_t t);
uint64_t hz_to_s(uint64_t t);
struct timeval hz_to_tv(uint64_t t);

/**
 * Print raw packet content.
 *
 * @para buf
 *   mbuf that carries the network packet buffer to be printed.
 */
inline void print_mraw(struct rte_mbuf* buf) {
    LOG_INFO("Showing Packet Content...\n");
    uint8_t* data = rte_pktmbuf_mtod(buf, uint8_t*);
    for (uint16_t loop = 0; loop < buf->data_len; loop++) {
        if (loop % 16 == 0)
            printf("%03d: ", loop);
        printf("%02x ", data[loop]);
        if ((loop + 1) % 8  == 0)
            printf(" ");
        if ((loop + 1) % 16 == 0)
            printf("\n");
    }
    if (buf->data_len % 16 != 0)
        printf("\n");
}

#endif 
