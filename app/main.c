#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include <dc_conf.h>
#include <dc_addon.h>
#include <dc_mbuf.h>
#include <dc_thread.h>
#include <dc_port.h>
#include <dc_fw_log.h>


extern int secondary_main(char *prog);

void
hogehoge(void)
{
        fprintf(stderr, "%s\n", __func__);
}

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

static void
usage(const char *prog)
{
        fprintf(stderr, "%s [-f FILE]\n", prog);
}

int
main(int ac,
     char *av[])
{
        int opt;
        const char *fname = "sample.conf";
        int ret = -1;
        bool is_secondary = false;

        for (int i = 0; i < ac; i++)
                fprintf(stderr, "av[%d]:%s\n", i, av[i]);

        while ((opt = getopt(ac, av, "f:2")) != -1) {
                switch (opt) {
                case 'f':
                        fname = optarg;
                        break;

                case '2':
                        is_secondary = true;
                        break;

                default:
                        usage(av[0]);
                        return ret;
                }
        }

        if (!fname) {
                usage(av[0]);
                return ret;
        };

        if (is_secondary)
                return secondary_main(av[0]);

        struct dc_conf_db_s *db;

        if ((db = dc_conf_file_open(fname)) != NULL) {
                dc_conf_addon_setup(db);

                if (dc_conf_init_rte(db, av[0]) < 0) {
                        fprintf(stderr, "failed to initialize RTE\n");
                        return -1;
                }

                dc_conf_walk(db, disp_conf, NULL);

                ret = dc_thread_launch(db);
                dc_conf_delete_all(db);

                ret = 0;
        }
        return ret;
}
