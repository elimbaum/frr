#ifndef _ZEBRA_GRPC_PROVIDER_H_
#define _ZEBRA_GRPC_PROVIDER_H_

struct zebra_dplane_rule_type {
	unsigned int op;
	unsigned int seq;
	unsigned int priority;
	unsigned int unique;
	unsigned int filter_src_mask;
	unsigned int filter_src_addr;
	unsigned int filter_dst_mask;
	unsigned int filter_dst_addr;
	unsigned int filter_udp_src_port;
	unsigned int filter_udp_dst_port;
	unsigned int filter_tcp_src_port;
	unsigned int filter_tcp_dst_port;
	unsigned int filter_ip_protocol;
	unsigned int filter_dsfield;
	unsigned int filter_pcp;
	unsigned int filter_vlan_id;
	unsigned int filter_vlan_flags;
	unsigned int action_src_mask;
	unsigned int action_src_addr;
	unsigned int action_dst_mask;
	unsigned int action_dst_addr;
	unsigned int action_udp_src_port;
	unsigned int action_udp_dst_port;
	unsigned int action_tcp_src_port;
	unsigned int action_tcp_dst_port;
	unsigned int action_dsfield;
	unsigned int action_pcp;
	unsigned int action_queue_id;
	unsigned int action_set_vlan_id;
	unsigned int action_vlan_flags;
	unsigned int action_nh_family;
	unsigned int action_nh_vrf_id;
	signed int  action_nh_ifindex;
	unsigned int action_nh_type;
	unsigned int action_nh_addr;
	unsigned long long action_nh_mac;
	unsigned int bound_intf_vrf_id;
	unsigned int bound_intf_ifindex;
};

#ifdef __cplusplus
#include <grpcpp/grpcpp.h>
#include "../grpc/pbr.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using pbr_policies::PolicyRequest;
using pbr_policies::PolicyReply;
using pbr_policies::Policy;

class PolicyClient
{
    public:
	PolicyClient(std::shared_ptr<Channel> channel)
            : stub_(Policy::NewStub(channel)) {}

	unsigned int send_rule_to_basebox(const pbr_policies::PolicyRequest &request)
	{
		PolicyReply reply;
		ClientContext context;

		Status status = stub_->Activate_Policy_Rule(&context, request, &reply);

		if (status.ok()){
			//			return reply.status();
			return 0;
		}
		else{
			std::cout << status.error_code() << ": " << status.error_message()
				  << std::endl;
			return 1;
		}
	}
    private:
        std::unique_ptr<Policy::Stub> stub_;
};
#else 
   typedef struct PolicyClient PolicyClient;
   typedef struct zebra_dplane_rule_type zebra_dplane_rule_type;
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__STDC__) || defined(__cplusplus)
extern PolicyClient * call_connect_to_basebox(char *addr);
extern unsigned int call_send_rule_to_basebox(PolicyClient*p,
					      zebra_dplane_rule_type *rt);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_GRPC_PROVIDER_H_ */

