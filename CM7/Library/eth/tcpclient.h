#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "lwip/ip_addr.h"
#include "lwip/netif.h"

#ifndef TCPCLIENT_TX_MSG_MAX_LEN
#define TCPCLIENT_TX_MSG_MAX_LEN        128U
#endif

#ifndef TCPCLIENT_TX_MBOX_SIZE
#define TCPCLIENT_TX_MBOX_SIZE          16U
#endif

#ifndef TCPCLIENT_RX_TIMEOUT_MS
#define TCPCLIENT_RX_TIMEOUT_MS         1000U
#endif

#ifndef TCPCLIENT_RECONNECT_DELAY_MS
#define TCPCLIENT_RECONNECT_DELAY_MS    1000U
#endif

#ifndef TCPCLIENT_LINK_WAIT_MS
#define TCPCLIENT_LINK_WAIT_MS          250U
#endif

typedef struct
{
    ip_addr_t ServerIp;
    uint16_t ServerPort;
    struct netif *Netif;
} TcpClientConfig_t;

void TcpClient_BuildConfig(TcpClientConfig_t *config,
                           const ip_addr_t *serverIp,
                           uint16_t serverPort,
                           struct netif *netif);

int32_t TcpClient_Init(const TcpClientConfig_t *config);
int32_t TcpClient_Send(const char *text);
uint8_t TcpClient_IsConnected(void);
void TcpClient_RequestReconnect(void);

#ifdef __cplusplus
}
#endif

#endif
