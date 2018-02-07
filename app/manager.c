#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "engine.h"

extern int secondary_main(char *prog);

static int
manager_main(void)
{
        fprintf(stderr, "start manager\n");
        while (1) {
                /* nothing to do */
                sleep(1);
        }
        return 0;
}

static void
usage(const char *prog)
{
        fprintf(stderr, "%s [-f FILE] [-2]\n", prog);
}

int
main(int ac,
     char *av[])
{
        struct engine_config_s conf;
        int opt, ret;

        memset(&conf, 0, sizeof(conf));
        conf.fname = "sample.conf";

        while ((opt = getopt(ac, av, "f:2")) != -1) {
                switch (opt) {
                case 'f':
                        conf.fname = optarg;
                        break;

                case '2':
                        return secondary_main(av[0]);

                default:
                        usage(av[0]);
                        return ret;
                }
        }

        ret = start_engine(&conf);
        if (!ret)
                ret = manager_main();

        return ret;
}
