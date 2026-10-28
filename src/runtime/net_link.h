/* Local links between game instances for the emulated ad-hoc network (see net_link.c). */
#pragma once
#include <stdint.h>
#include <stddef.h>

enum {
    NL_JOIN = 1,       /* guest -> hub: my node number */
    NL_BEACON,         /* ad-hoc group beacon: group name */
    NL_MATCH,          /* matching packet */
    NL_PTP_SYN, NL_PTP_SYNACK, NL_PTP_RST, NL_PTP_DATA, NL_PTP_FIN,
    NL_PDP,
    NL_PAD,            /* hub -> guest: that player's controller */
    NL_CMD,            /* hub -> guest: automation command */
};

int  net_link_hub(int want_port);            /* become node 0 (port 0: any); returns the TCP port, 0 on failure */
int  net_link_join(int port, int node);      /* connect to the hub as node 1..3 */
int  net_link_self(void);                    /* our node number, -1 when not linked */
int  net_link_active(void);
int  net_link_connected(int node);
void net_link_send(int dst, int type, const void *p, size_t n);   /* dst 0xFF: everyone else */
void net_link_poll(void);                    /* read and dispatch everything waiting */
void net_link_set_handler(void (*fn)(int src, int type, const uint8_t *p, size_t n));

/* players simulated in this process: frames sent to nodes 1..n go to fn, and fn answers with
 * net_link_inject (delivered at the next poll). n = 0 removes them. */
void net_link_virtual(int n, void (*fn)(int node, int type, const uint8_t *p, size_t n));
void net_link_inject(int src, int type, const void *p, size_t n);
