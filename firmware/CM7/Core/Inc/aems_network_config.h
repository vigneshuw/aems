#ifndef AEMS_NETWORK_CONFIG_H
#define AEMS_NETWORK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h7xx_hal.h"

/*
 * Board/network identity lives in this user-owned header so CubeMX code
 * generation does not overwrite it. For a fixed-IP deployment, set these four
 * bytes per board before flashing.
 */
#define AEMS_BOARD_IP_ADDR0       192U
#define AEMS_BOARD_IP_ADDR1       168U
#define AEMS_BOARD_IP_ADDR2       0U
#define AEMS_BOARD_IP_ADDR3       10U

#define AEMS_NETMASK_ADDR0        255U
#define AEMS_NETMASK_ADDR1        255U
#define AEMS_NETMASK_ADDR2        255U
#define AEMS_NETMASK_ADDR3        0U

#define AEMS_GATEWAY_ADDR0        0U
#define AEMS_GATEWAY_ADDR1        0U
#define AEMS_GATEWAY_ADDR2        0U
#define AEMS_GATEWAY_ADDR3        0U

/* Raspberry Pi / host server address used by the CM7 TCP client. */
#define AEMS_SERVER_IP_ADDR0      192U
#define AEMS_SERVER_IP_ADDR1      168U
#define AEMS_SERVER_IP_ADDR2      0U
#define AEMS_SERVER_IP_ADDR3      20U
#define AEMS_SERVER_PORT          10U

/* Locally administered unicast MAC prefix for AEMS boards. */
#define AEMS_MAC_PREFIX0          0x02U
#define AEMS_MAC_PREFIX1          0x80U
#define AEMS_MAC_PREFIX2          0xE1U

static inline uint32_t AEMS_Network_Mix32(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352dUL;
  value ^= value >> 15;
  value *= 0x846ca68bUL;
  value ^= value >> 16;
  return value;
}

static inline void AEMS_Network_GetBoardIp(uint8_t ip[4])
{
  ip[0] = AEMS_BOARD_IP_ADDR0;
  ip[1] = AEMS_BOARD_IP_ADDR1;
  ip[2] = AEMS_BOARD_IP_ADDR2;
  ip[3] = AEMS_BOARD_IP_ADDR3;
}

static inline void AEMS_Network_GetNetmask(uint8_t netmask[4])
{
  netmask[0] = AEMS_NETMASK_ADDR0;
  netmask[1] = AEMS_NETMASK_ADDR1;
  netmask[2] = AEMS_NETMASK_ADDR2;
  netmask[3] = AEMS_NETMASK_ADDR3;
}

static inline void AEMS_Network_GetGateway(uint8_t gateway[4])
{
  gateway[0] = AEMS_GATEWAY_ADDR0;
  gateway[1] = AEMS_GATEWAY_ADDR1;
  gateway[2] = AEMS_GATEWAY_ADDR2;
  gateway[3] = AEMS_GATEWAY_ADDR3;
}

static inline void AEMS_Network_GetServerIp(uint8_t ip[4])
{
  ip[0] = AEMS_SERVER_IP_ADDR0;
  ip[1] = AEMS_SERVER_IP_ADDR1;
  ip[2] = AEMS_SERVER_IP_ADDR2;
  ip[3] = AEMS_SERVER_IP_ADDR3;
}

static inline uint32_t AEMS_Network_Ipv4ToU32(const uint8_t ip[4])
{
  return ((uint32_t)ip[0] << 24) |
         ((uint32_t)ip[1] << 16) |
         ((uint32_t)ip[2] << 8) |
         (uint32_t)ip[3];
}

static inline void AEMS_Network_GetMac(uint8_t mac[6])
{
  uint32_t uid_hash;

  uid_hash = 0xA35A11CEUL;
  uid_hash = AEMS_Network_Mix32(uid_hash ^ HAL_GetUIDw0());
  uid_hash = AEMS_Network_Mix32(uid_hash ^ HAL_GetUIDw1());
  uid_hash = AEMS_Network_Mix32(uid_hash ^ HAL_GetUIDw2());

  mac[0] = AEMS_MAC_PREFIX0;
  mac[1] = AEMS_MAC_PREFIX1;
  mac[2] = AEMS_MAC_PREFIX2;
  mac[3] = (uint8_t)(uid_hash >> 16);
  mac[4] = (uint8_t)(uid_hash >> 8);
  mac[5] = (uint8_t)uid_hash;
}

#ifdef __cplusplus
}
#endif

#endif /* AEMS_NETWORK_CONFIG_H */
