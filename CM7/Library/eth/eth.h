#ifndef ETH_H
#define ETH_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"

// LAN8742A default strap: PHY address = 0
#define ETH_LAN8742A_PHY_ADDR_DEFAULT 0U

// SMI register indices
#define ETH_PHY_REG_BMSR   0x01U
#define ETH_PHY_REG_PHYID1 0x02U
#define ETH_PHY_REG_PHYID2 0x03U

// LAN8742A expected IDs
#define ETH_LAN8742A_PHYID1_EXPECTED 0x0007U
#define ETH_LAN8742A_PHYID2_EXPECTED_MASK 0xFFF0U
#define ETH_LAN8742A_PHYID2_EXPECTED 0xC130U

// Read a PHY register over SMI (MDIO). Returns false on HAL error or bad args.
bool ETH_ReadPhyReg(ETH_HandleTypeDef *heth,
                    uint32_t phy_addr,
                    uint32_t reg_addr,
                    uint16_t *value_out);

// Basic PHY readout + ID check (revision bits ignored).
// Returns true if PHYID1/2 match LAN8742A.
bool ETH_TestBasic(ETH_HandleTypeDef *heth,
                   uint32_t phy_addr,
                   uint16_t *phyid1,
                   uint16_t *phyid2,
                   uint16_t *bmsr);

#endif /* ETH_H */
