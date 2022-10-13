#ifndef _CORE_H_
#define _CORE_H_

#include "conf.h"
#include "list.h"

struct task_t {
    list_item_t super;
    uint64_t ID;
    char* addr;
    uint64_t len;
} __rte_cache_aligned;

struct ping_t {
    list_item_t super;
    uint64_t rtt;
};

/**
 * A structure to record the sender/receiver's state.
 */
struct conn_state_t {
    uint64_t ts;                // The timestamp of the packet is sent
    uint32_t seq;               // The packet's sequence number
    uint64_t offset;            // Offset in the buffer
    uint16_t bytes;             // This packet's length
};
struct conn_client_t {
    struct conn_state_t state[2 * MAX_WND];
    uint32_t last_sent;         // The sequence number of the last sent packet
    uint32_t last_acked;        // The sequence number of the latest acked packet
    uint32_t window;
};

/**
 * Initialize and allocate resources
 *
 * @para conf
 *   Global configuration
 */
void init_core(struct conf_t* conf);

/**
 * Free all memory referenced by the ASK
 *
 * @para conf
 *   Global configuration
 */
void exit_core(struct conf_t* conf);
/**
 * Daemon process for the master core
 *
 * @para conn
 *   A pointer to struct conn_t
 *
 * return
 *   - 0: Success
 */
int lcore_daemon(struct conn_t* conn);

/**
 * Daemon process for the slave cores
 *
 * @para arg
 *   A pointer to struct conn_t
 *
 * return
 *   - 0: Success
 */
int lcore_client(void* arg);

/**
 * Daemon process for the slave cores
 *
 * @para arg
 *   A pointer to struct conn_t
 *
 * return
 *   - 0: Success
 */
int lcore_server(void* arg);

int task_enqueue(struct task_t* todo);
// uint64_t task_dequeue(void);
struct task_t* task_dequeue(void);

#endif
