#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <sys/tree.h>
//#include <netinet/ether.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ether.h>

#include "dc_mbuf.h"
#include "dc_thread.h"
#include "dc_fw_log.h"
#include "dc_conf.h"

#if 1	/* RedBlack */
# define TREE_INIT(x)		RB_INIT(x)
# define TREE_ENTRY(x)		RB_ENTRY(x)
# define TREE_HEAD(x,y)		RB_HEAD(x,y)
# define TREE_GENERATE(x,y,z,c)	RB_GENERATE_STATIC(x,y,z,c)
# define TREE_FIND(x,y,z)	RB_FIND(x,y,z)
# define TREE_NFIND(x,y,z)	RB_NFIND(x,y,z)
# define TREE_INSERT(x,y,z)	RB_INSERT(x,y,z)
# define TREE_REMOVE(x,y,z)	RB_REMOVE(x,y,z)
# define TREE_ROOT(x)		RB_ROOT(x)
# define TREE_FOREACH(x,y,z)	RB_FOREACH(x,y,z)
# define TREE_NEXT(x,y,z)	RB_NEXT(x,y,z)
#else	/* Splay */
# define TREE_INIT(x)		SPLAY_INIT(x)
# define TREE_ENTRY(x)		SPLAY_ENTRY(x)
# define TREE_HEAD(x,y)		SPLAY_HEAD(x,y)
# define TREE_GENERATE(x,y,z,c)	SPLAY_GENERATE_STATIC(x,y,z,c)
# define TREE_FIND(x,y,z)	SPLAY_FIND(x,y,z)
# define TREE_NFIND(x,y,z)	SPLAY_NFIND(x,y,z)	/* XXX: nothing */
# define TREE_INSERT(x,y,z)	SPLAY_INSERT(x,y,z)
# define TREE_REMOVE(x,y,z)	SPLAY_REMOVE(x,y,z)
# define TREE_ROOT(x)		SPLAY_ROOT(x)
# define TREE_FOREACH(x,y,z)	SPLAY_FOREACH(x,y,z)
#endif

#ifndef RTE_MAX_LCORE
# define RTE_MAX_LCORE		128
#endif

#ifndef RTE_MAX_ETHPORTS
# define RTE_MAX_ETHPORTS	32
#endif

/*
 *
 */
struct dc_conf_node_s {
        struct dc_conf_s conf;

        TREE_ENTRY(dc_conf_node_s) entry;
        char buff[0];
};

TREE_HEAD(dc_conf_head_s, dc_conf_node_s);

struct dc_conf_db_s {
        char name[DC_CONF_STRING_MAX];
        struct dc_conf_head_s head;
};

static inline int
cmp_conf(const struct dc_conf_node_s *node0,
         const struct dc_conf_node_s *node1)
{
        return strncmp(node0->conf.name, node1->conf.name, DC_CONF_STRING_MAX);
}

TREE_GENERATE(dc_conf_head_s, dc_conf_node_s, entry, cmp_conf);

/*****************************************************************************
 * Raw operations
 *****************************************************************************/
/*
 *
 */
static inline struct dc_conf_node_s *
conf_find(struct dc_conf_head_s *head,
          const char *name)
{
        if (name) {
                struct dc_conf_node_s key;

                key.conf.name = name;
                return TREE_FIND(dc_conf_head_s, head, &key);
        }
        return NULL;
}

/*
 *
 */
static inline struct dc_conf_node_s *
conf_nfind(struct dc_conf_head_s *head,
           const char *name)
{
        if (name) {
                struct dc_conf_node_s key;

                key.conf.name = name;
                return TREE_NFIND(dc_conf_head_s, head, &key);
        }
        return NULL;
}

/*
 *
 */
static inline char *
cat_names(char *buff,
          int bsize,
          const char *array[])
{
        int n = 0;

        for (int i = 0; n < bsize && array[i]; i++)
                n += snprintf(&buff[n], bsize - n, "/%s", array[i]);
        if (n >= bsize)
                return NULL;
        return buff;
}

/*
 *
 */
static inline void
destroy_conf(struct dc_conf_head_s *head,
             struct dc_conf_node_s *node)
{
        TREE_REMOVE(dc_conf_head_s, head, node);
        node->conf.val  = NULL;
        node->conf.name = NULL;
        free(node);
}

/*
 *
 */
static const struct dc_conf_node_s *
conf_add(struct dc_conf_head_s *head,
         const char *name,
         const char *val)
{
        struct dc_conf_node_s *node;

        if (!name)
                return NULL;

        node = malloc(sizeof(*node) + (DC_CONF_STRING_MAX * 2));
        if (node) {
                char *name_p = &node->buff[0];

                strncpy(name_p, name, DC_CONF_STRING_MAX - 1);
                name_p[DC_CONF_STRING_MAX - 1] = '\0';
                node->conf.name = name_p;

                if (val) {
                        char *val_p  = &node->buff[DC_CONF_STRING_MAX];

                        strncpy(val_p, val, DC_CONF_STRING_MAX - 1);
                        val_p[DC_CONF_STRING_MAX - 1] = '\0';
                        node->conf.val = val_p;
                } else {
                        node->conf.val = NULL;
                }

                if (TREE_INSERT(dc_conf_head_s, head, node)) {
                        free(node);
                        node = NULL;
                }
        }
        return node;
}

/*
 *
 */
static const struct dc_conf_node_s *
conf_update(struct dc_conf_head_s *head,
            const char *name,
            const char *val)
{
        if (!name)
                return NULL;

        struct dc_conf_node_s *node = conf_find(head, name);
        if (node) {
                if (val) {
                        char *val_p  = &node->buff[DC_CONF_STRING_MAX];

                        strncpy(val_p, val, DC_CONF_STRING_MAX - 1);
                        val_p[DC_CONF_STRING_MAX - 1] = '\0';
                        node->conf.val = val_p;
                } else {
                        node->conf.val = NULL;
                }
                return node;
        }
        return conf_add(head, name, val);
}

/*****************************************************************************
 * Basic Operations
 *****************************************************************************/
/*
 *
 */
const struct dc_conf_s *
dc_conf_find(struct dc_conf_db_s *db,
             const char *key)
{
        struct dc_conf_node_s *node;

        node = conf_find(&db->head, key);
        if (node) {
                DC_FW_DEBUG("found %s %s", key, node->conf.val);
                return &node->conf;
        }

        DC_FW_INFO("not found %s", key);
        return NULL;
}

/*
 *
 */
const char *
dc_conf_find_val(struct dc_conf_db_s *db,
                 const char *key)
{
        struct dc_conf_node_s *node;

        node = conf_find(&db->head, key);
        if (node) {
                DC_FW_DEBUG("found %s %s", key, node->conf.val);
                return node->conf.val;
        }

        DC_FW_INFO("not found %s", key);
        return NULL;
}

/*
 *
 */
const struct dc_conf_s *
dc_conf_nfind(struct dc_conf_db_s *db,
              const char *key)
{
        struct dc_conf_node_s *node;

        node = conf_nfind(&db->head, key);
        if (node) {
                DC_FW_DEBUG("found %s %s", key, node->conf.val);
                return &node->conf;
        }
        DC_FW_INFO("not found %s", key);
        return NULL;
}

/*
 *
 */
const struct dc_conf_s *
dc_conf_add(struct dc_conf_db_s *db,
            const char *key,
            const char *val)
{
        const struct dc_conf_node_s *node = conf_add(&db->head, key, val);
        if (node) {
                DC_FW_DEBUG("added %s %s", key, val);
                return &node->conf;
        }
        DC_FW_ERR("failed to add %s", key);
        return NULL;
}

/*
 *
 */
const struct dc_conf_s *
dc_conf_update(struct dc_conf_db_s *db,
               const char *key,
               const char *val)
{
        const struct dc_conf_node_s *node = conf_update(&db->head, key, val);
        if (node)
                return &node->conf;
        return NULL;
}

/*
 *
 */
void
dc_conf_delete(struct dc_conf_db_s *db,
               const char *key)
{
        struct dc_conf_node_s *node = conf_find(&db->head, key);

        if (node)
                destroy_conf(&db->head, node);
}

/*
 *
 */
void
dc_conf_delete_all(struct dc_conf_db_s *db)
{
        struct dc_conf_node_s *node;

        while ((node = TREE_ROOT(&db->head)) != NULL)
                destroy_conf(&db->head, node);
}

/*
 *
 */
int
dc_conf_walk(struct dc_conf_db_s *db,
             int (*cb)(const char *db_name,
                       const struct dc_conf_s *,
                       void *arg),
             void *arg)
{
        int ret = -1;
        struct dc_conf_node_s *node;

        TREE_FOREACH(node, dc_conf_head_s, &db->head) {
                ret = cb(db->name, &node->conf, arg);
                if (ret)
                        break;
        }
        return ret;
}

/*
 *
 */
struct dc_conf_db_s *
dc_conf_create(const char *db_name)
{
        struct dc_conf_db_s *db;

        db = malloc(sizeof(*db));
        if (db) {
                snprintf(db->name, sizeof(db->name), "%s", db_name);
                TREE_INIT(&db->head);
        }
        return db;
}

/*
 *
 */
void
dc_conf_destroy(struct dc_conf_db_s *db)
{
        if (db) {
                dc_conf_delete_all(db);
                free(db);
        }
}

/******************************************************************************
 *	Generic Operations
 ******************************************************************************/
/*
 *
 */
static inline void
dc_conf_dir_delete(struct dc_conf_db_s *db,
                   const char *key)
{
        char name[DC_CONF_STRING_MAX];
        struct dc_conf_node_s *node;

        if (!key)
                return;

        snprintf(name, sizeof(name), "%s/", key);
        unsigned len = strlen(name);
        while ((node = conf_nfind(&db->head, name)) != NULL) {
                if (strncmp(name, node->conf.name, len))
                        break;
                destroy_conf(&db->head, node);
        }
        name[len] = '\0';
        node = conf_find(&db->head, name);
        if (node)
                destroy_conf(&db->head, node);
}

struct dc_conf_db_s *
dc_conf_file_open(const char *path)
{
        struct dc_conf_db_s *db = NULL;
        FILE *fp = fopen(path, "r");

        if (!fp)
                goto end;
        db = dc_conf_create(path);
        if (db) {
                char buff[1024];
                char *s;
                unsigned line = 0;

                while ((s = fgets(buff, sizeof(buff), fp)) != NULL) {
                        char *name = NULL;
                        char *val = name;
                        line++;

                        for (unsigned i = 0; i < sizeof(buff); i++) {
                                int c = s[i];

                                if (isspace(c)) {
                                        s[i] = '\0';

                                        if (!isblank(c))
                                                break;

                                        if (name && val == name)
                                                val = NULL;
                                } else {
                                        if (!name)
                                                val = name = &s[i];
                                        else if (!val)
                                                val = &s[i];
                                }
                        }

                        if (val == name)
                                val = NULL;
                        if (name && *name == '#')
                                name = NULL;
                        if (name) {
                                if (!dc_conf_add(db, name, val)) {
                                        DC_FW_NOTICE("(%u) ignored:%s",
                                                     line, name);
                                }
                        }
                }
        }
 end:
        if (fp)
                fclose(fp);
        if (!db)
                DC_FW_ERR("failed %s", path);
        return db;
}

/******************************************************************************
 *	Directory Operations
 ******************************************************************************/

/*
 * token separator: ','
 */
static const char *
get_token(char *dst,
          size_t size,
          const char *src)
{
        memset(dst, 0, size);

        while (*src != '\0' && isspace(*src))
                ++src;

        if (*src == '\0') {
                src = NULL;
                goto end;
        }

        for (unsigned i = 0; i < size && *src != '\0'; i++, src++) {
                if (*src == ',') {
                        src++;
                        goto end;
                }
                dst[i] = *src;
        }
        src = NULL;
 end:
        return src;
}

/*
 * ID: 0 ~ 63
 * 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
 * 0-15,16-63,
 * 0 < size <= 64
 */
unsigned
dc_conf_list(struct dc_conf_db_s *db,
             const char *key,
             char *list,
             unsigned size)
{
        const char *v = dc_conf_find_val(db, key);
        unsigned nb = 0;

        DC_FW_DEBUG("key:%s val:%s", key, v);

        while (v && size) {
                unsigned len = size - 1;

                v = get_token(list, len, v);

                len = strlen(list);
                if (!len)
                        break;
                list += (len + 1);
                size -= (len + 1);
                nb++;
        }
        return nb;
}

/*
 * /BASE_NAME/TARGET_NAME
 *
 */
static struct dc_conf_node_s *
dc_conf_dir(struct dc_conf_db_s *db,
            const char *base,
            char *target,
            size_t size)
{
        struct dc_conf_node_s *node;
        char name[size];

        snprintf(name, size, "%s/", base);

        node = conf_nfind(&db->head, name);
        if (node) {
                size_t len = strlen(name);

                if (strncmp(name, node->conf.name, len)) {
                        node = NULL;
                } else {
                        snprintf(target, size, "%s", &node->conf.name[len]);

                        char *p = strstr(target, "/");
                        if (p)
                                *p = '\0';
                        else
                                node = NULL;
                }
        }
        return node;
}

static struct dc_conf_node_s *
dc_conf_next_dir(struct dc_conf_db_s *db,
                 struct dc_conf_node_s *node,
                 const char *base,
                 char *target,
                 size_t size)
{
        char name[size];
        size_t len;
        (void) db;

        snprintf(name, size, "%s/%s/", base, target);
        len = strlen(name);

        while ((node = TREE_NEXT(dc_conf_head_s, &db->head, node)) != NULL) {

                DC_FW_DEBUG("key:%s name:%s", name, node->conf.name);

                if (strncmp(name, node->conf.name, len))
                        break;
        }

        if (node) {
                snprintf(name, size, "%s/", base);
                len = strlen(name);

                if (strncmp(name, node->conf.name, len)) {
                        node = NULL;
                } else {
                        snprintf(target, size, "%s",
                                 &node->conf.name[len]);
                        char *p = strstr(target, "/");
                        if (p)
                                *p = '\0';
                        else
                                node = NULL;
                }
        }
        return node;
}


/*****************************************************************************
 *	DPDK initializing:
 *****************************************************************************/

#define ARRAYOF(_a)	(sizeof(_a)/sizeof(_a[0]))

#define SET_ARG(_ac,_av,_v)                     \
        do {                                    \
                if (ARRAYOF(_av) - 1 > (unsigned) (_ac)) {      \
                        (_av)[(_ac)] = (_v);    \
                        (_ac) += 1;             \
                        (_av)[(_ac)] = NULL;    \
                }                               \
        } while (0)

/*
 *
 */
static int
set_rte_options(struct dc_conf_db_s *db,
                int av_size,
                char **av,
                char **buf_p,
                size_t *size_p)
{
        char *buf = *buf_p;
        unsigned size = *size_p;
        int ac = 0;
        const char *v = dc_conf_find_val(db, "/rte-options");

        while (v && size && av_size) {
                v = get_token(buf, size, v);

                size_t len = strlen(buf);
                if (!len) {
                        *av = NULL;
                        break;
                }
                *av = buf;

                buf += len + 1;
                size -= len + 1;

                ac++;
                av++;
                av_size--;
        }
        *buf_p = buf;
        *size_p = size;
        return ac;
}

/*
 *
 */
int
dc_conf_init_rte(struct dc_conf_db_s *db,
                 const char *prog)
{
        char *args;
        int ac = 0;
        char *av[64];
        size_t size = 1024;
        int ret = -1;

        args = malloc(size);
        if (args) {
                char *p = args;
                size_t len;
                char lcores[256];
                unsigned nb_th;

                len = snprintf(p, size, "%s", prog);
                SET_ARG(ac, av, p);
                p += (len + 1);
                size -= (len + 1);

                nb_th = dc_thread_lcores(db, lcores, sizeof(lcores));
                if (nb_th) {
                        len = snprintf(p, size, "-l");
                        SET_ARG(ac, av, p);
                        p += (len + 1);
                        size -= (len + 1);

                        len = snprintf(p, size, "%s", lcores);
                        SET_ARG(ac, av, p);
                        p += (len + 1);
                        size -= (len + 1);

                        int master = dc_conf_master_lcore(db);
                        if (master > 0) {
                                len = snprintf(p, size, "--master-lcore");
                                SET_ARG(ac, av, p);
                                p += (len + 1);
                                size -= (len + 1);

                                len = snprintf(p, size, "%d", master);
                                SET_ARG(ac, av, p);
                                p += (len + 1);
                                size -= (len + 1);
                        }
                }

                ac += set_rte_options(db,
                                      ARRAYOF(av) - ac, &av[ac],
                                      &p, &size);

                optind = 1;	/* reset getopt */
                ret = rte_eal_init(ac, av);

                dc_framework_log_init();

                if (ret < 0)
                        DC_FW_ERR("ret:%d %s",
                                  ret, rte_strerror(rte_errno));
                free(args);
       }
        return ret;
}

/*
 *
 */
int
dc_conf_thread_lcore(struct dc_conf_db_s *db,
                     const char *name)
{
        return dc_conf_get_integer(db, "/thread/%s/lcore", name);
}

/*
 *
 */
int
dc_conf_add_lcore_thread(struct dc_conf_db_s *db,
                         unsigned lcore_id,
                         const char *name)
{
        return dc_conf_add_string(db, name, "/lcore/%u", lcore_id);
}

/*
 *
 */
bool
dc_conf_is_master_thread(struct dc_conf_db_s *db,
                         const char *name)
{
        return dc_conf_get_boolean(db, "/thread/%s/is_master", name);
}

/*
 *
 */
int
dc_conf_add_master_lcore(struct dc_conf_db_s *db,
                         unsigned lcore_id)
{
        return dc_conf_add_integer(db, lcore_id, "/master-lcore");
}

/*
 *
 */
int
dc_conf_master_lcore(struct dc_conf_db_s *db)
{
        return dc_conf_get_integer(db, "/master-lcore");
}

/*
 *
 */
struct dc_conf_node_s *
dc_conf_thread_name_next(struct dc_conf_db_s *db,
                         struct dc_conf_node_s *node,
                         char *buff,
                         size_t buff_size)
{
        if (node)
                return dc_conf_next_dir(db, node, "/thread", buff, buff_size);
        return dc_conf_dir(db, "/thread", buff, buff_size);
}

/*
 *
 */
const char *
dc_conf_lcore_thread(struct dc_conf_db_s *db,
                     unsigned lcore_id)
{
        return dc_conf_get_string(db, "/lcore/%u", lcore_id);
}

/*
 *
 */
const char *
dc_conf_thread_mbufpool(struct dc_conf_db_s *db,
                        const char *name)
{
        return dc_conf_get_string(db, "/thread/%s/mbufpool", name);
}

/*
 *
 */
const char *
dc_conf_task_addon(struct dc_conf_db_s *db,
                   const char *name)
{
        return dc_conf_get_string(db, "/task/%s/addon", name);
}

/*
 *
 */
const char *
dc_conf_task_in_port(struct dc_conf_db_s *db,
                     const char *name)
{
        return dc_conf_get_string(db, "/task/%s/in-port", name);
}

/*
 *
 */
int
dc_conf_task_out_port_list(struct dc_conf_db_s *db,
                           const char *name,
                           const char **ports,
                           unsigned max_ports,
                           char *buff,
                           size_t buff_size)
{
        return dc_conf_get_string_list(db, ports, max_ports, buff, buff_size,
                                       "/task/%s/out-ports", name);
}

/*
 *
 */
int
dc_conf_ring_size(struct dc_conf_db_s *db,
                  const char *name)
{
        return dc_conf_get_integer(db, "/ring/%s/size", name);
}

/*
 *
 */
int
dc_conf_task_list(struct dc_conf_db_s *db,
                  const char *th_name,
                  const char **tasks,
                  unsigned max_tasks,
                  char *buff,
                  size_t buff_size)
{
        return dc_conf_get_string_list(db, tasks, max_tasks, buff, buff_size,
                                       "/thread/%s/tasks", th_name);
}

/*
 * netdev
 */
const char *
dc_conf_netdev_type(struct dc_conf_db_s *db,
                    const char *name)
{
        return dc_conf_get_string(db, "/netdev/%s/type", name);
}

/*
 *
 */
const char *
dc_conf_netdev_id_name(struct dc_conf_db_s *db,
                       uint16_t id)
{
        return dc_conf_get_string(db, "/netdev/id/%u", id);
}

/*
 *
 */
int
dc_conf_netdev_name_id(struct dc_conf_db_s *db,
                       const char *name)
{
        return dc_conf_get_integer(db, "/netdev/%s/id", name);
}

/*
 *
 */
int
dc_conf_add_netdev_id_name(struct dc_conf_db_s *db,
                           uint16_t id,
                           const char *name)
{
        return dc_conf_add_string(db, name, "/netdev/id/%u", id);
}

/*
 *
 */
int
dc_conf_add_netdev_name_id(struct dc_conf_db_s *db,
                           const char *name,
                           uint16_t id,
                           bool with_name)
{
        if (dc_conf_add_integer(db, id, "/netdev/%s/id", name))
                return -1;
        if (with_name)
                return dc_conf_add_netdev_id_name(db, id, name);
        return 0;
}

/*
 *
 */
int
dc_conf_add_netdev_name_type(struct dc_conf_db_s *db,
                             const char *name,
                             const char *type)
{
        return dc_conf_add_string(db, type, "/netdev/%s/type", name);
}

/*
 *
 */
unsigned
dc_conf_netdev_nb_rx_queues(struct dc_conf_db_s *db,
                            const char *name)
{
        return dc_conf_get_integer(db, "/netdev/%s/number_of_rx_queues", name);
}

/*
 *
 */
int
dc_conf_netdev_nb_tx_queues(struct dc_conf_db_s *db,
                            const char *name)
{
        return dc_conf_get_integer(db, "/netdev/%s/number_of_tx_queues", name);
}

/*
 *
 */
const char *
dc_conf_netdev_mbufpool(struct dc_conf_db_s *db,
                        const char *name)
{
        return dc_conf_get_string(db, "/netdev/%s/mbufpool", name);
}

/*
 * XXX: conflict <netinet/ether.h>
 */
extern struct ether_addr *ether_aton_r (const char *__asc,
                                        struct ether_addr *__addr) __THROW;

int
dc_conf_netdev_mac(struct dc_conf_db_s *db,
                   const char *name,
                   struct ether_addr *addr)
{
        const char *asc = dc_conf_get_string(db, "/netdev/%s/mac", name);
        if (asc) {
                if (ether_aton_r(asc, addr))
                        return 0;
        }
        return -1;
}

/*
 *
 */
int
dc_conf_add_netdev_mac(struct dc_conf_db_s *db,
                       const char *name,
                       const struct ether_addr *addr)
{
        char mac_asc[80];

        ether_format_addr(mac_asc, sizeof(mac_asc), addr);

        return dc_conf_add_string(db, mac_asc, "/netdev/%s/mac", name);
}

/*
 *
 */
const char *
dc_conf_bonding_mode(struct dc_conf_db_s *db,
                     const char *name)
{
        return dc_conf_get_string(db, "/netdev/%s/mode", name);
}

/*
 *
 */
int
dc_conf_bondig_interval(struct dc_conf_db_s *db,
                        const char *name)
{
        return dc_conf_get_integer(db, "/netdev/%s/interval_ms", name);
}

/*
 *
 */
int
dc_conf_bondig_downdelay(struct dc_conf_db_s *db,
                         const char *name)
{
        return dc_conf_get_integer(db, "/netdev/%s/downdelay_ms", name);
}

/*
 *
 */
int
dc_conf_bondig_updelay(struct dc_conf_db_s *db,
                       const char *name)
{
        return dc_conf_get_integer(db, "/netdev/%s/updelay_ms", name);
}

/*
 *
 */
int
dc_conf_bonding_slave_list(struct dc_conf_db_s *db,
                           const char *name,
                           const char **slaves,
                           unsigned max_slaves,
                           char *buff,
                           size_t buff_size)
{
        return dc_conf_get_string_list(db, slaves, max_slaves, buff, buff_size,
                                       "/netdev/%s/slaves", name);
}

/*
 *
 */
const char *
dc_conf_netdev_depend(struct dc_conf_db_s *db,
                      const char *name)
{
        return dc_conf_get_string(db, "/netdev/%s/depend", name);
}

/*
 *
 */
const char *
dc_conf_port_depend(struct dc_conf_db_s *db,
                    const char *name)
{
        return dc_conf_get_string(db, "/port/%s/depend", name);
}

/*
 *
 */
int
dc_conf_port_rx_queue(struct dc_conf_db_s *db,
                      const char *name)
{
        return dc_conf_get_integer(db, "/port/%s/rx-queue", name);
}

/*
 *
 */
int
dc_conf_port_tx_queue(struct dc_conf_db_s *db,
                      const char *name)
{
        return dc_conf_get_integer(db, "/port/%s/tx-queue", name);
}

/*
 *
 */
int
dc_conf_port_retry(struct dc_conf_db_s *db,
                   const char *name)
{
        return dc_conf_get_integer(db, "/port/%s/retry", name);
}

/*
 * addon
 */
int
dc_conf_add_addon(struct dc_conf_db_s *db,
                  const char *name,
                  const void *p)
{
        return dc_conf_add_pointer(db, p, "/addon/%s", name);
}

/*
 *
 */
const void *
dc_conf_addon(struct dc_conf_db_s *db,
              const char *name)
{
        return dc_conf_get_pointer(db, "/addon/%s", name);
}

/*
 * mbuf
 */
int
dc_conf_mbufpool_size(struct dc_conf_db_s *db,
                      const char *name)
{
        return dc_conf_get_integer(db, "/mbufpool/%s/number-of-mbufs_k", name);
}

/*
 *
 */
int
dc_conf_mbufpool_cache_size(struct dc_conf_db_s *db,
                            const char *name)
{
        return dc_conf_get_integer(db, "/mbufpool/%s/cache-size", name);
}

/*
 *
 */
int
dc_conf_mbufpool_ext_size(struct dc_conf_db_s *db,
                            const char *name)
{
        return dc_conf_get_integer(db, "/mbufpool/%s/ext-size", name);
}

/*
 *
 */
int
dc_conf_global_initializer_list(struct dc_conf_db_s *db,
                                const char **entries,
                                unsigned max_entries,
                                char *buff,
                                size_t buff_size)
{
        return dc_conf_get_string_list(db, entries, max_entries,
                                       buff, buff_size,
                                       "/global/initializer");
}
