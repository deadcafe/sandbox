#ifndef _DC_MBUF_H_
#define _DC_MBUF_H_

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_net.h>
#include <rte_mbuf_ptype.h>

struct dc_conf_db_s;

extern struct rte_mempool *dc_mbufpool_find(const char *name);
extern struct rte_mempool *dc_mbufpool(struct dc_conf_db_s *db,
                                       const char *name);

#endif	/* !_DC_MBUF_H_ */
