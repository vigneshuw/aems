#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "lwip/netbuf.h"
#include "lwip/mem.h"

#include "tcpclient.h"

#include <string.h>
#include <stdio.h>

static struct netconn *gConn = NULL;
static volatile uint8_t gTcpConnected = 0U;
static volatile uint8_t gReconnectRequested = 0U;
static TcpClientConfig_t gCfg;
static uint8_t gInitDone = 0U;
static sys_mbox_t gTxMbox;

static void TcpClient_Cleanup(struct netconn **conn, struct netbuf **buf)
{
    if ((buf != NULL) && (*buf != NULL))
    {
        netbuf_delete(*buf);
        *buf = NULL;
    }

    if ((conn != NULL) && (*conn != NULL))
    {
        netconn_close(*conn);
        netconn_delete(*conn);
        *conn = NULL;
    }

    gTcpConnected = 0U;
}

static void TcpClient_ProcessRx(struct netconn *conn, struct netbuf *buf)
{
    void *data;
    u16_t len;
    char msg[100];
    char reply[200];

    netbuf_first(buf);

    do
    {
        if (netbuf_data(buf, &data, &len) != ERR_OK)
        {
            continue;
        }

        if (len >= sizeof(msg))
        {
            len = sizeof(msg) - 1U;
        }

        memset(msg, 0, sizeof(msg));
        memcpy(msg, data, len);

        snprintf(reply, sizeof(reply), "\"%s\" was sent by the Server\n", msg);

        if (netconn_write(conn, reply, strlen(reply), NETCONN_COPY) != ERR_OK)
        {
            gTcpConnected = 0U;
            return;
        }
    }
    while (netbuf_next(buf) >= 0);
}

static void TcpClient_SendQueued(struct netconn *conn)
{
    void *msgPtr = NULL;

    while (sys_arch_mbox_tryfetch(&gTxMbox, &msgPtr) != SYS_MBOX_EMPTY)
    {
        char *tx = (char *)msgPtr;

        if (tx != NULL)
        {
            if (netconn_write(conn, tx, strlen(tx), NETCONN_COPY) != ERR_OK)
            {
                gTcpConnected = 0U;
                mem_free(tx);
                return;
            }

            mem_free(tx);
        }
    }
}

static void tcpclient_thread(void *arg)
{
    struct netbuf *buf = NULL;
    err_t err;

    LWIP_UNUSED_ARG(arg);

    for (;;)
    {
        while ((gCfg.Netif == NULL) ||
               (!netif_is_up(gCfg.Netif)) ||
               (!netif_is_link_up(gCfg.Netif)))
        {
            gTcpConnected = 0U;
            sys_msleep(TCPCLIENT_LINK_WAIT_MS);
        }

        gConn = netconn_new(NETCONN_TCP);
        if (gConn == NULL)
        {
            sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
            continue;
        }

        netconn_set_recvtimeout(gConn, TCPCLIENT_RX_TIMEOUT_MS);

        err = netconn_bind(gConn, IP_ADDR_ANY, 0);
        if (err != ERR_OK)
        {
            TcpClient_Cleanup(&gConn, &buf);
            sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
            continue;
        }

        err = netconn_connect(gConn, &gCfg.ServerIp, gCfg.ServerPort);
        if (err != ERR_OK)
        {
            TcpClient_Cleanup(&gConn, &buf);
            sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
            continue;
        }

        gTcpConnected = 1U;
        gReconnectRequested = 0U;

        for (;;)
        {
            if ((gCfg.Netif == NULL) ||
                (!netif_is_up(gCfg.Netif)) ||
                (!netif_is_link_up(gCfg.Netif)) ||
                (gReconnectRequested != 0U))
            {
                gTcpConnected = 0U;
                break;
            }

            TcpClient_SendQueued(gConn);
            if (gTcpConnected == 0U)
            {
                break;
            }

            err = netconn_recv(gConn, &buf);

            if (err == ERR_TIMEOUT)
            {
                continue;
            }

            if (err != ERR_OK)
            {
                gTcpConnected = 0U;
                break;
            }

            TcpClient_ProcessRx(gConn, buf);

            netbuf_delete(buf);
            buf = NULL;

            if (gTcpConnected == 0U)
            {
                break;
            }
        }

        TcpClient_Cleanup(&gConn, &buf);
        sys_msleep(TCPCLIENT_RECONNECT_DELAY_MS);
    }
}

void TcpClient_BuildConfig(TcpClientConfig_t *config,
                           const ip_addr_t *serverIp,
                           uint16_t serverPort,
                           struct netif *netif)
{
    if ((config == NULL) || (serverIp == NULL))
    {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->ServerIp = *serverIp;
    config->ServerPort = serverPort;
    config->Netif = netif;
}

int32_t TcpClient_Init(const TcpClientConfig_t *config)
{
    if ((config == NULL) || (config->Netif == NULL))
    {
        return -1;
    }

    if (gInitDone != 0U)
    {
        return 0;
    }

    gCfg = *config;

    if (sys_mbox_new(&gTxMbox, TCPCLIENT_TX_MBOX_SIZE) != ERR_OK)
    {
        return -2;
    }

    if (sys_thread_new("tcpclient_thread",
                       tcpclient_thread,
                       NULL,
                       DEFAULT_THREAD_STACKSIZE,
                       osPriorityNormal) == NULL)
    {
        sys_mbox_free(&gTxMbox);
        return -3;
    }

    gInitDone = 1U;
    return 0;
}

int32_t TcpClient_Send(const char *text)
{
    size_t len;
    char *copy;

    if ((gInitDone == 0U) || (text == NULL))
    {
        return -1;
    }

    len = strlen(text);
    if (len >= TCPCLIENT_TX_MSG_MAX_LEN)
    {
        len = TCPCLIENT_TX_MSG_MAX_LEN - 1U;
    }

    copy = (char *)mem_malloc(TCPCLIENT_TX_MSG_MAX_LEN);
    if (copy == NULL)
    {
        return -2;
    }

    memset(copy, 0, TCPCLIENT_TX_MSG_MAX_LEN);
    memcpy(copy, text, len);

    if (sys_mbox_trypost(&gTxMbox, copy) != ERR_OK)
    {
        mem_free(copy);
        return -3;
    }

    return 0;
}

uint8_t TcpClient_IsConnected(void)
{
    return gTcpConnected;
}

void TcpClient_RequestReconnect(void)
{
    gReconnectRequested = 1U;
}
