#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <rte_hash_crc.h>
#include <rte_ring.h>
#include <rte_memory.h>
#include <rte_malloc.h>
#include <rte_ethdev.h>
#include <immintrin.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include "util.h"
#include "core.h"
#include "conf.h"

#define MAX_TASK 65536
struct rte_ring* task_todo[MAX_LCORE];
struct rte_ring* task_done;
struct rte_mempool* task_pool = NULL;

void
init_core(struct conf_t* conf) {
    LOG_INFO("Initilizing and allocating resources for lcore ...\n");
    for (int loop = 0; loop < conf->total_lcore; loop++) {
        char name[20];
        sprintf(name, "TASK_TODO_%d", loop);
        task_todo[loop] = rte_ring_create(name, MAX_TASK, rte_socket_id(), 0);
    }
    task_done = rte_ring_create("TASK_DONE", MAX_TASK, rte_socket_id(), 0);
    task_pool = rte_mempool_create("TASK_POOL", MAX_TASK - 1, sizeof(struct task_t), 0, 0, NULL, NULL, NULL, NULL, rte_socket_id(), 0);
}

void
exit_core(struct conf_t* conf) {
    LOG_INFO("Freeing rte_ring ...\n");
    for (int loop = 0; loop < conf->total_lcore; loop++) {
        rte_ring_free(task_todo[loop]);
    }
    rte_ring_free(task_done);
    rte_mempool_free(task_pool);
}

uint64_t th_bytes[32] = {0};
int
task_enqueue(struct task_t* todo) {
    struct conf_t* conf = get_conf(); 
    struct task_t* task = NULL;
    int ret = rte_mempool_get(task_pool, (void**) &task);
    if (unlikely(ret != 0)) {
        return -1;
    }
    rte_memcpy(task, todo, sizeof(struct task_t));

    // LB 1
    int thread_id = todo->ID % conf->num_thread;

    // LB 2
    // int thread_id = 0;
    // for (int loop = 0; loop < CLIENT_THREAD; loop++) {
    //     if (th_bytes[loop] < th_bytes[thread_id])
    //         thread_id = loop;
    // }
    // th_bytes[thread_id] += task->shm_len;

    rte_ring_enqueue(task_todo[thread_id], (void*) task);
    return 0;
}

// uint64_t
// task_dequeue(void) {
//     uint64_t task_id = -1;
//     struct task_t* task = NULL;
//     int ret = rte_ring_dequeue(task_done, (void**) &task);
//     if (ret == 0) {
//         task_id = task->ID;
//         rte_mempool_put(task_pool, task);
//     }
//     return task_id;
// }
struct task_t*
task_dequeue(void) {
    struct task_t* task = NULL;
    int ret = rte_ring_dequeue(task_done, (void**) &task);
    if (ret == 0) {
        rte_mempool_put(task_pool, task);
    }
    return task;
}

static int ping_cmpfunc(list_item_t** a, list_item_t** b) {
    return ((*((struct ping_t**) a))->rtt) - ((*((struct ping_t**) b))->rtt); 
}

static inline void
send_all(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** bufs, uint16_t burst_num) {
    uint16_t nb_tx = rte_eth_tx_burst(port_id, queue_id, bufs, burst_num);
    uint16_t temp = 0;
    if (unlikely(nb_tx < burst_num)) {
        while (nb_tx < burst_num) {
            temp = rte_eth_tx_burst(port_id, queue_id, &bufs[nb_tx], burst_num - nb_tx);
            nb_tx += temp;
        }
    }
}

static inline void 
gen_ping(struct conn_t* conn, struct rte_mbuf *buf, uint16_t payload_len, uint32_t seq) {
    struct rte_ether_hdr *h_eth = NULL;
    struct rte_ipv4_hdr  *h_ip4 = NULL;
    struct rte_tcp_hdr   *h_tcp = NULL;
    // struct rte_udp_hdr   *h_udp = NULL;

    buf->pkt_len                = sizeof(struct rte_ether_hdr);
    buf->data_len               = sizeof(struct rte_ether_hdr);

    h_eth = rte_pktmbuf_mtod(buf, struct rte_ether_hdr*);
    h_eth->s_addr               = conn->src_mac;
    h_eth->d_addr               = conn->dst_mac;
    h_eth->ether_type           = htons(RTE_ETHER_TYPE_IPV4);

    h_ip4 = (struct rte_ipv4_hdr*) rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
    h_ip4->version_ihl          = 0x45;
    h_ip4->type_of_service      = 0;
    h_ip4->packet_id            = 0;
    h_ip4->fragment_offset      = 0;
    h_ip4->time_to_live         = 0x0f;
    h_ip4->next_proto_id        = IPPROTO_TCP;
    h_ip4->hdr_checksum         = 0;
    h_ip4->src_addr             = conn->src_addr;
    h_ip4->dst_addr             = conn->dst_addr;

    h_tcp = (struct rte_tcp_hdr*) rte_pktmbuf_append(buf, sizeof(struct rte_tcp_hdr));
    h_tcp->src_port             = conn->src_port;
    h_tcp->dst_port             = conn->dst_port;
    h_tcp->sent_seq             = htonl(seq);
    h_tcp->data_off             = 0x50;
    h_tcp->rx_win               = 0xffff;
    // h_tcp->recv_ack             = htonl(seq);
    // h_tcp->tcp_flags            = RTE_TCP_ACK_FLAG;

    // char* payload = rte_pktmbuf_append(buf, payload_len);
    h_ip4->total_length         = htons(buf->data_len - RTE_ETHER_HDR_LEN);
}

static inline uint64_t
gen_tcp(struct conn_t* conn, struct rte_mbuf *buf, uint64_t sent_bytes, struct task_t* task, uint32_t seq) {
    struct rte_ether_hdr *h_eth = NULL;
    struct rte_ipv4_hdr  *h_ip4 = NULL;
    struct rte_tcp_hdr   *h_tcp = NULL;

    uint16_t payload_len = (uint16_t) (conn->pkt_size - RTE_ETHER_HDR_LEN - sizeof(struct rte_ipv4_hdr) - sizeof(struct rte_tcp_hdr));
    // payload_len = RTE_MIN(payload_len, task->len - sent_bytes);

    buf->pkt_len                = sizeof(struct rte_ether_hdr);
    buf->data_len               = sizeof(struct rte_ether_hdr);

    h_eth = rte_pktmbuf_mtod(buf, struct rte_ether_hdr*);
    h_eth->s_addr               = conn->src_mac;
    h_eth->d_addr               = conn->dst_mac;
    h_eth->ether_type           = htons(RTE_ETHER_TYPE_IPV4);

    h_ip4 = (struct rte_ipv4_hdr*) rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
    h_ip4->version_ihl          = 0x45;
    h_ip4->type_of_service      = 0;
    h_ip4->packet_id            = 0;
    h_ip4->fragment_offset      = 0;
    h_ip4->time_to_live         = 0x0f;
    h_ip4->next_proto_id        = IPPROTO_TCP;
    h_ip4->hdr_checksum         = 0;
    h_ip4->src_addr             = conn->src_addr;
    h_ip4->dst_addr             = conn->dst_addr;

    h_tcp = (struct rte_tcp_hdr*) rte_pktmbuf_append(buf, sizeof(struct rte_tcp_hdr));
    h_tcp->src_port             = conn->src_port;
    h_tcp->dst_port             = conn->dst_port;
    h_tcp->sent_seq             = htonl(seq);
    h_tcp->data_off             = 0x50;
    h_tcp->rx_win               = 0xffff;

    char* payload = rte_pktmbuf_append(buf, payload_len);
    rte_memcpy(payload, task->addr + sent_bytes, payload_len);
    h_ip4->total_length         = htons(buf->data_len - RTE_ETHER_HDR_LEN);

    return payload_len;
}

static inline uint64_t
gen_udp(struct conn_t* conn, struct rte_mbuf *buf, uint64_t sent_bytes, struct task_t* task) {
    struct rte_ether_hdr *h_eth = NULL;
    struct rte_ipv4_hdr  *h_ip4 = NULL;
    struct rte_udp_hdr   *h_udp = NULL;

    uint16_t payload_len = (uint16_t) (conn->pkt_size - RTE_ETHER_HDR_LEN - sizeof(struct rte_ipv4_hdr) - sizeof(struct rte_udp_hdr));
    // payload_len = RTE_MIN(payload_len, task->len - sent_bytes);

    buf->pkt_len                = sizeof(struct rte_ether_hdr);
    buf->data_len               = sizeof(struct rte_ether_hdr);

    h_eth = rte_pktmbuf_mtod(buf, struct rte_ether_hdr*);
    h_eth->s_addr               = conn->src_mac;
    h_eth->d_addr               = conn->dst_mac;
    h_eth->ether_type           = htons(RTE_ETHER_TYPE_IPV4);

    h_ip4 = (struct rte_ipv4_hdr*) rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
    h_ip4->version_ihl          = 0x45;
    h_ip4->type_of_service      = 0;
    h_ip4->packet_id            = 0;
    h_ip4->fragment_offset      = 0;
    h_ip4->time_to_live         = 0x0f;
    h_ip4->next_proto_id        = IPPROTO_UDP;
    h_ip4->hdr_checksum         = 0;
    h_ip4->src_addr             = conn->src_addr;
    h_ip4->dst_addr             = conn->dst_addr;

    h_udp = (struct rte_udp_hdr*) rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr));
    h_udp->src_port             = conn->src_port;
    h_udp->dst_port             = conn->dst_port;
    h_udp->dgram_len            = htons(payload_len);

    char* payload = rte_pktmbuf_append(buf, payload_len);
    rte_memcpy(payload, task->addr + sent_bytes, payload_len);
    h_ip4->total_length         = htons(buf->data_len - RTE_ETHER_HDR_LEN);

    return sent_bytes + payload_len;
}

static inline void 
do_ping(struct conn_t* conn) {
    /* Only use one thread for rtt measurement */
    if (conn->ID > 1) {
        LOG_WARN("thread %u exits, only use the first thread for rtt test\n", conn->ID);
        return;
    }

    struct conf_t* conf = get_conf();
    struct list_t* list = list_alloc();

    struct rte_mbuf *bufs_tx[CLIENT_SIZE_BURST_TX];
    struct rte_mbuf *bufs_rx[CLIENT_SIZE_BURST_RX];
    // struct rte_ether_hdr *h_eth = NULL;
    struct rte_ipv4_hdr  *h_ip4 = NULL;
    struct rte_tcp_hdr   *h_tcp = NULL;
    // struct rte_udp_hdr   *h_udp = NULL;
    uint16_t nb_rx = 0;
    uint64_t ts_sent = 0, ts_recv = 0;
    uint64_t timeout = time_to_hz_ms(RTO);

    struct timeval begin = {0};
    struct timeval end = {0};
    gettimeofday(&begin, NULL);

    for (uint32_t loop = 0; loop < conf->num_ping; loop++) {
        uint32_t timeout_counter = 0;
        for (uint32_t inner = 0; inner < MAX_RETRY; inner++) {
            bufs_tx[0] = rte_pktmbuf_alloc(conn->mbuf_pool);
            gen_ping(conn, bufs_tx[0], 10, loop);
            ts_sent = rte_rdtsc();
            // print_mraw(bufs_tx[0]);
            send_all(conn->port_id, conn->queue_id, bufs_tx, 1);
            for (;;) {
                nb_rx = rte_eth_rx_burst(conn->port_id, conn->queue_id, bufs_rx, 8);
                if (nb_rx > 0) {
                    ts_recv = rte_rdtsc();
                    // h_eth = rte_pktmbuf_mtod(bufs_rx[0], struct rte_ether_hdr*);
                    h_ip4 = rte_pktmbuf_mtod_offset(bufs_rx[0], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
                    if (likely(h_ip4->next_proto_id == IPPROTO_TCP)) {
                        h_tcp = rte_pktmbuf_mtod_offset(bufs_rx[0], struct rte_tcp_hdr*, RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr));
                        if (ntohl(h_tcp->sent_seq) == loop) {
                            struct ping_t* rtt = (struct ping_t*) malloc(sizeof(struct ping_t));
                            rtt->rtt = ts_recv - ts_sent;
                            list_append(list, (struct list_item_t*) rtt);
                        }
                    }
                    rte_pktmbuf_free_bulk(bufs_rx, nb_rx);
                    break;
                }

                ts_recv = rte_rdtsc();
                if (ts_recv - ts_sent > timeout) {
                    timeout_counter++;
                    break;
                }
            }

            if (timeout_counter <= inner) 
                break;
        }

        if (timeout_counter == MAX_RETRY) {
            LOG_ERRO("Network is unreachable!\n");
            break;
        }
    }

    gettimeofday(&end, NULL);

    if (list_is_empty(list) == false) {
        list_sort(list, ping_cmpfunc);

        float perc[8] = {25.0, 50.0, 75.0, 90.0, 99.0, 99.9, 99.99, 99.999};
        int idxes[8] = {0};
        for (int loop = 0; loop < 8; loop++) {
            idxes[loop] = perc[loop] * list->length / 100.0;
        }
        FILE* fp = fopen(conf->rtt_path, "w");
        int counter = 0;
        if (fp != NULL) {
            LOG_LINE(75, '-', "Printing statistics");
            LOG_INFO("dperf  [Valid Duration] RunTime=%.2f sec; SentMessages=%u\n", time_diff(begin, end), conf->num_ping);
            LOG_INFO("dperf  ---> <MIN> observation = %lu us\n", hz_to_us(((struct ping_t*) list_get_first(list))->rtt));
            struct ping_t* iter = NULL;
            int temp_idx = 0;
            YC_LIST_FOREACH(iter, list, struct ping_t) {
                while (temp_idx < 8 && counter == idxes[temp_idx]) {
                    LOG_INFO("dperf  ---> percentile %.3f = %lu us\n", perc[temp_idx], hz_to_us(iter->rtt));
                    temp_idx++;
                }
                // LOG_INFO("%lu\n", iter->rtt);
                char buff[64] = {0};
                sprintf(buff, "loop=%06d    rtt=%lu us\n", ++counter, hz_to_us(iter->rtt));
                fputs(buff, fp);
            }
            fclose(fp);
            LOG_INFO("dperf  ---> <MAX> observation = %lu us\n", hz_to_us(((struct ping_t*) list_get_last(list))->rtt));
            LOG_INFO("Write rtt results to %s\n", conf->rtt_path);
            LOG_LINE(75, '-', "");
        } else {
            LOG_WARN("Cannot open %s\n", conf->rtt_path);
        }
    }

    list_free(list);
}

static inline void 
do_tcp(struct conn_t* conn, struct task_t* task, struct conn_client_t* ssc) {
    struct rte_mbuf *bufs_rx[CLIENT_SIZE_BURST_RX];
    struct rte_mbuf *bufs_tx[CLIENT_SIZE_BURST_TX];
    // struct rte_ether_hdr *h_eth   = NULL;
    struct rte_ipv4_hdr  *h_ip4   = NULL;
    struct rte_tcp_hdr   *h_tcp   = NULL;
    // struct rte_udp_hdr   *h_udp   = NULL;

    uint16_t nb_rx, loop, burst_num = 0;
    uint32_t seq, seq_index, seq_next;
    uint64_t ts_cur = 0, acked_bytes = 0, sent_bytes = 0, sent_pkts = 0;

    volatile bool* force_quit = get_quit();
    uint32_t counter = 0;

    // for (;;) {
    while (likely(acked_bytes < task->len)) {
        nb_rx = rte_eth_rx_burst(conn->port_id, conn->queue_id, bufs_rx, CLIENT_SIZE_BURST_RX);
        if (nb_rx > 0) {
            for (loop = 0; loop < nb_rx; loop++) {
                // h_eth = rte_pktmbuf_mtod(bufs_rx[loop], struct rte_ether_hdr*);
                h_ip4 = rte_pktmbuf_mtod_offset(bufs_rx[loop], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
                if (likely(h_ip4->next_proto_id == IPPROTO_TCP)) {
                    h_tcp = rte_pktmbuf_mtod_offset(bufs_rx[loop], struct rte_tcp_hdr*, RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr));
                    seq = ntohl(h_tcp->sent_seq);
                    seq_index = seq & (MAX_WND-1);
                    if (ssc->state[seq_index].seq == seq) {
                        // memset(&(ssc->state[seq_index]), 0, sizeof(struct conn_state_t));
                        acked_bytes += ssc->state[seq_index].bytes;
                    }
                    ssc->state[seq_index].ts = 0;
                    // memset(&(ssc->state[seq_index]), 0, sizeof(struct conn_state_t));
                }
            }
            rte_pktmbuf_free_bulk(bufs_rx, nb_rx);

            while (ssc->last_acked != ssc->last_sent) {
                seq_next  = ssc->last_acked + 1;
                seq_index = seq_next & (MAX_WND-1);
                if (ssc->state[seq_index].ts == 0)
                    ssc->last_acked = seq_next;
                else
                    break;
            }
        }

        burst_num = 0;
        ts_cur = rte_rdtsc();
        if (unlikely(((ssc->window + ssc->last_acked) == ssc->last_sent) || (sent_bytes >= task->len))) {
            while(burst_num < CLIENT_SIZE_BURST_TX) {
                seq_next  = ssc->last_acked + burst_num + 1;
                seq_index = seq_next & (MAX_WND-1);
                if (unlikely((ssc->state[seq_index].ts > 0) && (ts_cur > ssc->state[seq_index].ts + 4400000))) {
                    bufs_tx[burst_num] = rte_pktmbuf_alloc(conn->mbuf_pool);
                    gen_tcp(conn, bufs_tx[burst_num], ssc->state[seq_index].offset, task, seq_next);
                    ssc->state[seq_index].ts = ts_cur;
                    burst_num++;
                } else {
                    break;
                }
            }
        }

        while (burst_num < CLIENT_SIZE_BURST_TX) {
            if (unlikely(((ssc->window + ssc->last_acked) == ssc->last_sent) || (sent_bytes >= task->len)))
                break;
            sent_pkts++;
            ssc->last_sent++;
            seq_index                    = ssc->last_sent & (MAX_WND-1);
            bufs_tx[burst_num]           = rte_pktmbuf_alloc(conn->mbuf_pool);
            ssc->state[seq_index].bytes  = gen_tcp(conn, bufs_tx[burst_num], sent_bytes, task, ssc->last_sent);
            ssc->state[seq_index].seq    = ssc->last_sent;
            ssc->state[seq_index].ts     = ts_cur;
            ssc->state[seq_index].offset = sent_bytes;
            sent_bytes                  += ssc->state[seq_index].bytes;
            burst_num++;
        }
        send_all(conn->port_id, conn->queue_id, bufs_tx, burst_num);

        counter++;
        if (unlikely(counter == 4096)) {
            if (*force_quit == true) {
                break;
            }
            counter = 0;
        }
    }
}

static inline void 
do_udp(struct conn_t* conn, struct task_t* task) {
    // struct rte_mbuf *bufs_rx[CLIENT_SIZE_BURST_RX];
    struct rte_mbuf *bufs_tx[CLIENT_SIZE_BURST_TX];

    uint64_t sent_bytes = 0;
    uint16_t loop = 0;

    while(likely(task->len > sent_bytes)) {
        for (loop = 0; loop < CLIENT_SIZE_BURST_TX; loop++) {
            if (unlikely(task->len <= sent_bytes))
                break;
            bufs_tx[loop] = rte_pktmbuf_alloc(conn->mbuf_pool);
            sent_bytes = gen_udp(conn, bufs_tx[loop], sent_bytes, task);
        }
        send_all(conn->port_id, conn->queue_id, bufs_tx, loop);
    }
}

int
lcore_client(void* arg) {
    struct conn_t* conn = arg;
    if (rte_eth_dev_socket_id(conn->port_id) > 0 && rte_eth_dev_socket_id(conn->port_id) != (int) rte_socket_id())
        LOG_WARN("Port %u is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", conn->port_id);

    uint64_t counter = 0;
    if (conn->is_rtt == true) {
        do_ping(conn);
        return 0;
    } 

    int thread_id = conn->ID;
    int ret = 0;
    struct task_t* task = NULL;
    struct rte_ring* task_queue = task_todo[thread_id-1];
    struct conf_t* conf = get_conf();
    struct conn_client_t ssc = {0};
    ssc.last_sent = 0xffffffff;
    ssc.last_acked= 0xffffffff;
    ssc.window = conf->win_size;

    volatile bool* force_quit = get_quit();
    for (;;) {
        ret = rte_ring_dequeue(task_queue, (void**) &task);
        if (ret == 0) {
            if (conf->is_udp == false) {
                do_tcp(conn, task, &ssc);
            } else {
                do_udp(conn, task);
            }
            rte_ring_enqueue(task_done, (void*) task);
        }
        
        if (unlikely((counter & 0x4000) != 0)) {
            if (*force_quit == true) {
                break;
            }
            counter = 0;
        }
        counter++;
    }
    return 0;
}

int
lcore_server(void* arg) {
    struct conn_t* conn = arg;
    if (rte_eth_dev_socket_id(conn->port_id) > 0 && rte_eth_dev_socket_id(conn->port_id) != (int) rte_socket_id())
        LOG_WARN("Port %u is on remote NUMA node to polling thread.\n\tPerformance will not be optimal.\n", conn->port_id);

    struct rte_mbuf      *bufs_rx[SERVER_SIZE_BURST_RX];
    struct rte_mbuf      *bufs_tx[SERVER_SIZE_BURST_TX];
    struct rte_ether_hdr *h_eth   = NULL;
    struct rte_ipv4_hdr  *h_ip4   = NULL;
    struct rte_tcp_hdr   *h_tcp   = NULL;
    // struct rte_udp_hdr   *h_udp   = NULL;

    uint16_t rx_burst = SERVER_SIZE_BURST_RX;
    __attribute__((unused)) uint16_t tx_burst = SERVER_SIZE_BURST_TX;

    if (conn->is_rtt == true) {
        rx_burst = 8;
        tx_burst = 8;
    }

    uint16_t nb_rx, nb_tx, loop;
    uint32_t counter = 0;
    for (;;) {
        nb_rx = rte_eth_rx_burst(conn->port_id, conn->queue_id, bufs_rx, rx_burst);
        if (nb_rx > 0) {
            nb_tx = 0;
            for (loop = 0; loop < nb_rx; loop++) {
                h_eth = rte_pktmbuf_mtod(bufs_rx[loop], struct rte_ether_hdr*);
                h_ip4 = rte_pktmbuf_mtod_offset(bufs_rx[loop], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
                if (likely(h_ip4->next_proto_id == IPPROTO_TCP)) {
                    h_tcp = rte_pktmbuf_mtod_offset(bufs_rx[loop], struct rte_tcp_hdr*, RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr));

                    bufs_tx[nb_tx] = rte_pktmbuf_alloc(conn->mbuf_pool);

                    h_eth->d_addr   = h_eth->s_addr;
                    h_eth->s_addr   = conn->src_mac;
                    h_ip4->dst_addr = h_ip4->src_addr;
                    h_ip4->src_addr = conn->src_addr;
                    h_tcp->dst_port = h_tcp->src_port;
                    h_tcp->src_port = conn->src_port;
                    h_tcp->tcp_flags= RTE_TCP_ACK_FLAG;

                    h_ip4->total_length = htons(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr));

                    rte_memcpy(rte_pktmbuf_mtod(bufs_tx[nb_tx], struct rte_ether_hdr *), h_eth, RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr));
                    bufs_tx[nb_tx]->data_len = RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr);
                    bufs_tx[nb_tx]->pkt_len  = RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_tcp_hdr);

                    nb_tx++;
                }
            }
            send_all(conn->port_id, conn->queue_id, bufs_tx, nb_tx);
            rte_pktmbuf_free_bulk(bufs_rx, nb_rx);
        }
        counter++;
        if (unlikely(counter == 4096)) {
            counter = 0;
            volatile bool* force_quit = get_quit();
            if (*force_quit == true) {
                break;
            }
        }
    }

    return 0;
}