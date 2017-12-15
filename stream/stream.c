

#include <sys/tree.h>
#include <sys/queue.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <x86intrin.h>

struct mbuf_s {
        TAILQ_ENTRY(mbuf_s) q_node;

        uint64_t tsc;
        uint32_t hash;
        uint32_t reserved;

        uint8_t payload[512];
};

TAILQ_HEAD(mbuf_q, mbuf_s);

struct stream_s {
        uint64_t tsc;
        int id;
        uint32_t reserved;
        struct mbuf_q mbuf_q;
        RB_ENTRY(stream_s) t_node;
};


static inline int
cmp_stream(const struct stream_s *s1,
           const struct stream_s *s2)
{
        if (s1->tsc > s2->tsc)
                return 1;
        if (s1->tsc < s2->tsc)
                return -1;
        return s1->id - s2->id;
}

RB_HEAD(stream_tree_s, stream_s);
RB_GENERATE_STATIC(stream_tree_s, stream_s, t_node, cmp_stream);



#define NB_STREAMS	4096	/* must be 2^n */



uint64_t
rd_tsc(void)
{
        union {
                uint64_t tsc_64;
                struct {
                        uint32_t lo_32;
                        uint32_t hi_32;
                };
        } tsc;
        asm volatile("rdtsc" :
                     "=a" (tsc.lo_32),
                     "=d" (tsc.hi_32));
        return tsc.tsc_64;
}


#define pause()	_mm_pause()

uint32_t
calc_hash(const struct mbuf_s *m)
{
        (void) m;

        return lrand48();
}

void
mbuf_free(struct mbuf_s *m)
{
        if (m) {
                memset(m->payload, -1, sizeof(m->payload));
                free(m);
        }
}

struct mbuf_s *
mbuf_alloc(void)
{
        struct mbuf_s *m;

        m = malloc(sizeof(*m));
        if (m) {
                m->hash = calc_hash(m);
                m->tsc = rd_tsc();
                memset(m->payload, 0, sizeof(m->payload));
        }
        return m;
}

struct stream_s *
create_stream_table(struct stream_tree_s *tree)
{
        struct stream_s *tbl;

        RB_INIT(tree);

        tbl = malloc(sizeof(*tbl) * NB_STREAMS);
        if (tbl) {
                for (int i = 0; i < NB_STREAMS; i++) {
                        tbl[i].tsc = UINT64_C(-1);
                        tbl[i].id = i;
                        TAILQ_INIT(&tbl[i].mbuf_q);
                        RB_INSERT(stream_tree_s, tree, &tbl[i]);
                }
        }
        return tbl;
}

void
destroy_stream_table(struct stream_s *tbl)
{
        if (tbl) {
                for (unsigned i = 0; i < NB_STREAMS; i++) {
                        struct mbuf_s *m;

                        while ((m = TAILQ_FIRST(&tbl[i].mbuf_q)) != NULL) {
                                TAILQ_REMOVE(&tbl[i].mbuf_q, m, q_node);
                                mbuf_free(m);
                        }
                }
        }
}

static uint64_t TSC_HZ;

void
init_tsc(void)
{
        uint64_t now = rd_tsc();

        sleep(1);
        now -= rd_tsc();

        TSC_HZ = now;
}

void
busy_wait(uint64_t cycles)
{
        cycles += rd_tsc();

        do {
                pause();
        } while (cycles > rd_tsc());
}

unsigned
recv_mbufs(struct mbuf_s **buff,
           unsigned buff_size)

{
        unsigned nb;

        for (nb = 0; nb < buff_size; nb++) {
                buff[nb] = mbuf_alloc();
                if (buff[nb] == NULL)
                        break;
                busy_wait((lrand48() & 15) * 20);
        }

        return nb;
}

unsigned
send_mbufs(struct mbuf_s **buff,
           unsigned buff_size)

{
        unsigned nb;

        for (nb = 0; nb < buff_size; nb++) {
                mbuf_free(buff[nb]);
                busy_wait((lrand48() & 15) * 20);
        }
        return nb;
}


void
sched_put(struct stream_tree_s *tree,
          struct stream_s *tbl,
          struct mbuf_s *m)
{
        struct stream_s *sm = &tbl[m->hash & (NB_STREAMS - 1)];

        RB_REMOVE(stream_tree_s, tree, sm);

        TAILQ_INSERT_TAIL(&sm->mbuf_q, m, q_node);
        m = TAILQ_FIRST(&sm->mbuf_q);
        sm->tsc = m->tsc;
        RB_INSERT(stream_tree_s, tree, sm);
}

struct mbuf_s *
sched_pull(struct stream_tree_s *tree,
           uint64_t duration)
{
        struct stream_s *sm = RB_MIN(stream_tree_s, tree);
        uint64_t now = rd_tsc();
        struct mbuf_s *m = NULL;

        if (sm->tsc + duration <= now) {
                struct mbuf_s *next;

                RB_REMOVE(stream_tree_s, tree, sm);
                m = TAILQ_FIRST(&sm->mbuf_q);
                next = TAILQ_NEXT(m, q_node);
                if (next)
                        sm->tsc = next->tsc;
                else
                        sm->tsc = UINT64_C(-1);
                TAILQ_REMOVE(&sm->mbuf_q, m, q_node);
                RB_INSERT(stream_tree_s, tree, sm);
        }
        return m;
}

int
thread_entry(struct stream_tree_s *tree,
             struct stream_s *tbl)
{
       uint64_t sum = 0;
       uint64_t start = rd_tsc();
       uint64_t t = 0;
       
       while (1) {
               struct mbuf_s *buff[32];
               unsigned nb;
               uint64_t now;

               nb = recv_mbufs(buff, 32);
               for (unsigned i = 0; i < nb; i++) {
                       sched_put(tree, tbl, buff[i]);
               }

               for (nb = 0; nb < 32; nb++) {
                       buff[nb] = sched_pull(tree, TSC_HZ / 30);
                       if (buff[nb] == NULL)
                               break;
               }

               sum += send_mbufs(buff, nb);
               now = rd_tsc();

               if (((now - start) / TSC_HZ) > t) {
                       t = ((now - start) / TSC_HZ);
                       fprintf(stderr, "%lu\n",
                               (unsigned long) ((sum * TSC_HZ) / (now - start)));
               }
       }

       destroy_stream_table(tbl);
       return 0;
}

int
main(void)
{
        struct stream_tree_s tree;

        init_tsc();
        return thread_entry(&tree,
                            create_stream_table(&tree));
}
