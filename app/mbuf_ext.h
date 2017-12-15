#ifndef _MBUF_EXT_H_
#define _MBUF_EXT_H_

#include <sys/tree.h>
#include <stdint.h>

#include <dc_mbuf.h>

#include "termination.h"

struct mbuf_ext_s {
        struct rte_net_hdr_lens hdr_lens;
        uint32_t ptype;
        int cookie;
        unsigned rcv_thread_id;
        unsigned reserved;

        struct context_info_s *ctx;
        struct term_info_s *term;

        uint64_t rcv_tsc;
        uint64_t delay_tsc;

        RB_ENTRY(mbuf_ext_s) node;
} __attribute__((aligned(128)));

static inline struct mbuf_ext_s *
mbuf2ext(struct rte_mbuf *m)
{
        return (struct mbuf_ext_s *) (m + 1);
}

static inline struct rte_mbuf *
ext2mbuf(struct mbuf_ext_s *ext)
{
        return (struct rte_mbuf *) (ext - 1);
}

static inline void *
mbuf_default_buff(struct rte_mbuf *m)
{
        char *p = (char *) m + sizeof(*m)
                  + sizeof(struct mbuf_ext_s)
                  + RTE_MBUF_DEFAULT_DATAROOM;
        return p;
}

static inline uint32_t
mbuf_net_lens(struct rte_mbuf *m)
{
        struct mbuf_ext_s *ext = mbuf2ext(m);
        uint32_t ptype = rte_net_get_ptype(m, &ext->hdr_lens,
                                           RTE_PTYPE_ALL_MASK);

        ext->ptype = ptype;
        return ptype;
}




#endif /* !_MBUF_EXT_H_ */
