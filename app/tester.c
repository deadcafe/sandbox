#include <sys/queue.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <inttypes.h>

#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <rte_ip.h>
#include <rte_random.h>

#include <dc_fw_log.h>
#include <dc_thread.h>
#include <dc_port.h>
#include <dc_addon.h>

#include "termination.h"

struct test_term_s {
        struct {
                uint16_t src;
                uint16_t dst;
        } udp;

        struct {
                uint32_t src;
                uint32_t dst;
        } ip;

        struct {
                struct ether_addr src;
                struct ether_addr dst;
        } eth;
};

struct test_term_db_s {
        unsigned cur;
        unsigned nb_terms;

        struct term_db_s *terget_db;
        struct test_term_s terms[0];
};


static struct test_term_db_s *
create_test_term_db(unsigned th,
                    unsigned nb_ips,
                    unsigned nb_ports,
                    unsigned nb_term_in_ctx,
                    unsigned nb_workers)
{
        struct test_term_db_s *db = malloc(sizeof(*db) +
                                      (nb_ips * nb_ports) * sizeof(struct test_term_s));

        if (db) {
                char str[32];
                struct ether_addr edst;
                uint32_t ip_base;
                uint16_t port_base = 10001;
                struct test_term_s *term = db->terms;
                uint32_t idst;
                unsigned worker_id = 0;
                unsigned nb_bind_term = 0;
                struct context_info_s *ctx = NULL;

                db->nb_terms = nb_ips * nb_ports;
                db->cur = 0;

                eth_random_addr(edst.addr_bytes);

                snprintf(str, sizeof(str), "%u.0.0.1", th);
                inet_pton(AF_INET, str, &ip_base);
                ip_base = rte_be_to_cpu_32(ip_base);

                snprintf(str, sizeof(str), "192.168.0.1");
                inet_pton(AF_INET, str, &idst);

                db->terget_db = find_term_db();

                for (unsigned i = 0; i < nb_ips; i++) {
                        for (unsigned j = 0; j < nb_ports; j++) {
                                struct term_info_s *tinfo;

                                term->ip.src = rte_cpu_to_be_32(ip_base + i);
                                term->ip.dst = idst;

                                term->udp.src = rte_cpu_to_be_16(port_base + j);
                                term->udp.dst = rte_cpu_to_be_16(1234);

                                eth_random_addr(term->eth.src.addr_bytes);
                                term->eth.dst = edst;

                                tinfo = assign_term(db->terget_db,
                                                    4,
                                                    &term->ip.src,
                                                    term->udp.src,
                                                    term->udp.src);
                                if (!tinfo) {
                                        fprintf(stderr,
                                                "failed to assign term\n");
                                        return NULL;
                                }

                                if (!ctx || nb_bind_term >= nb_term_in_ctx) {
                                        ctx = assign_context(db->terget_db,
                                                             0, worker_id);
                                        if (!ctx) {
                                                fprintf(stderr,
                                                        "failed to assign ctx\n");
                                                return NULL;
                                        }
                                        nb_bind_term = 0;
                                        worker_id += 1;
                                        if (worker_id >= nb_workers)
                                                worker_id = 0;
                                }

                                if (bind_term_context(tinfo, ctx)) {
                                        fprintf(stderr, "failed to bind ctx\n");
                                        return NULL;
                                }
                                nb_bind_term++;
                                term++;
                        }
                }
        }
        return db;
}

/*****************************************************************************
 *	ether send task
 *****************************************************************************/
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

static void
create_frame(struct rte_mbuf *m,
             const struct test_term_s *term)
{
        uint64_t *body;
        uint64_t ol_flags = UINT64_C(0);
        struct udp_hdr *udp;
        struct ipv4_hdr *ip4;

        /* add body */
        {
                uint64_t *body;

                body = (uint64_t *) rte_pktmbuf_append(m, 192);
                *body = UINT64_C(0);
        }

        /* add udp header */
        {
                uint16_t sport = 5678;

                sport |= 0xc000;

                udp = (struct udp_hdr *) rte_pktmbuf_prepend(m, sizeof(*udp));
#if 1
                udp->src_port = term->udp.src;
                udp->dst_port = term->udp.dst;
#else
                udp->src_port = 8086;
                udp->dst_port = 4004;
#endif
                udp->dgram_len = rte_cpu_to_be_16(rte_pktmbuf_pkt_len(m));
                udp->dgram_cksum = 0;

#if 0
                m->l4_len = sizeof(*udp);
#else
                m->l4_len = 0;
#endif
                ol_flags |= PKT_TX_UDP_CKSUM;
        }

        /* add ipv4 header */
        {
                uint32_t saddr = 1234;
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
                ip4->src_addr = term->ip.src;
                ip4->dst_addr = term->ip.dst;
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
                ether_addr_copy(&term->eth.dst, &hdr->d_addr);
                ether_addr_copy(&term->eth.src, &hdr->s_addr);
                hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

                m->l2_len = sizeof(*hdr);
        }
#if 1
        udp->dgram_cksum = rte_ipv4_phdr_cksum(ip4, ol_flags);
#endif
        m->ol_flags = ol_flags;

        if (validate_tx_offload(m)) {
                fprintf(stderr, "invalid Tx offload flags\n");
        }
}

static unsigned
Tester_entry(struct dc_thread_s *th,
             struct dc_task_s *task,
             uint64_t tsc)
{
        struct rte_mbuf *buff[task->burst_size];
        struct test_term_db_s *db = task->addon_task_ext[0];

        for (int i = 0; i < RTE_DIM(buff); i++) {

                uint64_t v = rte_rand() % db->nb_terms;
                struct test_term_s *term = &db->terms[v];

                buff[i] = rte_pktmbuf_alloc(th->mp);
                if (!buff[i])
                        break;

                create_frame(buff[i], term);
                dc_port_send(task->out_ports[0], buff[i]);
        }
        return RTE_DIM(buff);
}

static int
init_addr(struct dc_conf_db_s *db,
          struct dc_thread_s *th,
          struct dc_task_s *task)
{

        task->addon_task_ext[0] = create_test_term_db(th->lcore_id,
                                                      128,
                                                      256,
                                                      128,
                                                      8);
        if (!task->addon_task_ext) {
                fprintf(stderr, "failed to create test db\n");
                return -1;
        }
        return 0;
}

static unsigned
TesterRecv_entry(struct dc_thread_s *th,
                 struct dc_task_s *task,
                 uint64_t tsc)
{
        struct rte_mbuf *buff[task->burst_size];
        int nb;
        int ret = 0;

        (void) th;

        do {
                nb = dc_port_recv(task->in_port, buff, RTE_DIM(buff));
                for (int i = 0; i < nb; i++)
                        dc_port_send(task->out_ports[0], buff[i]);
                ret += nb;
        } while (nb == task->burst_size);
        return ret;
}

static unsigned
entry_nothing_todo(struct dc_thread_s *th,
                   struct dc_task_s *task,
                   uint64_t tsc)
{
        (void) th;
        (void) task;
        return 0;
}

static int
init_nothing_todo(struct dc_conf_db_s *db,
                  struct dc_thread_s *th,
                  struct dc_task_s *task)
{
        (void) db;
        (void) th;
        (void) task;

        return 0;
}

/*****************************************************************************
 *
 *****************************************************************************/
static const struct dc_addon_s TesterRecv_addon = {
        .name = "TesterRecv",
        .task_init =  init_nothing_todo,
#if 1
        .task_entry = TesterRecv_entry,
#else
        .task_entry = entry_nothing_todo,
#endif

};

static struct dc_addon_constructor_s TesterRecv_constructor = {
        .addon = &TesterRecv_addon,
};

static const struct dc_addon_s TesterSend_addon = {
        .name = "TesterSend",
        .task_init =  init_addr,
#if 1
        .task_entry = Tester_entry,
#else
        .task_entry = entry_nothing_todo,
#endif
};

static struct dc_addon_constructor_s TesterSend_constructor = {
        .addon = &TesterSend_addon,
};

/*****************************************************************************
 *	constructor
 *****************************************************************************/
RTE_INIT(addon_reg);
static void
addon_reg(void)
{
        dc_addon_register(&TesterRecv_constructor);
        dc_addon_register(&TesterSend_constructor);
}
