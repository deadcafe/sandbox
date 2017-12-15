#ifndef _DC_THREAD_H_
#define _DC_THREAD_H_

#include <sys/queue.h>
#include <stdint.h>

#include <rte_ring.h>
#include <rte_mbuf.h>

#include "dc_port.h"

enum dc_master_cmd_e {
        DC_MASTER_CMD_NONE = 0,
        DC_MASTER_CMD_START_USAGE,
        DC_MASTER_CMD_STOP_USAGE,

        DC_MASTER_CMD_EXIT,
        DC_MASTER_CMD_NB,
};

enum dc_master_rsp_e {
        DC_MASTER_RSP_OK = 0,
        DC_MASTER_RSP_NG,

};

struct dc_usage_s {
        uint64_t tsc_sum;
        uint64_t events;
        uint64_t execs;
        uint64_t update;
};

union dc_master_cmd_s {
        uint64_t val;
        struct {
                uint32_t cookie;
                uint32_t req_rsp;
        };
};

struct dc_thread_s;

struct dc_master_ctroller_s {
        /*************************************
         * Request Area
         *************************************/
        MARKER cacheline0;

        rte_atomic64_t cmd;	/* union dc_master_cmd_s */
        uint64_t reserved0;

        uint8_t data[48];

        /*************************************
         * Response Area
         *************************************/
        MARKER cacheline1 __rte_cache_min_aligned;

        rte_atomic64_t result;
        unsigned nb_threads;
        unsigned reserved1;

        MARKER cacheline2 __rte_cache_min_aligned;
        struct dc_thread_s *th_info[RTE_MAX_LCORE];
};

enum dc_thread_state_e {
        DC_THREAD_STATE_INIT = -1,
        DC_THREAD_STATE_STOP,
        DC_THREAD_STATE_RUNNING,

        DC_THREAD_STATE_EXIT,
};

extern struct dc_master_ctroller_s *dc_thread_second(char *prog);


struct dc_thread_s;

#define DC_TASK_BURST_SIZE_DEFAULT	64

#define DC_MAX_NB_OUT_PORTS	64
struct dc_task_s {
        MARKER cacheline0;

        char name[32];
        STAILQ_ENTRY(dc_task_s) node;
        struct dc_thread_s *th;

        MARKER cacheline1 __rte_cache_min_aligned;

        struct dc_usage_s usage;

        void *addon_task_ext[4];
        unsigned (*entry)(struct dc_thread_s *, struct dc_task_s *, uint64_t);
        unsigned nb_out_ports;
        unsigned burst_size;

        struct dc_port_s *in_port;
        struct dc_port_s *out_ports[DC_MAX_NB_OUT_PORTS];
} __rte_cache_aligned;

#define DC_MAX_NB_TASKS		8

STAILQ_HEAD(dc_task_head_s, dc_task_s);

struct dc_thread_s;
STAILQ_HEAD(dc_thread_head_s, dc_thread_s);

struct dc_thread_s {
        MARKER cacheline0;

        char name[32];

        STAILQ_ENTRY(dc_thread_s) node;

        /* request from Master */
        rte_atomic32_t cmd;

	unsigned nb_slaves;
        unsigned thread_id;
        unsigned lcore_id;
        unsigned nb_tasks;

        uint64_t start_tsc;

        MARKER cacheline1 __rte_cache_min_aligned;

	/* changed by Slave */
        rte_atomic32_t state;

        struct dc_usage_s usage;

        struct rte_mempool *mp;
        void *addon_thread_ext;

        struct dc_task_head_s tasks;
        struct dc_port_head_s ports;
        struct dc_thread_head_s slaves;	/* master only */
} __rte_cache_aligned;


struct dc_conf_db_s;

extern int dc_thread_launch(struct dc_conf_db_s *db);
extern unsigned dc_thread_lcores(struct dc_conf_db_s *db,
                                 char *buff,
                                 size_t size);

extern int dc_thread_cmd_slaves(enum dc_thread_state_e cmd);

#endif /* !_DC_THREAD_H_ */
