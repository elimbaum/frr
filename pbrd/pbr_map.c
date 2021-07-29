/*
 * PBR-map Code
 * Copyright (C) 2018 Cumulus Networks, Inc.
 *               Donald Sharp
 *
 * FRR is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * FRR is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; see the file COPYING; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <zebra.h>

#include "thread.h"
#include "linklist.h"
#include "prefix.h"
#include "table.h"
#include "vrf.h"
#include "nexthop.h"
#include "nexthop_group.h"
#include "memory.h"
#include "log.h"
#include "vty.h"

#include "pbr_nht.h"
#include "pbr_map.h"
#include "pbr_zebra.h"

#include "pbr_memory.h"
#include "pbr_debug.h"
#include "pbr_vrf.h"

#include "pbr.h"

DEFINE_MTYPE_STATIC(PBRD, PBR_MAP, "PBR Map")
DEFINE_MTYPE_STATIC(PBRD, PBR_MAP_SEQNO, "PBR Map Sequence")
DEFINE_MTYPE_STATIC(PBRD, PBR_MAP_INTERFACE, "PBR Map Interface")

static uint32_t pbr_map_sequence_unique;

static bool pbr_map_check_valid_internal(struct pbr_map *pbrm);
static inline int pbr_map_compare(const struct pbr_map *pbrmap1,
				  const struct pbr_map *pbrmap2);

RB_GENERATE(pbr_map_entry_head, pbr_map, pbr_map_entry, pbr_map_compare)

struct pbr_map_entry_head pbr_maps = RB_INITIALIZER(&pbr_maps);

DEFINE_QOBJ_TYPE(pbr_map_sequence)

static inline int pbr_map_compare(const struct pbr_map *pbrmap1,
				  const struct pbr_map *pbrmap2)
{
	return strcmp(pbrmap1->name, pbrmap2->name);
}

static int pbr_map_sequence_compare(const struct pbr_map_sequence *pbrms1,
				    const struct pbr_map_sequence *pbrms2)
{
	if (pbrms1->seqno == pbrms2->seqno)
		return 0;

	if (pbrms1->seqno < pbrms2->seqno)
		return -1;

	return 1;
}

static void pbr_map_sequence_delete(struct pbr_map_sequence *pbrms)
{
	XFREE(MTYPE_TMP, pbrms->internal_nhg_name);

	QOBJ_UNREG(pbrms);
	XFREE(MTYPE_PBR_MAP_SEQNO, pbrms);
}

static int pbr_map_interface_compare(const struct pbr_map_interface *pmi1,
				     const struct pbr_map_interface *pmi2)
{
	return strcmp(pmi1->ifp->name, pmi2->ifp->name);
}

static void pbr_map_interface_list_delete(struct pbr_map_interface *pmi)
{
	struct pbr_map_interface *pmi_int;
	struct listnode *node, *nnode;
	struct pbr_map *pbrm;

	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		for (ALL_LIST_ELEMENTS(pbrm->incoming, node, nnode, pmi_int)) {
			if (pmi == pmi_int) {
				pbr_map_policy_delete(pbrm, pmi);
				return;
			}
		}
	}
}

static bool pbrms_is_installed(const struct pbr_map_sequence *pbrms,
			       const struct pbr_map_interface *pmi)
{

#if !defined(HAVE_BASEBOX)
	uint64_t is_installed = (uint64_t)1 << pmi->install_bit;

	is_installed &= pbrms->installed;

	if (is_installed)
		return true;

	return false;
#else
	if(pbrms && pmi)
	{
		if (pbrms->parent == pmi->pbrm)
		{
			if(bf_test_index(pbrms->parent->ifi_bitfield,
					 pmi->install_bit))
				return true;
		}
	}
	return false;
#endif /* HAVE_BASEBOX */
}

void pbr_set_action_clause_nexthop(struct pbr_map_sequence *pbrms,
				   struct nexthop *nhop)
{
	if(nhop){
		if (pbrms) {
			pbrms->nh_vrf_id   = nhop->vrf_id;
			pbrms->nh_ifindex  = nhop->ifindex;
			pbrms->nh_type     = nhop->type;
			pbrms->nh_addr.ipv4.s_addr   = nhop->gate.ipv4.s_addr;
			pbr_map_check(pbrms, true);
		}
	}
}

void pbr_set_match_clause_for_proto_id(struct pbr_map_sequence *pbrms,
				       uint32_t proto_id)
{
	if (pbrms) {
		pbrms->match_proto_id    = proto_id;
		zlog_info("setting pbrms->match_proto_id = %u ",pbrms->match_proto_id);

		pbr_map_check(pbrms, true);
	}
}
void pbr_set_match_clause_for_pcp(struct pbr_map_sequence *pbrms,
				  uint8_t pcp)
{
	if (pbrms) {
		pbrms->match_pcp    = pcp;
		zlog_info("setting pbrms->match_pcp = %u ", pbrms->match_pcp);

		pbr_map_check(pbrms, true);
	}
}
void pbr_set_action_clause_for_pcp(struct pbr_map_sequence *pbrms,
				   uint8_t pcp)
{
	if (pbrms) {
		pbrms->action_pcp    = pcp;
		zlog_info("setting pbrms->action_pcp = %u ", pbrms->action_pcp);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_udp_src_port(struct pbr_map_sequence *pbrms,
					   uint32_t udp_port)
{
	if (pbrms) {
		pbrms->match_udp_src_port  = (udp_port & 0xFFFF);
		zlog_info("setting pbrms->match_udp_src_port = %u ", pbrms->match_udp_src_port);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_udp_dst_port(struct pbr_map_sequence *pbrms,
					   uint32_t udp_port)
{
	if (pbrms) {
		pbrms->match_udp_dst_port  = (udp_port & 0xFFFF);
		zlog_info("setting pbrms->match_udp_dst_port = %u ", pbrms->match_udp_dst_port);

		pbr_map_check(pbrms, true);
	}
}


void pbr_set_action_clause_for_udp_src_port(struct pbr_map_sequence *pbrms,
					    uint32_t udp_port)
{
	if (pbrms) {
		pbrms->action_udp_src_port  = (udp_port & 0xFFFF);
		zlog_info("setting pbrms->action_udp_src_port = %u ", pbrms->action_udp_src_port);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_action_clause_for_udp_dst_port(struct pbr_map_sequence *pbrms,
					   uint32_t udp_port)
{
	if (pbrms) {
		pbrms->action_udp_dst_port  = (udp_port & 0xFFFF);
		zlog_info("setting pbrms->action_udp_dst_port = %u ", pbrms->action_udp_dst_port);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_tcp_src_port(struct pbr_map_sequence *pbrms,
					   uint32_t tcp_port)
{
	if (pbrms) {
		pbrms->match_tcp_src_port  = (tcp_port & 0xFFFF);
		zlog_info("setting pbrms->match_tcp_src_port = %u ", pbrms->match_tcp_src_port);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_tcp_dst_port(struct pbr_map_sequence *pbrms,
					   uint32_t tcp_port)
{
	if (pbrms) {
		pbrms->match_tcp_dst_port  = (tcp_port & 0xFFFF);
		zlog_info("setting pbrms->match_tcp_dst_port = %u ", pbrms->match_tcp_dst_port);

		pbr_map_check(pbrms, true);
	}
}


void pbr_set_action_clause_for_tcp_src_port(struct pbr_map_sequence *pbrms,
					    uint32_t tcp_port)
{
	if (pbrms) {
		pbrms->action_tcp_src_port  = (tcp_port & 0xFFFF);
		zlog_info("setting pbrms->action_tcp_src_port = %u ", pbrms->action_tcp_src_port);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_action_clause_for_tcp_dst_port(struct pbr_map_sequence *pbrms,
					    uint32_t tcp_port)
{
	if (pbrms) {
		pbrms->action_tcp_dst_port  = (tcp_port & 0xFFFF);
		zlog_info("setting pbrms->action_tcp_dst_port = %u ", pbrms->action_tcp_dst_port);

		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_vlan(struct pbr_map_sequence *pbrms,
				   uint16_t vlan_id,
				   uint16_t vlan_flags)
{
	if (pbrms) {
		pbrms->match_vlan_id    = vlan_id;
		pbrms->match_vlan_flags = vlan_flags;
		pbr_map_check(pbrms, true);
	}
}

void pbr_set_action_clause_for_vlan(struct pbr_map_sequence *pbrms,
				    uint16_t vlan_id)
{
	if (pbrms) {
		pbrms->set_vlan_id    = vlan_id;
		pbr_map_check(pbrms, true);
	}
}

void pbr_strip_action_clause_for_vlan(struct pbr_map_sequence *pbrms,
				      uint16_t flags)
{
	if (pbrms) {
		pbrms->action_vlan_flags = flags;
		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_dscp(struct pbr_map_sequence *pbrms,
				   uint8_t dscp_value)
{
	zlog_info("in pbr_set_match_clause_for_dscp");
	zlog_info(" dscp_value = %u", dscp_value);
	zlog_info("pbrms->dscp = %0x", pbrms->match_dsfield);
	if (pbrms) {
		/* if value is same, ignore; otherwise set and process */
		if (((pbrms->match_dsfield & PBR_DSFIELD_DSCP) >> 2) != dscp_value){
			pbrms->match_dsfield =
				(pbrms->match_dsfield & ~PBR_DSFIELD_DSCP) |
				(dscp_value << 2);
			zlog_info("pbrms->dscp = %0x", pbrms->match_dsfield);
			pbr_map_check(pbrms, true);
		}
	}
}
void pbr_reset_match_clause_for_dscp(struct pbr_map_sequence *pbrms)
{
	if (pbrms) {
		pbrms->match_dsfield &= ~PBR_DSFIELD_DSCP;
		pbr_map_check(pbrms, true);
	}
}

void pbr_set_action_clause_for_dscp(struct pbr_map_sequence *pbrms,
				    uint8_t dscp_value)
{
	if (pbrms) {
		/* if value is same, ignore; otherwise set and process */
		if (((pbrms->action_dsfield & PBR_DSFIELD_DSCP) >> 2) != dscp_value){
			pbrms->action_dsfield =
				(pbrms->action_dsfield & ~PBR_DSFIELD_DSCP) |
				(dscp_value << 2);
			pbr_map_check(pbrms, true);
		}
	}
}
void pbr_reset_action_clause_for_dscp(struct pbr_map_sequence *pbrms)
{
	if (pbrms) {
		pbrms->action_dsfield &= ~PBR_DSFIELD_DSCP;
		pbr_map_check(pbrms, true);
	}
}

void pbr_set_match_clause_for_ecn(struct pbr_map_sequence *pbrms,
				  uint8_t ecn_value)
{
	if (pbrms) {
		/* if value is same, ignore; otherwise set and process */
		if ((pbrms->match_dsfield & PBR_DSFIELD_ECN) != ecn_value){
			pbrms->match_dsfield =
				(pbrms->match_dsfield & ~PBR_DSFIELD_ECN)|
				(ecn_value);
			zlog_info("pbrms dscpfield %0x ecn %u",
				  pbrms->match_dsfield,
				  pbrms->match_dsfield&0x03);
			pbr_map_check(pbrms, true);
		}
	}
}
void pbr_reset_match_clause_for_ecn(struct pbr_map_sequence *pbrms)
{
	if (pbrms) {
		pbrms->match_dsfield &= ~PBR_DSFIELD_ECN;
		pbr_map_check(pbrms, true);
	}
}

void pbr_set_action_clause_for_ecn(struct pbr_map_sequence *pbrms,
				   uint8_t ecn_value)
{
	if (pbrms) {
		/* if value is same, ignore; otherwise set and process */
		if ((pbrms->action_dsfield & PBR_DSFIELD_ECN) != ecn_value){
			pbrms->action_dsfield =
				(pbrms->action_dsfield & ~PBR_DSFIELD_ECN)|
				(ecn_value);
			zlog_info("pbrms dscpfield %0x ecn %u",
				  pbrms->action_dsfield,
				  pbrms->action_dsfield&0x3);

			pbr_map_check(pbrms, true);
		}
	}
}
void pbr_reset_action_clause_for_ecn(struct pbr_map_sequence *pbrms)
{
	if (pbrms) {
		pbrms->action_dsfield &= ~PBR_DSFIELD_ECN;
		pbr_map_check(pbrms, true);
	}
}

void pbr_set_action_clause_for_queue_id(struct pbr_map_sequence *pbrms,
				       uint8_t queue_id)
{
	if (pbrms) {
		pbrms->action_queue_id    = queue_id;
		pbr_map_check(pbrms, true);
	}
}

/* If any sequence is installed on the interface, assume installed */
static bool
pbr_map_interface_is_installed(const struct pbr_map *pbrm,
			       const struct pbr_map_interface *check_pmi)
{

	struct pbr_map_sequence *pbrms;
	struct pbr_map_interface *pmi;
	struct listnode *node, *inode;

	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms))
		for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, inode, pmi))
			if (pmi == check_pmi && pbrms_is_installed(pbrms, pmi))
				return true;

	return false;
}

static bool pbr_map_interface_is_valid(const struct pbr_map_interface *pmi)
{
	/* Don't install rules without a real ifindex on the incoming interface.
	 *
	 * This can happen when we have config for an interface that does not
	 * exist or when an interface is changing vrfs.
	 */
	if (pmi->ifp && pmi->ifp->ifindex != IFINDEX_INTERNAL)
		return true;

	return false;
}

static void pbr_map_pbrms_update_common(struct pbr_map_sequence *pbrms,
					bool install, bool changed)
{
	struct pbr_map *pbrm;
	struct listnode *node;
	struct pbr_map_interface *pmi;

	pbrm = pbrms->parent;
#if !defined (HAVE_BASEBOX)
	if (pbrms->nhs_installed && pbrm->incoming->count)
#else
	if (pbrm->incoming->count)
#endif /* HAVE_BASEBOX */
	{
		for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, node, pmi)) {
			if (!pmi->ifp)
				continue;

			if (install && !pbr_map_interface_is_valid(pmi))
				continue;
			pbr_send_pbr_map(pbrms, pmi, install, changed);
		}
	}
}


static void pbr_map_pbrms_install(struct pbr_map_sequence *pbrms, bool changed)
{
	pbr_map_pbrms_update_common(pbrms, true, changed);
}

static void pbr_map_pbrms_uninstall(struct pbr_map_sequence *pbrms)
{
	pbr_map_pbrms_update_common(pbrms, false, false);
}

static const char *const pbr_map_reason_str[] = {
	"Invalid NH-group",     "Invalid NH",	 "No Nexthops",
	"Both NH and NH-Group", "Invalid Src or Dst", "Invalid VRF",
	"Deleting Sequence",
};

void pbr_map_reason_string(unsigned int reason, char *buf, int size)
{
	unsigned int bit;
	int len = 0;

	if (!buf)
		return;

	for (bit = 0; bit < array_size(pbr_map_reason_str); bit++) {
		if ((reason & (1 << bit)) && (len < size)) {
			len += snprintf((buf + len), (size - len), "%s%s",
					(len > 0) ? ", " : "",
					pbr_map_reason_str[bit]);
		}
	}
}

#if !defined (HAVE_BASEBOX)
void pbr_map_final_interface_deletion(struct pbr_map *pbrm,
				      struct pbr_map_interface *pmi)
{
	if (pmi->delete && !pbr_map_interface_is_installed(pbrm, pmi)) {
		listnode_delete(pbrm->incoming, pmi);
		pmi->pbrm = NULL;

		bf_release_index(pbrm->ifi_bitfield, pmi->install_bit);
		XFREE(MTYPE_PBR_MAP_INTERFACE, pmi);
	}
}
#else
void pbr_map_final_interface_deletion(struct pbr_map *pbrm,
				      struct pbr_map_interface *pmi)
{
	if (pmi->delete && pbr_map_interface_is_installed(pbrm, pmi)) {
		listnode_delete(pbrm->incoming, pmi);
		pmi->pbrm = NULL;

		bf_release_index(pbrm->ifi_bitfield, pmi->install_bit);
		XFREE(MTYPE_PBR_MAP_INTERFACE, pmi);
	}
}
#endif /* HAVE BASEBOX */
void pbr_map_interface_delete(struct pbr_map *pbrm, struct interface *ifp_del)
{
	struct listnode *node;
	struct pbr_map_interface *pmi;

	for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, node, pmi)) {
		if (ifp_del == pmi->ifp)
			break;
	}

	if (pmi)
		pbr_map_policy_delete(pbrm, pmi);
}

void pbr_map_add_interface(struct pbr_map *pbrm, struct interface *ifp_add)
{
	struct listnode *node;
	struct pbr_map_interface *pmi;

	for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, node, pmi)) {
		if (ifp_add == pmi->ifp)
			return;
	}

	pmi = XCALLOC(MTYPE_PBR_MAP_INTERFACE, sizeof(*pmi));
	pmi->ifp = ifp_add;
	pmi->pbrm = pbrm;
	listnode_add_sort(pbrm->incoming, pmi);

	bf_assign_index(pbrm->ifi_bitfield, pmi->install_bit);

	pbr_map_check_valid(pbrm->name);
	if (pbrm->valid)
		pbr_map_install(pbrm);
}

static int
pbr_map_policy_interface_update_common(const struct interface *ifp,
				       struct pbr_interface **pbr_ifp,
				       struct pbr_map **pbrm)
{
	if (!ifp->info) {
		DEBUGD(&pbr_dbg_map, "%s: %s has no pbr_interface info",
		       __func__, ifp->name);
		return -1;
	}

	*pbr_ifp = ifp->info;

	*pbrm = pbrm_find((*pbr_ifp)->mapname);

	if (!*pbrm) {
		DEBUGD(&pbr_dbg_map, "%s: applied PBR-MAP(%s) does not exist?",
		       __func__, (*pbr_ifp)->mapname);
		return -1;
	}

	return 0;
}

void pbr_map_policy_interface_update(const struct interface *ifp, bool state_up)
{
	struct pbr_interface *pbr_ifp;
	struct pbr_map_sequence *pbrms;
	struct pbr_map *pbrm;
	struct listnode *node, *inode;
	struct pbr_map_interface *pmi;

	if (pbr_map_policy_interface_update_common(ifp, &pbr_ifp, &pbrm))
		return;

	DEBUGD(&pbr_dbg_map, "%s: %s %s rules on interface %s", __func__,
	       pbr_ifp->mapname, (state_up ? "installing" : "removing"),
	       ifp->name);

	/*
	 * Walk the list and install/remove maps on the interface.
	 */
	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms))
		for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, inode, pmi))
			if (pmi->ifp == ifp && pbr_map_interface_is_valid(pmi))
				pbr_send_pbr_map(pbrms, pmi, state_up, true);
}

static void pbrms_vrf_update(struct pbr_map_sequence *pbrms,
			     const struct pbr_vrf *pbr_vrf)
{
	const char *vrf_name = pbr_vrf_name(pbr_vrf);

	if (pbrms->vrf_lookup
	    && (strncmp(vrf_name, pbrms->vrf_name, sizeof(pbrms->vrf_name))
		== 0)) {
		DEBUGD(&pbr_dbg_map, "\tSeq %u uses vrf %s (%u), updating map",
		       pbrms->seqno, vrf_name, pbr_vrf_id(pbr_vrf));

		pbr_map_check(pbrms, false);
	}
}

/* Vrf enabled/disabled */
void pbr_map_vrf_update(const struct pbr_vrf *pbr_vrf)
{
	struct pbr_map *pbrm;
	struct pbr_map_sequence *pbrms;
	struct listnode *node;

	if (!pbr_vrf)
		return;

	bool enabled = pbr_vrf_is_enabled(pbr_vrf);

	DEBUGD(&pbr_dbg_map, "%s: %s (%u) %s, updating pbr maps", __func__,
	       pbr_vrf_name(pbr_vrf), pbr_vrf_id(pbr_vrf),
	       enabled ? "enabled" : "disabled");

	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		DEBUGD(&pbr_dbg_map, "%s: Looking at %s", __func__, pbrm->name);
		for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms))
			pbrms_vrf_update(pbrms, pbr_vrf);
	}
}

void pbr_map_write_interfaces(struct vty *vty, struct interface *ifp)
{
	struct pbr_interface *pbr_ifp = ifp->info;

	if (pbr_ifp
	    && strncmp(pbr_ifp->mapname, "", sizeof(pbr_ifp->mapname)) != 0)
		vty_out(vty, " pbr-policy %s\n", pbr_ifp->mapname);
}

struct pbr_map *pbrm_find(const char *name)
{
	struct pbr_map pbrm;

	strlcpy(pbrm.name, name, sizeof(pbrm.name));

	return RB_FIND(pbr_map_entry_head, &pbr_maps, &pbrm);
}

extern void pbr_map_delete(struct pbr_map_sequence *pbrms)
{
	struct pbr_map *pbrm;
	struct listnode *inode;
	struct pbr_map_interface *pmi;

	pbrm = pbrms->parent;

	for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, inode, pmi))
		pbr_send_pbr_map(pbrms, pmi, false, false);

	if (pbrms->nhg)
		pbr_nht_delete_individual_nexthop(pbrms);

	listnode_delete(pbrm->seqnumbers, pbrms);

	if (pbrm->seqnumbers->count == 0) {
		RB_REMOVE(pbr_map_entry_head, &pbr_maps, pbrm);

		bf_free(pbrm->ifi_bitfield);
		XFREE(MTYPE_PBR_MAP, pbrm);
	}
}

static void pbr_map_delete_common(struct pbr_map_sequence *pbrms)
{
	struct pbr_map *pbrm = pbrms->parent;

	pbr_map_pbrms_uninstall(pbrms);

	pbrm->valid = false;
	pbrms->nhs_installed = false;
	pbrms->reason |= PBR_MAP_INVALID_NO_NEXTHOPS;
	XFREE(MTYPE_TMP, pbrms->nhgrp_name);
}

void pbr_map_delete_nexthops(struct pbr_map_sequence *pbrms)
{
	pbr_map_delete_common(pbrms);
}

void pbr_map_delete_vrf(struct pbr_map_sequence *pbrms)
{
	pbr_map_delete_common(pbrms);
}

struct pbr_map_sequence *pbrms_lookup_unique(uint32_t unique, char *ifname,
					     struct pbr_map_interface **ppmi)
{
	struct pbr_map_sequence *pbrms;
	struct listnode *snode, *inode;
	struct pbr_map_interface *pmi;
	struct pbr_map *pbrm;

	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, inode, pmi)) {
			if (strncmp(pmi->ifp->name, ifname, INTERFACE_NAMSIZ)
			    != 0)
				continue;

			if (ppmi)
				*ppmi = pmi;

			for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, snode,
						  pbrms)) {
				DEBUGD(&pbr_dbg_map, "%s: Comparing %u to %u",
				       __func__, pbrms->unique, unique);
				if (pbrms->unique == unique)
					return pbrms;
			}
		}
	}

	return NULL;
}

static void pbr_map_add_interfaces(struct pbr_map *pbrm)
{
	struct interface *ifp;
	struct pbr_interface *pbr_ifp;
	struct vrf *vrf;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name) {
		FOR_ALL_INTERFACES (vrf, ifp) {
			if (ifp->info) {
				pbr_ifp = ifp->info;
				if (strcmp(pbrm->name, pbr_ifp->mapname) == 0)
					pbr_map_add_interface(pbrm, ifp);
			}
		}
	}
}

/* Decodes a standardized DSCP into its representative value */

uint8_t pbr_map_decode_dscp_enum(const char *name)
{
	/* Standard Differentiated Services Field Codepoints */
	if (!strcmp(name, "cs0"))
		return 0;
	if (!strcmp(name, "cs1"))
		return 8;
	if (!strcmp(name, "cs2"))
		return 16;
	if (!strcmp(name, "cs3"))
		return 24;
	if (!strcmp(name, "cs4"))
		return 32;
	if (!strcmp(name, "cs5"))
		return 40;
	if (!strcmp(name, "cs6"))
		return 48;
	if (!strcmp(name, "cs7"))
		return 56;
	if (!strcmp(name, "af11"))
		return 10;
	if (!strcmp(name, "af12"))
		return 12;
	if (!strcmp(name, "af13"))
		return 14;
	if (!strcmp(name, "af21"))
		return 18;
	if (!strcmp(name, "af22"))
		return 20;
	if (!strcmp(name, "af23"))
		return 22;
	if (!strcmp(name, "af31"))
		return 26;
	if (!strcmp(name, "af32"))
		return 28;
	if (!strcmp(name, "af33"))
		return 30;
	if (!strcmp(name, "af41"))
		return 34;
	if (!strcmp(name, "af42"))
		return 36;
	if (!strcmp(name, "af43"))
		return 38;
	if (!strcmp(name, "ef"))
		return 46;
	if (!strcmp(name, "voice-admit"))
		return 44;

	/* No match? Error out */
	return -1;
}

struct pbr_map_sequence *pbrms_get(const char *name, uint32_t seqno)
{
	struct pbr_map *pbrm           = NULL;
	struct pbr_map_sequence *pbrms = NULL;
	struct listnode *node          = NULL;

	pbrm = pbrm_find(name);
	if (!pbrm) {
		pbrm = XCALLOC(MTYPE_PBR_MAP, sizeof(*pbrm));
		snprintf(pbrm->name, sizeof(pbrm->name), "%s", name);

		pbrm->seqnumbers = list_new();
		pbrm->seqnumbers->cmp =
			(int (*)(void *, void *))pbr_map_sequence_compare;
		pbrm->seqnumbers->del =
			 (void (*)(void *))pbr_map_sequence_delete;

		pbrm->incoming = list_new();
		pbrm->incoming->cmp =
			(int (*)(void *, void *))pbr_map_interface_compare;
		pbrm->incoming->del =
			(void (*)(void *))pbr_map_interface_list_delete;

		RB_INSERT(pbr_map_entry_head, &pbr_maps, pbrm);

		bf_init(pbrm->ifi_bitfield, 64);
		pbr_map_add_interfaces(pbrm);
	}

	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
		if (pbrms->seqno == seqno)
			break;

	}

	if (!pbrms) {
		pbrms = XCALLOC(MTYPE_PBR_MAP_SEQNO, sizeof(*pbrms));
		pbrms->unique = pbr_map_sequence_unique++;
		pbrms->seqno = seqno;
		pbrms->ruleno = pbr_nht_get_next_rule(seqno);
		pbrms->parent = pbrm;
		pbrms->match_vlan_id = 0;
		pbrms->set_vlan_id   = 0;
		pbrms->match_vlan_flags  = 0;
		pbrms->action_vlan_flags = 0;

		pbrms->match_proto_id  = PBR_UNDEFINED_VALUE;

		pbrms->match_pcp  = 0;
		pbrms->action_pcp = 0;

		pbrms->match_udp_src_port =  PBR_UNDEFINED_VALUE;
		pbrms->match_udp_dst_port =  PBR_UNDEFINED_VALUE;
		pbrms->action_udp_src_port = PBR_UNDEFINED_VALUE;
		pbrms->action_udp_dst_port = PBR_UNDEFINED_VALUE;
		pbrms->match_tcp_src_port  = PBR_UNDEFINED_VALUE;
		pbrms->match_tcp_dst_port  = PBR_UNDEFINED_VALUE;
		pbrms->action_tcp_src_port = PBR_UNDEFINED_VALUE;
		pbrms->action_tcp_dst_port = PBR_UNDEFINED_VALUE;

		pbrms->action_queue_id     = 255;
#if defined(HAVE_BASEBOX)
		pbrms->reason =
			PBR_MAP_INVALID_EMPTY;
#else
		pbrms->reason =
			PBR_MAP_INVALID_EMPTY |
			PBR_MAP_INVALID_NO_NEXTHOPS;
#endif /*HAVE_BASEBOX */
		pbrms->vrf_name[0] = '\0';

		QOBJ_REG(pbrms, pbr_map_sequence);
		listnode_add_sort(pbrm->seqnumbers, pbrms);
	}

	return pbrms;
}
#if !defined(HAVE_BASEBOX)
static void
pbr_map_sequence_check_nexthops_valid(struct pbr_map_sequence *pbrms)
{
	/* Check if any are present first */
	if (!pbrms->vrf_unchanged && !pbrms->vrf_lookup && !pbrms->nhg
	    && !pbrms->nhgrp_name) {
		pbrms->reason |= PBR_MAP_INVALID_NO_NEXTHOPS;
		return;
	}

	/*
	 * Check validness of vrf.
	 */

	/* This one can be considered always valid */
	if (pbrms->vrf_unchanged)
		pbrms->nhs_installed = true;

	if (pbrms->vrf_lookup) {
		struct pbr_vrf *pbr_vrf =
			pbr_vrf_lookup_by_name(pbrms->vrf_name);

		if (pbr_vrf && pbr_vrf_is_valid(pbr_vrf))
			pbrms->nhs_installed = true;
		else
			pbrms->reason |= PBR_MAP_INVALID_VRF;
	}

	/*
	 * Check validness of the nexthop or nexthop-group
	 */

	/* Only nexthop or nexthop group allowed */
	if (pbrms->nhg && pbrms->nhgrp_name)
		pbrms->reason |= PBR_MAP_INVALID_BOTH_NHANDGRP;

	if (pbrms->nhg &&
	    !pbr_nht_nexthop_group_valid(pbrms->internal_nhg_name))
		pbrms->reason |= PBR_MAP_INVALID_NEXTHOP;

	if (pbrms->nhgrp_name) {
		if (!pbr_nht_nexthop_group_valid(pbrms->nhgrp_name))
			pbrms->reason |= PBR_MAP_INVALID_NEXTHOP_GROUP;
		else
			pbrms->nhs_installed = true;
	}
}
#endif /* HAVE_BASEBOX */
static void pbr_map_sequence_check_not_empty(struct pbr_map_sequence *pbrms)
{
	if (!pbrms->src                   &&
	    !pbrms->dst                   &&
	    !pbrms->action_src            &&
	    !pbrms->action_dst            &&
	    !pbrms->match_dsfield         &&
	    !pbrms->action_dsfield        &&
	    !pbrms->match_pcp             &&
	    !pbrms->action_pcp            &&
	    !pbrms->mark                  &&
	    pbrms->action_queue_id == 255 &&
	    pbrms->match_proto_id      == PBR_UNDEFINED_VALUE &&
	    pbrms->match_udp_src_port  == PBR_UNDEFINED_VALUE &&
	    pbrms->action_udp_src_port == PBR_UNDEFINED_VALUE &&
	    pbrms->match_udp_dst_port  == PBR_UNDEFINED_VALUE &&
	    pbrms->action_udp_dst_port == PBR_UNDEFINED_VALUE &&
	    pbrms->match_tcp_src_port  == PBR_UNDEFINED_VALUE &&
	    pbrms->action_tcp_src_port == PBR_UNDEFINED_VALUE &&
	    pbrms->match_tcp_dst_port  == PBR_UNDEFINED_VALUE &&
	    pbrms->action_tcp_dst_port == PBR_UNDEFINED_VALUE &&
	    !pbrms->match_vlan_id     &&
	    !pbrms->set_vlan_id       &&
	    !pbrms->match_vlan_flags  &&
	    !pbrms->action_vlan_flags &&
	    !pbrms->vrf_unchanged     &&
	    !pbrms->vrf_lookup        &&
	    !pbrms->nhg               &&
	    !pbrms->nhgrp_name)
		pbrms->reason |= PBR_MAP_INVALID_EMPTY;
}
static void
pbr_map_sequence_check_set_and_strip_vlan(struct pbr_map_sequence *pbrms)
{
	/* the set vlan tag action does the following:
	 * 1- if the frame is untagged, it tags the frame with the set vlan id
	 * 2- if the frame is tagged, the set replaces the frame tag with the
	 *    configured set tag.
	 * the strip vlan any removes the inner tag .
	 * so a set vlan X with a strip action is not valid.
	 */
	if((pbrms->set_vlan_id != 0) &&
	   (pbrms->action_vlan_flags != 0))
		pbrms->reason |= PBR_MAP_INVALID_SET_STRIP_VLAN;
}

/*
 * Checks to see if we think that the pbmrs is valid.  If we think
 * the config is valid return true.
 */
static void pbr_map_sequence_check_valid(struct pbr_map_sequence *pbrms)
{
	pbr_map_sequence_check_not_empty(pbrms);
	pbr_map_sequence_check_set_and_strip_vlan(pbrms);
#if !defined(HAVE_BASEBOX)
	pbr_map_sequence_check_nexthops_valid(pbrms);
#endif
}

static bool pbr_map_check_valid_internal(struct pbr_map *pbrm)
{
	struct pbr_map_sequence *pbrms;
	struct listnode *node;

	pbrm->valid = true;
	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
		pbrms->reason = 0;
		pbr_map_sequence_check_valid(pbrms);
		/*
		 * A pbr_map_sequence that is invalid causes
		 * the whole shebang to be invalid
		 */
		if (pbrms->reason != 0)
			pbrm->valid = false;
	}

	return pbrm->valid;
}

/*
 * For a given PBR-MAP check to see if we think it is a
 * valid config or not.  If so note that it is and return
 * that we are valid.
 */
bool pbr_map_check_valid(const char *name)
{
	struct pbr_map *pbrm;

	pbrm = pbrm_find(name);
	if (!pbrm) {
		DEBUGD(&pbr_dbg_map,
		       "%s: Specified PBR-MAP(%s) does not exist?", __func__,
		       name);
		return false;
	}

	pbr_map_check_valid_internal(pbrm);
	return pbrm->valid;
}

void pbr_map_schedule_policy_from_nhg(const char *nh_group, bool installed)
{
	struct pbr_map_sequence *pbrms;
	struct pbr_map *pbrm;
	struct listnode *node;

	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		DEBUGD(&pbr_dbg_map, "%s: Looking at %s", __func__, pbrm->name);
		for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
			DEBUGD(&pbr_dbg_map, "\tNH Grp name: %s",
			       pbrms->nhgrp_name ?
			       pbrms->nhgrp_name : pbrms->internal_nhg_name);

			if (pbrms->nhgrp_name
			    && (strcmp(nh_group, pbrms->nhgrp_name) == 0)) {
				pbrms->nhs_installed = installed;

				pbr_map_check(pbrms, false);
			}

			if (pbrms->nhg
			    && (strcmp(nh_group, pbrms->internal_nhg_name)
				== 0)) {
				pbrms->nhs_installed = installed;

				pbr_map_check(pbrms, false);
			}
		}
	}
}

void pbr_map_policy_install(const char *name)
{
	struct pbr_map_sequence *pbrms;
	struct pbr_map *pbrm;
	struct listnode *node, *inode;
	struct pbr_map_interface *pmi;

	DEBUGD(&pbr_dbg_map, "%s: for %s", __func__, name);
	pbrm = pbrm_find(name);
	if (!pbrm)
		return;

	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
		DEBUGD(&pbr_dbg_map,
		       "%s: Looking at what to install %s(%u) %d %d", __func__,
		       name, pbrms->seqno, pbrm->valid, pbrms->nhs_installed);
#if !defined(HAVE_BASEBOX)
		if (pbrm->valid && pbrms->nhs_installed
		    && pbrm->incoming->count)
#else
		if(pbrm->incoming->count)
#endif /* HAVE_BASEBOX */
		{
			DEBUGD(&pbr_dbg_map, "\tInstalling %s %u", pbrm->name,
			       pbrms->seqno);
			for (ALL_LIST_ELEMENTS_RO(pbrm->incoming, inode, pmi))
				if (pbr_map_interface_is_valid(pmi))
					pbr_send_pbr_map(pbrms, pmi, true,
							 false);
		}
	}
}
#if !defined(HAVE_BASEBOX)
void pbr_map_policy_delete(struct pbr_map *pbrm, struct pbr_map_interface *pmi)
{
	struct listnode *node;
	struct pbr_map_sequence *pbrms;
	bool sent = false;

	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms))
		if (pbr_send_pbr_map(pbrms, pmi, false, true))
			sent = true; /* rule removal sent to zebra */

	pmi->delete = true;

	/*
	 * If we actually sent something for deletion, wait on zapi callback
	 * before clearing data.
	 */
	if (sent)
		return;

	pbr_map_final_interface_deletion(pbrm, pmi);
}
#else
void pbr_map_policy_delete(struct pbr_map *pbrm, struct pbr_map_interface *pmi)
{
	struct listnode *node;
	struct pbr_map_sequence *pbrms;

	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms))
		pbr_send_pbr_map(pbrms, pmi, false, true);

	pmi->delete = true;
	pbr_map_final_interface_deletion(pbrm, pmi);
}

#endif /* HAVE_BASEBOX */
/*
 * For a nexthop group specified, see if any of the pbr-maps
 * are using it and if so, check to see that we are still
 * valid for usage.  If we are valid then schedule the installation/deletion
 * of the pbr-policy.
 */
void pbr_map_check_nh_group_change(const char *nh_group)
{
	struct pbr_map_sequence *pbrms;
	struct pbr_map *pbrm;
	struct listnode *node, *inode;
	struct pbr_map_interface *pmi;
	bool found_name;

	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
			found_name = false;
			if (pbrms->nhgrp_name)
				found_name =
					!strcmp(nh_group, pbrms->nhgrp_name);
			else if (pbrms->nhg)
				found_name = !strcmp(nh_group,
						     pbrms->internal_nhg_name);

			if (found_name) {
				bool original = pbrm->valid;

				pbr_map_check_valid_internal(pbrm);

				if (pbrm->valid && (original != pbrm->valid))
					pbr_map_install(pbrm);

				if (pbrm->valid == false)
					for (ALL_LIST_ELEMENTS_RO(
						     pbrm->incoming, inode,
						     pmi))
						pbr_send_pbr_map(pbrms, pmi,
								 false, false);
			}
		}
	}
}

void pbr_map_check_vrf_nh_group_change(const char *nh_group,
				       struct pbr_vrf *pbr_vrf,
				       uint32_t old_vrf_id)
{
	struct pbr_map *pbrm;
	struct pbr_map_sequence *pbrms;
	struct listnode *node;


	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
			if (pbrms->nhgrp_name)
				continue;

			if (pbrms->nhg == NULL)
				continue;

			if (strcmp(nh_group, pbrms->internal_nhg_name))
				continue;

			if (pbrms->nhg->nexthop == NULL)
				continue;

			if (pbrms->nhg->nexthop->vrf_id != old_vrf_id)
				continue;

			pbrms->nhg->nexthop->vrf_id = pbr_vrf_id(pbr_vrf);
		}
	}
}

void pbr_map_check_interface_nh_group_change(const char *nh_group,
					     struct interface *ifp,
					     ifindex_t oldifindex)
{
	struct pbr_map *pbrm;
	struct pbr_map_sequence *pbrms;
	struct listnode *node;

	RB_FOREACH (pbrm, pbr_map_entry_head, &pbr_maps) {
		for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms)) {
			if (pbrms->nhgrp_name)
				continue;

			if (pbrms->nhg == NULL)
				continue;

			if (strcmp(nh_group, pbrms->internal_nhg_name))
				continue;

			if (pbrms->nhg->nexthop == NULL)
				continue;

			if (pbrms->nhg->nexthop->ifindex != oldifindex)
				continue;

			pbrms->nhg->nexthop->ifindex = ifp->ifindex;
		}
	}
}

void pbr_map_check(struct pbr_map_sequence *pbrms, bool changed)
{
	struct pbr_map *pbrm;
	bool install;

	pbrm = pbrms->parent;
	DEBUGD(&pbr_dbg_map, "%s: for %s(%u)", __func__, pbrm->name,
	       pbrms->seqno);
	if (pbr_map_check_valid(pbrm->name))
		DEBUGD(&pbr_dbg_map, "We are totally valid %s",
		       pbrm->name);

	if (pbrms->reason == PBR_MAP_VALID_SEQUENCE_NUMBER) {
		install = true;
		DEBUGD(&pbr_dbg_map, "%s: Installing %s(%u) reason: %llu",
		       __func__, pbrm->name, pbrms->seqno, pbrms->reason);
		DEBUGD(&pbr_dbg_map,
		       "\tSending PBR_MAP_POLICY_INSTALL event");
	} else {
		install = false;
		DEBUGD(&pbr_dbg_map, "%s: Removing %s(%u) reason: %llu",
		       __func__, pbrm->name, pbrms->seqno, pbrms->reason);
	}

	if (install)
		pbr_map_pbrms_install(pbrms, changed);
	else
		pbr_map_pbrms_uninstall(pbrms);
}

void pbr_map_install(struct pbr_map *pbrm)
{
	struct pbr_map_sequence *pbrms;
	struct listnode *node;

	if (!pbrm->incoming->count)
		return;

	for (ALL_LIST_ELEMENTS_RO(pbrm->seqnumbers, node, pbrms))
		pbr_map_pbrms_install(pbrms, false);
}

void pbr_map_init(void)
{
	RB_INIT(pbr_map_entry_head, &pbr_maps);

	pbr_map_sequence_unique = 1;
}
