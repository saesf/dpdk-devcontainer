#ifndef UTILS_H
#define UTILS_H
#include <iomanip>
#include <fstream>
#include <rte_jhash.h>
#include <rte_hash.h>
#include <rte_mbuf.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <iostream>

#define SWAP_BYTES(x) ((((x)&0xFF) << 8) | (((x) >> 8) & 0xFF))

typedef struct _5tuple_s{
	uint32_t src_ip;
    uint32_t dst_ip;
	uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol_type;
} __attribute__ ((packed)) _5tuple_t;

template<> struct std::hash<_5tuple_t> {
    std::size_t operator()(const _5tuple_t& t) const {
        return std::hash<int>()(t.src_ip) ^
            std::hash<int>()(t.dst_ip) ^
            std::hash<int>()(t.src_port) ^
			std::hash<int>()(t.dst_port) ^
			std::hash<int>()(t.protocol_type);
    }
};

typedef struct{
	uint32_t numPackets;
	uint64_t bytes;
}__attribute__ ((packed)) flowCounters_t;

typedef struct{
	uint32_t numPackets;
    uint64_t bytes;
	uint32_t numFlows;
	uint32_t oldNumFlows;
	uint8_t isFirst;
}__attribute__ ((packed)) aggFlowCounters_t;

inline void PrepareStats(std::ofstream &logs){

}
inline rte_hash *CreateHashTable(uint32_t maxEntry, int id,uint32_t keySize) {
    char name[RTE_HASH_NAMESIZE];
    struct rte_hash *h;
    /* create table */
    struct rte_hash_parameters hash_params = {
        .entries = maxEntry ,     /* table load = 50% */
        .key_len = keySize, /* Store IPv4 dest IP address */
        .hash_func = rte_jhash,
        .hash_func_init_val = 0,
        .socket_id = (int)rte_socket_id(),
        // .extra_flag = RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD,
    };
    snprintf(name, sizeof(name), "h_%d", id);
    hash_params.name = name;
    h = rte_hash_create(&hash_params);
    std::cout<<"id="<<id<<std::endl;
    if (h == NULL)
    {
        std::cout << "Problem creating the hash table for "<<id<<": " << rte_errno << " "
            << rte_strerror(rte_errno) << std::endl;
            exit(-1);
    }
    return h;
}

inline rte_ring *CreateRing(int id, int size) {
	rte_ring *cring = nullptr;

	char cname[NAME_MAX];
	snprintf(cname, sizeof(cname), "port-%04x", id);
	cring = rte_ring_create(cname, size, SOCKET_ID_ANY,
							RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
	if (cring == NULL) {
		std::cout << "failed to create rte_ring error: " << rte_errno << " "
				<< rte_strerror(rte_errno) << std::endl;
	}
	return cring;
}

inline std::string exec(const char *cmd) {
	std::array<char, 128> buffer;
	std::string result;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
	if (!pipe) {
		throw std::runtime_error("popen() failed!");
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}
	return result;
}

inline uint32_t GetNumMbufs(int numdevs) {
	int hugePageSize = 0;
	std::string response = exec(
		"cat /proc/meminfo | grep -E 'Hugetlb:' | tr -s ' ' | cut -d ' ' -f 2");
	try {
		hugePageSize = stoi(response);
	} catch (std::exception &err) {
		std::cout << "Hugepage calculation failure: " << err.what()
				<< std::endl; // Note: what() tells the exact error
		exit(-1);
	}
	if (hugePageSize == 0) {
		std::cout << "no HugePage memory available" << std::endl;
		exit(-1);
	}
	// hugePageSize /= 2;
	uint64_t nb_mbufs = ((uint64_t)hugePageSize * (uint64_t)1024 /
							(uint64_t)RTE_MBUF_DEFAULT_BUF_SIZE);
	uint64_t power = 1;
	// mbufPoolSizePerPort =
	//     (mbufPoolSizePerPort > (1 << 20)) ? (1 << 20) : mbufPoolSizePerPort;
	while (power <= nb_mbufs)
		power *= 2;
	power /= 4;
	power--;
	return power;
}
  
inline void PrintHeaders(std::ofstream &logs,rte_mbuf *raw) {
  struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(raw, struct rte_ether_hdr *);
  struct rte_ipv4_hdr *ip4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
  struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip4_hdr + 1);
  struct rte_arp_hdr *arp_hdr = (struct rte_arp_hdr *)(eth_hdr + 1);
  logs<<"ether src: "<< std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->src_addr.addr_bytes[0])<<":"
  					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->src_addr.addr_bytes[1])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->src_addr.addr_bytes[2])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->src_addr.addr_bytes[3])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->src_addr.addr_bytes[4])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->src_addr.addr_bytes[5]);

  logs<<"\tdst: "<< std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->dst_addr.addr_bytes[0])<<":"
  					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->dst_addr.addr_bytes[1])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->dst_addr.addr_bytes[2])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->dst_addr.addr_bytes[3])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->dst_addr.addr_bytes[4])<<":"
					 << std::hex << std::setw(2) << std::setfill('0') << int(eth_hdr->dst_addr.addr_bytes[5])<<std::endl;
  if (eth_hdr->ether_type == SWAP_BYTES(RTE_ETHER_TYPE_IPV4)) {
    logs << "\tip src: "
              << inet_ntoa(*(struct in_addr *)&(ip4_hdr->src_addr))
              << "\tdst: " << inet_ntoa(*(struct in_addr *)&(ip4_hdr->dst_addr))
              << std::endl;
    if (likely(ip4_hdr->next_proto_id == IPPROTO_UDP))
      logs << "\tudp src: " << htons(udp->src_port)
                << "\tdst: " << htons(udp->dst_port) << std::endl;
  }
}
#endif