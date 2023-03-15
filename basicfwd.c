/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <inttypes.h>
#include <sys/time.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <hiredis/hiredis.h>

#define RX_RING_SIZE 16384
#define TX_RING_SIZE 1024
#define NUM_MBUFS 262143
#define MBUF_CACHE_SIZE 500
#define BURST_SIZE 32768
#define MAX_LCORE_NUM 16

// 考虑开启RSS模式
static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.offloads = DEV_RX_OFFLOAD_CHECKSUM,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IP,
		},
	},
};

uint16_t lcore_to_queue[128];
uint16_t nb_rx[MAX_LCORE_NUM];
struct rte_mbuf *bufs[MAX_LCORE_NUM][BURST_SIZE];
redisContext *c[MAX_LCORE_NUM];
redisReply *reply[MAX_LCORE_NUM];

/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = rte_lcore_count(), tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	unsigned lcore_id;
	uint16_t q = 0;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			DEV_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up RX queue for every worker. */
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
		lcore_to_queue[lcore_id] = q;
		q++;
	}

	/* Allocate and set up RX queue for main lcore. */
	retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
			rte_eth_dev_socket_id(port), NULL, mbuf_pool);
	if (retval < 0)
		return retval;
	lcore_to_queue[rte_lcore_id()] = q;

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	if (retval != 0)
		return retval;

	return 0;
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static int
lcore_main(__rte_unused void *arg)
{
	uint16_t port = 0;
	uint16_t queue_id = lcore_to_queue[rte_lcore_id()];
	
	/* Get burst of RX packets. */
	nb_rx[queue_id] = rte_eth_rx_burst(port, queue_id,
			bufs[queue_id], BURST_SIZE);

	if (nb_rx[queue_id] > 0) {
		printf("Core %u: %u\n", rte_lcore_id(), nb_rx[queue_id]);
	}

	return 0;
}

static int
lcore_redis(__rte_unused void *arg)
{
	uint16_t queue_id = lcore_to_queue[rte_lcore_id()];

	for (uint16_t buf = 0; buf < nb_rx[queue_id]; buf++) {
		char *pktbuf = rte_pktmbuf_mtod(bufs[queue_id][buf], char *);
		char key[16];
		key[0] = pktbuf[0x17];
		for (int k = 0x1a; k < 0x26; k++) {
			key[k - 0x19] = pktbuf[k];
		}
		reply[queue_id] = redisCommand(c[queue_id], "SET %b 1", key, 13);
		freeReplyObject(reply[queue_id]);
	}

	for (uint16_t buf = 0; buf < nb_rx[queue_id]; buf++) {
		rte_pktmbuf_free(bufs[queue_id][buf]);
	}

	return 0;
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	uint16_t portid = 0;
	unsigned lcore_id;

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * 1,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initialize all ports. */
	if (port_init(portid, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
				portid);

	for (int i = 0; i < rte_lcore_count(); i++) {
		c[i] = redisConnect("127.0.0.1", 6379);
	}

	/* Run until the application is quit or killed. */
	for (;;) {
		struct timeval start,end;
		gettimeofday(&start, NULL );

		/* call lcore_main() on every worker lcore */
		RTE_LCORE_FOREACH_WORKER(lcore_id) {
			rte_eal_remote_launch(lcore_main, NULL, lcore_id);
		}

		/* call it on main lcore too */
		lcore_main(NULL);

		rte_eal_mp_wait_lcore();

		RTE_LCORE_FOREACH_WORKER(lcore_id) {
			rte_eal_remote_launch(lcore_redis, NULL, lcore_id);
		}

		/* call it on main lcore too */
		lcore_redis(NULL);

		rte_eal_mp_wait_lcore();

		int my_sum = 0;
		for (int i = 0; i < rte_lcore_count(); i++) {
			my_sum += nb_rx[i];
		}
		if (my_sum > 0) {
			gettimeofday(&end, NULL );
			long timeuse =1000000 * ( end.tv_sec - start.tv_sec ) + end.tv_usec - start.tv_usec;
			printf("test: %f(entrys/s)\n", my_sum / (timeuse / 1000000.0));
		}
	}

	return 0;
}
