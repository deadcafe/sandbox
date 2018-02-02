#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <rte_ethdev.h>
#include <rte_eth_bond.h>
#include <rte_mempool.h>
#include <rte_malloc.h>

#include <rte_port.h>
#include <rte_port_ethdev.h>
#include <rte_port_kni.h>
#include <rte_port_ring.h>
#include <rte_bus_vdev.h>

#include "dc_conf.h"
#include "dc_mbuf.h"
#include "dc_fw_log.h"
#include "dc_port.h"

#define DC_INVALID_ID	(0xffff)

#define RTE_RX_DESC_DEFAULT DC_NETDEV_RX_DESC_DEFAULT
#define RTE_TX_DESC_DEFAULT DC_NETDEV_TX_DESC_DEFAULT

static int get_netdev(struct dc_conf_db_s *db,
                      const char *name,
                      uint16_t *port_id);

/*****************************************************************************
 *	ether device
 *****************************************************************************/
enum dc_netdev_type_e {
        DC_NETDEV_TYPE_ETHDEV = 0,
        DC_NETDEV_TYPE_BONDING,
        DC_NETDEV_TYPE_KNI,
        DC_NETDEV_TYPE_PCAP,
        DC_NETDEV_TYPE_NULL,

        DC_NETDEV_TYPE_INVALID,
};

#define DC_NETDEV_TYPE_NB	DC_NETDEV_TYPE_INVALID

static const char *netdev_type_name[DC_NETDEV_TYPE_NB] = {
        "ethdev",
        "bonding",
        "kni",
        "pcap",
        "null",
};

static enum dc_netdev_type_e
get_netdev_type(struct dc_conf_db_s *db,
                const char *name)
{
        const char *p = dc_conf_netdev_type(db, name);
        if (p) {
                for (enum dc_netdev_type_e type = DC_NETDEV_TYPE_ETHDEV;
                     type < DC_NETDEV_TYPE_NB;
                     type++) {
                        if (!strcmp(netdev_type_name[type], p)) {
                                DC_FW_DEBUG("found netdev type: %s", p);
                                return type;
                        }
                }
                DC_FW_ERR("mismatched %s: %s", name, p);
        }
        return DC_NETDEV_TYPE_INVALID;
}

static struct rte_eth_conf PortConf = {
        .rxmode = {
                .mq_mode = ETH_MQ_RX_RSS,
                .max_rx_pkt_len = ETHER_MAX_LEN,
                .split_hdr_size = 0,
                .header_split   = 0, /**< Header Split disabled */
                .hw_ip_checksum = 1, /**< IP checksum offload enabled */
                .hw_vlan_filter = 0, /**< VLAN filtering disabled */
                .jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
                .hw_strip_crc   = 1, /**< CRC stripped by hardware */
        },
        .rx_adv_conf = {
                .rss_conf = {
                        .rss_key = NULL,
                        .rss_hf =  ( ETH_RSS_IP   |
                                     ETH_RSS_UDP  |
                                     ETH_RSS_TCP  |
                                     ETH_RSS_SCTP ),
                },
        },
        .txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
        .intr_conf = {
                .lsc = 1,
        },
};

static uint16_t
get_port_id_ethdev(struct dc_conf_db_s *db,
                   const char *name)
{
        uint16_t nb_dev = rte_eth_dev_count();

        for (uint16_t i = 0; i < nb_dev; i++) {
                const char *p = dc_conf_netdev_id_name(db, i);

                if (p && !strcmp(p, name))
                        return i;
        }
        DC_FW_NOTICE("not found ethdev: %s", name);
        return DC_INVALID_ID;
}

static struct rte_mempool *
netdev_mbufpool(struct dc_conf_db_s *db,
                const char *name)
{
        return dc_mbufpool(db, dc_conf_netdev_mbufpool(db, name));
}

static inline int
set_mac_addr(struct dc_conf_db_s *db,
             const char *name,
             uint16_t id)
{
        struct ether_addr addr;
        int ret = 0;

        ret = dc_conf_netdev_mac(db, name, &addr);
        if (ret) {
                fprintf(stderr, "use MAC in NIC\n");
                rte_eth_macaddr_get(id, &addr);
                ret = dc_conf_add_netdev_mac(db, name, &addr);
        } else {
                ret = rte_eth_dev_default_mac_addr_set(id, &addr);
                if (ret) {
                        DC_FW_ERR("failed rte_eth_dev_default_mac_addr_set(): %s",
                                  name);
                }
        }

        return ret;
}

/*
 *
 */
static int
create_netdev_ethdev(struct dc_conf_db_s *db,
                     const char *name)
{
        int nb_rx_q = dc_conf_netdev_nb_rx_queues(db, name);
        int nb_tx_q = dc_conf_netdev_nb_tx_queues(db, name);
        uint16_t nb_rxd = DC_NETDEV_RX_DESC_DEFAULT;
        uint16_t nb_txd = DC_NETDEV_TX_DESC_DEFAULT;
        int ret;
        uint16_t id;

        if (nb_rx_q < 0)
                nb_rx_q = 0;
        if (nb_tx_q < 0)
                nb_tx_q = 0;

        id = get_port_id_ethdev(db, name);
        if (id == DC_INVALID_ID) {
                DC_FW_ERR("not configured id ethdev: %s", name);
                return -1;
        }

        ret = rte_eth_dev_configure(id, nb_rx_q, nb_tx_q, &PortConf);
        if (!ret)
                ret = rte_eth_dev_adjust_nb_rx_tx_desc(id, &nb_rxd, &nb_txd);
        else
                DC_FW_ERR("%s: nb_rxd:%u nb_txd:%u", name, nb_rxd, nb_txd);

        for (int q = 0; q < nb_rx_q && !ret; q++) {
                ret = rte_eth_rx_queue_setup(id,
                                             q,
                                             nb_rxd,
                                             rte_eth_dev_socket_id(id),
                                             NULL,
                                             netdev_mbufpool(db, name));
        }
        for (int q = 0; q < nb_tx_q && !ret; q++) {
                ret = rte_eth_tx_queue_setup(id,
                                             q,
                                             nb_txd,
                                             rte_eth_dev_socket_id(id),
                                             NULL);
        }
        if (ret)
                goto end;

#if 0
        ret = set_mac_addr(db, name, id);
        if (ret)
                goto end;
#endif

        ret = rte_eth_dev_start(id);
        if (ret) {
                DC_FW_ERR("failed rte_eth_dev_start(): %s", name);
                goto end;
        }

        ret = dc_conf_add_netdev_name_id(db, name, id, false);
end:
        return ret;
}

#if 0
/*
 *
 */
static int
add_pci_devices(struct dc_conf_db_s *db)
{
        unsigned nb = rte_eth_dev_count();
        int ret = 0;

        for (uint16_t i = 0; i < nb && ret; i++) {
                const char *name;

                name = find_netdev_name_by_id(db, i);
                if (name)
                        ret = add_netdev_id(db, name, i, false);
                ret |= add_netdev_type(db, name,
                                       netdev_type_name[DC_NETDEV_TYPE_ETHDEV]);
        }

        if (ret)
                DC_FW_ERR("failed to %s", __func__);
        return ret;
}
#endif

/*****************************************************************************
 *	bonding device
 *****************************************************************************/
#define BONDING_MODE_INVALID	7
#define BONDING_MODE_NB		BONDING_MODE_INVALID

static char *bonding_mode_name[] = {
        "round_robin",
        "active_backup",
        "balance",
        "broadcast",
        "8023ad",
        "tlb",
        "alb",
};

static unsigned
get_bonding_mode(struct dc_conf_db_s *db,
                 const char *name)
{
        unsigned mode;

        const char *p = dc_conf_bonding_mode(db, name);
        if (!p)
                return BONDING_MODE_INVALID;

        for (mode = BONDING_MODE_ROUND_ROBIN;
             mode < BONDING_MODE_NB;
             mode++) {
                if (!strcmp(bonding_mode_name[mode], p))
                        return mode;
        }

        DC_FW_ERR("mismatched bonding mode: %s", p);
        return BONDING_MODE_INVALID;
}

static int
create_netdev_bonding(struct dc_conf_db_s *db,
                      const char *name)
{
        int ret = -1;
        uint16_t id;
        unsigned mode = BONDING_MODE_INVALID;
        char key[128];
        char buff[256];
        const char *slaves[16];
        int nb_slaves;
        uint8_t socket_id;
        int nb_rxq = dc_conf_netdev_nb_rx_queues(db, name);
        int nb_txq = dc_conf_netdev_nb_tx_queues(db, name);;
        uint16_t nb_rxd = RTE_RX_DESC_DEFAULT;
        uint16_t nb_txd = RTE_TX_DESC_DEFAULT;

        if (nb_rxq < 0)
                nb_rxq = 0;
        if (nb_txq < 0)
                nb_txq = 0;

        nb_slaves = dc_conf_bonding_slave_list(db, name,
                                               slaves, RTE_DIM(slaves),
                                               buff, sizeof(buff));
        if (nb_slaves <= 0) {
                DC_FW_ERR("nothing valid slaves: %s", name);
                return -1;
        }

        for (int i = 0; i < nb_slaves; i++) {
                ret = get_netdev(db, slaves[i], NULL);
                if (ret)
                        return ret;

                ret = dc_conf_netdev_name_id(db, slaves[i]);
                if (ret < 0)
                        return ret;
                socket_id = rte_eth_dev_socket_id(ret);
        }

        mode = get_bonding_mode(db, name);
        if (mode == BONDING_MODE_INVALID)
                return ret;

        snprintf(key, sizeof(key), "net_bonding_%s", name);
        ret = rte_eth_bond_create(key, mode, socket_id);
        if (ret < 0) {
                DC_FW_ERR("failed rte_eth_bond_create(): %s", name);
                return ret;
        }

        id = ret;
        ret = rte_eth_dev_configure(id, nb_rxq, nb_txq, &PortConf);
        if (ret) {
                DC_FW_ERR("failed rte_eth_dev_configure(): %s", name);
                return ret;
        }

        ret = rte_eth_dev_adjust_nb_rx_tx_desc(id, &nb_rxd, &nb_txd);
        if (ret) {
                DC_FW_ERR("failed rte_eth_dev_adjust_nb_rx_tx_desc(): %s",
                          name);
                return ret;
        } else {
                DC_FW_INFO("%s: nb_rxd:%u nb_txd:%u", name, nb_rxd, nb_txd);
        }

        for (int q = 0; q < nb_rxq; q++) {
                ret = rte_eth_rx_queue_setup(id, q, nb_rxd, socket_id,
                                             NULL,
                                             netdev_mbufpool(db, name));
                if (ret) {
                        DC_FW_ERR("failed rte_eth_rx_queue_setup(): %s",
                                  name);
                        return ret;
                }
        }

        for (int q = 0; q < nb_txq; q++) {
                ret = rte_eth_tx_queue_setup(id, q, nb_txd, socket_id, NULL);
                if (ret) {
                        DC_FW_ERR("failed rte_eth_tx_queue_setup(): %s",
                                  name);
                        return ret;
                }
        }

        int msec = dc_conf_bondig_interval(db, name);
        if (msec >= 0) {
                ret = rte_eth_bond_link_monitoring_set(id, msec);
                if (ret) {
                        DC_FW_ERR("failed rte_eth_bond_link_monitoring_set(): %s",
                                  name);
                        return ret;
                }
        }

        msec = dc_conf_bondig_downdelay(db, name);
        if (msec >= 0) {
                ret = rte_eth_bond_link_down_prop_delay_set(id, msec);
                if (ret) {
                        DC_FW_ERR("failed rte_eth_bond_link_down_prop_delay_set(): %s",
                                  name);
                        return ret;
                }
        }

        msec = dc_conf_bondig_updelay(db, name);
        if (msec >= 0) {
                ret = rte_eth_bond_link_up_prop_delay_set(id, msec);
                if (ret) {
                        DC_FW_ERR("failed rte_eth_bond_link_up_prop_delay_set(): %s",
                                  name);
                        return ret;
                }
        }

        for (int i = 0; i < nb_slaves; i++) {
                ret = dc_conf_netdev_name_id(db, slaves[i]);
                if (ret < 0)
                        return ret;

                ret = rte_eth_bond_slave_add(id, ret);
                if (ret) {
                        DC_FW_ERR("failed rte_eth_bond_slave_add(): %s",
                                  name);
                        return ret;
                }
        }

#if 1
        ret = set_mac_addr(db, name, id);
        if (ret)
                return ret;
#endif

        if (rte_eth_dev_start(id)) {
                DC_FW_ERR("failed rte_eth_dev_start(): %s", name);
                return -1;
        }

        return dc_conf_add_netdev_name_id(db, name, id, true);
}

/*****************************************************************************
 *	kni device
 *****************************************************************************/
static inline int
set_linux_if_mac(const char *raw,
                 uint16_t depend_id)
{
        int fd;
        struct ifreq ifr;
        int ret = -1;
        struct ether_addr *addr = (struct ether_addr *) &ifr.ifr_hwaddr.sa_data[0];

        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "kni_%s", raw);

        rte_eth_macaddr_get(depend_id, addr);

        fd = socket(AF_PACKET, SOCK_DGRAM, 0);
        if (fd >= 0) {
                ret = ioctl(fd, SIOCSIFHWADDR, &ifr);
                if (ret)
                        fprintf(stderr, "ioctl: %s\n", ifr.ifr_name);
                close(fd);
        }
        return ret;
}

static int
create_netdev_kni(struct dc_conf_db_s *db,
                  const char *raw)
{
        char name[128];
        const char *depend_dev;
        uint16_t depend_id = DC_INVALID_ID;
        uint16_t id;
        struct rte_eth_conf null_conf;

        memset(&null_conf, 0, sizeof(null_conf));

        depend_dev = dc_conf_netdev_depend(db, raw);
        if (depend_dev) {
                if (get_netdev(db, depend_dev, &depend_id))
                        return -1;
        }

        snprintf(name, sizeof(name), "net_kni_%s", raw);
        if (rte_eth_dev_get_port_by_name(name, &id)) {
                char args[128];

                memset(args, 0, sizeof(args));
                if (rte_vdev_init(name, args)) {
                        DC_FW_ERR("failed rte_vdev_init: %s", raw);
                        return -1;
                }

        }

        if (rte_eth_dev_get_port_by_name(name, &id)) {
                DC_FW_ERR("failed rte_eth_dev_get_port_by_name: %s", name);
                return -1;
        }

        if (rte_eth_dev_configure(id, 1, 1, &null_conf)) {
                DC_FW_ERR("failed rte_eth_dev_configure(): %s", raw);
                return -1;
        }

        if (rte_eth_rx_queue_setup(id, 0, 0, rte_socket_id(), NULL, netdev_mbufpool(db, raw))) {
                DC_FW_ERR("failed rte_eth_rx_queue_setup(): %s", raw);
                return -1;
        }
        if (rte_eth_tx_queue_setup(id, 0, 0, rte_socket_id(), NULL)) {
                DC_FW_ERR("failed rte_eth_tx_queue_setup(): %s", raw);
                return -1;
        }

        if (rte_eth_dev_start(id)) {
                DC_FW_ERR("failed rte_eth_dev_start(): %s", raw);
                return -1;
        }

#if 1
        if (depend_id != DC_INVALID_ID) {
                if (set_linux_if_mac(raw, depend_id))
                        return -1;
        }
#endif

        DC_FW_DEBUG("started netdev: %s %u", raw, id);
        return dc_conf_add_netdev_name_id(db, raw, id, true);
}

/*****************************************************************************
 *	pcap device
 *****************************************************************************/
static int
create_netdev_pcap(struct dc_conf_db_s *db,
                   const char *name)
{
        (void) db;
        (void) name;
        return -1;
}

/*****************************************************************************
 *	null device
 *****************************************************************************/
static int
create_netdev_null(struct dc_conf_db_s *db,
                   const char *raw)
{
        char name[128];
        uint16_t port_id;
        int ret = 0;
        struct rte_eth_conf null_conf;

        memset(&null_conf, 0, sizeof(null_conf));

        snprintf(name, sizeof(name), "net_null_%s", raw);

        if (rte_eth_dev_get_port_by_name(name, &port_id)) {
                char args[128];

                DC_FW_DEBUG("dev_null: %s", raw);

                snprintf(args, sizeof(args), "size=%u,copy=0",
                         RTE_MBUF_DEFAULT_BUF_SIZE);

                if (rte_vdev_init(name, args)) {
                        DC_FW_ERR("failed rte_vdev_init: %s", raw);
                        return -1;
                }
        }

        if (rte_eth_dev_get_port_by_name(name, &port_id)) {
                DC_FW_ERR("failed rte_eth_dev_get_port_by_name: %s", name);
                return -1;
        }

        if (!rte_eth_dev_is_valid_port(port_id)) {
                DC_FW_ERR("invalid port: %u", port_id);
                return -1;
        }

        ret = rte_eth_dev_configure(port_id, 0, 1, &null_conf);
        if (ret) {
                DC_FW_ERR("failed rte_eth_dev_configure(): %s %s",
                          name,
                          rte_strerror(-ret));
                return -1;
        }

#if 0
        if (rte_eth_rx_queue_setup(port_id, 0, 0, rte_socket_id(), NULL,
                                   get_netdev_mbufpool(db, name))) {
                DC_FW_ERR("failed rte_eth_rx_queue_setup(): %s", name);
                return -1;
        }
#endif

        if (rte_eth_tx_queue_setup(port_id, 0, 0, rte_socket_id(), NULL)) {
                DC_FW_ERR("failed rte_eth_tx_queue_setup(): %s", name);
                return -1;
        }
        if (rte_eth_dev_start(port_id)) {
                DC_FW_ERR("failed rte_eth_dev_start(): %s", name);
                return -1;
        }

        DC_FW_DEBUG("started netdev: %s %u", raw, port_id);
        return dc_conf_add_netdev_name_id(db, raw, port_id, true);
}

static int
create_netdev(struct dc_conf_db_s *db,
              const char *name)
{
        int ret;

        DC_FW_DEBUG("creating netdev: %s", name);

        switch (get_netdev_type(db, name)) {
        case DC_NETDEV_TYPE_ETHDEV:
                ret = create_netdev_ethdev(db, name);
                break;

        case DC_NETDEV_TYPE_BONDING:
                ret = create_netdev_bonding(db, name);
                break;

        case DC_NETDEV_TYPE_KNI:
                ret = create_netdev_kni(db, name);
                break;

        case DC_NETDEV_TYPE_PCAP:
                ret = create_netdev_pcap(db, name);
                break;

        case DC_NETDEV_TYPE_NULL:
                ret = create_netdev_null(db, name);
                break;

        case DC_NETDEV_TYPE_INVALID:
        default:
                ret = -1;
                break;
        }

        if (ret)
                DC_FW_ERR("failed to create netdev: %s", name);
        else
                DC_FW_DEBUG("created netdev: %s", name);
        return ret;
}

static int
get_netdev(struct dc_conf_db_s *db,
           const char *name,
           uint16_t *port_id)
{
        uint16_t id;

        DC_FW_DEBUG("getting netdev: %s", name);

        id = dc_conf_netdev_name_id(db, name);
        if (id != DC_INVALID_ID) {
                if (port_id)
                        *port_id = id;
                return 0;
        }

        if (!create_netdev(db, name)) {
                id = dc_conf_netdev_name_id(db, name);
                if (id != DC_INVALID_ID) {
                        if (port_id)
                                *port_id = id;
                        return 0;
                }
        }
        DC_FW_ERR("failed to get netdev: %s", name);
        return -1;
}

static struct dc_port_s *
create_port_netdev(struct dc_conf_db_s *db,
                   const char *port_name,
                   const char *dev_name,
                   enum dc_port_dir_e dir)
{
        struct dc_port_s *port;
        uint16_t port_id;

        DC_FW_DEBUG("creating port netdev: %s %s",
                    port_name, dev_name);

        if (get_netdev(db, dev_name, &port_id))
                return NULL;

        port = rte_zmalloc_socket(port_name, sizeof(*port),
                                  RTE_CACHE_LINE_SIZE,
                                  rte_socket_id());
        if (!port) {
                DC_FW_ERR("failed to alloc porn");
                return NULL;
        }

        snprintf(port->name, sizeof(port->name), "%s", port_name);

        port->dir = dir;
        if (dir == DC_PORT_DIR_IN) {
                struct rte_port_ethdev_reader_params params;
                struct rte_port_in_ops *ops = &rte_port_ethdev_reader_ops;

                int q_id = dc_conf_port_rx_queue(db, port_name);
                if (q_id < 0) {
                        rte_free(port);
                        return NULL;
                }
                port->dir = DC_PORT_DIR_IN;
                port->ops.in = ops;

                port->queue_id = q_id;
                port->port_id = port_id;

                params.port_id = port_id;
                params.queue_id = q_id;
                port->op = ops->f_create(&params, rte_socket_id());
        } else {
                union {
                        struct rte_port_ethdev_writer_params normal;
                        struct rte_port_ethdev_writer_nodrop_params nodrop;
                } params;
                struct rte_port_out_ops *ops = NULL;

                int q_id = dc_conf_port_tx_queue(db, port_name);
                if (q_id < 0) {
                        rte_free(port);
                        return NULL;
                }

                int retries = dc_conf_port_retry(db, port_name);
                if (retries < 0) {
                        params.normal.port_id = port_id;
                        params.normal.queue_id = q_id;
                        params.normal.tx_burst_sz = 8;

                        ops = &rte_port_ethdev_writer_ops;
                } else {
                        params.nodrop.port_id = port_id;
                        params.nodrop.queue_id = q_id;
                        params.nodrop.tx_burst_sz = 8;
                        params.nodrop.n_retries = retries;

                        ops = &rte_port_ethdev_writer_nodrop_ops;
                }
                port->queue_id = q_id;
                port->port_id = port_id;

                port->dir = DC_PORT_DIR_OUT;
                port->ops.out = ops;
                port->op = ops->f_create(&params, rte_socket_id());
        }

        if (port->op == NULL) {
                DC_FW_ERR("failed to create: %s", port_name);
                rte_free(port);
                port = NULL;
        } else {
                DC_FW_DEBUG("created port: %s", port_name);
        }
        return port;
}

static struct rte_ring *
get_ring(struct dc_conf_db_s *db,
         const char *name)
{
        struct rte_ring *ring;

        while ((ring = rte_ring_lookup(name)) == NULL) {
                int size = dc_conf_ring_size(db, name);

                if (size <= 0)
                        return NULL;

                ring = rte_ring_create(name, size,
                                       rte_socket_id(), RING_F_SC_DEQ);
                if (!ring) {
                        DC_FW_ERR("failed to rte_ring_create(): %s", name);
                        return NULL;
                } else {
                        DC_FW_DEBUG("created ring:%s", name);
                }
        }
        DC_FW_DEBUG("found ring: %s", name);
        return ring;
}

static struct dc_port_s *
create_port_ring(struct dc_conf_db_s *db,
                 const char *port_name,
                 const char *ring_name,
                 enum dc_port_dir_e dir)
{
        struct rte_ring *ring;
        struct dc_port_s *port;

        ring = get_ring(db, ring_name);
        if (!ring)
                return NULL;

        port = rte_zmalloc_socket(port_name, sizeof(*port),
                                  RTE_CACHE_LINE_SIZE,
                                  rte_socket_id());
        if (!port) {
                DC_FW_ERR("failed to alloc porn");
                return NULL;
        }

        snprintf(port->name, sizeof(port->name), "%s", port_name);

        port->dir = dir;
        if (dir == DC_PORT_DIR_IN) {
                struct rte_port_ring_reader_params params;
                struct rte_port_in_ops *ops = &rte_port_ring_reader_ops;

                port->ops.in = ops;

                params.ring = ring;
                port->op = ops->f_create(&params, rte_socket_id());
        } else {
                union {
                        struct rte_port_ring_writer_params normal;
                        struct rte_port_ring_writer_nodrop_params nodrop;
                } params;
                struct rte_port_out_ops *ops = NULL;
                int retries = dc_conf_port_retry(db, port_name);

                if (retries < 0) {
                        params.normal.ring = ring;
                        params.normal.tx_burst_sz = 16;

                        ops = &rte_port_ring_multi_writer_ops;
                } else {
                        params.nodrop.ring = ring;
                        params.nodrop.tx_burst_sz = 16;
                        params.nodrop.n_retries = retries;

                        ops = &rte_port_ring_multi_writer_nodrop_ops;
                }
                port->ops.out = ops;
                port->op = ops->f_create(&params, rte_socket_id());
        }

        if (port->op == NULL) {
                DC_FW_ERR("failed to create: %s", port_name);
                rte_free(port);
                port = NULL;
        } else {
                DC_FW_DEBUG("created port: %s", port_name);
        }
        return port;
}

static enum dc_port_type_e
get_port_type(const char *depend)
{
        const char *type_name[DC_PORT_TYPE_INVALID] = {
                "netdev",
                "ring",
        };
        char key[128];
        char *p;

        p = strchr(depend, '/');
        if (!p)
                return DC_PORT_TYPE_INVALID;
        snprintf(key, sizeof(key), "%s", ++p);
        p = strchr(key, '/');
        if (p)
                *p = '\0';

        for (enum dc_port_type_e type = DC_PORT_TYPE_ETHDEV;
             type < DC_PORT_TYPE_INVALID;
             type++) {
                if (!strcmp(type_name[type], key)) {
                        DC_FW_DEBUG("found port type: %s", key);
                        return type;
                }
        }

        DC_FW_ERR("mismatched port type: %s", key);
        return DC_PORT_TYPE_INVALID;
}

static struct dc_port_s *
port_create(struct dc_conf_db_s *db,
            const char *name,
            bool is_in)
{
        const char *depend = dc_conf_port_depend(db, name);
        if (!depend)
                return NULL;

        enum dc_port_type_e type = get_port_type(depend);

        depend = strrchr(depend, '/');
        if (!depend) {
                DC_FW_ERR("invalid port depend: %s", name);
                return NULL;
        }
        depend++;
        switch (type) {
        case DC_PORT_TYPE_ETHDEV:
                return create_port_netdev(db, name, depend, is_in);

        case DC_PORT_TYPE_RING:
                return create_port_ring(db, name, depend, is_in);

        case DC_PORT_TYPE_INVALID:
        default:
                return NULL;
        }
        return NULL;
}

struct dc_port_s *
dc_port_in_create(struct dc_conf_db_s *db,
                  const char *name)
{
        return port_create(db, name, DC_PORT_DIR_IN);
}

struct dc_port_s *
dc_port_out_create(struct dc_conf_db_s *db,
                   const char *name)
{
        return port_create(db, name, DC_PORT_DIR_OUT);
}
