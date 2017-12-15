#include <stdio.h>
#include <inttypes.h>

#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>

#include <dc_fw_log.h>
#include <dc_thread.h>
#include <dc_port.h>
#include <dc_addon.h>

/*****************************************************************************
 *	Master recv task
 *****************************************************************************/

struct worker_selector_s {
        uint64_t rr_cnt;
};


/*
 * from KNI
 */
static unsigned
KernelRecv_entry(struct dc_thread_s *th,
                 struct dc_task_s *task,
                 uint64_t tsc)
{
#if 1
        struct rte_mbuf *buff[task->burst_size];
        int nb;

        nb = dc_port_recv(task->in_port, buff, RTE_DIM(buff));
        for (int i = 0; i < nb; i++)
                dc_port_send(task->out_ports[0], buff[i]);
#endif
        return nb;
}

static int
KernelRecv_init(struct dc_conf_db_s *db,
                struct dc_thread_s *th,
                struct dc_task_s *task)
{
        return 0;
}

static const struct dc_addon_s KernelRecv_addon = {
        .name = "KernelRecv",
        .task_init =  KernelRecv_init,
        .task_entry = KernelRecv_entry,
};

static struct dc_addon_constructor_s KernelRecv_constructor = {
        .addon = &KernelRecv_addon,
};

/*****************************************************************************
 *	Master exec task
 *****************************************************************************/
static unsigned
KernelSend_entry(struct dc_thread_s *th,
                 struct dc_task_s *task,
                 uint64_t tsc)
{
#if 1
        struct rte_mbuf *buff[task->burst_size];
        int nb;

        nb = dc_port_recv(task->in_port, buff, RTE_DIM(buff));
        for (int i = 0; i < nb; i++)
                dc_port_send(task->out_ports[0], buff[i]);
#endif
        return nb;
}

static int
KernelSend_init(struct dc_conf_db_s *db,
                struct dc_thread_s *th,
                struct dc_task_s *task)
{
        return 0;
}

static const struct dc_addon_s KernelSend_addon = {
        .name = "KernelSend",
        .task_init =  KernelSend_init,
        .task_entry = KernelSend_entry,
};

static struct dc_addon_constructor_s KernelSend_constructor = {
        .addon = &KernelSend_addon,
};

/*****************************************************************************
 *	Master polling netdev task
 *****************************************************************************/
struct eth_stats_s {
        uint16_t port_id;
        uint8_t reserved[6];
        char name[32];
        struct rte_eth_link link;
};

struct netdev_stats_s {
        unsigned nb_ports;
        unsigned reserved;

        uint64_t next;
        struct eth_stats_s ports[0];
};

static void
dump_link(const char *name,
          uint16_t id,
          const struct rte_eth_link *link)
{
        fprintf(stdout, "ethdev %u %s link\n", id, name);

        fprintf(stdout, "  speed:%"PRIu32"\n", link->link_speed);
        fprintf(stdout, "  duplex:%"PRIu16"\n", link->link_duplex);
        fprintf(stdout, "  autoneg:%"PRIu16"\n", link->link_autoneg);
        fprintf(stdout, "  status:%"PRIu16"\n", link->link_status);
}

static unsigned
DevicePoll_entry(struct dc_thread_s *th,
                 struct dc_task_s *task,
                 uint64_t tsc)
{
        struct netdev_stats_s *stats = task->addon_task_ext[0];
        uint64_t now = rte_rdtsc();

        if (stats->next > now)
                return 0;

        stats->next = now + (rte_get_tsc_hz() * 5);

#if 1
        for (unsigned i = 0; i < stats->nb_ports; i++) {
                rte_eth_link_get(stats->ports[i].port_id,
                                 &stats->ports[i].link);
        }

        for (unsigned i = 0; i < stats->nb_ports; i++) {
                if (!strncmp(stats->ports[i].name,
                             "net_null_",
                             strlen("net_null_")))
                        continue;

                dump_link(stats->ports[i].name,
                          stats->ports[i].port_id,
                          &stats->ports[i].link);
        }
#endif

        return stats->nb_ports;
}

static int
DevicePoll_init(struct dc_conf_db_s *db,
                struct dc_thread_s *th,
                struct dc_task_s *task)
{
        unsigned nb = rte_eth_dev_count();

        struct netdev_stats_s *stats =
                rte_zmalloc_socket("",
                                   sizeof(*stats) + (sizeof(struct eth_stats_s) * nb),
                                   RTE_CACHE_LINE_SIZE,
                                   rte_lcore_to_socket_id(th->lcore_id));
        if (!stats)
                return -1;

        stats->nb_ports = nb;
        unsigned port_id, i = 0;
        RTE_ETH_FOREACH_DEV(port_id) {
                stats->ports[i].port_id = port_id;
                rte_eth_dev_get_name_by_port(port_id, stats->ports[i].name);
                i++;
        }

        task->addon_task_ext[0] = stats;
        return 0;
}

static const struct dc_addon_s DevicePoll_addon = {
        .name = "DevicePoll",
        .task_init =  DevicePoll_init,
        .task_entry = DevicePoll_entry,
};

static struct dc_addon_constructor_s DevicePoll_constructor = {
        .addon = &DevicePoll_addon,
};

/*****************************************************************************
 *	constructor
 *****************************************************************************/
RTE_INIT(master_addon_reg);
static void
master_addon_reg(void)
{
        dc_addon_register(&KernelRecv_constructor);
        dc_addon_register(&KernelSend_constructor);
        dc_addon_register(&DevicePoll_constructor);
}
