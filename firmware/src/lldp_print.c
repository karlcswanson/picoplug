/*
 * lldp_print.c - human-readable dump of a parsed neighbor for the debug UART.
 */
#include "lldp_print.h"

#include <stdio.h>

void lldp_print(const lldp_neighbor_t *n)
{
    char s[LLDP_ID_STR_LEN];

    printf("--- %s neighbor (from %02x:%02x:%02x:%02x:%02x:%02x%s) ---\n",
           n->proto == NEIGHBOR_PROTO_CDP ? "CDP" : "LLDP",
           n->src_mac[0], n->src_mac[1], n->src_mac[2], n->src_mac[3], n->src_mac[4], n->src_mac[5],
           n->vlan_tagged ? ", 802.1Q tagged" : "");
    if (n->has_platform) printf("  platform:  %s\n", n->platform);
    if (n->has_vtp_domain) printf("  vtp:       %s\n", n->vtp_domain);
    if (n->has_duplex) printf("  duplex:    %s\n", n->duplex_full ? "full" : "half");
    lldp_format_chassis_id(&n->chassis_id, s, sizeof s);
    printf("  chassis:   %s\n", s);
    lldp_format_port_id(&n->port_id, s, sizeof s);
    printf("  port:      %s\n", s);
    printf("  ttl:       %u s\n", n->ttl);
    if (n->has_port_desc) printf("  port desc: %s\n", n->port_desc);
    if (n->has_sys_name)  printf("  sys name:  %s\n", n->sys_name);
    if (n->has_sys_desc)  printf("  sys desc:  %s\n", n->sys_desc);
    if (n->has_caps) {
        printf("  caps:      ");
        for (unsigned b = 0; b < 11; b++)
            if (n->caps_enabled & (1u << b)) printf("%s ", lldp_cap_name(b));
        printf("(supported 0x%04x)\n", n->caps_supported);
    }
    for (int i = 0; i < n->n_mgmt_addrs; i++) {
        const lldp_mgmt_addr_t *m = &n->mgmt_addrs[i];
        lldp_format_addr(m->afi, m->addr, m->len, s, sizeof s);
        printf("  mgmt:      %s (if %u)\n", s, (unsigned)m->if_number);
    }
    if (n->has_pvid) printf("  pvid:      %u\n", n->pvid);
    for (int i = 0; i < n->n_vlans; i++)
        printf("  vlan:      %u \"%s\"\n", n->vlans[i].id, n->vlans[i].name);
    if (n->macphy.present)
        printf("  mac/phy:   autoneg %s/%s, mau %u\n",
               n->macphy.autoneg_supported ? "sup" : "nosup",
               n->macphy.autoneg_enabled ? "on" : "off", n->macphy.mau_type);
    if (n->power_mdi.present) {
        printf("  poe:       %s, pairs %u, class %d",
               n->power_mdi.pse ? "PSE" : "PD", n->power_mdi.power_pairs,
               n->power_mdi.power_class == 0xFF ? -1 : n->power_mdi.power_class);
        if (n->power_mdi.dll_present)
            printf(", req %u.%u W, alloc %u.%u W, prio %u",
                   n->power_mdi.pd_requested_dw / 10, n->power_mdi.pd_requested_dw % 10,
                   n->power_mdi.pse_allocated_dw / 10, n->power_mdi.pse_allocated_dw % 10,
                   n->power_mdi.power_priority);
        printf("\n");
    }
    if (n->has_max_frame) printf("  max frame: %u\n", n->max_frame_size);
    for (int i = 0; i < n->n_med_policies; i++) {
        const lldp_med_policy_t *p = &n->med_policies[i];
        printf("  med pol:   %s vlan %u%s pri %u dscp %u%s\n", lldp_med_app_name(p->app_type),
               p->vlan_id, p->tagged ? " (tagged)" : "", p->l2_priority, p->dscp,
               p->unknown_policy ? " [unknown]" : "");
    }
    if (n->med_power.present)
        printf("  med power: %s %u.%u W prio %u\n", n->med_power.power_type ? "PD" : "PSE",
               n->med_power.power_dw / 10, n->med_power.power_dw % 10, n->med_power.power_priority);
    if (n->n_unknown) {
        printf("  unknown:   %u TLV(s)", n->n_unknown);
        for (int i = 0; i < n->n_unknown_stored; i++) {
            const lldp_unknown_tlv_t *u = &n->unknown[i];
            if (u->is_org) printf(" [oui %02x%02x%02x/%u len %u]", u->oui[0], u->oui[1], u->oui[2], u->subtype, u->len);
            else printf(" [type %u len %u]", u->type, u->len);
        }
        printf("\n");
    }
    if (n->truncated) printf("  (some fields truncated to fit)\n");
}
