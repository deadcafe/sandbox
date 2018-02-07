
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include <dc_conf.h>
#include <dc_addon.h>
#include <dc_mbuf.h>
#include <dc_thread.h>
#include <dc_port.h>
#include <dc_fw_log.h>

#include "engine.h"

static int
disp_conf(const char *db_name,
          const struct dc_conf_s *conf,
          void *arg)
{
        (void) arg;
        (void) conf;
        (void) db_name;

#if 1
        fprintf(stderr, "%s key:%s %s\n", db_name, conf->name, conf->val);
#endif
        return 0;
}

static int
engine_main(const char *prog,
            const char *fname,
            void (*ringer_func)(void))
{
        struct dc_conf_db_s *db;
        int ret = -1;

        if ((db = dc_conf_file_open(fname)) != NULL) {
                dc_conf_addon_setup(db);

                if (dc_conf_init_rte(db, prog) < 0) {
                        fprintf(stderr, "failed to initialize RTE\n");
                        return -1;
                }

                dc_conf_walk(db, disp_conf, NULL);

                ret = dc_thread_launch(db, ringer_func);
                dc_conf_delete_all(db);
        }
        return ret;
}

static pthread_barrier_t barrier;

static void
wait_for_engine(void)
{
        pthread_barrier_wait(&barrier);
}

static void *
engine_entry(void *arg)
{
        struct engine_config_s *config = arg;

        engine_main(config->prog, config->fname, wait_for_engine);
        return arg;
}

int
start_engine(struct engine_config_s *config)
{
        pthread_t th;
        int ret;

        pthread_barrier_init(&barrier, NULL, 2);
        ret = pthread_create(&th, NULL, engine_entry, config);
        if (!ret)
                wait_for_engine();
        pthread_barrier_destroy(&barrier);
        return ret;
}
