#ifndef _TERMINATION_H_
#define _TERMINATION_H_

#include <sys/queue.h>
#include <stdint.h>

#include <rte_memzone.h>
#include <rte_hash.h>

typedef uint64_t be64_t;
typedef uint32_t be32_t;
typedef uint16_t be16_t;

struct db_stats_s {
        uint64_t nb_rcv;
        uint64_t nb_snd;
        uint64_t nb_drop;

        uint64_t by_rcv;
        uint64_t by_snd;

}__rte_aligned(RTE_CACHE_LINE_SIZE);


struct context_info_s;

#define INVALID_WORKER	-1


struct term_key_v4_s {
        be32_t src_ip;		/* node */
        be32_t dst_ip;		/* server */

        union {
                uint32_t ports;
                struct {
                        be16_t src_port;	/* node */
                        be16_t dst_port;	/* server */
                };
        };

        uint32_t zero_pad;
} __attribute__((packed));

struct term_info_s {
        struct term_key_v4_s keys[3];
        unsigned nb_keys;

        unsigned handle;
        int worker_id;
        unsigned serial_nb;

        struct term_db_s *db __rte_aligned(RTE_CACHE_LINE_SIZE * 2);

        struct term_info_s volatile *next;
        struct context_info_s *ctx;
        struct db_stats_s stats;

        TAILQ_ENTRY(term_info_s) node;
} __rte_aligned(RTE_CACHE_LINE_SIZE * 4);

TAILQ_HEAD(term_list_s, term_info_s);

#define INVALID_CONTEXT_ID	0

struct context_info_s {
        struct term_db_s *db;

        unsigned serial_nb;
        unsigned id;
        int worker_id;

        /* XXX not yet */
        struct term_info_s volatile *term_head;
        struct db_stats_s stats;

        TAILQ_ENTRY(context_info_s) node;
} __rte_aligned(RTE_CACHE_LINE_SIZE);

TAILQ_HEAD(context_list_s, context_info_s);


struct term_db_s {
        const struct rte_memzone *mz;

        /* ctx */
        struct context_list_s ctx_free;
        struct context_list_s ctx_used;
        unsigned ctx_entries;
        unsigned ctx_assigned;
        struct context_info_s *ctx_head;

        /* terms */
        struct rte_hash *term_hash;
        struct term_list_s term_free;
        struct term_list_s term_used;
        unsigned term_entries;
        unsigned term_assigned;
        struct term_info_s term_head[0] __rte_cache_aligned;
};

/*****************************************************************************
 *	prototypes
 *****************************************************************************/
/* for manager */
extern struct term_db_s *create_term_db(unsigned nb_terms,
                                        unsigned nb_contexts);
extern struct term_db_s *find_term_db(void);

extern void reset_term_db(struct term_db_s *db);
extern void destroy_term_db(struct term_db_s *db);


extern struct term_info_s *assign_term(struct term_db_s *db,
                                       be32_t server_ip,
                                       const be16_t *server_ports,
                                       be32_t node_ip,
                                       const be16_t *node_ports);

extern void release_term(struct term_info_s *term);
extern int bind_term_context(struct term_info_s *term,
                             struct context_info_s *ctx);
extern void unbind_term_context(struct term_info_s *term);

extern struct context_info_s *assign_context(struct term_db_s *db,
                                             unsigned id,
                                             int worker_id);
extern void release_context(struct context_info_s *ctx);

/* for engine */
extern int find_term(struct term_db_s *db,
                     const struct term_key_v4_s **keys,
                     unsigned nb_keys,
                     uint64_t *hit_mask,
                     struct term_info_s *term_pp[]);

#endif	/* !_TERMINATION_H_ */
