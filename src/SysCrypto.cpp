#include "sysPlatform/SysCrypto.h"

namespace SysPlatform {

SysSHA256::SysSHA256()
{

}

SysSHA256::~SysSHA256()
{

}

void SysSHA256::reset()
{
    return;
}

void SysSHA256::update(const void *data, size_t len)
{

}

void SysSHA256::finalize(void *hash, size_t len)
{

}

bool SysEd25519::verify(const uint8_t signature[64], const uint8_t publicKey[32],
                       const void *message, size_t len)
{
    return true;
}

}