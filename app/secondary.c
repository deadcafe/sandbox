#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include <rte_ethdev.h>
#include <rte_cycles.h>

#include <dc_conf.h>
#include <dc_addon.h>
#include <dc_mbuf.h>
#include <dc_thread.h>
#include <dc_port.h>
#include <dc_fw_log.h>

enum stats_type_e {
        STATS_TYPE_THREAD,
        STATS_TYPE_TASK,
        STATS_TYPE_PORT,
        STATS_TYPE_DEV,
};

struct stats_pack_s {
        enum stats_type_e type;

        uint64_t tsc;
        union {
                struct dc_thread_s *th;
                struct dc_task_s *task;
                struct dc_port_s *port;
                uint16_t dev_id;
        } raw;

        union {
                struct dc_usage_s usage;	/* thread & task */
                union dc_port_stats_u port;
                struct rte_eth_stats eth;
        } stats[2];
};

static void
dump_thread_stats(const struct dc_thread_s *th,
                  uint64_t tsc,
                  const struct dc_usage_s *start,
                  const struct dc_usage_s *end)
{
        fprintf(stderr,
                "Thread:%s tsc:%"PRIu64" sum:%"PRIu64" event:%"PRIu64" exec:%"PRIu64"\n",
                th->name,
                tsc,
                end->tsc_sum - start->tsc_sum,
                end->events  - start->events,
                end->execs   - start->execs);
}

static void
dump_task_stats(const struct dc_task_s *task,
                uint64_t tsc,
                const struct dc_usage_s *start,
                const struct dc_usage_s *end)
{
        fprintf(stderr,
                "Task:%s tsc:%"PRIu64" sum:%"PRIu64" event:%"PRIu64" exec:%"PRIu64"\n",
                task->name,
                tsc,
                end->tsc_sum - start->tsc_sum,
                end->events  - start->events,
                end->execs   - start->execs);
}

static void
dump_port_stats(const struct dc_port_s *port,
                uint64_t tsc,
                const union dc_port_stats_u *start,
                const union dc_port_stats_u *end)
{
        union dc_port_stats_u stats;

        stats.val[0] = end->val[0] - start->val[0];
        stats.val[1] = end->val[1] - start->val[1];
        if (stats.val[0] | stats.val[1]) {
                if (port->dir == DC_PORT_DIR_IN) {
                        fprintf(stderr,
                                "Port:%s tsc:%"PRIu64" Inbound in:%"PRIu64" drop:%"PRIu64"\n",
                                port->name,
                                tsc,
                                stats.in.n_pkts_in,
                                stats.in.n_pkts_drop);
                } else {
                        fprintf(stderr,
                                "Port:%s tsc:%"PRIu64" Outbound in:%"PRIu64" drop:%"PRIu64"\n",
                                port->name,
                                tsc,
                                stats.out.n_pkts_in,
                                stats.out.n_pkts_drop);
                }
        }
}

static void
dump_dev_stats(uint16_t dev_id,
               uint64_t tsc,
               const struct rte_eth_stats *start,
               const struct rte_eth_stats *end)
{
        struct rte_eth_stats stats;

        stats.ipackets  = end->ipackets  - start->ipackets;
        stats.opackets  = end->opackets  - start->opackets;
        stats.ibytes    = end->ibytes    - start->ibytes;
        stats.obytes    = end->obytes    - start->obytes;
        stats.imissed   = end->imissed   - start->imissed;
        stats.ierrors   = end->ierrors   - start->ierrors;
        stats.oerrors   = end->oerrors   - start->oerrors;
        stats.rx_nombuf = end->rx_nombuf - start->rx_nombuf;

        if (stats.ipackets |
            stats.opackets |
            stats.ibytes |
            stats.obytes |
            stats.imissed |
            stats.ierrors |
            stats.oerrors |
            stats.rx_nombuf) {
                char name[32];

                rte_eth_dev_get_name_by_port(dev_id, name);
                fprintf(stderr,
                        "eth dev_id:%u %s tsc:%"PRIu64" ip:%"PRIu64" op:%"PRIu64" ib:%"PRIu64" ob:%"PRIu64" im:%"PRIu64" ie:%"PRIu64" oe:%"PRIu64" no:%"PRIu64"\n",
                        dev_id,
                        name,
                        tsc,
                        stats.ipackets,
                        stats.opackets,
                        stats.ibytes,
                        stats.obytes,
                        stats.imissed,
                        stats.ierrors,
                        stats.oerrors,
                        stats.rx_nombuf);
        }
}

static void
stats_dump(struct stats_pack_s *pack,
           unsigned nb)
{
        for (unsigned i = 0; i < nb; i++) {
                switch (pack[i].type) {
                case STATS_TYPE_THREAD:
                        fprintf(stderr, "\n");
                        dump_thread_stats(pack[i].raw.th,
                                          pack[i].tsc,
                                          &pack[i].stats[0].usage,
                                          &pack[i].stats[1].usage);
                        break;

                case STATS_TYPE_TASK:
                        dump_task_stats(pack[i].raw.task,
                                        pack[i].tsc,
                                        &pack[i].stats[0].usage,
                                        &pack[i].stats[1].usage);
                        break;

                case STATS_TYPE_PORT:
                        dump_port_stats(pack[i].raw.port,
                                        pack[i].tsc,
                                        &pack[i].stats[0].port,
                                        &pack[i].stats[1].port);
                        break;

                case STATS_TYPE_DEV:
                        dump_dev_stats(pack[i].raw.dev_id,
                                       pack[i].tsc,
                                       &pack[i].stats[0].eth,
                                       &pack[i].stats[1].eth);
                        break;

                default:
                        break;
                }
        }
}

static void
stats_begin(struct stats_pack_s *pack,
            unsigned nb)
{
        for (unsigned i = 0; i < nb; i++) {
                pack[i].tsc = rte_rdtsc();

                switch (pack[i].type) {
                case STATS_TYPE_THREAD:
                        rte_memcpy(&pack[i].stats[0].usage,
                                   &pack[i].raw.th->usage,
                                   sizeof(pack[i].stats[0].usage));
                        break;

                case STATS_TYPE_TASK:
                        rte_memcpy(&pack[i].stats[0].usage,
                                   &pack[i].raw.task->usage,
                                   sizeof(pack[i].stats[0].usage));
                        break;

                case STATS_TYPE_PORT:
                        dc_port_stats(pack[i].raw.port,
                                      &pack[i].stats[0].port);
                        break;

                case STATS_TYPE_DEV:
                        rte_eth_stats_get(pack[i].raw.dev_id,
                                          &pack[i].stats[0].eth);
                        break;

                default:
                        break;
                }
        }
}

static void
stats_stop(struct stats_pack_s *pack,
           unsigned nb)
{
        for (unsigned i = 0; i < nb; i++) {
                pack[i].tsc = rte_rdtsc() - pack[i].tsc;

                switch (pack[i].type) {
                case STATS_TYPE_THREAD:
                        rte_memcpy(&pack[i].stats[1].usage,
                                   &pack[i].raw.th->usage,
                                   sizeof(pack[i].stats[0].usage));
                        break;

                case STATS_TYPE_TASK:
                        rte_memcpy(&pack[i].stats[1].usage,
                                   &pack[i].raw.task->usage,
                                   sizeof(pack[i].stats[0].usage));
                        break;

                case STATS_TYPE_PORT:
                        dc_port_stats(pack[i].raw.port,
                                      &pack[i].stats[1].port);
                        break;

                case STATS_TYPE_DEV:
                        rte_eth_stats_get(pack[i].raw.dev_id,
                                          &pack[i].stats[1].eth);
                        break;

                default:
                        break;
                }
        }
}

static void
stats_setup(struct dc_master_ctroller_s *ctrl,
            struct stats_pack_s *pack)
{
        unsigned i;
        unsigned nb_th = ctrl->nb_threads;
        unsigned cnt_th = 0, cnt_task = 0, cnt_port = 0, cnt_dev = 0;

        for (i = 0; i < nb_th; i++) {
                struct dc_thread_s *th = ctrl->th_info[i];

                pack->type = STATS_TYPE_THREAD;
                pack->raw.th = th;
                pack++;
                cnt_th++;

#if 0
                fprintf(stderr, "%s th:%u task:%u port:%u dev:%u\n",
                        __func__, cnt_th, cnt_task, cnt_port, cnt_dev);
#endif

                struct dc_task_s *task;
                STAILQ_FOREACH(task, &th->tasks, node) {
                        pack->type = STATS_TYPE_TASK;
                        pack->raw.task = task;
                        pack++;
                        cnt_task++;

#if 0
                        fprintf(stderr, "%s th:%u task:%u port:%u dev:%u\n",
                                __func__, cnt_th, cnt_task, cnt_port, cnt_dev);
#endif
               }

                struct dc_port_s *port;
                STAILQ_FOREACH(port, &th->ports, node) {
                        pack->type = STATS_TYPE_PORT;
                        pack->raw.port = port;
                        pack++;
                        cnt_port++;

#if 0
                        fprintf(stderr, "%s th:%u task:%u port:%u dev:%u\n",
                                __func__, cnt_th, cnt_task, cnt_port, cnt_dev);
#endif
                }
        }

        uint16_t dev_id;
        RTE_ETH_FOREACH_DEV(dev_id) {
                pack->type = STATS_TYPE_DEV;
                pack->raw.dev_id = dev_id;
                pack++;
                cnt_dev++;

#if 0
                fprintf(stderr, "%s th:%u task:%u port:%u dev:%u\n",
                        __func__, cnt_th, cnt_task, cnt_port, cnt_dev);
#endif
        }
}

static unsigned
get_nb_stats(struct dc_master_ctroller_s *ctrl)
{
        unsigned nb_th = ctrl->nb_threads;
        unsigned nb_task = 0;
        unsigned nb_port = 0;
        unsigned nb_pmd = rte_eth_dev_count();

        for (unsigned i = 0; i < nb_th; i++) {
                struct dc_thread_s *th = ctrl->th_info[i];

                nb_port += dc_port_count(&th->ports);
                nb_task += th->nb_tasks;
        }

        fprintf(stderr, "%s thread:%u task:%u port:%u dev:%u\n",
                __func__, nb_th, nb_task, nb_port, nb_pmd);

        return nb_th + nb_task + nb_port + nb_pmd;
}

static void
stats_all(struct dc_master_ctroller_s *ctrl,
          unsigned nb_stats,
          unsigned sec)
{
        struct stats_pack_s pack[nb_stats];

        fprintf(stderr, "number of stats:%u\n", nb_stats);

        if (nb_stats) {
                stats_setup(ctrl, pack);
                stats_begin(pack, nb_stats);

                sleep(sec);

                stats_stop(pack, nb_stats);
                stats_dump(pack, nb_stats);
        }
}

int
secondary_main(char *prog)
{
        struct dc_master_ctroller_s *ctrl = dc_thread_second(prog);

        if (!ctrl)
                return -1;

        stats_all(ctrl, get_nb_stats(ctrl), 1);
#if 0
        fprintf(stderr, "HZ:%"PRIu64"\n", rte_get_tsc_hz());
#endif
        return 0;
}

