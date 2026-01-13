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

void FakeWiFiDriver::FakeNetworkIterator::Set(Span<WifiNetwork> networks)
{
    mNetworks = networks;
    currentindex = 0;
}

bool FakeWiFiDriver::FakeNetworkIterator::Next(DeviceLayer::NetworkCommissioning::Network & item)
{
    VerifyOrReturnValue(currentindex < mNetworks.size(), false);
    auto span = MutableByteSpan{ item.networkID };
    SuccessOrDie(CopySpanToMutableSpan(mNetworks[currentindex].ssid, span));
    item.networkIDLen = static_cast<uint8_t>(span.size());
    item.connected = mNetworks[currentindex].connected;
    currentindex++;
    return true;
}

CHIP_ERROR FakeWiFiDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    mNetworkStatusChangeCallback = networkStatusChangeCallback;
    return CHIP_NO_ERROR;
}

DeviceLayer::NetworkCommissioning::NetworkIterator * FakeWiFiDriver::GetNetworks()
{
    mNetworkIterator.Set({ mNetworks.data(), mNetworks.size() });
    return &mNetworkIterator;
}

CHIP_ERROR FakeWiFiDriver::SetEnabled(bool enabled)
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

CHIP_ERROR FakeWiFiDriver::CommitConfiguration()
{
    configWasCommitted = true;
    return CHIP_NO_ERROR;
}

bool FakeWiFiDriver::ConfigWasCommitted()
{
    bool v = configWasCommitted;
    configWasCommitted = false;
    return v;
}

CHIP_ERROR FakeWiFiDriver::RevertConfiguration()
{
    configWasReverted = true;
    return CHIP_NO_ERROR;
}

bool FakeWiFiDriver::ConfigWasReverted()
{
    bool v = configWasReverted;
    configWasReverted = false;
    return v;
}

FakeWiFiDriver::NetworkCommissioningStatusEnum FakeWiFiDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText,
                                                 uint8_t & outNetworkIndex)
{
    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
        return networkId.data_equal(net.ssid);
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

FakeWiFiDriver::NetworkCommissioningStatusEnum FakeWiFiDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
        return networkId.data_equal(net.ssid);
    });
    if (it == mNetworks.end() || index >= mNetworks.size())
    {
        SuccessOrDie(CopyCharSpanToMutableCharSpan("Reorder/NotFound"_span, outDebugText));
        return NetworkCommissioningStatusEnum::kNetworkIDNotFound;
    }
    WifiNetwork net = *it;
    mNetworks.erase(it);
    mNetworks.insert(mNetworks.begin() + index, net);
    return NetworkCommissioningStatusEnum::kSuccess;
}

void FakeWiFiDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
        return networkId.data_equal(net.ssid);
    });
    if (it == mNetworks.end())
    {
        callback->OnResult(NetworkCommissioningStatusEnum::kNetworkIDNotFound, "Connect/NotFound"_span, -1);
    }
    mCurrentlyConnectingNetworkId = networkId;
    mConnectCallback = callback;
}

void FakeWiFiDriver::FinaliseConnectNetwork(bool success)
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
        std::find_if(mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
            return mCurrentlyConnectingNetworkId.data_equal(net.ssid);
        })->connected = true;
    }
    else
    {
        mConnectCallback->OnResult(NetworkCommissioningStatusEnum::kOtherConnectionFailure, "Connect/Failure"_span, -1);
    }
    mConnectCallback = nullptr;
    mCurrentlyConnectingNetworkId = ByteSpan();
};

#if CHIP_DEVICE_CONFIG_SUPPORTS_CONCURRENT_CONNECTION
CHIP_ERROR FakeWiFiDriver::DisconnectFromNetwork()
{
    for (auto & net : mNetworks)
    {
        net.connected = false;
    }
    return CHIP_NO_ERROR;
}
#endif // CHIP_DEVICE_CONFIG_SUPPORTS_CONCURRENT_CONNECTION

FakeWiFiDriver::NetworkCommissioningStatusEnum FakeWiFiDriver::AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                                      uint8_t & outNetworkIndex)
{
    if (mNetworks.size() >= kMaxNetworks && std::none_of(
                                                mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
                                                    return ssid.data_equal(net.ssid);
                                                }))
    {
        SuccessOrDie(CopyCharSpanToMutableCharSpan("Add/Full"_span, outDebugText));
        return NetworkCommissioningStatusEnum::kBoundsExceeded;
    }

    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
        return ssid.data_equal(net.ssid);
    });

    if (it != mNetworks.end())
    {
        it->credentials = credentials;
        #if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
        it->networkIdentifier = ByteSpan();
        it->clientIdentityKeypair.reset();
        it->clientIdentifier = ByteSpan();
        #endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC

        outNetworkIndex = static_cast<uint8_t>(std::distance(mNetworks.begin(), it));
        return NetworkCommissioningStatusEnum::kSuccess;
    }

    WifiNetwork network;
    network.ssid = ssid;
    network.credentials = credentials;
    mNetworks.push_back(network);
    outNetworkIndex = static_cast<uint8_t>(mNetworks.size() - 1);
    return NetworkCommissioningStatusEnum::kSuccess;
}

void FakeWiFiDriver::ScanNetworks(ByteSpan ssid, ScanCallback * callback)
{
    mScanCallback = callback;
    mCurrentlyScanningSsid = ssid;
}

void FakeWiFiDriver::FinaliseScanNetworks(bool success)
{
    VerifyOrReturn(mScanCallback != nullptr);

    static DeviceLayer::NetworkCommissioning::WiFiScanResponse scanResults[kMaxNetworks + 1];

    scanResults[0].ssidLen = 5;
    memcpy(scanResults[0].ssid, Uint8::from_const_char("First"), 5);
    memcpy(scanResults[0].bssid, Uint8::from_const_char("BSSID1"), 6);

    scanResults[1].ssidLen = 6;
    memcpy(scanResults[1].ssid, Uint8::from_const_char("Second"), 6);
    memcpy(scanResults[1].bssid, Uint8::from_const_char("BSSID2"), 6);

    scanResults[2].ssidLen = 5;
    memcpy(scanResults[2].ssid, Uint8::from_const_char("Third"), 5);
    memcpy(scanResults[2].bssid, Uint8::from_const_char("BSSID3"), 6);

    scanResults[3].ssidLen = 6;
    memcpy(scanResults[3].ssid, Uint8::from_const_char("Fourth"), 6);
    memcpy(scanResults[3].bssid, Uint8::from_const_char("BSSID4"), 6);

    if (success)
    {
        if (!mCurrentlyScanningSsid.empty())
        {
            auto it = std::find_if(std::begin(scanResults), std::end(scanResults), [&](const auto & res) {
                return mCurrentlyScanningSsid.data_equal(ByteSpan(res.ssid, res.ssidLen));
            });
            if (it != std::end(scanResults))
            {
                TestResponseIterator<1> iterator(it, 1u);
                mScanCallback->OnFinished(NetworkCommissioningStatusEnum::kSuccess, CharSpan(), &iterator);
            }
            else
            {
                mScanCallback->OnFinished(NetworkCommissioningStatusEnum::kSuccess, "Scan/NoMatch"_span, nullptr);
            }
        }
        else
        {
            TestResponseIterator<4> iterator(scanResults);
            mScanCallback->OnFinished(NetworkCommissioningStatusEnum::kSuccess, CharSpan(), &iterator);
        }
    }
    else
    {
        mScanCallback->OnFinished(NetworkCommissioningStatusEnum::kUnknownError, "Scan/Failure"_span, nullptr);
    }
    mScanCallback = nullptr;
    mCurrentlyScanningSsid = ByteSpan();
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
CHIP_ERROR FakeWiFiDriver::AddOrUpdateNetworkWithPDC(ByteSpan ssid, ByteSpan networkIdentity,
                                                Optional<uint8_t> clientIdentityNetworkIndex, NetworkCommissioningStatusEnum & outStatus,
                                                MutableCharSpan & outDebugText, MutableByteSpan & outClientIdentity,
                                                uint8_t & outNetworkIndex)
{
    VerifyOrReturnError(PDCEnabled, CHIP_ERROR_UNSUPPORTED_FEATURE);

    if (mNetworks.size() >= kMaxNetworks && std::none_of(
                                                mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
                                                    return ssid.data_equal(net.ssid);
                                                }))
    {
        SuccessOrDie(CopyCharSpanToMutableCharSpan("Add/Full"_span, outDebugText));
        outStatus = NetworkCommissioningStatusEnum::kBoundsExceeded;
        return CHIP_NO_ERROR;
    }

    auto it = std::find_if(mNetworks.begin(), mNetworks.end(), [&](const WifiNetwork & net) {
        return ssid.data_equal(net.ssid);
    });

    // Update a copy first in case of errors
    WifiNetwork network;

    network.ssid = ssid;
    network.credentials = ByteSpan();
    network.networkIdentifier = networkIdentity;
    network.clientIdentityKeypair = MakeShared<P256Keypair>();

    if (clientIdentityNetworkIndex.HasValue())
    {
        // Link the client identity to an existing network.
        uint8_t index = clientIdentityNetworkIndex.Value();
        VerifyOrReturnError(index < mNetworks.size(), CHIP_ERROR_INVALID_ARGUMENT);

        network.clientIdentifier = mNetworks[index].clientIdentifier;
    }
    else
    {
        // Generate a new client identity.
        CHIP_ERROR err = network.clientIdentityKeypair->Initialize(ECPKeyTarget::ECDSA);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err);

        MutableByteSpan clientIdentity(network.clientIdentity);
        err = NewChipNetworkIdentity(*network.clientIdentityKeypair, clientIdentity);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err);

        network.clientIdentifier = clientIdentity;
    }

    if (it != mNetworks.end())
    {
        *it = network;
        outNetworkIndex = static_cast<uint8_t>(std::distance(mNetworks.begin(), it));
        outStatus = NetworkCommissioningStatusEnum::kSuccess;
    }
    else
    {
        mNetworks.push_back(network);
        outNetworkIndex = static_cast<uint8_t>(mNetworks.size() - 1);
        outStatus = NetworkCommissioningStatusEnum::kSuccess;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR FakeWiFiDriver::GetNetworkIdentity(uint8_t networkIndex, MutableByteSpan & outNetworkIdentity)
{
    VerifyOrReturnError(PDCEnabled, CHIP_ERROR_UNSUPPORTED_FEATURE);

    CopySpanToMutableSpan(mNetworks[networkIndex].networkIdentifier, outNetworkIdentity);

    return CHIP_NO_ERROR;
}

CHIP_ERROR FakeWiFiDriver::GetClientIdentity(uint8_t networkIndex, MutableByteSpan & outClientIdentity)
{
    VerifyOrReturnError(PDCEnabled, CHIP_ERROR_UNSUPPORTED_FEATURE);

    CopySpanToMutableSpan(mNetworks[networkIndex].clientIdentifier, outClientIdentity);

    return CHIP_NO_ERROR;
}

CHIP_ERROR FakeWiFiDriver::SignWithClientIdentity(uint8_t networkIndex, const ByteSpan & message,
                                            Crypto::P256ECDSASignature & outSignature)
{
    VerifyOrReturnError(PDCEnabled, CHIP_ERROR_UNSUPPORTED_FEATURE);
    VerifyOrReturnError(networkIndex < mNetworks.size(), CHIP_ERROR_INVALID_ARGUMENT);
    return mNetworks[networkIndex].clientIdentityKeypair->ECDSA_sign_msg(message.data(), message.size(), outSignature);
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC

uint32_t FakeWiFiDriver::GetSupportedWiFiBandsMask() const
{
    uint32_t mask = 0;
    for (auto band : supportedWifiBands)
    {
        mask |= static_cast<uint32_t>(1UL << chip::to_underlying(band));
    }
    return mask;
}

} // namespace Testing
} // namespace chip
