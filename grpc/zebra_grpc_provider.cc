#include "zebra_grpc_provider.h"

//PolicyClient * call_connect_to_basebox(char *target_str)
//{
//        
//    std::shared_ptr<Channel> channel;
//    static PolicyClient my_client(grpc::CreateChannel(
//				  target_str, grpc::InsecureChannelCredentials()));
//    return &my_client;
//
//    grpc_connectivity_state state;
//    channel = grpc::CreateChannel(target_str,
//                                  grpc::InsecureChannelCredentials());
//    state = channel->GetState(true);
//    if(state != GRPC_CHANNEL_READY)
//    {
//            return NULL;
//    }
//    static PolicyClient my_client(channel);
//
//}

PolicyClient * call_connect_to_basebox(char *target_str)
{
      static PolicyClient *my_ptr = NULL;
      std::shared_ptr<Channel> channel;
      channel = grpc::CreateChannel(target_str,
                                  grpc::InsecureChannelCredentials());
      PolicyClient *ptr = new PolicyClient(channel);
      delete my_ptr;
      my_ptr = ptr;
      return my_ptr;
}


uint32_t call_send_rule_to_basebox(PolicyClient*p, zebra_dplane_rule_type *rt)
{
    PolicyRequest request;
    request.set_op(rt->op);
    request.set_seq(rt->seq);
    request.set_priority(rt->priority);
    request.set_unique(rt->unique);
    request.set_filter_src_mask(rt->filter_src_mask);
    request.set_filter_src_addr(rt->filter_src_addr);
    request.set_filter_dst_mask(rt->filter_dst_mask);
    request.set_filter_dst_addr(rt->filter_dst_addr);
    request.set_filter_udp_src_port(rt->filter_udp_src_port);
    request.set_filter_udp_dst_port(rt->filter_udp_dst_port);
    request.set_filter_tcp_src_port(rt->filter_tcp_src_port);
    request.set_filter_tcp_dst_port(rt->filter_tcp_dst_port);
    request.set_filter_ip_protocol(rt->filter_ip_protocol);
    request.set_filter_dsfield(rt->filter_dsfield);
    request.set_filter_pcp(rt->filter_pcp);
    request.set_filter_vlan_id(rt->filter_vlan_id);
    request.set_filter_vlan_flags(rt->filter_vlan_flags);

    request.set_action_src_mask(rt->action_src_mask);
    request.set_action_src_addr(rt->action_src_addr);
    request.set_action_dst_mask(rt->action_dst_mask);
    request.set_action_dst_addr(rt->action_dst_addr);
    request.set_action_udp_src_port(rt->action_udp_src_port);
    request.set_action_udp_dst_port(rt->action_udp_dst_port);
    request.set_action_tcp_src_port(rt->action_tcp_src_port);
    request.set_action_tcp_dst_port(rt->action_tcp_dst_port);
    request.set_action_dsfield(rt->action_dsfield);
    request.set_action_pcp(rt->action_pcp);
    request.set_action_queue_id(rt->action_queue_id);
    request.set_action_set_vlan_id(rt->action_set_vlan_id);
    request.set_action_vlan_flags(rt->action_vlan_flags);
    request.set_action_nh_family(rt->action_nh_family);
    request.set_action_nh_vrf_id(rt->action_nh_vrf_id);
    request.set_action_nh_ifindex(rt->action_nh_ifindex);
    request.set_action_nh_type(rt->action_nh_type);
    request.set_action_nh_addr(rt->action_nh_addr);
    request.set_action_nh_mac(rt->action_nh_mac);
    request.set_bound_intf_vrf_id(rt->bound_intf_vrf_id);
    request.set_bound_intf_ifindex(rt->bound_intf_ifindex);
    if (p == NULL)
            return 1;
    else
            return (p->send_rule_to_basebox(request));
}

