
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include "dc_conf.h"
#include "dc_port.h"
#include "dc_mbuf.h"
#include "dc_fw_log.h"
#include "dc_addon.h"
#include "dc_thread.h"

/*
 * Master:0
 * Worker:1~N
 */
static struct dc_thread_s *lcore_info[RTE_MAX_LCORE];
static struct dc_master_ctroller_s *master_controller;

#define DC_MSTER_CONTROLLER	"DC_MASTER_CONTROLLER"


static struct dc_master_ctroller_s *
create_master_ctrl(void)
{
        struct dc_master_ctroller_s *ctrl = NULL;
        const struct rte_memzone *mz;
        size_t size = sizeof(*ctrl);

        mz = rte_memzone_reserve(DC_MSTER_CONTROLLER,
                                 size,
                                 rte_socket_id(),
                                 RTE_MEMZONE_2MB | RTE_MEMZONE_1GB | RTE_MEMZONE_SIZE_HINT_ONLY);
        if (mz) {
                ctrl = mz->addr;

                memset(ctrl, 0, size);
                master_controller = ctrl;
        }
        return ctrl;
}

static struct dc_master_ctroller_s *
find_master_ctrl(void)
{
        struct dc_master_ctroller_s *ctrl = NULL;
        const struct rte_memzone *mz;

        if (master_controller)
                return master_controller;

        mz = rte_memzone_lookup(DC_MSTER_CONTROLLER);
        if (mz) {
                ctrl = mz->addr;
                master_controller = ctrl;
        }
        return ctrl;
}

static inline unsigned
task_sched(struct dc_thread_s *th)
{
        struct dc_task_s *task;
        uint64_t now = rte_rdtsc();
        uint64_t th_sub = 0;
        unsigned th_cnt = 0;

        STAILQ_FOREACH(task, &th->tasks, node) {
                unsigned ret;
                uint64_t last, sub;
#if 0
                DC_FW_ERR("th:%s tsk:%s", th->name, task->name);
#endif
                ret = task->entry(th, task, now);

                last = now;
                now = rte_rdtsc();
                sub = now - last;

                task->usage.tsc_sum += sub;
                task->usage.events += ret;
                task->usage.execs += 1;
                task->usage.update = now;

                th_sub += sub;
                th_cnt += ret;
        }

        th->usage.tsc_sum += th_sub;
        th->usage.events += th_cnt;
        th->usage.execs += 1;
        th->usage.update = now;

        return th_cnt;
}

/*
 * change slave state
 */
int
dc_thread_cmd_slaves(enum dc_thread_state_e cmd)
{
        unsigned nb_slaves = master_controller->th_info[0]->nb_slaves;

        if (rte_lcore_id() != master_controller->th_info[0]->lcore_id) {
                DC_FW_ERR("not master thread");
                return -1;
        }

        for (unsigned i = 0; i < nb_slaves; i++) {
                struct dc_thread_s *th = master_controller->th_info[i + 1];

                rte_atomic32_set(&th->cmd, cmd);
        }

        for (unsigned i = 0; i < nb_slaves; i++) {
                struct dc_thread_s *th = master_controller->th_info[i + 1];

                while (rte_atomic32_read(&th->state) != cmd)
                        rte_pause();
        }
        return 0;
}

static inline void
sleep_tick(void)
{
#if 0
        struct timespec tp = {
                .tv_sec = 0,
                .tv_nsec = 1000 * 1000,
        };

        nanosleep(&tp, NULL);
#else
        rte_pause();
#endif
}

static void
master_entry(struct dc_thread_s *th)
{
        th->start_tsc = rte_rdtsc();

        DC_FW_ERR("waked up: %s %"PRIu64, th->name, th->start_tsc);

        dc_thread_cmd_slaves(DC_THREAD_STATE_STOP);

        DC_FW_DEBUG("slaves are ready");

        dc_thread_cmd_slaves(DC_THREAD_STATE_RUNNING);
        th->start_tsc = rte_rdtsc();

        DC_FW_DEBUG("start master:%"PRIu64, th->start_tsc);
        while (1) {
                if (!task_sched(th)) {
                        /* nothing events */
                        sleep_tick();
                }
        }
}

static void
slave_entry(struct dc_thread_s *th)
{
        enum dc_thread_state_e state = rte_atomic32_read(&th->state);

        th->start_tsc = rte_rdtsc();

        DC_FW_ERR("waked up: %s %"PRIu64, th->name, th->start_tsc);

        while (1) {
                enum dc_thread_state_e cmd = rte_atomic32_read(&th->cmd);

                if (cmd != state) {
                        state = cmd;
                        rte_atomic32_set(&th->state, cmd);

                        switch (state) {
                        case DC_THREAD_STATE_STOP:
                                DC_FW_DEBUG("stop %s", th->name);
                                break;

                        case DC_THREAD_STATE_RUNNING:
                                th->start_tsc = rte_rdtsc();
                                DC_FW_DEBUG("start %s %"PRIu64, th->name, th->start_tsc);
                                break;

                        case DC_THREAD_STATE_EXIT:
                        default:
                                DC_FW_DEBUG("exit %s", th->name);
                                return;
                        }
                }

                if (state == DC_THREAD_STATE_RUNNING) {
                        if (!task_sched(th)) {
                                /* nothing events */
                                sleep_tick();
                        }
                } else {
                        rte_pause();
                }
        }
}

static int
thread_entry(void *arg __rte_unused)
{
        unsigned lcore_id = rte_lcore_id();

        if (lcore_id == rte_get_master_lcore())
                master_entry(lcore_info[lcore_id]);
        else
                slave_entry(lcore_info[lcore_id]);

        DC_FW_INFO("bye: %s", lcore_info[lcore_id]->name);
        return 0;
}

static struct dc_task_s *
create_task(struct dc_conf_db_s *db,
            struct dc_thread_s *th,
            const char *name)
{
        struct dc_task_s *task;

        DC_FW_DEBUG("creating task: %s", name);

        task = rte_zmalloc_socket(name, sizeof(*task), RTE_CACHE_LINE_SIZE,
                                  rte_lcore_to_socket_id(th->lcore_id));
        if (task) {
                const char *in_port;

                snprintf(task->name, sizeof(task->name), "%s", name);

                in_port = dc_conf_task_in_port(db, name);
                if (in_port) {
                        task->in_port = dc_port_find(&th->ports, in_port,
                                                     DC_PORT_DIR_IN);
                        if (!task->in_port) {
                                task->in_port = dc_port_in_create(db, in_port);
                                if (!task->in_port) {
                                        rte_free(task);
                                        task = NULL;
                                        goto end;
                                }
                                STAILQ_INSERT_TAIL(&th->ports, task->in_port, node);
                        }
                }

                /* create out-ports */
                char buff[512];
                int nb_ports;
                const char *ports[DC_MAX_NB_OUT_PORTS];

                nb_ports = dc_conf_task_out_port_list(db, name,
                                                      ports, RTE_DIM(ports),
                                                      buff, sizeof(buff));
                for (int i = 0; i < nb_ports; i++) {
                        task->out_ports[i] = dc_port_find(&th->ports, ports[i],
                                                          DC_PORT_DIR_OUT);
                        if (!task->out_ports[i]) {
                                task->out_ports[i] = dc_port_out_create(db, ports[i]);
                                if (!task->out_ports[i]) {
                                        rte_free(task);
                                        task = NULL;
                                        goto end;
                                }
                                STAILQ_INSERT_TAIL(&th->ports, task->out_ports[i],
                                                   node);
                        }
                }
                task->nb_out_ports = nb_ports;
                task->burst_size = DC_TASK_BURST_SIZE_DEFAULT;
                task->th = th;

                const char *addon = dc_conf_task_addon(db, name);
                if (!addon) {
                        rte_free(task);
                        task = NULL;
                        goto end;
                }
                if (dc_addon_task_init(db, addon, th, task)) {
                        DC_FW_ERR("failed init addon: %s", addon);
                        rte_free(task);
                        task = NULL;
                        goto end;
                }

                DC_FW_DEBUG("created task: %s", name);
        }
 end:
        return task;
}

static int
create_task_list(struct dc_conf_db_s *db,
                 struct dc_thread_s *th,
                 const char *th_name)
{
        char buff[256];
        const char *tasks[DC_MAX_NB_TASKS];
        int nb_tasks;

        nb_tasks = dc_conf_task_list(db, th_name,
                                     tasks, RTE_DIM(tasks),
                                     buff, sizeof(buff));
        if (nb_tasks <= 0)
                return -1;

        for (int i = 0; i < nb_tasks; i++) {
                struct dc_task_s *task;

                task = create_task(db, th, tasks[i]);
                if (task) {
                        th->nb_tasks++;
                        STAILQ_INSERT_TAIL(&th->tasks, task, node);
                } else {
                        DC_FW_ERR("failed to create task: %s", tasks[i]);
                        return -1;
                }
        }
        return 0;
}

static struct rte_mempool *
thread_mbufpool(struct dc_conf_db_s *db,
               const char *name)
{
        return dc_mbufpool(db, dc_conf_thread_mbufpool(db, name));
}

static struct dc_thread_s *
create_thread(struct dc_conf_db_s *db,
              const char *name,
              unsigned lcore_id,
              unsigned thread_id)
{
        struct dc_thread_s *th;

        DC_FW_DEBUG("creating thread: %s", name);

        th = rte_zmalloc_socket(NULL, sizeof(*th),
                                RTE_CACHE_LINE_SIZE * 2,
                                rte_socket_id());
        if (th) {
                th->mp = thread_mbufpool(db, name);
                if (th->mp == NULL) {
                        rte_free(th);
                        th = NULL;
                        goto end;
                }

                snprintf(th->name, sizeof(name), "%s", name);

                th->thread_id = thread_id;
                th->lcore_id = lcore_id;
                th->nb_slaves = 0;
                rte_atomic32_set(&th->state, DC_THREAD_STATE_INIT);
                rte_atomic32_set(&th->cmd, DC_THREAD_STATE_INIT);

                STAILQ_INIT(&th->tasks);
                STAILQ_INIT(&th->ports);
                STAILQ_INIT(&th->slaves);

                if (create_task_list(db, th, name)) {
                        rte_free(th);
                        th = NULL;
                        goto end;
                }
                DC_FW_DEBUG("created thread: %s", name);
        } else {
                DC_FW_ERR("not enough memory: %s", name);
        }
 end:
        return th;
}

/*****************************************************************************
 *
 *****************************************************************************/
struct dc_master_ctroller_s *
dc_thread_second(char *prog)
{
        char c_flag[] = "-c1";
        char n_flag[] = "-n4";
        char mp_flag[] = "--proc-type=secondary";
        char *argp[] = {
                prog,
                c_flag,
                n_flag,
                mp_flag,
        };

        optind = 1;
        if (0 > rte_eal_init(4, argp))
                return NULL;
        return find_master_ctrl();
}

static int
disp_conf(const char *db_name,
          const struct dc_conf_s *conf,
          void *arg)
{
        (void) arg;
        (void) conf;
        (void) db_name;

        DC_FW_DEBUG("%s key:%s %s\n", db_name, conf->name, conf->val);
        return 0;
}

int
dc_thread_launch(struct dc_conf_db_s *db)
{
        unsigned lcore_id;
        unsigned nb_slaves = 0;
        int ret = -1;
        struct dc_master_ctroller_s *ctrl = create_master_ctrl();

        if (!ctrl)
                goto end;

        if (dc_addon_global_init(db))
                goto end;

        /* create slaves */
        RTE_LCORE_FOREACH_SLAVE(lcore_id) {
                const char *name;
                struct dc_thread_s *th;
                unsigned thread_id;

                name = dc_conf_lcore_thread(db, lcore_id);
                if (!name)
                        goto end;

                DC_FW_DEBUG("creating lcore_id: %u", lcore_id);

                thread_id = ++nb_slaves;

                th = create_thread(db,
                                   name,
                                   lcore_id,
                                   thread_id);
                if (!th)
                        goto end;

                ctrl->th_info[thread_id] = th;
                lcore_info[lcore_id] = th;

                DC_FW_DEBUG("done lcore_id: %u", lcore_id);
        }

        /* create master */
        {
                lcore_id = rte_get_master_lcore();

                const char *name = dc_conf_lcore_thread(db, lcore_id);
                if (!name)
                        goto end;

                DC_FW_DEBUG("creating lcore_id: %u", lcore_id);

                struct dc_thread_s *th = create_thread(db,
                                                       name,
                                                       lcore_id,
                                                       0);
                if (!th)
                        goto end;

                th->nb_slaves = nb_slaves;

                for (unsigned i = 1; i <= nb_slaves; i++) {
                        ctrl->th_info[i]->nb_slaves = nb_slaves;
                        STAILQ_INSERT_TAIL(&th->slaves, ctrl->th_info[i], node);
                }

                ctrl->th_info[0] = th;
                lcore_info[lcore_id] = th;
                ctrl->nb_threads = nb_slaves + 1;

                DC_FW_DEBUG("done lcore_id: %u", lcore_id);
        }

        dc_conf_walk(db, disp_conf, NULL);
        sleep(3);

        ret = rte_eal_mp_remote_launch(thread_entry, NULL, CALL_MASTER);
        rte_eal_mp_wait_lcore();
 end:
        return ret;
}

static unsigned
get_lcore_list(struct dc_conf_db_s *db,
                unsigned *lcore_ids,
                unsigned max_lcore)
{
        struct dc_conf_node_s *node = NULL;
        unsigned nb_lcores = 0;
        char name[128];

        while ((node = dc_conf_thread_name_next(db, node,
                                                name, sizeof(name))) != NULL &&
               (nb_lcores < max_lcore)) {
                int lcore_id = dc_conf_thread_lcore(db, name);

                if (lcore_id < 0) {
                        DC_FW_ERR("invalid lcore_id : %s", name);
                        return 0;
                }

                if (dc_conf_add_lcore_thread(db, lcore_id, name))
                        return 0;

                if (dc_conf_is_master_thread(db, name)) {
                        if (dc_conf_add_master_lcore(db, lcore_id))
                                return 0;
                }

                lcore_ids[nb_lcores] = lcore_id;
                nb_lcores++;
        }
        return nb_lcores;
}

/*
 * not yet, rte initialized
 */
unsigned
dc_thread_lcores(struct dc_conf_db_s *db,
                 char *buff,
                 size_t size)
{
        unsigned lcore_ids[64];
        unsigned nb_lcores;
        unsigned s = 0;

        nb_lcores = get_lcore_list(db, lcore_ids, RTE_DIM(lcore_ids));
        for (unsigned i = 0; i < nb_lcores; i++) {
                if (s)
                        s += snprintf(&buff[s], size - s, ",%u", lcore_ids[i]);
                else
                        s += snprintf(&buff[s], size - s, "%u", lcore_ids[i]);
        }

        DC_FW_DEBUG("number of lcores:%u", nb_lcores);
        return nb_lcores;
}
