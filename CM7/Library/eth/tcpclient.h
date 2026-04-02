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

typedef void (*TcpClientRxHandler_t)(const char *data, uint16_t length);

typedef struct
{
    ip_addr_t ServerIp;
    uint16_t ServerPort;
    struct netif *Netif;
    TcpClientRxHandler_t RxHandler;
} TcpClientConfig_t;

/**
 * @brief Populate a TCP client configuration structure.
 * @param config Pointer to the destination configuration structure.
 * @param serverIp Pointer to the IPv4/IPv6 server address.
 * @param serverPort Remote TCP server port number.
 * @param netif Pointer to the LwIP network interface used by the client.
 * @param rxHandler Callback invoked after a TCP packet is acknowledged.
 * @return None.
 */
void TcpClient_BuildConfig(TcpClientConfig_t *config,
                           const ip_addr_t *serverIp,
                           uint16_t serverPort,
                           struct netif *netif,
                           TcpClientRxHandler_t rxHandler);

/**
 * @brief Initialize the TCP client task and internal resources.
 * @param config Pointer to a valid TCP client configuration structure.
 * @return `0` on success or if already initialized.
 * @return `-1` if `config` is `NULL` or `config->Netif` is `NULL`.
 * @return `-2` if the internal transmit mailbox creation fails.
 * @return `-3` if the TCP client task creation fails.
 */
int32_t TcpClient_Init(const TcpClientConfig_t *config);

/**
 * @brief Queue a text message for transmission to the connected TCP server.
 * @param text Pointer to a null-terminated string to send.
 * @return `0` on success.
 * @return `-1` if the TCP client is not initialized or `text` is `NULL`.
 * @return `-2` if memory allocation for the queued message fails.
 * @return `-3` if the transmit mailbox is full and the message cannot be queued.
 */
int32_t TcpClient_Send(const char *text);

/**
 * @brief Get the current TCP connection state.
 * @param None.
 * @return `1` when the client is currently connected to the server.
 * @return `0` when the client is disconnected.
 */
uint8_t TcpClient_IsConnected(void);

/**
 * @brief Request that the TCP client close the current connection and reconnect.
 * @param None.
 * @return None.
 */
void TcpClient_RequestReconnect(void);

#ifdef __cplusplus
}
#endif

#endif
