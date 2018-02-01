#ifndef _DC_CONF_H_
#define _DC_CONF_H_

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <rte_ether.h>

#include "dc_fw_log.h"

#define DC_CONF_STRING_MAX	256

struct dc_conf_db_s;

struct dc_conf_s {
        const char *name;
        const char *val;
};

/*
 *
 */
extern int dc_conf_init_rte(struct dc_conf_db_s *db,
                            const char *prog);

/*
 * prototypes
 * names format:
 *   must be NULL terminated
 */
extern struct dc_conf_db_s *dc_conf_create(const char *db_name);
extern void dc_conf_destroy(struct dc_conf_db_s *db);

extern const struct dc_conf_s *dc_conf_find(struct dc_conf_db_s *db,
                                            const char *key);
extern const char *dc_conf_find_val(struct dc_conf_db_s *db,
                                    const char *key);
extern const struct dc_conf_s *dc_conf_nfind(struct dc_conf_db_s *db,
                                             const char *key);
extern const struct dc_conf_s *dc_conf_add(struct dc_conf_db_s *db,
                                           const char *key,
                                           const char *val);
extern const struct dc_conf_s *dc_conf_update(struct dc_conf_db_s *db,
                                              const char *key,
                                              const char *val);
extern unsigned dc_conf_list(struct dc_conf_db_s *db,
                             const char *key,
                             char *list,
                             unsigned size);

extern void dc_conf_delete(struct dc_conf_db_s *db,
                           const char *key);

extern void dc_conf_delete_all(struct dc_conf_db_s *db);

extern int dc_conf_walk(struct dc_conf_db_s *db,
                        int (*cb)(const char *db_name,
                                  const struct dc_conf_s *conf,
                                  void *arg),
                        void *arg);
extern struct dc_conf_db_s *dc_conf_file_open(const char *path);
extern unsigned dc_conf_thread(struct dc_conf_db_s *db,
                               char *buff,
                               size_t size);

extern int dc_init_rte(struct dc_conf_db_s *db,
                       const char *prog);

/*
 * thread op
 */
extern int dc_conf_thread_lcore(struct dc_conf_db_s *db,
                                const char *name);
extern int dc_conf_add_lcore_thread(struct dc_conf_db_s *db,
                                    unsigned lcore_id,
                                    const char *name);
extern bool dc_conf_is_master_thread(struct dc_conf_db_s *db,
                                     const char *name);

extern int dc_conf_add_master_lcore(struct dc_conf_db_s *db,
                                    unsigned lcore_id);
extern int dc_conf_master_lcore(struct dc_conf_db_s *db);

extern struct dc_conf_node_s *
dc_conf_thread_name_next(struct dc_conf_db_s *db,
                         struct dc_conf_node_s *node,
                         char *buff,
                         size_t buff_size);

extern const char *dc_conf_lcore_thread(struct dc_conf_db_s *db,
                                        unsigned lcore_id);

extern const char *dc_conf_thread_mbufpool(struct dc_conf_db_s *db,
                                           const char *name);

extern const char *dc_conf_task_addon(struct dc_conf_db_s *db,
                                      const char *name);

extern const char *dc_conf_task_in_port(struct dc_conf_db_s *db,
                                        const char *name);

extern int dc_conf_task_out_port_list(struct dc_conf_db_s *db,
                                      const char *name,
                                      const char **ports,
                                      unsigned max_ports,
                                      char *buff,
                                      size_t buff_size);

extern int dc_conf_ring_size(struct dc_conf_db_s *db,
                             const char *name);

extern int dc_conf_task_list(struct dc_conf_db_s *db,
                             const char *th_name,
                             const char **tasks,
                             unsigned max_tasks,
                             char *buff,
                             size_t buff_size);

/*
 * netdev
 */
extern const char *dc_conf_netdev_type(struct dc_conf_db_s *db,
                                       const char *name);

extern const char *dc_conf_netdev_id_name(struct dc_conf_db_s *db,
                                          uint16_t id);

extern int dc_conf_netdev_name_id(struct dc_conf_db_s *db,
                                  const char *name);

extern int dc_conf_add_netdev_id_name(struct dc_conf_db_s *db,
                                      uint16_t id,
                                      const char *name);

extern int dc_conf_add_netdev_name_id(struct dc_conf_db_s *db,
                                      const char *name,
                                      uint16_t id,
                                      bool with_name);
extern int dc_conf_add_netdev_name_type(struct dc_conf_db_s *db,
                                        const char *name,
                                        const char *type);
extern unsigned dc_conf_netdev_nb_rx_queues(struct dc_conf_db_s *db,
                                            const char *name);

extern int dc_conf_netdev_nb_tx_queues(struct dc_conf_db_s *db,
                                       const char *name);

extern const char *dc_conf_netdev_mbufpool(struct dc_conf_db_s *db,
                                           const char *name);

extern int dc_conf_netdev_mac(struct dc_conf_db_s *db,
                              const char *name,
                              struct ether_addr *addr);

extern int dc_conf_add_netdev_mac(struct dc_conf_db_s *db,
                                  const char *name,
                                  const struct ether_addr *addr);

extern const char *dc_conf_bonding_mode(struct dc_conf_db_s *db,
                                        const char *name);
extern int dc_conf_bondig_interval(struct dc_conf_db_s *db,
                                   const char *name);
extern int dc_conf_bondig_downdelay(struct dc_conf_db_s *db,
                                    const char *name);
extern int dc_conf_bondig_updelay(struct dc_conf_db_s *db,
                                  const char *name);
extern int dc_conf_bonding_slave_list(struct dc_conf_db_s *db,
                                      const char *name,
                                      const char **slaves,
                                      unsigned max_slaves,
                                      char *buff,
                                      size_t buff_size);

extern const char *dc_conf_netdev_depend(struct dc_conf_db_s *db,
                                         const char *name);

extern int dc_conf_port_rx_queue(struct dc_conf_db_s *db,
                                 const char *name);
extern int dc_conf_port_tx_queue(struct dc_conf_db_s *db,
                                 const char *name);
extern int dc_conf_port_retry(struct dc_conf_db_s *db,
                              const char *name);
extern const char *dc_conf_port_depend(struct dc_conf_db_s *db,
                                       const char *name);

/*
 * addon
 */
extern int dc_conf_add_addon(struct dc_conf_db_s *db,
                             const char *name,
                             const void *p);
extern const void *dc_conf_addon(struct dc_conf_db_s *db,
                                 const char *name);

/*
 * mbuf
 */
extern int dc_conf_mbufpool_size(struct dc_conf_db_s *db,
                                 const char *name);
extern int dc_conf_mbufpool_cache_size(struct dc_conf_db_s *db,
                                       const char *name);
extern int dc_conf_mbufpool_ext_size(struct dc_conf_db_s *db,
                                     const char *name);

extern int dc_conf_global_initializer_list(struct dc_conf_db_s *db,
                                           const char **entries,
                                           unsigned max_entries,
                                           char *buff,
                                           size_t buff_size);

/*
 * conf tools
 */
static inline int
dc_conf_get_integer(struct dc_conf_db_s *db,
                    const char const *fmt,
                    ...)
{
        char key[128];
        int ret = -1;
        const char *v;
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        v = dc_conf_find_val(db, key);
        if (v) {
                errno = 0;
                ret = strtol(v, NULL, 10);
                if (errno) {
                        DC_FW_ERR("invalid key:%s value:%s", key, v);
                        ret = -1;
                }
       } else {
                DC_FW_ERR("nothing %s", key);
        }
        return ret;
}

/*
 *
 */
static inline const void *
dc_conf_get_pointer(struct dc_conf_db_s *db,
                    const char const *fmt,
                    ...)
{
        char key[128];
        const char *v;
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        v = dc_conf_find_val(db, key);
        if (v) {
                uintptr_t p;

                errno = 0;
                p = strtoull(v, NULL, 16);
                if (errno) {
                        DC_FW_ERR("invalid key:%s value:%s", key, v);
                        return NULL;
                }
                return (const void *) p;
        }
        return NULL;
}

/*
 *
 */
static inline inline bool
dc_conf_get_boolean(struct dc_conf_db_s *db,
                    const char const *fmt,
                    ...)
{
        char key[128];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        return (dc_conf_find(db, key) != NULL);
}

/*
 *
 */
static inline const char *
dc_conf_get_string(struct dc_conf_db_s *db,
                   const char const *fmt,
                   ...)
{
        char key[128];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        return dc_conf_find_val(db, key);
}

/*
 *
 */
static inline int
dc_conf_get_string_list(struct dc_conf_db_s *db,
                        const char **list,
                        size_t list_size,
                        char *buff,
                        size_t buff_size,
                        const char const *fmt,
                        ...)
{
        char key[128];
        unsigned nb;
        const char *p = buff;
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        nb = dc_conf_list(db, key, buff, buff_size);
        if (list_size < nb) {
                DC_FW_ERR("too many lists key:%s nb:%u", key, nb);
                return -1;
        }
        for (unsigned i = 0; i < nb; i++) {
                list[i] = p;
                p += (strlen(p) + 1);
        }
        return (int) nb;
}

/*
 *
 */
static inline int
dc_conf_get_integer_list(struct dc_conf_db_s *db,
                         int *list,
                         size_t list_size,
                         char *buff,
                         size_t buff_size,
                         const char const *fmt,
                         ...)
{
        char key[128];
        unsigned nb;
        const char *p = buff;
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        nb = dc_conf_list(db, key, buff, buff_size);
        if (list_size < nb) {
                DC_FW_ERR("too many lists key:%s nb:%u", key, nb);
                return -1;
        }
        for (unsigned i = 0; i < nb; i++) {
                char *end_p;

                errno = 0;
                list[i] = strtol(p, &end_p, 10);
                if (errno || *end_p != '\0') {
                        DC_FW_ERR("invalid key:%s value:%s", key, p);
                        return -1;
                }
                p = ++end_p;
        }
        return (int) nb;
}

/*
 *
 */
static inline int
dc_conf_add_integer(struct dc_conf_db_s *db,
                    int val,
                    const char const *fmt,
                    ...)
{
        char key[128];
        char str[32];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        snprintf(str, sizeof(str), "%d", val);
        if (dc_conf_add(db, key, str))
                return 0;
        return -1;
}

/*
 *
 */
static inline int
dc_conf_add_string(struct dc_conf_db_s *db,
                   const char *str,
                   const char const *fmt,
                   ...)
{
        char key[128];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        if (dc_conf_add(db, key, str))
                return 0;
        return -1;
}

/*
 *
 */
static inline int
dc_conf_add_pointer(struct dc_conf_db_s *db,
                    const void *p,
                    const char const *fmt,
                    ...)
{
        char str[64];
        char key[128];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        snprintf(str, sizeof(str), "%p", p);
        if (dc_conf_add(db, key, str))
                return 0;
        return -1;
}

/*
 *
 */
static inline int
dc_conf_add_boolean(struct dc_conf_db_s *db,
                    const char const *fmt,
                    ...)
{
        char key[128];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        if (dc_conf_add(db, key, NULL))
                return 0;
        return -1;
}

/*
 *
 */
static inline int
dc_conf_apend_string_list(struct dc_conf_db_s *db,
                          const char *str,
                          const char const *fmt,
                          ...)
{
        char buf[256];
        char key[128];
        const char *v;
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        v = dc_conf_find_val(db, key);
        if (v) {
                snprintf(buf, sizeof(buf), "%s,%s", v, str);
                str = buf;
        }

        if (dc_conf_update(db, key, str))
                return 0;
        return -1;
}

/*
 *
 */
static inline int
dc_conf_apend_integer_list(struct dc_conf_db_s *db,
                           int val,
                           const char const *fmt,
                           ...)
{
        char buf[256];
        char key[128];
        const char *v;
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(key, sizeof(key), fmt, ap);
        va_end(ap);

        v = dc_conf_find_val(db, key);
        if (v)
                snprintf(buf, sizeof(buf), "%s,%d", v, val);
        else
                snprintf(buf, sizeof(buf), "%d", val);

        if (dc_conf_update(db, key, buf))
                return 0;
        return -1;
}

#endif /* !_DC_CONF_H_ */
