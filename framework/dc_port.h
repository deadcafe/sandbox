#ifndef _DC_PORT_H_
#define _DC_PORT_H_

#include <sys/queue.h>
#include <string.h>

#include <rte_port.h>
#include <rte_mbuf.h>

/* number of default descriptors */
#define DC_NETDEV_RX_DESC_DEFAULT	512
#define DC_NETDEV_TX_DESC_DEFAULT	256

struct dc_conf_db_s;

enum dc_port_type_e {
        DC_PORT_TYPE_ETHDEV = 0,
        DC_PORT_TYPE_RING,

        DC_PORT_TYPE_INVALID,
};

enum dc_port_dir_e {
        DC_PORT_DIR_IN = 0,
        DC_PORT_DIR_OUT,
};


struct dc_port_s {
        MARKER cacheline0;

        char name[32];

        STAILQ_ENTRY(dc_port_s) node;

        uint16_t port_id;
        uint16_t queue_id;

        enum dc_port_type_e type;
        enum dc_port_dir_e dir;

        void *op;

        union {
                const struct rte_port_in_ops *in;
                const struct rte_port_out_ops *out;
        } ops;

} __rte_cache_aligned;


union dc_port_stats_u {
        uint64_t val[2];
        struct rte_port_in_stats in;
        struct rte_port_out_stats out;
};


STAILQ_HEAD(dc_port_head_s, dc_port_s);

static inline struct dc_port_s *
dc_port_find(struct dc_port_head_s *head,
             const char *name,
             enum dc_port_dir_e dir)
{
        struct dc_port_s *port;

        STAILQ_FOREACH(port, head, node) {
                if (port->dir == dir &&
                    !strcmp(port->name, name)) {
                        return port;
                }
        }
        return NULL;
}


extern struct dc_port_s *dc_port_out_create(struct dc_conf_db_s *db,
                                            const char *name);

extern struct dc_port_s *dc_port_in_create(struct dc_conf_db_s *db,
                                           const char *name);



static inline int
dc_port_recv(struct dc_port_s *port,
             struct rte_mbuf **pkts,
             unsigned n_pkts)
{
        return port->ops.in->f_rx(port->op, pkts, n_pkts);
}

static inline int
dc_port_send(struct dc_port_s *port,
             struct rte_mbuf *pkts)
{
        return port->ops.out->f_tx(port->op, pkts);
}

static inline int
dc_port_send_bulk(struct dc_port_s *port,
                  struct rte_mbuf **pkts,
                  uint64_t mask)
{
        return port->ops.out->f_tx_bulk(port->op, pkts, mask);
}

static inline int
dc_port_flush(struct dc_port_s *port)
{
        return port->ops.out->f_flush(port->op);
}

static inline void
dc_port_stats(struct dc_port_s *port,
              union dc_port_stats_u *stats)
{
        if (port->dir == DC_PORT_DIR_IN)
                port->ops.in->f_stats(port->op, &stats->in, 0);
        else
                port->ops.out->f_stats(port->op, &stats->out, 0);
}

static inline unsigned
dc_port_count(struct dc_port_head_s *head)
{
        unsigned nb = 0;
        struct dc_port_s *port;

        STAILQ_FOREACH(port, head, node)
                nb++;
        return nb;
}

#endif	/* !_DC_PORT_H_ */
