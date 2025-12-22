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

#include <algorithm>

namespace chip {
namespace Testing {

// FakeThreadDriver::FakeNetworkIterator methods
void FakeThreadDriver::FakeNetworkIterator::Set(Span<ThreadNetwork> networks)
{
    mNetworks = networks;
    currentindex = 0;
}

bool FakeThreadDriver::FakeNetworkIterator::Next(DeviceLayer::NetworkCommissioning::Network & item)
{
    VerifyOrReturnValue(currentindex < mNetworks.size(), false);
    auto span = MutableByteSpan{ item.networkID };
    uint8_t extPanId[Thread::kSizeExtendedPanId];
    SuccessOrDie(mNetworks[currentindex].operationalDataset.GetExtendedPanId(extPanId));
    ByteSpan extPanIdSpan{ extPanId };
    SuccessOrDie(CopySpanToMutableSpan(extPanIdSpan, span));
    item.networkIDLen = static_cast<uint8_t>(span.size());
    item.connected = mNetworks[currentindex].connected;
    currentindex++;
    return true;
}

CHIP_ERROR FakeThreadDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    mNetworkStatusChangeCallback = networkStatusChangeCallback;
    return CHIP_NO_ERROR;
}

DeviceLayer::NetworkCommissioning::NetworkIterator * FakeThreadDriver::GetNetworks()
{
    mNetworkIterator.Set({ mNetworks.data(), mNetworks.size() });
    return &mNetworkIterator;
}

CHIP_ERROR FakeThreadDriver::SetEnabled(bool enabled)
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

CHIP_ERROR FakeThreadDriver::CommitConfiguration()
{
    configWasCommitted = true;
    return CHIP_NO_ERROR;
}

bool FakeThreadDriver::ConfigWasCommitted()
{
    bool v = configWasCommitted;
    configWasCommitted = false;
    return v;
}

CHIP_ERROR FakeThreadDriver::RevertConfiguration()
{
    configWasReverted = true;
    return CHIP_NO_ERROR;
}

bool FakeThreadDriver::ConfigWasReverted()
{
    bool v = configWasReverted;
    configWasReverted = false;
    return v;
}

FakeThreadDriver::NetworkCommissioningStatusEnum FakeThreadDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText,
                                                uint8_t & outNetworkIndex)
{
    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const ThreadNetwork & net) {
        uint8_t extPanId[Thread::kSizeExtendedPanId];
        SuccessOrDie(net.operationalDataset.GetExtendedPanId(extPanId));
        return networkId.data_equal(ByteSpan{ extPanId });
    });
    if (it == mNetworks.end())
    {
        SuccessOrDie(CopyCharSpanToMutableCharSpan("Remove/NotFound"_span, outDebugText));
        return NetworkCommissioningStatusEnum::kNetworkIDNotFound;
    }
    outNetworkIndex = static_cast<uint8_t>(std::distance(mNetworks.begin(), it));
    mNetworks.erase(it);
    return NetworkCommissioningStatusEnum::kSuccess;
}

FakeThreadDriver::NetworkCommissioningStatusEnum FakeThreadDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const ThreadNetwork & net) {
        uint8_t extPanId[Thread::kSizeExtendedPanId];
        SuccessOrDie(net.operationalDataset.GetExtendedPanId(extPanId));
        return networkId.data_equal(ByteSpan{ extPanId });
    });
    if (it == mNetworks.end() || index >= mNetworks.size())
    {
        SuccessOrDie(CopyCharSpanToMutableCharSpan("Reorder/NotFound"_span, outDebugText));
        return NetworkCommissioningStatusEnum::kNetworkIDNotFound;
    }
    ThreadNetwork net = *it;
    mNetworks.erase(it);
    mNetworks.insert(mNetworks.begin() + index, net);
    return NetworkCommissioningStatusEnum::kSuccess;
}

void FakeThreadDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const ThreadNetwork & net) {
        uint8_t extPanId[Thread::kSizeExtendedPanId];
        SuccessOrDie(net.operationalDataset.GetExtendedPanId(extPanId));
        return networkId.data_equal(ByteSpan{ extPanId });
    });
    if (it == mNetworks.end())
    {
        callback->OnResult(NetworkCommissioningStatusEnum::kNetworkIDNotFound, "Connect/NotFound"_span, -1);
    }
    mCurrentlyConnectingNetworkId = networkId;
    mConnectCallback = callback;
}

void FakeThreadDriver::FinaliseConnectNetwork(bool success)
{
    VerifyOrReturn(mConnectCallback != nullptr);
    if (success)
    {
        mConnectCallback->OnResult(NetworkCommissioningStatusEnum::kSuccess, "Connect/Success"_span, 0);
        if (mNetworkStatusChangeCallback)
        {
            mNetworkStatusChangeCallback->OnNetworkingStatusChange(
                NetworkCommissioningStatusEnum::kSuccess, Optional{ mCurrentlyConnectingNetworkId }, testErrorValue);
        }
        std::find_if(mNetworks.begin(), mNetworks.end(), [&](const ThreadNetwork & net) {
            uint8_t extPanId[Thread::kSizeExtendedPanId];
            SuccessOrDie(net.operationalDataset.GetExtendedPanId(extPanId));
            return mCurrentlyConnectingNetworkId.data_equal(ByteSpan{ extPanId });
        })->connected = true;
    }
    else
    {
        mConnectCallback->OnResult(NetworkCommissioningStatusEnum::kOtherConnectionFailure, "Connect/Failure"_span, -1);
    }
    mConnectCallback = nullptr;
    mCurrentlyConnectingNetworkId = ByteSpan();
}

#if CHIP_DEVICE_CONFIG_SUPPORTS_CONCURRENT_CONNECTION
CHIP_ERROR FakeThreadDriver::DisconnectFromNetwork()
{
    for (auto & net : mNetworks)
    {
        net.connected = false;
    }
    return CHIP_NO_ERROR;
}
#endif

FakeThreadDriver::NetworkCommissioningStatusEnum FakeThreadDriver::AddOrUpdateNetwork(ByteSpan operationalDataset, MutableCharSpan & outDebugText,
                                                    uint8_t & outNetworkIndex)
{
    uint8_t extPanId[Thread::kSizeExtendedPanId];
    Thread::OperationalDataset opd;
    VerifyOrReturnError(opd.Init(operationalDataset) == CHIP_NO_ERROR, NetworkCommissioningStatusEnum::kUnknownError);
    VerifyOrReturnError(opd.GetExtendedPanId(extPanId) == CHIP_NO_ERROR, NetworkCommissioningStatusEnum::kUnknownError);
    ByteSpan extPanIdSpan{ extPanId };
    if (mNetworks.size() >= kMaxNetworks && std::none_of(
                                                mNetworks.begin(), mNetworks.end(), [&](const ThreadNetwork & net) {
                                                    uint8_t xpanId[Thread::kSizeExtendedPanId];
                                                    SuccessOrDie(net.operationalDataset.GetExtendedPanId(xpanId));
                                                    return extPanIdSpan.data_equal(ByteSpan{ xpanId });
                                                }))
    {
        SuccessOrDie(CopyCharSpanToMutableCharSpan("Add/Full"_span, outDebugText));
        return NetworkCommissioningStatusEnum::kBoundsExceeded;
    }

    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const ThreadNetwork & net) {
        uint8_t xpanId[Thread::kSizeExtendedPanId];
        SuccessOrDie(net.operationalDataset.GetExtendedPanId(xpanId));
        return extPanIdSpan.data_equal(ByteSpan{ xpanId });
    });

    if (it != mNetworks.end())
    {
        it->operationalDataset = opd;
        outNetworkIndex = static_cast<uint8_t>(std::distance(mNetworks.begin(), it));
        return NetworkCommissioningStatusEnum::kSuccess;
    }

    mNetworks.push_back(ThreadNetwork{opd, false});
    outNetworkIndex = static_cast<uint8_t>(mNetworks.size() - 1);
    return NetworkCommissioningStatusEnum::kSuccess;
}

void FakeThreadDriver::FinaliseScanNetworks(bool success)
{
    VerifyOrReturn(mScanCallback != nullptr);

    static DeviceLayer::NetworkCommissioning::ThreadScanResponse scanResults[kMaxNetworks + 1];

    scanResults[0].extendedPanId = 1234;
    scanResults[1].extendedPanId = 2345;
    scanResults[2].extendedPanId = 3456;
    scanResults[3].extendedPanId = 4567;

    if (success)
    {
        TestResponseIterator<4> iterator(scanResults);
        mScanCallback->OnFinished(NetworkCommissioningStatusEnum::kSuccess, CharSpan(), &iterator);
    }
    else
    {
        mScanCallback->OnFinished(NetworkCommissioningStatusEnum::kUnknownError, "Scan/Failure"_span, nullptr);
    }
    mScanCallback = nullptr;
}

} // namespace Testing
} // namespace chip
