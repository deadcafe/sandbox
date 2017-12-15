
#include <stdio.h>

#include <rte_mbuf.h>
#include <rte_errno.h>

#include "dc_conf.h"
#include "dc_fw_log.h"
#include "dc_mbuf.h"

struct rte_mempool *
dc_mbufpool_find(const char *name)
{
        struct rte_mempool *mp = rte_mempool_lookup(name);

        if (mp)
                DC_FW_DEBUG("found mp: %s", name);
        else
                DC_FW_NOTICE("not found mp: %s", name);
        return mp;
}

struct rte_mempool *
dc_mbufpool(struct dc_conf_db_s *db,
            const char *name)
{
        struct rte_mempool *mp;

        if (!name)
                return NULL;

        mp = dc_mbufpool_find(name);
        if (!mp) {
                int nb_mb;
                int cache_size;
                int ext_size;

                nb_mb = dc_conf_mbufpool_size(db, name);
                if (nb_mb < 0)
                        goto end;

                cache_size = dc_conf_mbufpool_cache_size(db, name);
                if (cache_size < 0)
                        goto end;

                ext_size = dc_conf_mbufpool_ext_size(db, name);
                if (ext_size < 0)
                        goto end;

                mp = rte_pktmbuf_pool_create(name,
                                             nb_mb * 1024,
                                             cache_size,
                                             ext_size,
                                             RTE_MBUF_DEFAULT_DATAROOM,
                                             rte_socket_id());

                if (mp) {
                        DC_FW_DEBUG("Ok: mbuf pool:%s number-of-mbufs:%d cache:%d ext:%d",
                                    name, nb_mb, cache_size, ext_size);
                } else {
                        DC_FW_ERR("Ng: mbuf pool:%s number-of-mbufs:%d cache:%d ext:%d",
                                  name, nb_mb, cache_size, ext_size);
                }
        }
 end:
        return mp;
}
