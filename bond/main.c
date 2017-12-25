#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_bus_pci.h>
#include <rte_eth_bond.h>
#include <rte_bus_vdev.h>
#include <rte_cycles.h>
#include <rte_net.h>
#include <rte_lcore.h>

#define RTE_RX_DESC_DEFAULT 128
#define RTE_TX_DESC_DEFAULT 512

struct bond_info {
        uint16_t id;		/* port id */
        struct ether_addr mac;
        struct rte_mempool *mp;
        uint32_t ipaddr;
        unsigned cookie;
        unsigned nb_rxq;
        unsigned nb_txq;
        unsigned nb_eth;
        unsigned primary;	/* id: port_ids[primary] */

        uint64_t rx_cnt[16];
        uint64_t tx_cnt[16];
        uint64_t rx_cksum_l3[16];
        uint64_t rx_cksum_l4[16];

        uint16_t portids[];
};

#define INVALID_PORT_ID	(UINT16_C(-1))


static void
free_bond_info(struct bond_info *info)
{
        if (info)
                free(info);
}


struct rss_type_info {
        const char *str;
        uint64_t rss_type;
};

static const struct rss_type_info RSS_type_table[] = {
        { "ipv4", ETH_RSS_IPV4 },
        { "ipv4-frag", ETH_RSS_FRAG_IPV4 },
        { "ipv4-tcp", ETH_RSS_NONFRAG_IPV4_TCP },
        { "ipv4-udp", ETH_RSS_NONFRAG_IPV4_UDP },
        { "ipv4-sctp", ETH_RSS_NONFRAG_IPV4_SCTP },
        { "ipv4-other", ETH_RSS_NONFRAG_IPV4_OTHER },
        { "ipv6", ETH_RSS_IPV6 },
        { "ipv6-frag", ETH_RSS_FRAG_IPV6 },
        { "ipv6-tcp", ETH_RSS_NONFRAG_IPV6_TCP },
        { "ipv6-udp", ETH_RSS_NONFRAG_IPV6_UDP },

        { "ipv6-sctp", ETH_RSS_NONFRAG_IPV6_SCTP },
        { "ipv6-other", ETH_RSS_NONFRAG_IPV6_OTHER },
        { "l2-payload", ETH_RSS_L2_PAYLOAD },
        { "ipv6-ex", ETH_RSS_IPV6_EX },
        { "ipv6-tcp-ex", ETH_RSS_IPV6_TCP_EX },
        { "ipv6-udp-ex", ETH_RSS_IPV6_UDP_EX },
        { "port", ETH_RSS_PORT },
        { "vxlan", ETH_RSS_VXLAN },
        { "geneve", ETH_RSS_GENEVE },
        { "nvgre", ETH_RSS_NVGRE },
};

struct offload_flag_info {
        uint64_t flag;
        const char *name;
};

static const struct offload_flag_info offload_flag_info[] = {
        { PKT_TX_SEC_OFFLOAD, "PKT_TX_SEC_OFFLOAD" },
        { PKT_TX_MACSEC, "PKT_TX_MACSEC" },
        { PKT_TX_TUNNEL_VXLAN, "PKT_TX_TUNNEL_VXLAN" },
        { PKT_TX_TUNNEL_GRE, "PKT_TX_TUNNEL_GRE" },
        { PKT_TX_TUNNEL_IPIP, "PKT_TX_TUNNEL_IPIP" },
        { PKT_TX_TUNNEL_GENEVE, "PKT_TX_TUNNEL_GENEVE" },
        { PKT_TX_TUNNEL_MPLSINUDP, "PKT_TX_TUNNEL_MPLSINUDP" },
        { PKT_TX_TUNNEL_MASK, "PKT_TX_TUNNEL_MASK" },
        { PKT_TX_QINQ_PKT, "PKT_TX_QINQ_PKT" },
        { PKT_TX_TCP_SEG, "PKT_TX_TCP_SEG" },
        { PKT_TX_IEEE1588_TMST, "PKT_TX_IEEE1588_TMST" },
        { PKT_TX_TCP_CKSUM,  "PKT_TX_TCP_CKSUM" },
        { PKT_TX_SCTP_CKSUM, "PKT_TX_SCTP_CKSUM" },
        { PKT_TX_UDP_CKSUM, "PKT_TX_UDP_CKSUM" },
        { PKT_TX_IP_CKSUM, "PKT_TX_IP_CKSUM" },
        { PKT_TX_IPV4, "PKT_TX_IPV4" },
        { PKT_TX_IPV6, "PKT_TX_IPV6" },
        { PKT_TX_VLAN_PKT, "PKT_TX_VLAN_PKT" },
        { PKT_TX_OUTER_IP_CKSUM, "PKT_TX_OUTER_IP_CKSUM" },
        { PKT_TX_OUTER_IPV4, "PKT_TX_OUTER_IPV4" },
        { PKT_TX_OUTER_IPV6, "PKT_TX_OUTER_IPV6" },
};

static inline void
dump_offload_flags(const uint64_t ol_flags)
{
        fprintf(stdout, "offload flags: 0x%16.16"PRIx64"\n", ol_flags);

        for (unsigned i = 0; i < RTE_DIM(offload_flag_info); i++) {
                uint64_t flag = offload_flag_info[i].flag;

                flag &= ol_flags;
                if (flag == offload_flag_info[i].flag)
                        fprintf(stdout,
                                "offload flag:  0x%16.16"PRIx64" %s\n",
                                flag,
                                offload_flag_info[i].name);
        }
}

static void
dump_rss_offloads(const char *name,
                  uint64_t offloads)
{
        fprintf(stdout,
                "%s offload: 0x%16.16"PRIx64"\n",
                name,
                offloads);

        for (unsigned i = 0; i < RTE_DIM(RSS_type_table); i++) {
                if (offloads & RSS_type_table[i].rss_type)
                        fprintf(stdout, "  %s\n",
                                RSS_type_table[i].str);
        }
}

struct speed_capa {
        uint32_t flag;
        const char *str;
};

static const struct speed_capa Speed_Capa[] = {
        { ETH_LINK_SPEED_AUTONEG, "Auto" },
        { ETH_LINK_SPEED_FIXED,   "Fixed" },
        { ETH_LINK_SPEED_10M_HD,  "10 Mbps half-duplex" },
        { ETH_LINK_SPEED_10M,     "10 Mbps full-duplex" },
        { ETH_LINK_SPEED_100M_HD, "100 Mbps half-duplex" },
        { ETH_LINK_SPEED_100M,    "100 Mbps full-duplex", },
        { ETH_LINK_SPEED_1G,      "1 Gbps" },
        { ETH_LINK_SPEED_2_5G,    "2.5 Gbps" },
        { ETH_LINK_SPEED_5G,      "5 Gbps" },
        { ETH_LINK_SPEED_10G,     "10 Gbps" },
        { ETH_LINK_SPEED_20G,     "20 Gbps" },
        { ETH_LINK_SPEED_25G,     "25 Gbps" },
        { ETH_LINK_SPEED_40G,     "40 Gbps" },
        { ETH_LINK_SPEED_50G,     "50 Gbps" },
        { ETH_LINK_SPEED_56G,     "56 Gbps" },
        { ETH_LINK_SPEED_100G,    "100 Gbps" },
};

static void
dump_speed_capa(uint32_t capa)
{
        fprintf(stdout,
                "speed_capa: 0x%8.8"PRIx32"\n",
                capa);

        for (unsigned i = 0; i < RTE_DIM(Speed_Capa); i++) {
                if (capa & Speed_Capa[i].flag)
                        fprintf(stdout, "  %s\n",
                                Speed_Capa[i].str);
        }
}

struct ptype_info {
        uint32_t type;
        const char *name;
};

static const struct ptype_info ptype_info[] = {
        { RTE_PTYPE_L2_ETHER, "L2_ETHER", },
        { RTE_PTYPE_L2_ETHER_TIMESYNC, "L2_ETHER_TIMESYNC", },
        { RTE_PTYPE_L2_ETHER_ARP, "L2_ETHER_ARP", },
        { RTE_PTYPE_L2_ETHER_LLDP, "L2_ETHER_LLDP", },
        { RTE_PTYPE_L2_ETHER_NSH, "L2_ETHER_NSH", },
        { RTE_PTYPE_L2_ETHER_VLAN, "L2_ETHER_VLAN", },
        { RTE_PTYPE_L2_ETHER_QINQ, "L2_ETHER_QINQ", },
        { RTE_PTYPE_L3_IPV4, "L3_IPV4", },
        { RTE_PTYPE_L3_IPV4_EXT, "L3_IPV4_EXT", },
        { RTE_PTYPE_L3_IPV6, "L3_IPV6", },
        { RTE_PTYPE_L3_IPV4_EXT_UNKNOWN, "L3_IPV4_EXT_UNKNOWN", },
        { RTE_PTYPE_L3_IPV6_EXT, "L3_IPV6_EXT", },
        { RTE_PTYPE_L3_IPV6_EXT_UNKNOWN, "L3_IPV6_EXT_UNKNOWN", },
        { RTE_PTYPE_L4_TCP, "L4_TCP", },
        { RTE_PTYPE_L4_UDP, "L4_UDP", },
        { RTE_PTYPE_L4_FRAG, "L4_FRAG", },
        { RTE_PTYPE_L4_SCTP, "L4_SCTP", },
        { RTE_PTYPE_L4_ICMP, "L4_ICMP", },
        { RTE_PTYPE_L4_NONFRAG, "L4_NONFRAG", },
        { RTE_PTYPE_TUNNEL_IP, "TUNNEL_IP", },
        { RTE_PTYPE_TUNNEL_GRE, "TUNNEL_GRE", },
        { RTE_PTYPE_TUNNEL_VXLAN, "TUNNEL_VXLAN", },
        { RTE_PTYPE_TUNNEL_NVGRE, "TUNNEL_NVGRE", },
        { RTE_PTYPE_TUNNEL_GENEVE, "TUNNEL_GENEVE", },
        { RTE_PTYPE_TUNNEL_GRENAT, "TUNNEL_GRENAT", },
        { RTE_PTYPE_TUNNEL_GTPC, "TUNNEL_GTPC", },
        { RTE_PTYPE_TUNNEL_GTPU, "TUNNEL_GTPU", },
        { RTE_PTYPE_TUNNEL_ESP, "TUNNEL_ESP", },
        { RTE_PTYPE_INNER_L2_ETHER, "INNER_L2_ETHER", },
        { RTE_PTYPE_INNER_L2_ETHER_VLAN, "INNER_L2_ETHER_VLAN", },
        { RTE_PTYPE_INNER_L2_ETHER_QINQ, "INNER_L2_ETHER_QINQ", },
        { RTE_PTYPE_INNER_L3_IPV4, "INNER_L3_IPV4", },
        { RTE_PTYPE_INNER_L3_IPV4_EXT, "INNER_L3_IPV4_EXT", },
        { RTE_PTYPE_INNER_L3_IPV6, "INNER_L3_IPV6", },
        { RTE_PTYPE_INNER_L3_IPV4_EXT_UNKNOWN, "INNER_L3_IPV4_EXT_UNKNOWN", },
        { RTE_PTYPE_INNER_L3_IPV6_EXT, "INNER_L3_IPV6_EXT", },
        { RTE_PTYPE_INNER_L3_IPV6_EXT_UNKNOWN, "INNER_L3_IPV6_EXT_UNKNOWN", },
        { RTE_PTYPE_INNER_L4_TCP, "INNER_L4_TCP", },
        { RTE_PTYPE_INNER_L4_UDP, "INNER_L4_UDP", },
        { RTE_PTYPE_INNER_L4_FRAG, "INNER_L4_FRAG", },
        { RTE_PTYPE_INNER_L4_SCTP, "INNER_L4_SCTP", },
        { RTE_PTYPE_INNER_L4_ICMP, "INNER_L4_ICMP", },
        { RTE_PTYPE_INNER_L4_NONFRAG, "INNER_L4_NONFRAG", },
};

static int
check_ptype(uint16_t portid)
{
        unsigned nb;

        uint32_t ptype_mask = RTE_PTYPE_L3_MASK;

        nb = (unsigned) rte_eth_dev_get_supported_ptypes(portid, RTE_PTYPE_ALL_MASK, NULL, 0);
        if (nb) {
                uint32_t ptypes[nb];

                nb = rte_eth_dev_get_supported_ptypes(portid, RTE_PTYPE_ALL_MASK,
                                                      ptypes, nb);
                for (unsigned i = 0; i < nb; ++i) {
                        for (unsigned j = 0; j < RTE_DIM(ptype_info); j++) {
                                if (ptypes[i] & ptype_info[j].type) {
                                        fprintf(stdout, "port %u is supported %s\n",
                                                portid,
                                                ptype_info[j].name);
                                        break;
                                }
                        }
                }
        }
        return 0;
}

static void
dump_ptype(uint32_t ptype)
{
#if 0
        for (unsigned i = 0; i < RTE_DIM(ptype_info); i++) {
                uint32_t type = ptype_info[i].type & ptype;

                if (type == ptype_info[i].type)
                        fprintf(stdout, "ptype is %s\n", ptype_info[i].name);
        }
#endif
}

static void
dump_mac(uint16_t portid)
{
        struct ether_addr addr;

        rte_eth_macaddr_get(portid, &addr);
        fprintf(stdout,
                "Port:%u Mac "
                "%02"PRIx8":%02"PRIx8":%02"PRIx8
                ":%02"PRIx8":%02"PRIx8":%02"PRIx8"\n",
                portid,
                addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
                addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]);
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
                        .rss_hf =  ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
                },
        },
        .txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
        .intr_conf = {
                .lsc = 1,
        },
};

static void
dump_pci_dev(const struct rte_pci_device *dev)
{
        if (dev) {
                fprintf(stdout,
                        PCI_PRI_FMT,
                        dev->addr.domain,
                        dev->addr.bus,
                        dev->addr.devid,
                        dev->addr.function);
                fprintf(stdout,
                        " - vendor:%x device:%x\n",
                        dev->id.vendor_id,
                        dev->id.device_id);

                for (unsigned i = 0;
                     i != RTE_DIM(dev->mem_resource);
                     i++) {
                        fprintf(stdout,
                                "   %16.16"PRIx64" %16.16"PRIx64"\n",
                                dev->mem_resource[i].phys_addr,
                                dev->mem_resource[i].len);
                }
        }
}

static void
dump_rxconf(const struct rte_eth_rxconf *conf)
{
        fprintf(stdout, "RxConf offloads: %16.16"PRIx64"\n",
                conf->offloads);
}

static void
dump_txconf(const struct rte_eth_txconf *conf)
{
        fprintf(stdout, "TxConf offloads: %16.16"PRIx64"\n",
                conf->offloads);
}

static void
dump_eth_dev_info(uint16_t portid,
                  const struct rte_eth_dev_info *info)
{
        fprintf(stdout,
                "port:%u driver:%s if_index:%u\n",
                portid, info->driver_name, info->if_index);
        fprintf(stdout,
                "Rx capa port:0x%16.16"PRIx64" queue:0x%16.16"PRIx64"\n",
                info->rx_offload_capa,
                info->rx_queue_offload_capa);
        fprintf(stdout,
                "Tx capa port:0x%16.16"PRIx64" queue:0x%16.16"PRIx64"\n",
                info->tx_offload_capa,
                info->tx_queue_offload_capa);
        {
                uint64_t offloads = info->flow_type_rss_offloads;

                offloads &= ETH_RSS_PROTO_MASK;
                dump_rss_offloads("flow_type_rss_offloads", offloads);

        }
        dump_speed_capa(info->speed_capa);
        dump_pci_dev(info->pci_dev);

        dump_rxconf(&info->default_rxconf);
        dump_txconf(&info->default_txconf);
}

static void
dump_eth_queue(uint16_t portid,
               uint16_t queueid)
{
        struct rte_eth_rxq_info rx_qinfo;
        struct rte_eth_txq_info tx_qinfo;

        if (rte_eth_rx_queue_info_get(portid, queueid, &rx_qinfo))
                fprintf(stderr, "failed rte_eth_rx_queue_info_get\n");
        else {
                fprintf(stdout, "Port:%u Rx Queue:%u info\n", portid, queueid);
                fprintf(stdout, "mp:%p scattered_rx:%u nb_desc:%u\n",
                        rx_qinfo.mp, rx_qinfo.scattered_rx, rx_qinfo.nb_desc);
                dump_rxconf(&rx_qinfo.conf);
        }
        if (rte_eth_tx_queue_info_get(portid, queueid, &tx_qinfo))
                fprintf(stderr, "failed rte_eth_tx_queue_info_get\n");
        else {
                fprintf(stdout, "Port:%u Tx Queue:%u info\n", portid, queueid);
                fprintf(stdout, "nb_desc:%u\n", tx_qinfo.nb_desc);
                dump_txconf(&tx_qinfo.conf);
        }
}

#if 0
static const struct rte_eth_rxconf RxConf = {
        .rx_thresh = {
        },
        .rx_free_thresh,
        .rx_drop_en = 0,,
        .rx_deferred_start = 0,
        .offloads,
};
#endif

static void
init_slave_port(uint16_t portid,
                struct rte_mempool *mp,
                struct ether_addr *mac)
{

        if (portid >= rte_eth_dev_count()) {
                rte_exit(EXIT_FAILURE,
                         "number of ports %u\n", rte_eth_dev_count());
        }

        uint16_t nb_rxd = RTE_RX_DESC_DEFAULT;
        uint16_t nb_txd = RTE_TX_DESC_DEFAULT;
        struct rte_eth_dev_info info;
        struct rte_eth_rss_conf rss_conf;
        unsigned nb_ports = 1;	/* slave port is always One port. */

        if (rte_eth_dev_configure(portid, nb_ports, nb_ports, &PortConf))
                rte_exit(EXIT_FAILURE,
                         "port %u dev_configure\n", portid);

        if (rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd, &nb_txd))
                rte_exit(EXIT_FAILURE,
                         "port %u adjust_nb_rx_tx_desc\n", portid);

        rte_eth_dev_info_get(portid, &info);
        dump_eth_dev_info(portid, &info);

        info.default_rxconf.offloads |= DEV_RX_OFFLOAD_CHECKSUM;

        for (unsigned q = 0; q < nb_ports; q++) {
                if (rte_eth_rx_queue_setup(portid,
                                           q,
                                           nb_rxd,
                                           rte_eth_dev_socket_id(portid),
                                           &info.default_rxconf,
                                           mp))
                        rte_exit(EXIT_FAILURE,
                                 "port:%u rx_queue_setup\n",
                                 portid);

                if (rte_eth_tx_queue_setup(portid,
                                           q,
                                           nb_txd,
                                           rte_eth_dev_socket_id(portid),
                                           &info.default_txconf))
                        rte_exit(EXIT_FAILURE,
                                 "port:%u tx_queue_setup\n",
                                 portid);

                dump_eth_queue(portid, q);
        }
        check_ptype(portid);
#if 0
        if (rte_eth_dev_start(portid) < 0)
                rte_exit(EXIT_FAILURE, "port %u start\n", portid);
#endif

        dump_mac(portid);
        if (mac)
                rte_eth_macaddr_get(portid, mac);

        sleep(1);

#if 0
        if (rte_eth_dev_rss_hash_conf_get(portid, &rss_conf))
                rte_exit(EXIT_FAILURE,
                         "port:%u rss_hash_conf_get\n", portid);
        dump_rss_offloads("rss_hf", rss_conf.rss_hf);
#endif
        fprintf(stdout, "---\n");
}

#if 0
static int
wrap_rte_eth_bond_create(const char *name,
                         uint8_t mode,
                         uint8_t socket_id)
{
        char devargs[52];
        uint16_t port_id;
        int ret;

        if (!name)
                return -EINVAL;

        ret = snprintf(devargs, sizeof(devargs),
                       "driver=net_bonding,mode=%d,socket_id=%d",
                       mode, socket_id);
        if (ret < 0 || ret >= (int) sizeof(devargs))
                return -ENOMEM;

        ret = rte_vdev_init(name, devargs);
        if (ret)
                return ret;

        ret = rte_eth_dev_get_port_by_name(name, &port_id);
        if (ret)
                return ret;

      return port_id;
}
#endif

static void
init_bond_port(struct bond_info *bond_info)
{
        int bond_port;
        uint16_t nb_rxd = RTE_RX_DESC_DEFAULT;
        uint16_t nb_txd = RTE_TX_DESC_DEFAULT;
        struct rte_eth_dev_info eth_info;
        uint8_t socket_id = rte_eth_dev_socket_id(bond_info->portids[bond_info->primary]);
        int ret;
        char name[32];

        snprintf(name, sizeof(name), "net_bonding#%u", bond_info->cookie);

        fprintf(stderr, "bond socket:%u\n", socket_id);

        bond_port = rte_eth_bond_create(name,
                                        BONDING_MODE_ACTIVE_BACKUP,
                                        socket_id);
        if (bond_port < 0)
                rte_exit(EXIT_FAILURE,
                         "Failed to create bond port: %d %s\n",
                         -bond_port,
                         rte_strerror(-bond_port));

        ret = rte_eth_dev_configure(bond_port,
                                    bond_info->nb_rxq,
                                    bond_info->nb_txq,
                                    &PortConf);
        if (ret)
                rte_exit(EXIT_FAILURE,
                         "bond dev_configure: %d %s\n",
                         -ret,
                         rte_strerror(-ret));

        fprintf(stderr, "---done:%d\n", __LINE__);

        if (rte_eth_dev_adjust_nb_rx_tx_desc(bond_port, &nb_rxd, &nb_txd))
                rte_exit(EXIT_FAILURE, "bond dev_adjust_nb_rx_tx_desc\n");

        fprintf(stderr, "---done:%d\n", __LINE__);

        rte_eth_dev_info_get(bond_port, &eth_info);
        for (unsigned q = 0; q < bond_info->nb_rxq; q++) {
                if (rte_eth_rx_queue_setup(bond_port,
                                           q,
                                           nb_rxd,
                                           rte_eth_dev_socket_id(bond_port),
                                           NULL,
                                           bond_info->mp))
                        rte_exit(EXIT_FAILURE,
                                 "bond queue:%u rx_queue_setup\n",
                                 q);
                bond_info->rx_cnt[q] = UINT64_C(0);
        }

        fprintf(stderr, "---done:%d\n", __LINE__);

        for (unsigned q = 0; q < bond_info->nb_txq; q++) {
                if (rte_eth_tx_queue_setup(bond_port,
                                           q,
                                           nb_txd,
                                           rte_eth_dev_socket_id(bond_port),
                                           NULL))
                        rte_exit(EXIT_FAILURE,
                                 "bond queue:%u tx_queue_setup\n",
                                 q);

                bond_info->tx_cnt[q] = UINT64_C(0);

                dump_eth_queue(bond_port, q);
        }

        fprintf(stderr, "---done:%d\n", __LINE__);

        for (unsigned i = 0; i < bond_info->nb_eth; i++) {
                if (rte_eth_bond_slave_add(bond_port, bond_info->portids[i]))
                        rte_exit(EXIT_FAILURE,
                                 "bond slave:%u slave_add\n",
                                 bond_info->portids[i]);
        }

        fprintf(stderr, "---done:%d\n", __LINE__);

        if (rte_eth_bond_mac_address_set(bond_port, &bond_info->mac))
                rte_exit(EXIT_FAILURE, "bond port %u mac reset\n", bond_port);

        fprintf(stderr, "---done:%d\n", __LINE__);

        if (rte_eth_bond_primary_set(bond_port,
                                     bond_info->portids[bond_info->primary]))
                rte_exit(EXIT_FAILURE, "bond primary set\n");

        fprintf(stderr, "---done:%d\n", __LINE__);

        dump_mac(bond_port);
        rte_eth_promiscuous_enable(bond_port);

        fprintf(stderr, "---done:%d\n", __LINE__);

        if (rte_eth_dev_start(bond_port))
                rte_exit(EXIT_FAILURE, "bond port %u start\n", bond_port);

        fprintf(stderr, "---done:%d\n", __LINE__);

        fprintf(stderr,
                "Primary %d\n",
                rte_eth_bond_primary_get(bond_port));

        rte_delay_ms(200);
        bond_info->id = bond_port;
}

static void
queue_start(uint16_t port,
            unsigned nb_q)
{
        for (uint16_t q = 0; q < nb_q; q++) {
                if (rte_eth_dev_rx_queue_start(port, q))
                        rte_exit(EXIT_FAILURE, "port:%u queue:%u Rx start\n",
                                 port, q);

                if (rte_eth_dev_tx_queue_start(port, q))
                        rte_exit(EXIT_FAILURE, "port:%u queue:%u Tx start\n",
                                 port, q);
        }
}

static struct bond_info *
create_bond(struct rte_mempool *mp,
            unsigned cookie,
            unsigned nb_rxq,
            unsigned nb_txq,
            unsigned primary,
            unsigned nb_eth,
            const uint16_t portids[])
{
        struct bond_info *info = NULL;
        size_t size = sizeof(*info) + sizeof(uint16_t) * nb_eth;

        if (primary < nb_eth) {
                info = malloc(size);
                if (info) {

                        memset(info, -1, size);
                        info->mp = mp;
                        info->ipaddr = random();
                        info->cookie = cookie;
                        info->nb_rxq = nb_rxq;
                        info->nb_txq = nb_txq;
                        info->primary = primary;
                        info->nb_eth = nb_eth;
                        for (unsigned i = 0; i < 16; i++) {
                                info->rx_cnt[i] = UINT64_C(0);
                                info->tx_cnt[i] = UINT64_C(0);
                                info->rx_cksum_l3[i] = UINT64_C(0);
                                info->rx_cksum_l4[i] = UINT64_C(0);
                        }

                        for (unsigned i = 0; i < nb_eth; i++) {
                                struct ether_addr *mac = NULL;

                                info->portids[i] = portids[i];
                                if (i == primary) {
                                        mac = &info->mac;
                                        info->id = info->portids[i];
                                }
                                init_slave_port(info->portids[i], mp, mac);
                        }
#if 1
                        init_bond_port(info);
#endif
                }
        }
        return info;
}

static inline int
validate_tx_offload(const struct rte_mbuf *m)
{
        uint64_t ol_flags = m->ol_flags;

        /* Does packet set any of available offloads? */
        if (!(ol_flags & PKT_TX_OFFLOAD_MASK))
                return 0;

        if (ol_flags & PKT_TX_OUTER_IP_CKSUM) {
                uint64_t inner_l3_offset = m->l2_len;

                inner_l3_offset += m->outer_l2_len + m->outer_l3_len;

                /* Headers are fragmented */
                if (rte_pktmbuf_data_len(m) < inner_l3_offset + m->l3_len + m->l4_len) {
                        fprintf(stderr,
                                "%d Headers are fragmented\n",
                                __LINE__);
                        return -ENOTSUP;
                }
        }

        /* IP checksum can be counted only for IPv4 packet */
        if ((ol_flags & PKT_TX_IP_CKSUM) && (ol_flags & PKT_TX_IPV6)) {
                fprintf(stderr,
                        "%d IP checksum can be counted only for IPv4 packet\n",
                        __LINE__);
                return -EINVAL;
        }

        /* IP type not set when required */
        if (ol_flags & (PKT_TX_L4_MASK | PKT_TX_TCP_SEG))
                if (!(ol_flags & (PKT_TX_IPV4 | PKT_TX_IPV6))) {
                        fprintf(stderr,
                                "%d IP type not set when required\n",
                                __LINE__);
                        return -EINVAL;
                }

        /* Check requirements for TSO packet */
        if (ol_flags & PKT_TX_TCP_SEG)
                if ((m->tso_segsz == 0) ||
                                ((ol_flags & PKT_TX_IPV4) &&
                                 !(ol_flags & PKT_TX_IP_CKSUM))) {
                        fprintf(stderr,
                                "%d Check requirements for TSO packet\n",
                                __LINE__);
                        return -EINVAL;
                }

        /* PKT_TX_OUTER_IP_CKSUM set for non outer IPv4 packet. */
        if ((ol_flags & PKT_TX_OUTER_IP_CKSUM) &&
            !(ol_flags & PKT_TX_OUTER_IPV4)) {
                fprintf(stderr,
                        "%d PKT_TX_OUTER_IP_CKSUM set for non outer IPv4 packet\n",
                        __LINE__);
                return -EINVAL;
        }

        return 0;
}

#define IP_DEFTTL  64   /* from RFC 1340. */
#define IP_VERSION 0x40
#define IP_HDRLEN  0x05 /* default IP header length == five 32-bits words. */
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)

/*
 * in come frame
 */
static void
create_frame(struct rte_mbuf *m,
             const struct ether_addr *dmac,
             const struct ether_addr *smac,
             uint32_t daddr,
             uint16_t dport)
{
        uint64_t *body;
        uint64_t ol_flags = UINT64_C(0);
        struct udp_hdr *udp;
        struct ipv4_hdr *ip4;

        /* add body */
        {
                uint64_t *body;

                body = (uint64_t *) rte_pktmbuf_append(m, sizeof(body));
                *body = UINT64_C(0);
        }

        /* add udp header */
        {
                uint16_t sport = random();

                sport |= 0xc000;

                udp = (struct udp_hdr *) rte_pktmbuf_prepend(m, sizeof(*udp));
#if 1
                udp->src_port = rte_cpu_to_be_16(sport);
                udp->dst_port = rte_cpu_to_be_16(dport);
#else
                udp->src_port = 8086;
                udp->dst_port = 4004;
#endif
                udp->dgram_len = rte_cpu_to_be_16(rte_pktmbuf_pkt_len(m));
                udp->dgram_cksum = 0;

                m->l4_len = 0;
                ol_flags |= PKT_TX_UDP_CKSUM;
        }

        /* add ipv4 header */
        {
                uint32_t saddr = random();
                uint16_t len;

                ip4 = (struct ipv4_hdr *) rte_pktmbuf_prepend(m, sizeof(*ip4));
                ip4->version_ihl = IP_VHL_DEF;;
                ip4->type_of_service = 0;
                ip4->total_length = rte_cpu_to_be_16(rte_pktmbuf_pkt_len(m));
                ip4->packet_id = 0;
                ip4->fragment_offset = 0;
                ip4->time_to_live = IP_DEFTTL;
                ip4->next_proto_id = IPPROTO_UDP;
                ip4->hdr_checksum = 0;
#if 1
                ip4->src_addr = rte_cpu_to_be_32(saddr);
                ip4->dst_addr = rte_cpu_to_be_32(daddr);
#else
                ip4->src_addr = 1234;
                ip4->dst_addr = 5678;
#endif

                m->l3_len = sizeof(*ip4);
                ol_flags |= PKT_TX_IP_CKSUM;
                ol_flags |= PKT_TX_IPV4;
        }

        /* add ether header */
        {
                struct ether_hdr *hdr;

                hdr = (struct ether_hdr *) rte_pktmbuf_prepend(m, sizeof(*hdr));
                ether_addr_copy(dmac, &hdr->d_addr);
                ether_addr_copy(smac, &hdr->s_addr);
                hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

                m->l2_len = sizeof(*hdr);
        }
#if 1
        udp->dgram_cksum = rte_ipv4_phdr_cksum(ip4, ol_flags);
#endif
        m->ol_flags = ol_flags;

        if (validate_tx_offload(m)) {
                fprintf(stderr, "invalid Tx offload flags\n");
                dump_offload_flags(m->ol_flags);
                exit(0);
        }
}

static void
pkt_process(struct bond_info *binfo,
            unsigned rxq,
            unsigned txq,
            const struct ether_addr *dmac,
            uint32_t daddr)
{
        struct rte_mbuf *mbufs[64];
        unsigned nb;

        nb = rte_eth_rx_burst(binfo->id,
                              rxq,
                              mbufs,
                              RTE_DIM(mbufs));
        binfo->rx_cnt[rxq] += nb;

        if (nb) {
                for (unsigned i = 0; i < nb; i++) {
                        struct rte_mbuf *m = mbufs[i];
                        uint64_t ol_flags = m->ol_flags;
                        struct rte_net_hdr_lens hdr_lens;
                        uint32_t ptype = rte_net_get_ptype(m, &hdr_lens, RTE_PTYPE_ALL_MASK);

                        dump_ptype(ptype);

#if 1
                        if ((ol_flags & PKT_RX_IP_CKSUM_MASK) == PKT_RX_IP_CKSUM_BAD)
                                binfo->rx_cksum_l3[rxq] += 1;
                        if ((ol_flags & PKT_RX_L4_CKSUM_MASK) == PKT_RX_L4_CKSUM_BAD)
                                binfo->rx_cksum_l4[rxq] += 1;
#endif

                        rte_pktmbuf_free(m);
                }
        }

        if (!rte_pktmbuf_alloc_bulk(binfo->mp,
                                    mbufs,
                                    RTE_DIM(mbufs) / 16)) {
                unsigned num = RTE_DIM(mbufs) / 16;

                for (unsigned i = 0; i < num; i++) {
                        create_frame(mbufs[i],
                                     dmac,
                                     &binfo->mac,
                                     daddr,
                                     5050);
                }
                nb = rte_eth_tx_burst(binfo->id,
                                      txq,
                                      mbufs,
                                      num);

                binfo->tx_cnt[rxq] += nb;
#if 0
                if (nb != num)
                        fprintf(stderr, "failed to send:%zu\n", num - nb);
#endif

                for (; nb < num; nb++)
                        rte_pktmbuf_free(mbufs[nb]);
        }
}

static void
dump_stats_raw(const char *name,
               uint16_t id,
               uint16_t nb_q,
               const struct rte_eth_stats *stats)
{
        fprintf(stdout, "port %u %s stats dump\n", id, name);

        fprintf(stdout, "  ipackets:%"PRIu64"\n", stats->ipackets);
        fprintf(stdout, "  opackets:%"PRIu64"\n", stats->opackets);
        fprintf(stdout, "  ibytes:%"PRIu64"\n", stats->ibytes);
        fprintf(stdout, "  obytes:%"PRIu64"\n", stats->obytes);
        fprintf(stdout, "  imissed:%"PRIu64"\n", stats->imissed);
        fprintf(stdout, "  ierrors:%"PRIu64"\n", stats->ierrors);
        fprintf(stdout, "  oerrors:%"PRIu64"\n", stats->oerrors);
        fprintf(stdout, "  rx_nombuf:%"PRIu64"\n", stats->rx_nombuf);

#if 0
        for (unsigned i = 0; i < nb_q; i++) {
                fprintf(stdout, "    q_ipackets %u:%"PRIu64"\n", i, stats->q_ipackets[i]);
                fprintf(stdout, "    q_opackets %u:%"PRIu64"\n", i, stats->q_opackets[i]);
                fprintf(stdout, "    q_ibytes %u:%"PRIu64"\n", i, stats->q_ibytes[i]);
                fprintf(stdout, "    q_obytes %u:%"PRIu64"\n", i, stats->q_obytes[i]);
                fprintf(stdout, "    q_errors %u:%"PRIu64"\n", i, stats->q_errors[i]);
        }
#endif
}

static void
dump_stats(struct bond_info *binfo)
{
        struct rte_eth_stats stats;

        if (rte_eth_stats_get(binfo->id, &stats)) {
                fprintf(stderr, "failed to get stats: %u\n", binfo->id);
                return;
        }
        dump_stats_raw("Bond", binfo->id, binfo->nb_rxq, &stats);

        for (unsigned i = 0; i < binfo->nb_eth; i++) {
                if (rte_eth_stats_get(binfo->portids[i], &stats)) {
                        fprintf(stderr, "failed to get stats: %u\n", binfo->portids[i]);
                } else {
                        dump_stats_raw("Slave", binfo->portids[i], binfo->nb_rxq, &stats);
                }
        }

        for (unsigned i = 0; i < binfo->nb_rxq; i++) {
                fprintf(stdout,
                        "queue:%u rx_cnt:%"PRIu64" tx_cnt:%"PRIu64
                        " L3 cksum:%"PRIu64" L4 cksum:%"PRIu64"\n",
                        i,
                        binfo->rx_cnt[i],
                        binfo->tx_cnt[i],
                        binfo->rx_cksum_l3[i],
                        binfo->rx_cksum_l4[i]);
        }
}

static int
link_check(uint16_t id)
{
        struct rte_eth_link eth_link;

        rte_eth_link_get(id, &eth_link);
        if (eth_link.link_status == ETH_LINK_DOWN) {
                fprintf(stderr, "port %u is down\n", id);
                return -1;
        }
        return 0;
}

int
main(int ac,
     char *av[])
{
        int ret;
        struct rte_mempool *mp;
        struct bond_info *binfo[2];
        uint16_t eth_port_0[] = { 0, 1 };
        uint16_t eth_port_1[] = { 2, 3 };
        unsigned cookie = 0;

        ret = rte_eal_init(ac, av);
        if (ret < 0) {
                rte_strerror(rte_errno);
                return -1;
        }

        fprintf(stderr, "lcore count:%u master:%u\n",
                rte_lcore_count(), rte_get_master_lcore());

        mp = rte_pktmbuf_pool_create("MP",
                                     1024 * 256,
                                     256,
                                     0,
                                     RTE_MBUF_DEFAULT_BUF_SIZE,
                                     rte_socket_id());
        if (!mp)
                rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

        binfo[0] = create_bond(mp,
                               cookie++,
                               16,
                               16,
                               0,
                               RTE_DIM(eth_port_0), eth_port_0);

        binfo[1] = create_bond(mp,
                               cookie++,
                               16,
                               16,
                               0,
                               RTE_DIM(eth_port_1), eth_port_1);

        dump_mac(binfo[0]->id);
        dump_mac(binfo[1]->id);

#if 1
        fprintf(stderr,
                "Bond 0 Primary %d\n",
                rte_eth_bond_primary_get(binfo[0]->id));
        fprintf(stderr,
                "Bond 1 Primary %d\n",
                rte_eth_bond_primary_get(binfo[1]->id));
        sleep(3);

        rte_eth_dev_set_link_down(2);
        sleep(3);
        fprintf(stderr,
                "Bond 0 Primary %d\n",
                rte_eth_bond_primary_get(binfo[0]->id));
        fprintf(stderr,
                "Bond 1 Primary %d\n",
                rte_eth_bond_primary_get(binfo[1]->id));

        rte_eth_dev_set_link_up(2);
        sleep(3);
#endif

        fprintf(stderr,
                "Bond 0 Primary %d\n",
                rte_eth_bond_primary_get(binfo[0]->id));
        fprintf(stderr,
                "Bond 1 Primary %d\n",
                rte_eth_bond_primary_get(binfo[1]->id));
        sleep(3);

        unsigned loops = 0;
        while (!link_check(binfo[0]->id) &&
               !link_check(binfo[1]->id)) {
                for (unsigned q = 0; q < 16; q++) {

                        pkt_process(binfo[0], q, q,
                                    &binfo[1]->mac,
                                    binfo[1]->ipaddr);

                        pkt_process(binfo[1], q, q,
                                    &binfo[0]->mac,
                                    binfo[0]->ipaddr);
                }

                loops++;
                if (loops % 10000 == 0) {
                        dump_stats(binfo[0]);
                        dump_stats(binfo[1]);
                }
        }

        rte_exit(0, "done, bye\n");
        return 0;
}
