

#include <rte_lcore.h>
#include <rte_malloc.h>

#include "jbuff.h"

static inline int
cmp_mbuf_ext(const struct mbuf_ext_s *e0,
             const struct mbuf_ext_s *e1)
{
        if ((e0->rcv_tsc + e0->delay_tsc) > (e1->rcv_tsc + e1->delay_tsc))
                return 1;
        if ((e0->rcv_tsc + e0->delay_tsc) < (e1->rcv_tsc + e1->delay_tsc))
                return -1;

        if (e0->rcv_tsc > e1->rcv_tsc)
                return 1;
        if (e0->rcv_tsc < e1->rcv_tsc)
                return -1;

        return e0->cookie - e1->cookie;
}

RB_GENERATE_STATIC(mbuf_ext_tree_s, mbuf_ext_s, node, cmp_mbuf_ext);

struct jbuff_s *
jbuff_create(void)
{
        struct jbuff_s *jb = rte_malloc_socket("jbuff",
                                               sizeof(*jb),
                                               RTE_CACHE_LINE_SIZE,
                                               rte_socket_id());
        if (jb) {
                RB_INIT(&jb->tree);
                jb->nb_entries = 0;
        }
        return jb;
}

int
jbuff_enqueue(struct jbuff_s *jb,
              struct mbuf_ext_s *ext)
{
        if (RB_INSERT(mbuf_ext_tree_s, &jb->tree, ext))
                return -1;
        return 0;

}

struct mbuf_ext_s *
jbuff_dequeue(struct jbuff_s *jb,
              uint64_t now_tsc)
{
        struct mbuf_ext_s *ext;

        ext = RB_MIN(mbuf_ext_tree_s, &jb->tree);
        if (ext) {
                if (ext->rcv_tsc + ext->delay_tsc <= now_tsc) {
                        RB_REMOVE(mbuf_ext_tree_s, &jb->tree, ext);
                        return ext;
                }
        }
        return NULL;
}

