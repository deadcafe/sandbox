
#include <errno.h>
#include <string.h>

#include <rte_atomic.h>
#include <rte_lcore.h>
#include <rte_hash_crc.h>
#include <rte_prefetch.h>
#include <rte_byteorder.h>

#include <dc_addon.h>
#include <dc_conf.h>

#include "termination.h"

#define XXX_DB_NAME		"TermContextDB"



/*****************************************************************************
 *	Termination
 *****************************************************************************/
/*
 *
 */
struct term_db_s *
create_term_db(unsigned nb_terms,
               unsigned nb_contexts)
{
        unsigned entries = rte_align32pow2(nb_terms);
        unsigned contexts = rte_align32pow2(nb_contexts);
        struct term_db_s *db;
        size_t sz = sizeof(*db) +
                    (sizeof(struct term_info_s) * entries) +
                    (sizeof(struct context_info_s) * contexts);
        const struct rte_memzone *mz;

        mz = rte_memzone_reserve(XXX_DB_NAME,
                                 sz,
                                 rte_socket_id(),
                                 RTE_MEMZONE_1GB | RTE_MEMZONE_SIZE_HINT_ONLY);
        if (!mz)
                return NULL;

        db = mz->addr;
        db->mz = mz;

        /* create context pool */
        db->ctx_entries = contexts;
        db->ctx_assigned = 0;
        TAILQ_INIT(&db->ctx_free);
        TAILQ_INIT(&db->ctx_used);
        db->ctx_head = (struct context_info_s *) &db->term_head[entries];
        for (unsigned i = 0; i < contexts; i++) {
                struct context_info_s *ctx = &db->ctx_head[i];

                ctx->db = db;
                ctx->serial_nb = i;
                ctx->id = INVALID_CONTEXT_ID;
                TAILQ_INSERT_TAIL(&db->ctx_free, ctx, node);
        }

        /* create term pool */
        db->term_entries = entries;
        db->term_assigned = 0;
        TAILQ_INIT(&db->term_free);
        TAILQ_INIT(&db->term_used);
        for (unsigned i = 0; i < entries; i++) {
                struct term_info_s *term = &db->term_head[i];

                memset(term, 0, sizeof(*term));
                term->db = db;
                term->serial_nb = i;
                TAILQ_INSERT_TAIL(&db->term_free, term, node);
        }

        /* create term hash table */
        struct rte_hash_parameters param;

        memset(&param, 0, sizeof(param));
        param.name      = "term_db";
        param.entries   = rte_align32pow2(nb_terms * 3);
        param.key_len   = sizeof(struct term_key_v4_s);
        param.hash_func = rte_hash_crc;
        param.socket_id = rte_socket_id();

        db->term_hash = rte_hash_create(&param);
        if (db->term_hash) {
                for (unsigned i = 0; i < entries; i++)
                        db->term_head[i].handle = i;

                return db;
        }

        rte_memzone_free(mz);
        return NULL;
}

/*
 *
 */
struct term_db_s *
find_term_db(void)
{
        const struct rte_memzone *mz;
        struct term_db_s *db = NULL;

        mz = rte_memzone_lookup(XXX_DB_NAME);
        if (mz)
                db = mz->addr;
        return db;
}

/*
 *
 */
void
reset_term_db(struct term_db_s *db)
{
        struct context_info_s *ctx;

        while ((ctx = TAILQ_FIRST(&db->ctx_used)) != NULL)
                release_context(ctx);

        rte_hash_reset(db->term_hash);
}

/*
 *
 */
void
destroy_term_db(struct term_db_s *db)
{
        reset_term_db(db);
        rte_memzone_free(db->mz);
}

/*
 *
 */
int
find_term(struct term_db_s *db,
          const struct term_key_v4_s **keys,
          unsigned nb_keys,
          uint64_t *hit_mask,
          struct term_info_s *term_pp[])
{
        return rte_hash_lookup_bulk_data(db->term_hash,
                                         (const void **) keys, nb_keys,
                                         hit_mask,
                                         (void **) term_pp);
}

static inline void
set_key(struct term_key_v4_s *key,
        be32_t node_ip,
        be32_t server_ip,
        be16_t node_port,
        be16_t server_port)
{
        key->src_ip   = node_ip;
        key->dst_ip   = server_ip;
        key->src_port = node_port;
        key->dst_port = server_port;
        key->zero_pad = 0;
}


/*
 * return:
 *   matched:   0 ~ (nb_keys - 1)
 *   unmatched: nb_keys
 */
static inline unsigned
find_key_pos(const struct term_key_v4_s *key,
             const struct term_key_v4_s *keys,
             unsigned nb_keys)
{
        unsigned ret;

        for (ret = 0; ret < nb_keys; ret++) {
                if (!memcmp(key, &keys[ret], sizeof(*key)))
                        break;
        }
        return ret;
}

static inline void
init_term(struct term_info_s *term,
          struct term_db_s *db,
          be32_t server_ip,
          const be16_t *server_ports,
          be32_t node_ip,
          const be16_t *node_ports)
{
        unsigned nb_valid_keys = 0;

        for (unsigned i = 0; i < RTE_DIM(term->keys); i++) {
                set_key(&term->keys[nb_valid_keys],
                        node_ip, server_ip,
                        node_ports[i], server_ports[i]);

                /* check uniq key */
                if (find_key_pos(&term->keys[nb_valid_keys],
                                 term->keys, nb_valid_keys) == nb_valid_keys)
                        nb_valid_keys++;
        }

        term->nb_keys = nb_valid_keys;
        term->worker_id = INVALID_WORKER;
        term->ctx = NULL;
        term->db = db;
}

static int
add_term_key(struct term_db_s *db,
             struct term_info_s *term)
{
        int ret[term->nb_keys];

        memset(ret, -1, sizeof(ret));
        for (unsigned i = 0; i < term->nb_keys; i++) {
                ret[i] = rte_hash_add_key_data(db->term_hash,
                                               &term->keys[i], term);
                if (ret[i])
                        goto err;
        }
        return 0;

 err:
        for (unsigned i = 0; ret[i] == 0; i++)
                rte_hash_del_key(db->term_hash, &term->keys[i]);
        return -1;
}

/*
 *
 */
struct term_info_s *
assign_term(struct term_db_s *db,
            be32_t server_ip,
            const be16_t *server_ports,
            be32_t node_ip,
            const be16_t *node_ports)
{
        struct term_info_s *term;

        term = TAILQ_FIRST(&db->term_free);
        if (term) {
                init_term(term, db,
                          server_ip, server_ports,
                          node_ip, node_ports);

                if (add_term_key(db, term)) {
                        term = NULL;
                } else {
                        TAILQ_REMOVE(&db->term_free, term, node);
                        TAILQ_INSERT_TAIL(&db->term_used, term, node);
                        db->term_assigned += 1;
                }
        }
        return term;
}

/*
 *
 */
void
release_term(struct term_info_s *term)
{
        struct term_db_s *db = term->db;

        for (unsigned i = 0; i < term->nb_keys; i++)
                rte_hash_del_key(db->term_hash, &term->keys[i]);

        TAILQ_REMOVE(&db->term_used, term, node);
        TAILQ_INSERT_TAIL(&db->term_free, term, node);
        db->term_assigned -= 1;
}

/*
 *
 */
int
bind_term_context(struct term_info_s *term,
                  struct context_info_s *ctx)
{
        if (term->ctx)
                return -EEXIST;

        term->worker_id = ctx->worker_id;
        term->ctx = ctx;
        term->next = ctx->term_head;
        ctx->term_head = term;

        rte_smp_wmb();
        return 0;
}

static void
add_stats(struct db_stats_s *dst,
          const struct db_stats_s *src)
{
        dst->nb_rcv += src->nb_rcv;
        dst->nb_snd += src->nb_snd;
        dst->nb_drop += src->nb_drop;
}

/*
 *
 */
void
unbind_term_context(struct term_info_s *term)
{
        if (term->ctx) {
                struct context_info_s *ctx = term->ctx;
                struct term_info_s *prev = (struct term_info_s *) ctx->term_head;

                if (prev == term) {
                        ctx->term_head = term->next;
                        rte_smp_wmb();
                } else {
                        while (prev) {
                                if (prev->next == term) {
                                        prev->next = term->next;
                                        rte_smp_wmb();
                                        break;
                                }
                                prev = (struct term_info_s *) prev->next;
                        }

                        /* error */
                }

                /* maybe leak last stats */
                add_stats(&ctx->stats, &term->stats);

                term->worker_id = INVALID_WORKER;
                term->ctx = NULL;
        }
}

/*****************************************************************************
 *	Context
 *****************************************************************************/
/*
 *
 */
struct context_info_s *
assign_context(struct term_db_s *db,
               unsigned id,
               int worker_id)
{
        struct context_info_s *ctx;

        ctx = TAILQ_FIRST(&db->ctx_free);
        if (ctx) {
                TAILQ_REMOVE(&db->ctx_free, ctx, node);

                ctx->id = id;
                ctx->term_head = NULL;
                ctx->worker_id = worker_id;
                memset(&ctx->stats, 0, sizeof(ctx->stats));

                db->ctx_assigned += 1;
                TAILQ_INSERT_TAIL(&db->ctx_used, ctx, node);
        } else {
                fprintf(stderr, "already assigned ctx:%u\n", db->ctx_assigned);
        }
        return ctx;
}

/*
 *
 */
void
release_context(struct context_info_s *ctx)
{
        struct term_db_s *db = ctx->db;
        struct term_info_s *term;

        while ((term = (struct term_info_s *) ctx->term_head) != NULL)
                unbind_term_context(term);

        TAILQ_REMOVE(&db->ctx_used, ctx, node);
        ctx->id = INVALID_CONTEXT_ID;
        ctx->worker_id = INVALID_WORKER;

        db->ctx_assigned -= 1;
        TAILQ_INSERT_TAIL(&db->ctx_free, ctx, node);
}

/*****************************************************************************
 *	constructor
 *****************************************************************************/
static int
TermDB_init(struct dc_conf_db_s *db)
{
        unsigned nb_terms = dc_conf_get_integer(db, "/number_of_terminations");
        unsigned nb_ctxs =  dc_conf_get_integer(db, "/number_of_contexts");

        if (create_term_db(nb_terms, nb_ctxs)) {
                fprintf(stderr, "Ok, created DB\n");
                return 0;
        }

        fprintf(stderr, "Failed to create DB\n");
        return -1;
}

static const struct dc_addon_s TermDB_addon = {
        .name = "TermDB",
        .is_global = true,
        .global_init =  TermDB_init,
};

static struct dc_addon_constructor_s TermDB_constructor = {
        .addon = &TermDB_addon,
};

RTE_INIT(addon_reg);
static void
addon_reg(void)
{
        dc_addon_register(&TermDB_constructor);
}
