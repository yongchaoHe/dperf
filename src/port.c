#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <rte_timer.h>
#include <rte_ethdev.h>

#include "port.h"
#include "util.h"
#include "conf.h"

static inline void
print_dev_conf(uint16_t port_id) {
    struct conf_t* conf = get_conf();
    char if_name[20];

    struct rte_ether_addr addr;
    struct rte_eth_dev_info dev_info;
    rte_eth_macaddr_get(port_id, &addr);
    rte_eth_dev_info_get(port_id, &dev_info);
    LOG_INFO("Showing configuration for port %hu ...\n", port_id);
    LOG_LINE(75, '-', "Port Configuration");
    if_indextoname(dev_info.if_index, if_name);
    LOG_INFO("Ethernet interface %s is on NUMA Node (Socket) %d\n", if_name, dev_info.device->numa_node);
    LOG_INFO("Device Info, Driver: %s, MAC: %02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8", PCI: %s\n",
        dev_info.driver_name,
        addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
        addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5],
        dev_info.device->name
    );
    LOG_INFO("\n");
    LOG_INFO("Queue Q_Use   Q_All   Desc_Use   Desc_All    Burst    Ring    Queues\n");
    LOG_INFO("RX:      %2hu    %4hu      %5d      %5hu      %2hu     %4hu        %2hu\n",
        dev_info.nb_rx_queues,
        dev_info.max_rx_queues,
        conf->total_lcore * SIZE_RING_RX,
        dev_info.rx_desc_lim.nb_max,
        dev_info.default_rxportconf.burst_size,
        dev_info.default_rxportconf.ring_size,
        dev_info.default_rxportconf.nb_queues
    );
    LOG_INFO("TX:      %2hu    %4hu      %5d      %5hu      %2hu     %4hu        %2hu\n",
        dev_info.nb_tx_queues,
        dev_info.max_tx_queues,
        conf->total_lcore * SIZE_RING_TX,
        dev_info.tx_desc_lim.nb_max,
        dev_info.default_txportconf.burst_size,
        dev_info.default_txportconf.ring_size,
        dev_info.default_txportconf.nb_queues
    );
    LOG_LINE(75, '-', NULL);
}

static inline void
assert_link_status(uint16_t port_id) {
    LOG_INFO("Asserting link status for Port %hu ...", port_id);
    struct rte_eth_link link;
    uint8_t rep_cnt = 9; /* 9s (9 * 1000ms) in total */
    memset(&link, 0, sizeof(link));
    do {
        rte_eth_link_get_nowait(port_id, &link);
        if (link.link_status == ETH_LINK_UP)
            break;
        rte_delay_ms(1000);
    } while (--rep_cnt);
    if (link.link_status == ETH_LINK_DOWN)
        rte_exit(EXIT_FAILURE, ":: error: link is still down\n");
    else
        printf(" Done\n");
}

int
init_port(void) {
    struct conf_t* conf = get_conf();

    int ret = 0;
    uint16_t num_queue = conf->total_lcore;
    struct conn_t* conn_arr = conf->conn;
    uint16_t port_id = conf->port_id;
    // ret = rte_eth_dev_get_port_by_name(conf->port_name, &port_id);
    // if (ret != 0) {
    //     LOG_ERRO("Cannot find the device %s\n", conf->port_name);
    //     return -1;
    // }

    if (!rte_eth_dev_is_valid_port(port_id)) {
        LOG_ERRO("Port %hu is not a valid port\n", port_id);
        return -1;
    }

    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(port_id, &dev_info);

    struct rte_eth_conf port_conf = {
        .rxmode = {
            .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
        },
    };
    if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
        port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    } else {
        LOG_WARN("Device does not support DEV_TX_OFFLOAD_MBUF_FAST_FREE\n");
    }

    ret = rte_eth_dev_configure(port_id, num_queue, num_queue, &port_conf);
    if (ret != 0) {
        LOG_ERRO("Failed to configure port %hu\n", port_id);
        return ret;
    } else {
        LOG_INFO("Configuring Port %hu: %hu receive queues and %hu transmit queues will be set up\n", port_id, num_queue, num_queue);
    }

    struct rte_eth_txconf txq_conf = dev_info.default_txconf;
    struct rte_eth_rxconf rxq_conf = dev_info.default_rxconf;;
    txq_conf.offloads = port_conf.txmode.offloads;
    rxq_conf.offloads = port_conf.rxmode.offloads;

    uint16_t q_start = 0;
    for (uint16_t q_id = q_start; q_id < num_queue; q_id++) {
        struct conn_t conn = conn_arr[q_id];
        ret = rte_eth_rx_queue_setup(port_id, conn.queue_id, SIZE_RING_RX, rte_eth_dev_socket_id(port_id), &rxq_conf, conn.mbuf_pool);
        if (ret < 0)
            return ret;
        ret = rte_eth_tx_queue_setup(port_id, conn.queue_id, SIZE_RING_TX, rte_eth_dev_socket_id(port_id), &txq_conf);
        if (ret < 0)
            return ret;
        LOG_INFO("  -- set up "CO_RED"rx %hu"CO_RESET" (%d receive descs) and "CO_YELLOW"tx %hu"CO_RESET" (%d transmit descs)\n", conn.queue_id, SIZE_RING_RX, conn.queue_id, SIZE_RING_TX);
    }
    ret = rte_eth_promiscuous_enable(port_id);
    if (ret == 0) {
        LOG_INFO("Enable receipt in promiscuous mode for Port %hu\n", port_id);
    }
    ret = rte_eth_dev_start(port_id);
    if (ret < 0) {
        LOG_ERRO("Failed to start port %hu\n", port_id);
        return ret;
    } else {
        LOG_INFO("Start port %hu successfully!\n", port_id);
    }
    assert_link_status(port_id);

    print_dev_conf(port_id);
    return 0;
}

static inline int
convert_mask_to_depth(uint32_t mask) {
    int ret = 0;
    int temp = 1;
    for (int loop = 0; loop < 32; loop++) {
        if ((temp & mask) != 0)
            ret++;
        temp = temp << 1;
    }
    return ret;
}

static inline void
add_rule(uint16_t port_id, struct rte_flow_attr* attr, struct rte_flow_item pattern[], struct rte_flow_action action[]) {
    struct rte_flow *flow = NULL;
    struct rte_flow_error error;
    int res = rte_flow_validate(port_id, attr, pattern, action, &error);
    if (!res) {
        flow = rte_flow_create(port_id, attr, pattern, action, &error);
        if (flow == NULL) {
            rte_exit(EXIT_FAILURE, "Flow can't be created %d message: %s\n", error.type, error.message ? error.message : "(no stated reason)");
        }
    } else {
        switch(res) {
            case -ENOSYS:
                rte_exit(EXIT_FAILURE, "-ENOSYS: underlying device does not support this functionality.\n");
                break;
            case -EIO:
                rte_exit(EXIT_FAILURE, "-EIO: underlying device is removed.\n");
                break;
            case -EINVAL:
                rte_exit(EXIT_FAILURE, "-EINVAL: unknown or invalid rule specification.\n");
                break;
            case -ENOTSUP:
                rte_exit(EXIT_FAILURE, "-ENOTSUP: valid but unsupported rule specification (e.g. partial bit-masks are unsupported).\n");
                break;
            case -EEXIST:
                rte_exit(EXIT_FAILURE, "-EEXIST: collision with an existing rule.\n");
                break;
            case -ENOMEM:
                rte_exit(EXIT_FAILURE, "-ENOMEM: not enough memory to execute the function.\n");
                break;
            case -EBUSY:
                rte_exit(EXIT_FAILURE, "-EBUSY: action cannot be performed due to busy device resources.\n");
                break;
            default:
                rte_exit(EXIT_FAILURE, "Flow can't be created %d message: %s\n", error.type, error.message ? error.message : "(no stated reason)");
                break;
        }

    }
}

#define MAX_PATTERN_NUM 4
static inline void
_init_flow(int index, uint16_t port_id,  uint32_t dst_ip, uint32_t mask_ip, uint16_t dst_port, uint16_t mask_port, uint16_t dst_queue) {
    /* properties of a flow rule such as its direction (ingress or egress) and priority */
    struct rte_flow_attr attr;
    /* part of a matching pattern that either matches specific packet data or traffic properties.
     * It can also describe properties of the pattern itself, such as inverted matching.
     * represented by struct rte_flow_item_xxx
     */
    struct rte_flow_item_eth  eth_spec;
    struct rte_flow_item_eth  eth_mask;
    struct rte_flow_item_ipv4 ip_spec;
    struct rte_flow_item_ipv4 ip_mask;
    struct rte_flow_item_udp  udp_spec;
    struct rte_flow_item_udp  udp_mask;
    struct rte_flow_item_tcp  tcp_spec;
    struct rte_flow_item_tcp  tcp_mask;
    /* traffic properties to look for, a combination of any number of items. */
    struct rte_flow_item pattern[MAX_PATTERN_NUM];
    /* operations to perform whenever a packet is matched by a pattern. */
    struct rte_flow_action action[MAX_PATTERN_NUM];
    /* assign packets to a specified queue rx */
    struct rte_flow_action_queue queue = { .index = dst_queue };

    memset(pattern, 0, sizeof(pattern));
    memset(action, 0, sizeof(action));
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    /* apply this rule to ingress traffic */
    attr.ingress = 1;

    /* set the onley action: Assigns packets to a given queue index*/
    action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[0].conf = &queue;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    /* skip ehternet header */
    memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
    memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = &eth_spec;
    pattern[0].mask = &eth_mask;

    /* skip ip header */
    memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
    memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
    ip_spec.hdr.dst_addr = dst_ip;
    ip_mask.hdr.dst_addr = mask_ip;
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ip_spec;
    pattern[1].mask = &ip_mask;

    // pattern[2].type = RTE_FLOW_ITEM_TYPE_END;
    // add_rule(port_id, &attr, pattern, action);

    // struct in_addr ip_src = { .s_addr = ip_spec.hdr.src_addr };
    // struct in_addr ip_dst = { .s_addr = ip_spec.hdr.dst_addr };
    // LOG_INFO(
    //     "  %02d:  %s/%d  %s/%d  0x%04x/0x%04x  0x%04x/0x%04x  TCP/UDP "CO_YELLOW"→"CO_RESET"  %2hu\n",
    //     index + 1,
    //     inet_ntoa(ip_src), convert_mask_to_depth(ip_mask.hdr.src_addr),
    //     inet_ntoa(ip_dst), convert_mask_to_depth(ip_mask.hdr.dst_addr),
    //     0, 0,
    //     0, 0,
    //     dst_queue
    // );

    /* match udp destination port */
    /*
    memset(&udp_spec, 0, sizeof(struct rte_flow_item_udp));
    memset(&udp_mask, 0, sizeof(struct rte_flow_item_udp));
    udp_spec.hdr.dst_port = dst_port;
    udp_mask.hdr.dst_port = mask_port;
    pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
    pattern[2].spec = &udp_spec;
    pattern[2].mask = &udp_mask;

    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;
    add_rule(port_id, &attr, pattern, action);
    */

    /* match tcp destination port */
    memset(&tcp_spec, 0, sizeof(struct rte_flow_item_tcp));
    memset(&tcp_mask, 0, sizeof(struct rte_flow_item_tcp));
    tcp_spec.hdr.dst_port = dst_port;
    tcp_mask.hdr.dst_port = mask_port;
    pattern[2].type = RTE_FLOW_ITEM_TYPE_TCP;
    pattern[2].spec = &tcp_spec;
    pattern[2].mask = &tcp_mask;

    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;
    add_rule(port_id, &attr, pattern, action);

    struct in_addr ip_src = { .s_addr = ip_spec.hdr.src_addr };
    struct in_addr ip_dst = { .s_addr = ip_spec.hdr.dst_addr };
    LOG_INFO("  %02d:  %s/%d  %s/%d  0x%04x/0x%04x  0x%04x/0x%04x  TCP/UDP "CO_YELLOW"→"CO_RESET"  %2hu\n",
        index + 1,
        inet_ntoa(ip_src), convert_mask_to_depth(ip_mask.hdr.src_addr),
        inet_ntoa(ip_dst), convert_mask_to_depth(ip_mask.hdr.dst_addr),
        ntohs(udp_spec.hdr.src_port), ntohs(udp_mask.hdr.src_port),
        ntohs(dst_port), ntohs(mask_port),
        dst_queue
    );
}

void 
init_flow(void) {
    struct conf_t* conf = get_conf();

    LOG_INFO("Populating Flow Director rules ...\n");
    LOG_LINE(75, '-', "FlowDirector (ingress)");
    LOG_INFO("Index  S_IP/Mask  D_IP/Mask   S_Port/Mask    D_Port/Mask    Proto    Queue\n");
    int loop_start = 0;
    for (int loop = loop_start; loop < conf->total_lcore; loop++) {
        _init_flow(loop, conf->conn[loop].port_id, htonl(0), htonl(0), conf->conn[loop].src_port, htons(0xffff), conf->conn[loop].queue_id);
    }
    LOG_LINE(75, '-', NULL);
}