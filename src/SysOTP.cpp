#include "Arduino.h"    
#include "sysPlatform/SysOTP.h"

namespace SysPlatform {

uint64_t SysOTP::getUidLower()
{
    return ((uint64_t)HW_OCOTP_CFG1 << 32) + (uint64_t)HW_OCOTP_CFG0;
}

uint64_t SysOTP::getUidUpper()
{
    return ((uint64_t)HW_OCOTP_MAC1 << 32) + (uint64_t)HW_OCOTP_MAC0;
}

uint32_t SysOTP::getDevicePBKHLower()
{
    return IMXRTfuseRead(&HW_OCOTP_GP1);
}

uint32_t SysOTP::getDevicePBKHUpper()
{
    return IMXRTfuseRead(&HW_OCOTP_GP2);
}

uint32_t SysOTP::getDevelPBKHLower()
{
    return IMXRTfuseRead(&HW_OCOTP_0x690);
}
uint32_t SysOTP::getDevelPBKHUpper()
{
    return IMXRTfuseRead(&HW_OCOTP_0x6A0);
}

uint32_t SysOTP::getEUIDHLower()
{
    return IMXRTfuseRead(&HW_OCOTP_0x6B0);
}
uint32_t SysOTP::getEUIDHUpper()
{
    return IMXRTfuseRead(&HW_OCOTP_0x6C0);
}

uint16_t SysOTP::getProductId()
{
    return IMXRTfuseRead(&HW_OCOTP_SW_GP1) & 0xFFFFU;
}

uint32_t SysOTP::getLocks()
{
    return IMXRTfuseRead(&HW_OCOTP_LOCK);
}

}
