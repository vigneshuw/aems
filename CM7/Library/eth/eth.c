#include "eth.h"

bool ETH_ReadPhyReg(ETH_HandleTypeDef *heth,
                    uint32_t phy_addr,
                    uint32_t reg_addr,
                    uint16_t *value_out)
{
    uint32_t value = 0U;

    if (heth == NULL || value_out == NULL)
    {
        return false;
    }

    if (HAL_ETH_ReadPHYRegister(heth, phy_addr, reg_addr, &value) != HAL_OK)
    {
        return false;
    }

    *value_out = (uint16_t)(value & 0xFFFFU);
    return true;
}

bool ETH_TestBasic(ETH_HandleTypeDef *heth,
                   uint32_t phy_addr,
                   uint16_t *phyid1,
                   uint16_t *phyid2,
                   uint16_t *bmsr)
{
    uint16_t id1 = 0U;
    uint16_t id2 = 0U;
    uint16_t status = 0U;

    if (!ETH_ReadPhyReg(heth, phy_addr, ETH_PHY_REG_PHYID1, &id1))
    {
        return false;
    }

    if (!ETH_ReadPhyReg(heth, phy_addr, ETH_PHY_REG_PHYID2, &id2))
    {
        return false;
    }

    if (!ETH_ReadPhyReg(heth, phy_addr, ETH_PHY_REG_BMSR, &status))
    {
        return false;
    }

    if (phyid1 != NULL)
    {
        *phyid1 = id1;
    }
    if (phyid2 != NULL)
    {
        *phyid2 = id2;
    }
    if (bmsr != NULL)
    {
        *bmsr = status;
    }

    if (id1 != ETH_LAN8742A_PHYID1_EXPECTED)
    {
        return false;
    }
    if ((id2 & ETH_LAN8742A_PHYID2_EXPECTED_MASK) != ETH_LAN8742A_PHYID2_EXPECTED)
    {
        return false;
    }

    return true;
}
