#include "lwip/opt.h"
#include "lwip/errno.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/mem.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "tcpclient.h"

#include <string.h>

#define TCPCLIENT_CONNECT_TIMEOUT_MS   1000U
#define TCPCLIENT_RX_BUFFER_LEN        100U
#define TCPCLIENT_STREAM_CHUNK_LEN     3072U

typedef struct
{
    uint16_t length;
    uint8_t data[TCPCLIENT_TX_MSG_MAX_LEN];
} TcpClientTxMessage_t;

typedef struct
{
    uint8_t active;
    uint8_t header_sent;
    uint16_t header_len;
    uint16_t header_offset;
    uint32_t total_size;
    uint32_t bytes_sent;
    TcpClientStreamReadFn read_fn;
    TcpClientStreamDoneFn done_fn;
    void *context;
    uint8_t header[TCPCLIENT_TX_MSG_MAX_LEN];
    uint8_t chunk[TCPCLIENT_STREAM_CHUNK_LEN];
} TcpClientStreamState_t;

typedef struct
{
    int sock;
    volatile uint8_t connected;
    volatile uint8_t reconnect_requested;
    uint8_t initialized;
    TcpClientConfig_t cfg;
    sys_mbox_t tx_mbox;
    sys_mutex_t tx_mutex;
    volatile uint32_t pending_tx_msgs;
    TcpClientStreamState_t stream;
} TcpClientState_t;

static TcpClientState_t gTcpClient;

static void TcpClient_CloseSocket(void)
{
    if (gTcpClient.sock >= 0)
    {
        lwip_close(gTcpClient.sock);
        gTcpClient.sock = -1;
    }

    gTcpClient.connected = 0U;
    gTcpClient.stream.active = 0U;
}

static int32_t TcpClient_SetSocketBlockingMode(int sock, int nonblocking)
{
    int flags;

    flags = lwip_fcntl(sock, F_GETFL, 0);
    if (flags < 0)
    {
        return -1;
    }

    if (nonblocking != 0)
    {
        flags |= O_NONBLOCK;
    }
    else
    {
        flags &= ~O_NONBLOCK;
    }

    if (lwip_fcntl(sock, F_SETFL, flags) < 0)
    {
        return -2;
    }

    return 0;
}

/*
 * @brief Make a connect to the server once in a non-blocking fashion
 */
static int32_t TcpClient_ConnectOnce(void)
{
    struct sockaddr_in server_addr;
    fd_set write_set;
    struct timeval timeout;
    int result;
    int so_error;
    socklen_t so_error_len;

    /*
     * Check for the network to be ready. Basically if the link is up.
     * If network link is not ready, return -1
     */
    if ((gTcpClient.cfg.Netif == NULL) ||
        (!netif_is_up(gTcpClient.cfg.Netif)) ||
        (!netif_is_link_up(gTcpClient.cfg.Netif)))
    {
        return -1;
    }

    /*
     * Create a socket Ipv4, fails we get a return code of -2
     */
    gTcpClient.sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (gTcpClient.sock < 0)
    {
        return -2;
    }

    /*
     * Make the socket non-blocking.
     */
    if (TcpClient_SetSocketBlockingMode(gTcpClient.sock, 1) != 0)
    {
        TcpClient_CloseSocket();
        return -3;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(gTcpClient.cfg.ServerPort);
    server_addr.sin_addr.s_addr = ip4_addr_get_u32(ip_2_ip4(&gTcpClient.cfg.ServerIp));

    /*
     * Initiate a connection and set connected to 1 when success
     */
    result = lwip_connect(gTcpClient.sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (result == 0)
    {
        if (TcpClient_SetSocketBlockingMode(gTcpClient.sock, 0) != 0)
        {
            TcpClient_CloseSocket();
            return -8;
        }
        gTcpClient.connected = 1U;
        return 0;
    }

    /*
     * Connect failed immediately for the below reasons
     */
    if ((errno != EINPROGRESS) && (errno != EALREADY) && (errno != EWOULDBLOCK))
    {
        TcpClient_CloseSocket();
        return -4;
    }

    // Make the socket writable
    FD_ZERO(&write_set);
    FD_SET(gTcpClient.sock, &write_set);

    // Wait for the connect to finish, if not timeout
    timeout.tv_sec = (long)(TCPCLIENT_CONNECT_TIMEOUT_MS / 1000U);
    timeout.tv_usec = (long)((TCPCLIENT_CONNECT_TIMEOUT_MS % 1000U) * 1000U);

    result = lwip_select(gTcpClient.sock + 1, NULL, &write_set, NULL, &timeout);
    if ((result <= 0) || (!FD_ISSET(gTcpClient.sock, &write_set)))
    {
        TcpClient_CloseSocket();
        return -5;
    }

    /*
     * After non-blocking connect and select says socket is writable. SO_ERROR gives
     * any pending errors on the socket. If there are any errors close socket and wait for
     * new connection.
     */
    so_error = 0;
    so_error_len = (socklen_t)sizeof(so_error);
    if (lwip_getsockopt(gTcpClient.sock, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) < 0)
    {
        TcpClient_CloseSocket();
        return -6;
    }

    if (so_error != 0)
    {
        TcpClient_CloseSocket();
        return -7;
    }

    if (TcpClient_SetSocketBlockingMode(gTcpClient.sock, 0) != 0)
    {
        TcpClient_CloseSocket();
        return -8;
    }

    gTcpClient.connected = 1U;
    return 0;
}

// Process the Rx from the Server
static void TcpClient_ProcessRx(const char *rx_data, int32_t rx_len)
{
    uint8_t msg[TCPCLIENT_RX_BUFFER_LEN];
    size_t copy_len;

    if (rx_len <= 0)
    {
        return;
    }

    copy_len = ((size_t)rx_len < sizeof(msg)) ? (size_t)rx_len : sizeof(msg);
    memcpy(msg, rx_data, copy_len);

    if (gTcpClient.cfg.RxHandler != NULL)
    {
        gTcpClient.cfg.RxHandler((const char *)msg, (uint16_t)copy_len);
    }
}

static int32_t TcpClient_SendSocketBuffer(const uint8_t *data, uint16_t length)
{
    int sent_len;
    uint16_t offset;

    offset = 0U;
    while (offset < length)
    {
        sent_len = lwip_send(gTcpClient.sock, &data[offset], (size_t)(length - offset), 0);
        if (sent_len <= 0)
        {
            gTcpClient.reconnect_requested = 1U;
            return -1;
        }

        offset = (uint16_t)(offset + (uint16_t)sent_len);
    }

    return 0;
}

static void TcpClient_FinishStream(void)
{
    if ((gTcpClient.stream.active != 0U) && (gTcpClient.stream.done_fn != NULL))
    {
        gTcpClient.stream.done_fn(gTcpClient.stream.context);
    }

    memset(&gTcpClient.stream, 0, sizeof(gTcpClient.stream));
}

static int32_t TcpClient_ProcessStream(void)
{
    uint16_t chunk_len;
    int32_t read_status;

    if (gTcpClient.stream.active == 0U)
    {
        return 0;
    }

    if (gTcpClient.stream.header_sent == 0U)
    {
        if (TcpClient_SendSocketBuffer(&gTcpClient.stream.header[gTcpClient.stream.header_offset],
                                       (uint16_t)(gTcpClient.stream.header_len - gTcpClient.stream.header_offset)) != 0)
        {
            TcpClient_FinishStream();
            return -1;
        }

        gTcpClient.stream.header_sent = 1U;
        gTcpClient.stream.header_offset = gTcpClient.stream.header_len;
        return 1;
    }

    if (gTcpClient.stream.bytes_sent >= gTcpClient.stream.total_size)
    {
        TcpClient_FinishStream();
        return 0;
    }

    if (gTcpClient.stream.read_fn == NULL)
    {
        TcpClient_FinishStream();
        return -1;
    }

    chunk_len = 0U;
    read_status = gTcpClient.stream.read_fn(gTcpClient.stream.context,
                                            gTcpClient.stream.chunk,
                                            sizeof(gTcpClient.stream.chunk),
                                            &chunk_len);
    if ((read_status != 0) || (chunk_len == 0U))
    {
        TcpClient_FinishStream();
        return -1;
    }

    if (TcpClient_SendSocketBuffer(gTcpClient.stream.chunk, chunk_len) != 0)
    {
        TcpClient_FinishStream();
        return -1;
    }

    gTcpClient.stream.bytes_sent += chunk_len;
    if (gTcpClient.stream.bytes_sent >= gTcpClient.stream.total_size)
    {
        TcpClient_FinishStream();
    }

    return 1;
}

static void TcpClient_SendQueued(void)
{
    void *msg_ptr = NULL;

    while (sys_arch_mbox_tryfetch(&gTcpClient.tx_mbox, &msg_ptr) != SYS_MBOX_EMPTY)
    {
        TcpClientTxMessage_t *tx = (TcpClientTxMessage_t *)msg_ptr;

        if (tx != NULL)
        {
            sys_mutex_lock(&gTcpClient.tx_mutex);
            if (TcpClient_SendSocketBuffer(tx->data, tx->length) != 0)
            {
                sys_mutex_unlock(&gTcpClient.tx_mutex);
                if (gTcpClient.pending_tx_msgs > 0U)
                {
                    gTcpClient.pending_tx_msgs--;
                }
                mem_free(tx);
                return;
            }
            sys_mutex_unlock(&gTcpClient.tx_mutex);
            if (gTcpClient.pending_tx_msgs > 0U)
            {
                gTcpClient.pending_tx_msgs--;
            }

            mem_free(tx);
        }
    }
}

static void TcpClient_Task(void *arg)
{
    char rx_buffer[TCPCLIENT_RX_BUFFER_LEN];
    fd_set read_set;
    struct timeval timeout;
    int32_t rx_len;

    LWIP_UNUSED_ARG(arg);

    for (;;)
    {
        if (gTcpClient.connected == 0U)
        {
            (void)TcpClient_ConnectOnce();
            if (gTcpClient.connected == 0U)
            {
                sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
                continue;
            }
        }

        if (gTcpClient.reconnect_requested != 0U)
        {
            gTcpClient.reconnect_requested = 0U;
            TcpClient_CloseSocket();
            sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
            continue;
        }

        TcpClient_SendQueued();
        if (gTcpClient.reconnect_requested != 0U)
        {
            continue;
        }

        if (gTcpClient.stream.active != 0U)
        {
            sys_mutex_lock(&gTcpClient.tx_mutex);
            (void)TcpClient_ProcessStream();
            sys_mutex_unlock(&gTcpClient.tx_mutex);
            if (gTcpClient.reconnect_requested != 0U)
            {
                continue;
            }
            if (gTcpClient.stream.active != 0U)
            {
                continue;
            }
        }

        FD_ZERO(&read_set);
        FD_SET(gTcpClient.sock, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(TCPCLIENT_RX_TIMEOUT_MS * 1000U);

        rx_len = lwip_select(gTcpClient.sock + 1, &read_set, NULL, NULL, &timeout);
        if (rx_len < 0)
        {
            TcpClient_CloseSocket();
            sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
            continue;
        }

        if (rx_len == 0)
        {
            continue;
        }

        if (FD_ISSET(gTcpClient.sock, &read_set))
        {
            rx_len = lwip_recv(gTcpClient.sock, rx_buffer, sizeof(rx_buffer), 0);
            if (rx_len <= 0)
            {
                TcpClient_CloseSocket();
                sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
                continue;
            }

            TcpClient_ProcessRx(rx_buffer, rx_len);
        }
    }
}

void TcpClient_BuildConfig(TcpClientConfig_t *config,
                           const ip_addr_t *serverIp,
                           uint16_t serverPort,
                           struct netif *netif,
                           TcpClientRxHandler_t rxHandler)
{
    if ((config == NULL) || (serverIp == NULL))
    {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->ServerIp = *serverIp;
    config->ServerPort = serverPort;
    config->Netif = netif;
    config->RxHandler = rxHandler;
}

int32_t TcpClient_Init(const TcpClientConfig_t *config)
{
    if ((config == NULL) || (config->Netif == NULL))
    {
        return -1;
    }

    if (gTcpClient.initialized != 0U)
    {
        return 0;
    }

    memset(&gTcpClient, 0, sizeof(gTcpClient));
    gTcpClient.sock = -1;
    gTcpClient.cfg = *config;

    if (sys_mbox_new(&gTcpClient.tx_mbox, TCPCLIENT_TX_MBOX_SIZE) != ERR_OK)
    {
        return -2;
    }

    if (sys_mutex_new(&gTcpClient.tx_mutex) != ERR_OK)
    {
        sys_mbox_free(&gTcpClient.tx_mbox);
        return -3;
    }

    if (sys_thread_new("tcpclient_socket",
                       TcpClient_Task,
                       NULL,
                       DEFAULT_THREAD_STACKSIZE,
                       osPriorityNormal) == NULL)
    {
        sys_mutex_free(&gTcpClient.tx_mutex);
        sys_mbox_free(&gTcpClient.tx_mbox);
        return -4;
    }

    gTcpClient.initialized = 1U;
    return 0;
}

int32_t TcpClient_SendBuffer(const uint8_t *data, uint16_t length)
{
    TcpClientTxMessage_t *copy;

    if ((gTcpClient.initialized == 0U) || (data == NULL) || (length == 0U))
    {
        return -1;
    }

    if (length > TCPCLIENT_TX_MSG_MAX_LEN)
    {
        length = TCPCLIENT_TX_MSG_MAX_LEN;
    }

    copy = (TcpClientTxMessage_t *)mem_malloc(sizeof(TcpClientTxMessage_t));
    if (copy == NULL)
    {
        return -2;
    }

    memset(copy, 0, sizeof(*copy));
    copy->length = length;
    memcpy(copy->data, data, length);

    if (sys_mbox_trypost(&gTcpClient.tx_mbox, copy) != ERR_OK)
    {
        mem_free(copy);
        return -3;
    }

    gTcpClient.pending_tx_msgs++;

    return 0;
}

int32_t TcpClient_Send(const char *text)
{
    size_t len;

    if (text == NULL)
    {
        return -1;
    }

    len = strlen(text);
    return TcpClient_SendBuffer((const uint8_t *)text, (uint16_t)len);
}

int32_t TcpClient_StartStream(const uint8_t *header,
                              uint16_t header_len,
                              uint32_t total_size,
                              TcpClientStreamReadFn read_fn,
                              TcpClientStreamDoneFn done_fn,
                              void *context)
{
    if ((gTcpClient.initialized == 0U) ||
        (header == NULL) ||
        (header_len == 0U) ||
        (header_len > TCPCLIENT_TX_MSG_MAX_LEN) ||
        (read_fn == NULL))
    {
        return -1;
    }

    sys_mutex_lock(&gTcpClient.tx_mutex);
    if (gTcpClient.stream.active != 0U)
    {
        sys_mutex_unlock(&gTcpClient.tx_mutex);
        return -2;
    }

    memset(&gTcpClient.stream, 0, sizeof(gTcpClient.stream));
    memcpy(gTcpClient.stream.header, header, header_len);
    gTcpClient.stream.active = 1U;
    gTcpClient.stream.header_len = header_len;
    gTcpClient.stream.total_size = total_size;
    gTcpClient.stream.read_fn = read_fn;
    gTcpClient.stream.done_fn = done_fn;
    gTcpClient.stream.context = context;
    sys_mutex_unlock(&gTcpClient.tx_mutex);

    return 0;
}

uint8_t TcpClient_IsConnected(void)
{
    return gTcpClient.connected;
}

void TcpClient_RequestReconnect(void)
{
    gTcpClient.reconnect_requested = 1U;
}
