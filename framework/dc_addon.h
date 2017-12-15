#ifndef _DC_ADDON_H_
#define _DC_ADDON_H_

#include <sys/queue.h>
#include <stdbool.h>

struct dc_conf_db_s;
struct dc_thread_s;
struct dc_task_s;

/*
 * Task addon
 */
struct dc_addon_s {
        const char const *name;
        bool is_global;	/* else then task */

        union {
                int (*task_init)(struct dc_conf_db_s *,
                                 struct dc_thread_s *, struct dc_task_s *);
                int (*global_init)(struct dc_conf_db_s *);
        };
        unsigned (*task_entry)(struct dc_thread_s *, struct dc_task_s *, uint64_t);

} __attribute__((aligned(64)));


struct dc_addon_constructor_s {
        const struct dc_addon_s *addon;
        TAILQ_ENTRY(dc_addon_constructor_s) entry;
};

extern void dc_addon_register(struct dc_addon_constructor_s *node);

extern void dc_conf_addon_setup(struct dc_conf_db_s *db);

extern int dc_addon_task_init(struct dc_conf_db_s *db,
                              const char *name,
                              struct dc_thread_s *th,
                              struct dc_task_s *task);

extern int dc_addon_global_init(struct dc_conf_db_s *db);

#endif	/* !_DC_ADDON_H_ */
