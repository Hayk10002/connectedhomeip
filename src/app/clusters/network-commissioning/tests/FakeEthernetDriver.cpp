/*
 *    Copyright (c) 2025 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
#include "FakeDrivers.h"

namespace chip {
namespace Testing {

// FakeEthernetDriver::FakeNetworkIterator methods
void FakeEthernetDriver::FakeNetworkIterator::Set(Span<DeviceLayer::NetworkCommissioning::Network> networks)
{
    mNetworks = networks;
    currentindex = 0;
}

bool FakeEthernetDriver::FakeNetworkIterator::Next(DeviceLayer::NetworkCommissioning::Network & item)
{
    VerifyOrReturnValue(currentindex < mNetworks.size(), false);
    item = mNetworks[currentindex++];
    return true;
}

// FakeEthernetDriver methods
CHIP_ERROR FakeEthernetDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    mNetworkStatusChangeCallback = networkStatusChangeCallback;
    return CHIP_NO_ERROR;
}

void FakeEthernetDriver::SetNetwork(ByteSpan interfaceName, bool connected)
{
    auto span = MutableByteSpan{ mNetwork.networkID };
    SuccessOrDie(CopySpanToMutableSpan(interfaceName, span));
    mNetwork.networkIDLen = static_cast<uint8_t>(interfaceName.size());
    SetNetworkConnected(connected);
}

void FakeEthernetDriver::SetNetworkConnected(bool connected)
{
    if (mNetwork.connected == connected)
    {
        return;
    }
    mNetwork.connected = connected;
    if (mNetworkStatusChangeCallback)
    {
        ByteSpan networkId{ mNetwork.networkID, mNetwork.networkIDLen };
        mNetworkStatusChangeCallback->OnNetworkingStatusChange(
            app::Clusters::NetworkCommissioning::NetworkCommissioningStatusEnum::kSuccess, Optional{ networkId },
            testErrorValue);
    }
}

CHIP_ERROR FakeEthernetDriver::SetEnabled(bool enabled)
{
    if (GetEnabled() == enabled)
    {
        return CHIP_NO_ERROR;
    }
    if (setEnabledAllowed)
    {
        mEnabled = enabled;
        return CHIP_NO_ERROR;
    }
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

} // namespace Testing
} // namespace chip
