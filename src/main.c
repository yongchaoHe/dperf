/**
 *      _                  __
 *   __| |_ __   ___ _ __ / _|
 *  / _` | '_ \ / _ \ '__| |_
 * | (_| | |_) |  __/ |  |  _|
 *  \__,_| .__/ \___|_|  |_|
 *       |_|
 * 
 * @Author   Yongchao He
 * @Email    Yongchao-He@outlook.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>

#include <rte_eal.h>
#include <rte_flow.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>

#include "util.h"
#include "conf.h"
#include "port.h"
#include "core.h"
#include "stat.h"
#include "list.h"

void stop(void) {
    struct conf_t* conf = get_conf();
    LOG_WARN("Closing and releasing resources ...\n");

    exit_stat();

    LOG_INFO("Freeing mempool resources for conn ...\n");
    for (int loop = 0; loop < conf->total_lcore; loop++) {
        rte_mempool_free(conf->conn[loop].mbuf_pool);
    }

    LOG_INFO("Stopping and clossing Port %hu ...\n", conf->port_id);
    rte_eth_dev_stop(conf->port_id);
    rte_eth_dev_close(conf->port_id);
    exit_core(conf);
}

void recv_test(struct conn_t* conn) {
    struct rte_mbuf      *bufs_rx[SERVER_SIZE_BURST_RX];
    // struct rte_mbuf      *bufs_tx[SERVER_SIZE_BURST_TX];
    // struct rte_ether_hdr *h_eth   = NULL;
    // struct rte_ipv4_hdr  *h_ip4   = NULL;
    // struct rte_tcp_hdr   *h_tcp   = NULL;
    // struct rte_udp_hdr   *h_udp   = NULL;

    volatile bool* force_quit = get_quit();
    uint16_t nb_rx, loop;
    for (;;) {
        if (*force_quit == true)
            break;
        nb_rx = rte_eth_rx_burst(conn->port_id, conn->queue_id, bufs_rx, SERVER_SIZE_BURST_RX);
        if (nb_rx > 0) {
            for (loop = 0; loop < nb_rx; loop++) {
                print_mraw(bufs_rx[loop]);
            }
            rte_pktmbuf_free_bulk(bufs_rx, nb_rx);
            break;
        }
    }
}

int
lcore_daemon(struct conn_t* arg) {
    struct conn_t* conn = arg;
    if (rte_eth_dev_socket_id(conn->port_id) > 0 && rte_eth_dev_socket_id(conn->port_id) != (int) rte_socket_id())
        LOG_WARN("Port %u is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", conn->port_id);

    struct conf_t* conf = get_conf();
    uint64_t alltime = rte_get_timer_hz() * time_double(conf->all_time);

    struct list_t* list = list_alloc();
    if (conf->is_client == true && conf->is_rtt == false) {
        uint64_t off = 0;
        uint64_t data_size = conf->data_size;
        if (conf->data_size == 0) {
            data_size = 10 * conf->bufsize;
        }
        uint64_t inc_id = 0;
        while (off < data_size) {
            for (int loop = 0; loop < conf->num_thread; loop++) {
                uint64_t len = RTE_MIN(conf->bufsize, data_size - off);
                char* addr = (char* ) malloc(len * sizeof(char));
                // for (uint64_t iter = 0; iter < len; iter+=1024) {
                //     addr[iter] = '0';
                // }
                // char* addr = (char* ) rte_malloc(NULL, len * sizeof(char), 0);
                // char* addr = (char* ) rte_zmalloc_socket(NULL, len * sizeof(char), 0, ((int) rte_socket_id()));

                struct task_t* task = (struct task_t* ) malloc(sizeof(struct task_t));
                task->addr = addr;
                task->len = len;
                // task->ID = loop;
                task->ID = inc_id++;
                list_append(list, (struct list_item_t*) task);
            }
            off = off + conf->bufsize;
        }
    }

    struct task_t* iter = NULL;
    YC_LIST_FOREACH(iter, list, struct task_t) {
        task_enqueue(iter);
    }

    init_stat();

    uint32_t counter = 0;
    struct task_t* task = NULL;
    if (conf->is_client == true && (conf->is_rtt == true || conf->data_size > 0)) {
        for (;;) {
            task = task_dequeue();
            if (task != NULL) {
                counter++;
                if (counter == list->length) {
                    set_quit();
                    break;
                }
            }
            if (conf->is_rtt == true)
                break;
        }

        rte_eal_mp_wait_lcore();

        YC_LIST_FOREACH(iter, list, struct task_t) {
            free(iter->addr);
            // rte_free(iter->addr);
        }
        list_free(list);
        return 0;
    }

    volatile bool* force_quit = get_quit();
    while(likely(!(*force_quit))) {
        task = task_dequeue();
        if (task != NULL) {
            if (conf->data_size == 0)
                task_enqueue(task);
        }
        if (counter == 1000) {
            int ret = update_stat(alltime);
            if (conf->is_client == true && ret == 1) {
                break;
            }
            // rte_delay_ms(1);
            counter = 0;
        }
        // if (counter < 1 && conf->is_server == true)
        //     recv_test(conn);
        counter++;
    }
    if (conf->is_client == true && conf->data_size == 0 && conf->is_rtt == false) {
        set_quit();
    }

    rte_eal_mp_wait_lcore();

    YC_LIST_FOREACH(iter, list, struct task_t) {
        free(iter->addr);
        // rte_free(iter->addr);
    }
    list_free(list);
    return 0;
}

int
main(int argc, char** argv) {
    opt_parser(argc, argv);

    char** my_argv = (char**) calloc(MAX_ARGC, sizeof(char*));
    for (int loop = 0; loop < MAX_ARGC; loop++)
        my_argv[loop] = (char*) calloc(LEN_ARGV, sizeof(char));
    int my_argc = opt_genargv(argv[0], my_argv);

    int ret = 0;
    unsigned nb_ports = 0;

    optind = 1;
    LOG_INFO("Initilizing EAL ...\n");
    LOG_INFO("EAL arguments: ");
    for (int loop = 0; loop < my_argc; loop++)
        printf("%s ", my_argv[loop]);
    printf("\n");
    ret = rte_eal_init(my_argc, my_argv);
    if (ret < 0) {
        LOG_ERRO("Error with EAL initialization\n");
    } else {
        LOG_INFO("EAL initialization done!\n");
    }

    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports < 2 || (nb_ports & 1)) {
        LOG_ERRO("Number of ports must be even [%u Port(s) Detected]\n", nb_ports);
    } else {
        LOG_INFO("%u ports detected\n", nb_ports);
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    struct conf_t* conf = get_conf();

    init_conn();
    if (init_port() == -1) {
        LOG_ERRO("Initilize port failed!\n");
        exit(-1);
    }
    init_flow();
    init_core(conf);

    // uint16_t client_id = 1;
    // uint16_t server_id = 1;
    LOG_INFO("Launching lcore daemon ...\n");
    for (int loop = 1; loop < conf->total_lcore; loop++) {
        if (conf->is_server == true) {
            LOG_INFO("  -- launch "CO_RED"lcore_server"CO_RESET" on lcore %u (Thread %u)\n", conf->conn[loop].lcore_id, conf->conn[loop].ID);
            rte_eal_remote_launch(lcore_server, &conf->conn[loop], conf->conn[loop].lcore_id);
        } else if (conf->is_client == true) {
            LOG_INFO("  -- launch "CO_YELLOW"lcore_client"CO_RESET" on lcore %u (Thread %u)\n", conf->conn[loop].lcore_id, conf->conn[loop].ID);
            rte_eal_remote_launch(lcore_client, &conf->conn[loop], conf->conn[loop].lcore_id);
        }
    }

    LOG_INFO("Press Ctl+C to exit...\n");
    lcore_daemon(&conf->conn[0]);

    // rte_eal_mp_wait_lcore();
    stop();

    return 0;
}
