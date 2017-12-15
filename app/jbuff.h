#ifndef _JBUFF_H_
#define _JBUFF_H_

#include <sys/tree.h>
#include <stdint.h>

#include "mbuf_ext.h"

struct jbuff_s {
        RB_HEAD(mbuf_ext_tree_s, mbuf_ext_s) tree;
        unsigned nb_entries;
};

extern struct jbuff_s *jbuff_create(void);
extern int jbuff_enqueue(struct jbuff_s *jb,
                         struct mbuf_ext_s *ext);
extern struct mbuf_ext_s *jbuff_dequeue(struct jbuff_s *jb,
                                        uint64_t now_tsc);

#endif /* !_JBUFF_H_ */
