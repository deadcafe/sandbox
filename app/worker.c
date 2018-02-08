#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <rte_ip.h>
#include <rte_random.h>
#include <rte_ether.h>
#include <rte_hash_crc.h>

#include <dc_fw_log.h>
#include <dc_thread.h>
#include <dc_port.h>
#include <dc_addon.h>
#include <dc_mbuf.h>

#include "mbuf_ext.h"
#include "jbuff.h"
#include "termination.h"

/*
 *
 */
static inline unsigned
route_mbuf(struct dc_task_s *task,
           struct rte_mbuf *m)
{
        unsigned ret;

        ret = rte_hash_crc_4byte(m->hash.rss, 1234) % (task->nb_out_ports - 2);

        return ret + 2;
}

static inline void
rx_packet_proc(struct dc_task_s *task,
               struct rte_mbuf *m,
               struct term_key_v4_s *key,
               uint64_t ts)
{
        uint64_t ol_flags = m->ol_flags;
        uint32_t ptype = m->packet_type;
        struct mbuf_ext_s *ext = mbuf2ext(m);

        ext->ptype = 0;

        if (RTE_ETH_IS_TUNNEL_PKT(ptype)) {
                DC_FW_ERR("tunnel packet");
                return;
        }

        if (!RTE_ETH_IS_IPV4_HDR(ptype)) {
                DC_FW_ERR("not IPv4 packet");
                return;
        }

        if (!(ol_flags & PKT_RX_RSS_HASH)) {
                DC_FW_ERR("exception IPv4 packet");
                return;
        }

        if (ol_flags & PKT_RX_IP_CKSUM_MASK == PKT_RX_IP_CKSUM_BAD) {
                DC_FW_ERR("Bad IP checksum packet");
                return;
        }

        if (ol_flags & PKT_RX_L4_CKSUM_MASK == PKT_RX_L4_CKSUM_BAD) {
                DC_FW_ERR("Bad L4 checksum packet");
                return;
        }

        ext->ptype = rte_net_get_ptype(m, &ext->hdr_lens, RTE_PTYPE_ALL_MASK);
        if (ext->ptype & (RTE_PTYPE_L3_IPV4 | RTE_PTYPE_L4_UDP)
            == (RTE_PTYPE_L3_IPV4 | RTE_PTYPE_L4_UDP)) {
                struct ipv4_hdr *ipv4;
                struct udp_hdr *udp;

                ipv4 = rte_pktmbuf_mtod_offset(m,
                                               struct ipv4_hdr *,
                                               ext->hdr_lens.l2_len);
                udp = rte_pktmbuf_mtod_offset(m,
                                              struct udp_hdr *,
                                              ext->hdr_lens.l2_len + ext->hdr_lens.l3_len);
                key->src_ip   = ipv4->src_addr;
                key->dst_ip   = ipv4->dst_addr;
                key->src_port = udp->src_port;
                key->dst_port = udp->dst_port;
                key->zero_pad = 0;
        } else {
                memset(key, 0, sizeof(*key));
        }
        ext->rcv_tsc = ts;
        ext->rcv_thread_id = task->th->thread_id;
}

static inline void
prefetch_mbuf(struct rte_mbuf *m)
{
        char *p = (char *) m;

        rte_prefetch0(p);
        rte_prefetch0(p + 128);
        rte_prefetch0(p + 256);
}

/*****************************************************************************
 *	ether receive task
 *****************************************************************************/
static unsigned
EthRecv_entry(struct dc_thread_s *th,
              struct dc_task_s *task,
              uint64_t tsc)
{
        struct rte_mbuf *buff[32];
        int nb;

        nb = dc_port_recv(task->in_port, buff, RTE_DIM(buff));
        if (nb) {
                struct term_key_v4_s keys[nb];
                struct term_key_v4_s *keys_p[nb];
                struct term_info_s *terms[nb];
                uint64_t hits = UINT64_C(0);

                for (int i = 0; i < nb; i++) {
                        keys_p[i] = &keys[i];
                        rx_packet_proc(task, buff[i], keys_p[i], tsc++);
                }

                find_term(task->addon_task_ext[0],
                          (const struct term_key_v4_s **) keys_p,
                          nb,
                          &hits,
                          terms);

                for (int i = 0; i < nb; i++) {
                        int next_hop = 0;
                        struct mbuf_ext_s *ext = mbuf2ext(buff[i]);

                        if ((hits & (UINT64_C(1)) << i) &&
                            terms[i]->worker_id >= 0) {
                                next_hop = terms[i]->worker_id + 2;

                                ext->term = terms[i];
                                ext->ctx = terms[i]->ctx;
                                ext->cookie = terms[i]->serial_nb;
                        }
                        dc_port_send(task->out_ports[next_hop], buff[i]);
                }
        }

        return nb;
}

static int
EthRecv_init(struct dc_conf_db_s *db,
             struct dc_thread_s *th,
             struct dc_task_s *task)
{
        task->addon_task_ext[0] = find_term_db();
        if (!task->addon_task_ext) {
                fprintf(stderr, "not found TermDB\n");
                return -1;
        }

        return 0;
}

static const struct dc_addon_s EthRecv_addon = {
        .name = "EthRecv",
        .task_init =  EthRecv_init,
        .task_entry = EthRecv_entry,
};

static struct dc_addon_constructor_s EthRecv_constructor = {
        .addon = &EthRecv_addon,
};

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

static inline int
tx_packet_proc(struct dc_task_s *task,
               struct rte_mbuf *m)
{
        struct ether_hdr *eth_hdr;
        struct ipv4_hdr *ipv4_hdr;
        struct udp_hdr *udp_hdr;
        struct mbuf_ext_s *ext = mbuf2ext(m);
        uint64_t ol_flags = UINT64_C(0);


        ext->term->stats.nb_rcv += 1;
        ext->term->stats.by_rcv += rte_pktmbuf_pkt_len(m);

        m->tx_offload = UINT64_C(0);

        rte_pktmbuf_trim(m, 16);
        eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
        m->l2_len = sizeof(*eth_hdr);

        ipv4_hdr = rte_pktmbuf_mtod_offset(m,
                                           struct ipv4_hdr *,
                                           ext->hdr_lens.l2_len);
        ipv4_hdr->hdr_checksum = 0;
        m->l3_len = sizeof(*ipv4_hdr);
        ol_flags |= PKT_TX_IP_CKSUM;
        ol_flags |= PKT_TX_IPV4;

        udp_hdr = rte_pktmbuf_mtod_offset(m,
                                          struct udp_hdr *,
                                          ext->hdr_lens.l2_len + ext->hdr_lens.l3_len);
        udp_hdr->dgram_cksum = 0;
        m->l4_len = 0;
        ol_flags |= PKT_TX_UDP_CKSUM;

        udp_hdr->dgram_cksum = rte_ipv4_phdr_cksum(ipv4_hdr, ol_flags);

        m->ol_flags = ol_flags;
        m->packet_type = 0;
        if (validate_tx_offload(m)) {
                DC_FW_ERR("invalid Tx offload flags");
                return -1;
        }
        return 0;
}

static unsigned
EthSend_entry(struct dc_thread_s *th,
              struct dc_task_s *task,
              uint64_t now)
{
        unsigned ret = 0;
        bool retry = false;
        struct jbuff_s *jb = task->addon_task_ext[1];
        uint64_t timer = rte_get_tsc_hz();
        struct mbuf_ext_s *ext;

        /* buffering 1ms */
        timer >>= 10;

        do {
                struct rte_mbuf *buff[96];

                int nb = dc_port_recv(task->in_port, buff, RTE_DIM(buff));
                for (int i = 0; i < nb; i++) {
                        if (i+i < nb)
                                prefetch_mbuf(buff[i + 1]);
                        if (tx_packet_proc(task, buff[i])) {
                                fprintf(stderr, "invalid packet\n");
                                goto err;
                        } else {
                                struct mbuf_ext_s *ext = mbuf2ext(buff[i]);

                                ext->delay_tsc = (timer + i);
                                if (jbuff_enqueue(jb, ext)) {
                                        fprintf(stderr, "failed to en-queue jbuff\n");
                                err:
                                        rte_pktmbuf_free(buff[i]);
                                        buff[i] = NULL;
                                }
                        }
                }
                ret += nb;
        } while (retry);

        while ((ext = jbuff_dequeue(jb, now)) != NULL) {
                struct rte_mbuf *m = ext2mbuf(ext);

                ext->term->stats.nb_snd += 1;
                ext->term->stats.by_snd += rte_pktmbuf_pkt_len(m);

                dc_port_send(task->out_ports[1], m);
        }
        return ret;
}

static int
EthSend_init(struct dc_conf_db_s *db,
             struct dc_thread_s *th,
             struct dc_task_s *task)
{
        task->addon_task_ext[0] = find_term_db();
        if (!task->addon_task_ext[0]) {
                fprintf(stderr, "not found TermDB\n");
                return -1;
        }

        task->addon_task_ext[1] = jbuff_create();
        if (!task->addon_task_ext[1]) {
                fprintf(stderr, "failed to create jbuff\n");
                return -1;
        }

        return 0;
}

static const struct dc_addon_s EthSend_addon = {
        .name = "EthSend",
        .task_init =  EthSend_init,
        .task_entry = EthSend_entry,
};

static struct dc_addon_constructor_s EthSend_constructor = {
        .addon = &EthSend_addon,
};

/*****************************************************************************
 *	constructor
 *****************************************************************************/
RTE_INIT(addon_reg);
static void
addon_reg(void)
{
        dc_addon_register(&EthRecv_constructor);
        dc_addon_register(&EthSend_constructor);
}
