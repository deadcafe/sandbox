
#include <sys/queue.h>

#include <stdio.h>

#include "dc_conf.h"
#include "dc_thread.h"
#include "dc_fw_log.h"
#include "dc_addon.h"

/*
 *
 */
TAILQ_HEAD(dc_addon_constructor_head_s, dc_addon_constructor_s);

static struct dc_addon_constructor_head_s constructor_head =
        TAILQ_HEAD_INITIALIZER(constructor_head);

void
dc_addon_register(struct dc_addon_constructor_s *node)
{
        TAILQ_INSERT_TAIL(&constructor_head, node, entry);
}

/*
 *
 */
void
dc_conf_addon_setup(struct dc_conf_db_s *db)
{
        struct dc_addon_constructor_s *node;

        TAILQ_FOREACH(node, &constructor_head, entry) {
                dc_conf_add_addon(db, node->addon->name, node->addon);
        }
}

int
dc_addon_task_init(struct dc_conf_db_s *db,
                   const char *name,
                   struct dc_thread_s *th,
                   struct dc_task_s *task)
{
        const struct dc_addon_s *addon;

        addon = (const struct dc_addon_s *) dc_conf_addon(db, name);
        if (!addon || addon->is_global)
                return -1;

        task->entry = addon->task_entry;
        return addon->task_init(db, th, task);
}

/*
 *
 */
int
dc_addon_global_init(struct dc_conf_db_s *db)
{
        const char *entries[16];
        char buff[1024];
        int nb;
        int ret = 0;

        nb = dc_conf_global_initializer_list(db, entries, 16, buff, sizeof(buff));
        for (int i = 0; i < nb; i++) {
                const struct dc_addon_s *addon;

                addon = (const struct dc_addon_s *) dc_conf_addon(db, entries[i]);
                if (!addon || !addon->is_global)
                        return -1;

                ret = addon->global_init(db);
                if (ret) {
                        DC_FW_ERR("failed in global initializer:%s", entries[i]);
                        break;
                }
        }
        return ret;
}
