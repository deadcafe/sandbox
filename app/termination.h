#ifndef _TERMINATION_H_
#define _TERMINATION_H_

#include <sys/queue.h>
#include <stdint.h>

#include <rte_memzone.h>
#include <rte_hash.h>

typedef uint64_t be64_t;
typedef uint32_t be32_t;
typedef uint16_t be16_t;


struct term_key_s {
        union {
                be32_t ipv4_addr;
                uint8_t ipv6_addr[16];
                uint64_t addr[2];
        };

        be16_t port;
        uint16_t ip_ver;	/* 4 or 6 */
} __attribute__((packed));


struct db_stats_s {
        uint64_t nb_rcv;
        uint64_t nb_snd;
        uint64_t nb_drop;

        uint64_t by_rcv;
        uint64_t by_snd;

}__rte_aligned(RTE_CACHE_LINE_SIZE);


struct context_info_s;

#define INVALID_WORKER	-1

struct term_info_s {
        struct term_key_s key[2];
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
                                       uint16_t ip_ver,
                                       const void *addr,
                                       be16_t port0,
                                       be16_t port1);

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
                     const struct term_key_s **keys,
                     unsigned nb_keys,
                     uint64_t *hit_mask,
                     struct term_info_s *term_pp[]);

#endif	/* !_TERMINATION_H_ */
