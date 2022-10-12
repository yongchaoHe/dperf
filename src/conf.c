#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#include "util.h"
#include "conf.h"

/**
 * Gloabl configuration
 */
struct conf_t global_conf = {0};

/**
 * A flag to indicate whether force quit the application
 */
volatile bool force_quit = false;

struct conf_t* get_conf(void) {
    return &global_conf;
}

volatile bool* 
get_quit(void) { 
    return &force_quit; 
}

void 
set_quit(void) { 
    force_quit = true; 
}

void
signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n");
        LOG_WARN("Signal %d received, preparing to exit ...\n", signum);
        force_quit = true;
    } else {
        printf("\n");
        LOG_WARN("Unrecognized signal %d\n", signum);
    }
}

static inline void
show_usage(char* app) {
    LOG_INFO("Usage: %s [-B host|-N nic] [-s|-c host] [options]\n", app);
    LOG_INFO("       %s [-h|--help]\n", app);
    LOG_INFO("\n");
    LOG_INFO("Client/Server:\n");
    LOG_INFO("    -i, --interval  #[s|ms]        seconds between periodic bandwidth reports\n");
    LOG_INFO("    -p, --port      #              server port to listen on/connect to\n");
    LOG_INFO("    -B, --bind      <host>         bind to <host>, an interface or multicast address\n");
    LOG_INFO("    -N, --nic       <nic>          bind to <nic>, a network interface\n");
    LOG_INFO("    -P, --parallel  #              number of threads to run\n");
    LOG_INFO("        --rtt       #              run %s client for latency test in ping pong mode\n", app);
    LOG_INFO("\n");
    LOG_INFO("Server specific:\n");
    LOG_INFO("    -s, --server                   run in server mode\n");
    LOG_INFO("\n");
    LOG_INFO("Client specific:\n");
    LOG_INFO("    -c, --client    <host>         run in client mode, connecting to <host>\n");
    LOG_INFO("    -w, --window    #              maximum TCP sliding window size (<= %d)\n", MAX_WND);
    LOG_INFO("        --bufsize   #[KMG]         lengof of buffer size to read (default=%s)\n", BUFSIZE);
    LOG_INFO("    -l, --len       #              the size of packet to be sent (Defaults: 1500 Bytes)\n");
    LOG_INFO("    -t, --time      #              time in seconds to transmit for (default 10 secs)\n");
    LOG_INFO("    -n, --num       #[KMG]         number of bytes to transmit (instead of -t)\n");
    LOG_INFO("        --rttnum                   number of packets to transmit in rtt test (Defaults: %d)\n", NUM_PING);
    LOG_INFO("    -u, --udp                      use UDP rather than TCP\n");
    exit(0);
}

static uint64_t 
convert_to_bytes(char* str) {
    uint64_t temp = atoi(str);
    if (has_suffix(str, "K") == true) {
        temp = temp * 1000;
    } else if (has_suffix(str, "M") == true) {
        temp = temp * 1000 * 1000;
    } else if (has_suffix(str, "G") == true) {
        temp = temp * 1000 * 1000 * 1000;
    } else if (has_suffix(str, "T") == true) {
        temp = temp * 1000 * 1000 * 1000 * 1000;
    } else {
        LOG_WARN("Unrecognized suffix in %s. Supported suffix is K/M/G/T, default to Byte\n", str);
    }
    return temp;
}

int 
opt_parser(int argc, char** argv) {
    char* app = argv[0];
    
    if (argc == 1) 
        show_usage(app);

    static int lopt = 0;
    static struct option opts[] = {
        {"interval", required_argument, &lopt, 1},
        {"port",     required_argument, &lopt, 2},
        {"bind",     required_argument, &lopt, 3},
        {"parallel", required_argument, &lopt, 4},
        {"rtt",      no_argument,       &lopt, 5},
        {"server",   no_argument,       &lopt, 6},
        {"client",   required_argument, &lopt, 7},
        {"window",   required_argument, &lopt, 8},
        {"len",      required_argument, &lopt, 9},
        {"time",     required_argument, &lopt, 10},
        {"num",      required_argument, &lopt, 11},
        {"udp",      no_argument,       &lopt, 12},
        {"help",     no_argument,       &lopt, 13},
        {"nic",      required_argument, &lopt, 14},
        {"bufsize",  required_argument, &lopt, 15},
        {"rttnum",   required_argument, &lopt, 16},
        {0, 0, 0, 0}
    };

    struct conf_t* conf = get_conf();
    // Set default value
    conf->num_thread = 1;
    conf->bufsize = convert_to_bytes(BUFSIZE);
    // conf->data_size = convert_to_bytes(BUFSIZE);
    conf->data_size = 0;
    strcpy(conf->path_to_cpumem, "dperf_resource.txt");
    conf->interval.tv_sec = STAT_INTERVAL;
    conf->interval.tv_usec= 0;
    conf->all_time.tv_sec = STAT_ALLTIME;
    conf->all_time.tv_usec= 0;
    conf->win_size = MAX_WND;
    conf->pkt_size = MTU;
    conf->num_ping = NUM_PING;
    conf->port_base= DEFAULT_PORT;
    strcpy(conf->rtt_path, "dperf.rtt");

    int c, opt_index = 0;
    while ((c = getopt_long(argc, argv, "i:p:B:N:P:sc:w:l:t:n:uh", opts, &opt_index)) != -1) {
        switch(c) {
        case 0:
            switch(lopt) {
            case 1:
                time_strto_tv(optarg, &conf->interval);
                break;
            case 2:
                conf->port_base = atoi(optarg);
                break;
            case 3:
                strncpy(conf->src_ip_str, optarg, LEN_IP_ADDR-1);
                break;
            case 4:
                conf->num_thread = atoi(optarg);
                break;
            case 5:
                conf->is_rtt = true;
                break;
            case 6:
                conf->is_server = true;
                break;
            case 7:
                conf->is_client = true;
                strncpy(conf->dst_ip_str, optarg, LEN_IP_ADDR-1);
                break;
            case 8:
                conf->win_size = RTE_MIN(atoi(optarg), MAX_WND);
                break;
            case 9:
                conf->pkt_size = convert_to_bytes(optarg);
                break;
            case 10:
                time_strto_tv(optarg, &conf->all_time);
                break;
            case 11:
                conf->data_size = convert_to_bytes(optarg);
                break;
            case 12:
                conf->is_udp = true;
                break;
            case 13:
                show_usage(app);
                break;
            case 14:
                if (nic_getip_by_name(optarg, conf->src_ip_str) == -1) {
                    return 0;
                }
                break;
            case 15:
                conf->bufsize = convert_to_bytes(optarg);
                break;
            case 16:
                conf->num_ping = atoi(optarg);
                break;
            default:
                show_usage(app);
                break;
            }
            break;
        case 'i':
            time_strto_tv(optarg, &conf->interval);
            break;
        case 'p':
            conf->port_base = atoi(optarg);
            break;
        case 'B':
            strncpy(conf->src_ip_str, optarg, LEN_IP_ADDR-1);
            break;
        case 'N':
            if (nic_getip_by_name(optarg, conf->src_ip_str) == -1) {
                return 0;
            }
            break;
        case 'P':
            conf->num_thread = atoi(optarg);
            break;
        case 's':
            conf->is_server = true;
            break;
        case 'c':
            conf->is_client = true;
            strncpy(conf->dst_ip_str, optarg, LEN_IP_ADDR-1);
            break;
        case 'w':
            conf->win_size = RTE_MIN(atoi(optarg), MAX_WND);
            break;
        case 'l':
            conf->pkt_size = convert_to_bytes(optarg);
            break;
        case 't':
            time_strto_tv(optarg, &conf->all_time);
            break;
        case 'n':
            conf->data_size = convert_to_bytes(optarg);
            break;
        case 'u':
            conf->is_udp = true;
            break;
        case 'h':
            show_usage(app);
            break;
        default:
            /* getopt_long already printed an error message. */
            show_usage(app);
        }
    }

    if (conf->is_server == false && conf->is_client == false) {
        LOG_ERRO("%s\n", "Not specify work mode (-s|-c host)");
        show_usage(app);
    } else if (conf->is_server == true && conf->is_client == true) {
        LOG_ERRO("%s\n", "Cannot specify work mode both in server and client (-s|-c host)");
        show_usage(app);
    }

    if (strlen(conf->src_ip_str) == 0) {
        LOG_ERRO("%s\n", "Not specify local host");
        show_usage(app);
    }

    struct in_addr s_addr;
    inet_aton(conf->src_ip_str, &s_addr);
    conf->src_ip = s_addr.s_addr;

    if (conf->is_client == true) {
        inet_aton(conf->dst_ip_str, &s_addr);
        conf->dst_ip = s_addr.s_addr;
    }

    if (conf->data_size > 0)
        conf->bufsize = RTE_MIN(conf->bufsize, conf->data_size);
    conf->total_lcore = conf->num_thread + 1;

    return 0;
}

int opt_genargv(char* app, char** my_argv) {
    int my_argc = 0;
    strcpy(my_argv[my_argc++], app);
    strcpy(my_argv[my_argc++], "-c");
    int ret = 0;
    int numa_node = 0;
    char* nic_name = NULL;
    char* businfo  = NULL;
    char* cpu_list = NULL;
    char* cpu_mask = NULL;
    struct conf_t* conf = get_conf();
    if (nic_getname_by_ip(conf->src_ip_str, &nic_name) == 0) {
        if (nic_getbusinfo_by_name(nic_name, &businfo) == 0) {
            numa_node = nic_getnumanode_by_businfo(businfo);
            if (numa_node >= 0) {
                ret = nic_getcpus_by_numa(numa_node, &cpu_list);
                if (ret == 0) {
                    ret = cpu_getmask(cpu_list, conf->num_thread+1, &cpu_mask);
                    if (ret > 0) {
                        strcpy(my_argv[my_argc++], cpu_mask);
                        free(cpu_mask);
                    } else {
                        LOG_ERRO("Cannot allocate CPU cores\n");
                        exit(-1);
                    }
                } else {
                    LOG_ERRO("Cannot read CPU core list\n");
                    exit(-1);
                }
            } else {
                LOG_ERRO("Cannot get NUMA node index\n");
                exit(-1);
            }
            free(businfo);
        } else {
            LOG_ERRO("Cannot read the businfo of %s\n", nic_name);
            exit(-1);
        }
        free(nic_name);
    } else {
        LOG_ERRO("Cannot find device for %s\n", conf->src_ip_str);
        exit(-1);
    }
    strcpy(my_argv[my_argc++], "-n");
    strcpy(my_argv[my_argc++], "4");
    // TODO --socket-mem
    strcpy(my_argv[my_argc++], "--socket-mem=0,8192");
    strcpy(my_argv[my_argc++], "-d");
    strcpy(my_argv[my_argc++], "librte_mempool.so");
    strcpy(my_argv[my_argc++], "--huge-unlink");
    strcpy(my_argv[my_argc++], "--log-level=4");

    return my_argc;
}

static inline 
void print_conn(void) {
    struct conf_t* conf = get_conf();
    LOG_LINE(90, '-', "Thread Configuration");
    LOG_INFO("ID       SRC MAC           DST MAC         SRC IP     DST IP     Q  P   C \n");
    for (uint16_t loop = 0; loop <= conf->num_thread; loop++) {
        struct conn_t* conn = &(conf->conn[loop]);
        LOG_INFO("%02u  %02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8" %02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8"  0x%08x 0x%08x  %02hu  %hu  %02u  %s\n",
            conn->ID,
            conn->src_mac.addr_bytes[0], conn->src_mac.addr_bytes[1], conn->src_mac.addr_bytes[2],
            conn->src_mac.addr_bytes[3], conn->src_mac.addr_bytes[4], conn->src_mac.addr_bytes[5],
            conn->dst_mac.addr_bytes[0], conn->dst_mac.addr_bytes[1], conn->dst_mac.addr_bytes[2],
            conn->dst_mac.addr_bytes[3], conn->dst_mac.addr_bytes[4], conn->dst_mac.addr_bytes[5],
            ntohl(conn->src_addr), ntohl(conn->dst_addr),
            conn->queue_id,
            conn->port_id,
            conn->lcore_id,
            conn->mbuf_pool->name
        );
    }
    LOG_LINE(90, '-', NULL);
}

void init_conn(void) {
    struct conf_t* conf = get_conf();

    int ret = 0;
    char* nic_name = NULL;
    char* businfo  = NULL;
    if (nic_getname_by_ip(conf->src_ip_str, &nic_name) == 0) {
        if (nic_getbusinfo_by_name(nic_name, &businfo) == 0) {
            for (uint16_t loop = 0; loop <= conf->num_thread; loop++) {
                ret = rte_eth_dev_get_port_by_name(businfo, &conf->port_id);
                rte_eth_macaddr_get(conf->port_id, &conf->conn[loop].src_mac);
                conf->conn[loop].src_addr = conf->src_ip;
                conf->conn[loop].src_port = htons(conf->port_base + loop);
                conf->conn[loop].port_id = conf->port_id;
                conf->conn[loop].queue_id = loop;
                conf->conn[loop].is_rtt   = conf->is_rtt;

                char mbuf_pool_name[20];
                sprintf(mbuf_pool_name, "MBUF_POOL_%hu", loop);
                conf->conn[loop].mbuf_pool= rte_pktmbuf_pool_create(mbuf_pool_name, SIZE_MBUF_POOL, SIZE_MCACHE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
                if (conf->conn[loop].mbuf_pool == NULL) {
                    LOG_ERRO("Cannot create mbuf pool %s\n", mbuf_pool_name);
                    exit(-1);
                }
            }
            free(businfo);
        } else {
            LOG_ERRO("Cannot read the businfo of %s\n", nic_name);
            exit(-1);
        }
        free(nic_name);
    } else {
        LOG_ERRO("Cannot find device for %s\n", conf->src_ip_str);
        exit(-1);
    }

    if (conf->is_client == true) {
        for (uint16_t loop = 0; loop <= conf->num_thread; loop++) {
            conf->conn[loop].dst_mac = nic_getarp_by_ip(conf->dst_ip_str);
            conf->conn[loop].dst_addr = conf->dst_ip;
            conf->conn[loop].dst_port = htons(conf->port_base + loop);
            conf->conn[loop].pkt_size = conf->pkt_size;
        }
    }

    unsigned lcore_id;
    uint16_t index = 0;
    RTE_LCORE_FOREACH(lcore_id) {
        conf->conn[index].ID = index;
        conf->conn[index++].lcore_id = lcore_id;
    }

    print_conn();
}
