#include <array>
#include <atomic>
#include <bits/stdint-uintn.h>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <netinet/in.h>
#include <rte_hash.h>
#include <rte_icmp.h>
#include <rte_ring.h>
#include <rte_ring_core.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <arpa/inet.h>
#include <future>

#include <unistd.h>
#include <random>

#include "yaml-cpp/yaml.h"
 
#include "utils.h"

struct portstat_t{
	uint64_t tx;
	uint64_t rx;
	uint64_t rxB;
	uint64_t txB;
	uint64_t dropped;
} __rte_cache_aligned;
portstat_t *stats;
portstat_t *oldStats;

YAML::Node config;
std::string configPath="/workspaces/config.yaml";

int numLcores;
int numCoresPerPort;
int firstCore;
std::vector<int> ports;
static int PER_FLOW_AGG=0;
static int MAX_NUM_FLOWS=2000000;

volatile bool force_quit;
const int MEMPOOL_CACHE_SIZE = 512;
struct rte_mempool *mbufPool = NULL;

const int RTE_TEST_RX_DESC_DEFAULT = 4096;
const int RTE_TEST_TX_DESC_DEFAULT = 4096;
uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;  
unsigned lcore_id;
uint16_t nb_ports;
uint16_t nb_ports_available = 0;
uint16_t portid;
int enabledPortsMask = 0;
rte_ether_addr l2fwd_ports_eth_addr[RTE_MAX_ETHPORTS];
struct lcore_queue_conf *qconf;

  
struct rte_eth_conf port_conf = {
	.txmode =
		{
			.mq_mode = RTE_ETH_MQ_TX_NONE,
		},
};

typedef struct{
	int srcport;
	int srcq;
	int dstport;
	int dstq;
	uint8_t coreid;
	rte_hash *conTable;
	flowCounters_t *flows;
	uint32_t *frees;
	int numFrees;
	int currentFlowIndex;
}forwardArgs_t;

forwardArgs_t *fwargs;
std::atomic_int threadIndex = {0};


#define MAX_PKT_BURST 128

static int forward(forwardArgs_t args,int count){
	std::cout<<"core="<<firstCore+args.coreid<<
				", srcport="<<args.srcport<<
				", srcq="<<args.srcq<<
				", dstport="<<args.dstport<<
				", dstq="<<args.dstq<<
			std::endl;
	std::ofstream logs("/tmp/log-core-"+std::to_string(args.srcport)+"-"+std::to_string(args.coreid)+".txt", std::ofstream::out);
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int srcStats=args.srcport*numCoresPerPort+args.coreid;
	int dstStats=args.srcport*numCoresPerPort+args.coreid;

	struct rte_eth_fp_ops *p;
	void *qd;
	p = &rte_eth_fp_ops[args.srcport];
	qd = p->rxq.data[args.srcq];
	struct rte_mbuf *m;
	std::vector<_5tuple_t> test2;
	// nb_rx = p->rx_pkt_burst(qd, pkts_burst, MAX_PKT_BURST);
	_5tuple_t key;
	flowCounters_t *value;
	if(PER_FLOW_AGG==0){
		while(true){
			//simpleFWD
			const uint16_t rx_c = p->rx_pkt_burst(qd, pkts_burst, MAX_PKT_BURST);
			if (rx_c == 0)
				continue;
			stats[srcStats].rx += rx_c;
			const uint16_t tx_c = rte_eth_tx_burst(args.dstport, args.dstq, pkts_burst, rx_c);
			stats[dstStats].tx+=tx_c;
			if(tx_c!=rx_c){
				rte_pktmbuf_free_bulk(pkts_burst+tx_c,rx_c-tx_c);
				stats[dstStats].dropped+=(rx_c-tx_c);
			}
		}	
	}
	else{
		uint32_t udpStack=(RTE_PTYPE_L2_ETHER|RTE_PTYPE_L3_IPV4|RTE_PTYPE_L4_UDP);
		uint32_t tcpStack=(RTE_PTYPE_L2_ETHER|RTE_PTYPE_L3_IPV4|RTE_PTYPE_L4_TCP);
		while(true){
			const uint16_t rx_c = p->rx_pkt_burst(qd, pkts_burst, MAX_PKT_BURST);
			if (rx_c == 0)
				continue;
			stats[srcStats].rx += rx_c;
			
			for(int i=0;i<rx_c;i++){
				m = pkts_burst[i];
				stats[srcStats].rxB+=m->data_len;
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));
				rte_ether_hdr *eth_hdr =rte_pktmbuf_mtod(m, rte_ether_hdr *);
				rte_ipv4_hdr *ip4_hdr = (rte_ipv4_hdr *)(eth_hdr + 1);
				if(eth_hdr->ether_type != SWAP_BYTES(RTE_ETHER_TYPE_IPV4) || (ip4_hdr->next_proto_id!=17&&ip4_hdr->next_proto_id!=6) )
					continue;
				// if(m->packet_type !=udpStack && m->packet_type !=tcpStack)
				// 	continue;
				rte_udp_hdr *udp = (rte_udp_hdr *)(ip4_hdr + 1);
				// rte_tcp_hdr *tcp = (rte_tcp_hdr *)(ip4_hdr + 1);
				// PrintHeaders(logs,m);
				
				key.dst_ip=ip4_hdr->dst_addr;
				key.src_ip=ip4_hdr->src_addr;
				key.src_port=udp->src_port;
				key.dst_port=udp->dst_port;
				key.protocol_type=ip4_hdr->next_proto_id;
				int index=rte_hash_lookup_with_hash_data(args.conTable,&key,m->hash.rss,(void **)&value);
				if(index>=0)
				{				
					value->numPackets++;
					value->bytes+=m->pkt_len;
					// logs<<"old port="<<args.srcport<<", core="<<args.coreid << ", ip src: "<< inet_ntoa(*(struct in_addr *)&(key.src_ip))<< ", dst: " << inet_ntoa(*(struct in_addr *)&(key.dst_ip))<<", srcport="<<htons(key.src_port)<<", dstport="<<htons(key.dst_port)<<", nump="<<value->numPackets<<", value="<<value<<std::endl;
				}
				else
				{
					if(args.currentFlowIndex<MAX_NUM_FLOWS){
						value=&args.flows[args.currentFlowIndex];
						args.currentFlowIndex++;
					}
					else if(args.numFrees>0)
					{
						value=&args.flows[args.numFrees-1];
						args.numFrees--;
					}
					else
						std::cout<<"flow table overflow"<<std::endl;
					value->numPackets=1;
					value->bytes=m->pkt_len;
					index=rte_hash_add_key_with_hash_data(args.conTable, &key, m->hash.rss,value);
					// logs<<"new port="<<args.srcport<<", core="<<args.coreid << ", ip src: "<< inet_ntoa(*(struct in_addr *)&(key.src_ip))<< ", dst: " << inet_ntoa(*(struct in_addr *)&(key.dst_ip))<<", srcport="<<htons(key.src_port)<<", dstport="<<htons(key.dst_port)<<", nump="<<value->numPackets<<", value="<<value<<std::endl;
				}
				const uint16_t tx_c = rte_eth_tx_burst(args.dstport, args.dstq, &m, 1);
				if(tx_c==0){
					rte_pktmbuf_free(m);
					stats[dstStats].dropped++;
				}
				else{
					stats[dstStats].tx++;
					stats[dstStats].txB+=m->data_len;
				}
			}
		}
	}
}

inline int launchThreads(void *arg) {
  int indx = threadIndex.fetch_add(1);
  if(indx<numLcores)
  	forward(fwargs[indx], indx);
  return 0;
}

inline void check_all_ports_link_status(uint32_t port_mask) {
	const int CHECK_INTERVAL = 100; /* 100ms */
	const int MAX_CHECK_TIME = 90;  /* 9s (90 * 100ms) in total */
	uint16_t portid;
	uint8_t count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;
	int ret;
	char link_status_text[RTE_ETH_LINK_MAX_STR_LEN];

	printf("\nChecking link status\n");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		all_ports_up = 1;
		RTE_ETH_FOREACH_DEV(portid) {
		if (force_quit)
			return;
		if ((port_mask & (1 << portid)) == 0)
			continue;
		memset(&link, 0, sizeof(link));
		ret = rte_eth_link_get_nowait(portid, &link);
		if (ret < 0) {
			all_ports_up = 0;
			if (print_flag == 1)
				printf("Port %u link get failed: %s\n", portid, rte_strerror(-ret));
			continue;
		}
		/* print link status if flag set */
		if (print_flag == 1) {
			rte_eth_link_to_str(link_status_text, sizeof(link_status_text),
								&link);
			printf("Port %d %s\n", portid, link_status_text);
			continue;
		}
		/* clear all_ports_up flag if any link down */
		if (link.link_status == RTE_ETH_LINK_DOWN) {
			all_ports_up = 0;
			break;
		}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
		print_flag = 1;
		printf("done\n");
		}
	}
}

inline void InitDPDK() {

	for (auto const &port : ports) {
		enabledPortsMask |= (1 << port);
	}
	char lcores[32];
	char numcores[32];

	sprintf(lcores, "%d-%d", firstCore,firstCore+numLcores);
	sprintf(numcores, "%d", numLcores);

	std::vector<char *> argv;
	argv.push_back((char *)"simplefwd");
	argv.push_back((char *)"-l");
	argv.push_back(lcores);
	argv.push_back((char *)"-n");
	argv.push_back(numcores);
	argv.push_back((char *)"--no-telemetry");
	argv.push_back(nullptr);

	int ret = rte_eal_init(argv.size() - 1, argv.data());
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments: \n");

	/* convert to number of cycles */
	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

	/* check port mask to possible port mask */
	if (enabledPortsMask & ~((1 << nb_ports) - 1))
		rte_exit(EXIT_FAILURE, "Invalid portmask; possible (0x%x)\n",
				(1 << nb_ports) - 1);

	uint32_t nb_mbufs = GetNumMbufs(ports.size());
	std::cout << "allocating " << nb_mbufs << " mbufs ("
				<< (RTE_MBUF_DEFAULT_BUF_SIZE * nb_mbufs) / 1024 / 1024 << " MB)"
				<< std::endl;

	/* create the mbuf pool */
	mbufPool =
		rte_pktmbuf_pool_create("mbuf_pool", nb_mbufs, MEMPOOL_CACHE_SIZE, 0,
								RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (mbufPool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool: %d, %s\n", rte_errno,
				rte_strerror(rte_errno));

	port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
	port_conf.rx_adv_conf.rss_conf.rss_key = NULL;
	port_conf.rx_adv_conf.rss_conf.rss_hf = RTE_ETH_RSS_UDP;

	/* Initialise each port */
	RTE_ETH_FOREACH_DEV(portid) {
		struct rte_eth_rxconf rxq_conf;
		struct rte_eth_txconf txq_conf;
		struct rte_eth_conf local_port_conf = port_conf;
		struct rte_eth_dev_info dev_info;

		/* skip ports that are not enabled */
		if ((enabledPortsMask & (1 << portid)) == 0) {
			printf("Skipping disabled port %u\n", portid);
			continue;
		}
		nb_ports_available++;

		/* init port */
		printf("Initializing port %u (", portid);
		fflush(stdout);

		ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
						"Error during getting device (port %u) info: %s\n", portid,
						strerror(-ret));
		if (dev_info.device) {
			char name[32];
			strlcpy(name, rte_dev_name(dev_info.device), sizeof(name));
			std::cout << name << ") ";
		}
		ret = rte_eth_macaddr_get(portid, &l2fwd_ports_eth_addr[portid]);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot get MAC address: err=%d, port=%u\n", ret,
						portid);
		printf("%hhx:%hhx:%hhx:%hhx:%hhx:%hhx ",
				l2fwd_ports_eth_addr[portid].addr_bytes[0],
				l2fwd_ports_eth_addr[portid].addr_bytes[1],
				l2fwd_ports_eth_addr[portid].addr_bytes[2],
				l2fwd_ports_eth_addr[portid].addr_bytes[3],
				l2fwd_ports_eth_addr[portid].addr_bytes[4],
				l2fwd_ports_eth_addr[portid].addr_bytes[5]);
		local_port_conf.rx_adv_conf.rss_conf.rss_hf = dev_info.flow_type_rss_offloads;
		if (local_port_conf.rx_adv_conf.rss_conf.rss_hf != port_conf.rx_adv_conf.rss_conf.rss_hf) {
			printf("Port %u modified RSS hash function based on hardware support,"
					"requested:%#" PRIx64" configured:%#" PRIx64"", portid, port_conf.rx_adv_conf.rss_conf.rss_hf, local_port_conf.rx_adv_conf.rss_conf.rss_hf);
		}

		if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
			local_port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
		ret = rte_eth_dev_configure(portid, numCoresPerPort,
									numCoresPerPort, &local_port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
						ret, portid);

		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd, &nb_txd);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
						"Cannot adjust number of descriptors: err=%d, port=%u\n", ret,
						portid);

		for (int j = 0; j < numCoresPerPort; j++) {
			/* init one RX queue */
			fflush(stdout);
			rxq_conf = dev_info.default_rxconf;
			rxq_conf.offloads = local_port_conf.rxmode.offloads;
			ret = rte_eth_rx_queue_setup(portid, j, nb_rxd,
											rte_eth_dev_socket_id(portid), &rxq_conf,
											mbufPool);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
						ret, portid);
		}
		for (int j = 0; j < numCoresPerPort; j++) {
			/* init one TX queue on each port */
			fflush(stdout);
			txq_conf = dev_info.default_txconf;
			txq_conf.offloads = local_port_conf.txmode.offloads;
			ret = rte_eth_tx_queue_setup(portid, j, nb_txd,
											rte_eth_dev_socket_id(portid), &txq_conf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
						ret, portid);
		}

		
		ret = rte_eth_dev_set_ptypes(portid, RTE_PTYPE_UNKNOWN, NULL, 0);
		if (ret < 0)
			printf("Port %u, Failed to disable Ptype parsing\n", portid);
		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n", ret,
						portid);


		printf("done\n");
		ret = rte_eth_promiscuous_enable(portid);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				"rte_eth_promiscuous_enable:err=%s, port=%u\n",
				rte_strerror(-ret), portid);
	}
	if (!nb_ports_available) {
	rte_exit(EXIT_FAILURE,
			"All available ports are disabled. Please set portmask.\n");
	}
	check_all_ports_link_status(enabledPortsMask);
}

int
main(int argc, char **argv)
{
	YAML::Node config = YAML::LoadFile(configPath);
    numLcores = config["num_lcores"].as<int>();
    firstCore = config["first_core"].as<int>();
    ports =config["ports"].as<std::vector<int>>();
    if(numLcores<ports.size()){
      std::cout<<"num of cores should be greater than ports"<<std::endl;
      exit(-1);
    }
    if(numLcores%ports.size()!=0){
      std::cout<<"num of cores should be divisible by number ports"<<std::endl;
      exit(-1);
    }
	fwargs = new forwardArgs_t[numLcores];
	stats= new portstat_t[numLcores];
	oldStats= new portstat_t[numLcores];
	memset(stats, 0, sizeof(portstat_t)*numLcores);
	memset(oldStats, 0, sizeof(portstat_t)*numLcores);
	numCoresPerPort=numLcores/ports.size();
	for(int i=0;i<ports.size();i++){
		for(int j=0;j<numCoresPerPort;j++)
		{
			fwargs[i*numCoresPerPort+j].srcport=i;
			fwargs[i*numCoresPerPort+j].srcq=j;
			fwargs[i*numCoresPerPort+j].dstport=(i+1)%ports.size();
			fwargs[i*numCoresPerPort+j].dstq=j;
			fwargs[i*numCoresPerPort+j].coreid=j;
		}
	}
    InitDPDK();
	if(argc>1 && *(argv[1])=='1')
	{
		std::cout<<"per flow aggregation enabled"<<std::endl;
		PER_FLOW_AGG=1;
	}
	else {
		std::cout<<"per flow aggregation disabled"<<std::endl;
	}
	for(int i=0;i<ports.size();i++){
		for(int j=0;j<numCoresPerPort;j++)
		{
			fwargs[i*numCoresPerPort+j].conTable=CreateHashTable(MAX_NUM_FLOWS, i*numCoresPerPort+j,sizeof(_5tuple_t));
			fwargs[i*numCoresPerPort+j].flows=new flowCounters_t[MAX_NUM_FLOWS];
			fwargs[i*numCoresPerPort+j].frees=new uint32_t[MAX_NUM_FLOWS];
			fwargs[i*numCoresPerPort+j].numFrees=0;
			fwargs[i*numCoresPerPort+j].currentFlowIndex=0;
		}
	}
	rte_hash *srcIPAggregate=CreateHashTable(MAX_NUM_FLOWS, 1000,sizeof(uint32_t));
	aggFlowCounters_t *srcIPCounterAgg=new aggFlowCounters_t[MAX_NUM_FLOWS];
	memset(srcIPCounterAgg,0,MAX_NUM_FLOWS*sizeof(aggFlowCounters_t));
	uint32_t *frees=new uint32_t[MAX_NUM_FLOWS];
	int numFrees=0;
	int currentFlowIndex=0;

	FILE* srcIPAggLog = std::fopen("/tmp/srcIPAggLog.txt", "w");

	rte_eal_mp_remote_launch(launchThreads, NULL, SKIP_MAIN);

	
    bool stop = false;
	int updated=0;
	
	portstat_t tmp;
	_5tuple_t *key;
	flowCounters_t *counters;
	aggFlowCounters_t *aggCounter;
	uint32_t iter=0;
    while (stop == false) {
		// system("clear");
		std::string ss;
		for(auto &port:ports){
			memset(&tmp, 0, sizeof(portstat_t));
			for(int j=0;j<numCoresPerPort;j++){
				tmp.rx=stats[port*numCoresPerPort+j].rx;
				tmp.rxB=stats[port*numCoresPerPort+j].rxB;
				tmp.tx=stats[port*numCoresPerPort+j].tx;
				tmp.txB=stats[port*numCoresPerPort+j].txB;
				tmp.dropped=stats[port*numCoresPerPort+j].dropped;
			}
			ss+=std::to_string(port)+": RX="+std::to_string(tmp.rx)+", TX="+
							std::to_string(tmp.tx)+", drop="+
							std::to_string(tmp.dropped)+"\t";
			if(tmp.rx!=oldStats[port].rx || tmp.tx!=oldStats[port].tx || tmp.dropped !=oldStats[port].dropped){
				updated++;
				oldStats[port].rx=tmp.rx;
				oldStats[port].tx=tmp.tx;
				oldStats[port].dropped=tmp.dropped;
			}
		}

		if(PER_FLOW_AGG==1){
			// memset(srcIPCounterAgg,0,MAX_NUM_FLOWS*sizeof(aggFlowCounters_t));
			for(int i=0;i<ports.size();i++){
				for(int j=0;j<numCoresPerPort;j++)
				{
					iter=0;
					while (rte_hash_iterate(fwargs[i*numCoresPerPort+j].conTable, (const void **)&key, (void **)&counters, &iter) >= 0) {
						
						uint32_t ip=(i==0)?key->src_ip:key->dst_ip;
						if(ip==0 || ip==0xffffffff)
							continue;
						// uint32_t ip=key->src_ip;
						int perIPAggIndex=rte_hash_lookup_data(srcIPAggregate,(const void *)&(ip),(void **)&aggCounter);
						
						if(perIPAggIndex>=0)
						{
							// aggCounter->numPackets+=counters->numPackets;
							// aggCounter->bytes+=counters->bytes;
							// aggCounter->numFlows++;
							// std::cout<<"old port="<<i<<", core="<<j << ", ip src: "<< inet_ntoa(*(struct in_addr *)&(key->src_ip))<< ", dst: " << inet_ntoa(*(struct in_addr *)&(key->dst_ip))<<", srcport="<<htons(key->src_port)<<", dstport="<<htons(key->dst_port)<<", nump="<<counters->numPackets<< std::endl;
						}
						else
						{
							if(currentFlowIndex<MAX_NUM_FLOWS){
								aggCounter=&srcIPCounterAgg[currentFlowIndex];
								currentFlowIndex++;
							}
							else if(numFrees>0)
							{
								aggCounter=&srcIPCounterAgg[numFrees-1];
								numFrees--;
							}
							else
								std::cout<<"flow table overflow"<<std::endl;
							perIPAggIndex=rte_hash_add_key_data(srcIPAggregate, (const void *)&(ip),aggCounter);
							// aggCounter->numPackets=counters->numPackets;
							// aggCounter->bytes=counters->bytes;
							// aggCounter->numFlows=1;
							// std::cout<<"new port="<<i<<", core="<<j << ", ip src: "<< inet_ntoa(*(struct in_addr *)&(key->src_ip))<< ", dst: " << inet_ntoa(*(struct in_addr *)&(key->dst_ip))<<", srcport="<<htons(key->src_port)<<", dstport="<<htons(key->dst_port)<<", nump="<<counters->numPackets<< std::endl;
						}
						if(aggCounter->isFirst==0){
							aggCounter->numPackets=counters->numPackets;
							aggCounter->bytes=counters->bytes;
							aggCounter->numFlows=1;
							aggCounter->isFirst=1;
						}
						else{
							aggCounter->numPackets+=counters->numPackets;
							aggCounter->bytes+=counters->bytes;
							aggCounter->numFlows++;
						}
					}
				}
			}
			uint32_t *ip;
			char buffer[128];
			rewind(srcIPAggLog);
			iter=0;
			int flowPS=0;
			while (rte_hash_iterate(srcIPAggregate, (const void **)&ip, (void **)&aggCounter, &iter) >= 0) {
				flowPS+=(aggCounter->numFlows-aggCounter->oldNumFlows);
				fprintf(srcIPAggLog, "%s, numPackets=%d, bytes=%ld, numFlows=%d, newFlows=%d,\n",inet_ntoa(*(struct in_addr *)ip),aggCounter->numPackets,aggCounter->bytes,aggCounter->numFlows,(aggCounter->numFlows-aggCounter->oldNumFlows));
				aggCounter->isFirst=0;
				aggCounter->oldNumFlows=aggCounter->numFlows;
			}
			std::cout<<"flow per scond="<<flowPS/2<<std::endl;
			fprintf(srcIPAggLog, "===========\n");
			fflush(srcIPAggLog);
		}	

		if(updated>0){
			updated=0;
			std::cout<<ss<<std::endl;
		}
    	usleep(1000* 1000);
    }

    RTE_LCORE_FOREACH_WORKER(lcore_id) {
      if (rte_eal_wait_lcore(lcore_id) < 0) {
        break;
      }
    }

	RTE_ETH_FOREACH_DEV(portid) {
      if ((enabledPortsMask & (1 << portid)) == 0)
        continue;
      printf("Closing port %d...", portid);
      int ret = rte_eth_dev_stop(portid);
      if (ret != 0)
        printf("rte_eth_dev_stop: err=%d, port=%d\n", ret, portid);
      rte_eth_dev_close(portid);
      printf(" Done\n");
    }

    /* clean up the EAL */
    rte_eal_cleanup();
    printf("Bye...\n");
}