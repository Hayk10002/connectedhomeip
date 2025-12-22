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
#pragma once

#include <clusters/NetworkCommissioning/Attributes.h>
#include <clusters/NetworkCommissioning/Enums.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/Span.h>
#include <lib/support/ThreadOperationalDataset.h>
#include <platform/NetworkCommissioning.h>
#include <protocols/interaction_model/StatusCode.h>

#include <vector>

namespace chip {
namespace Testing {

class FakeEthernetDriver : public DeviceLayer::NetworkCommissioning::EthernetDriver
{
public:
    class FakeNetworkIterator : public DeviceLayer::NetworkCommissioning::NetworkIterator
    {
    public:
        void Set(Span<DeviceLayer::NetworkCommissioning::Network> networks);
        bool Next(DeviceLayer::NetworkCommissioning::Network & item) override;
        size_t Count() override { return mNetworks.size(); }
        void Release() override {};

    private:
        Span<DeviceLayer::NetworkCommissioning::Network> mNetworks;
        size_t currentindex = 0;
    };

    FakeEthernetDriver() { mNetworkIterator.Set({ &mNetwork, 1 }); }

    CHIP_ERROR Init(NetworkStatusChangeCallback * networkStatusChangeCallback) override;
    uint8_t GetMaxNetworks() override { return 1; }
    void SetNetwork(ByteSpan interfaceName, bool connected = true);
    void SetNetworkConnected(bool connected);
    DeviceLayer::NetworkCommissioning::NetworkIterator * GetNetworks() override { return &mNetworkIterator; }
    CHIP_ERROR SetEnabled(bool enabled) override;
    bool GetEnabled() override { return mEnabled; };

public:
    bool setEnabledAllowed = false;
    Optional<int32_t> testErrorValue{ 123 };

private:
    NetworkStatusChangeCallback * mNetworkStatusChangeCallback{ nullptr };
    FakeNetworkIterator mNetworkIterator;
    DeviceLayer::NetworkCommissioning::Network mNetwork;
    bool mEnabled           = true;
};

class FakeWiFiDriver : public DeviceLayer::NetworkCommissioning::WiFiDriver
{
public:
    using NetworkCommissioningStatusEnum = app::Clusters::NetworkCommissioning::NetworkCommissioningStatusEnum;
    using WifiBandEnum                   = app::Clusters::NetworkCommissioning::WiFiBandEnum;

    struct WifiNetwork
    {
        ByteSpan ssid;
        ByteSpan credentials;
        bool connected = false;
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
        ByteSpan networkIdentifier;
        Platform::SharedPtr<P256Keypair> clientIdentityKeypair;
        ByteSpan clientIdentifier;
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
    };

    class FakeNetworkIterator : public DeviceLayer::NetworkCommissioning::NetworkIterator
    {
    public:
        void Set(Span<WifiNetwork> networks);
        bool Next(DeviceLayer::NetworkCommissioning::Network & item) override;
        size_t Count() override { return mNetworks.size(); }
        void Release() override {}

    private:
        Span<WifiNetwork> mNetworks;
        size_t currentindex = 0;
    };

    template <size_t N>
    class TestResponseIterator : public DeviceLayer::NetworkCommissioning::WiFiScanResponseIterator
    {
    public:
        template <typename... Args>
        TestResponseIterator(Args &&... args) : mItems{ std::forward<Args>(args)... }
        {}

        size_t Count() override { return N; }
        bool Next(DeviceLayer::NetworkCommissioning::WiFiScanResponse & item) override
        {
            if (mIndex < N)
            {
                item = mItems[mIndex];
                mIndex++;
                return true;
            }
            return false;
        }
        void Release() override {}

    private:
        Span<DeviceLayer::NetworkCommissioning::WiFiScanResponse> mItems;
        size_t mIndex = 0;
    };

    FakeWiFiDriver() { mNetworks.reserve(kMaxNetworks); }

    // Internal::BaseDriver
    CHIP_ERROR Init(NetworkStatusChangeCallback * networkStatusChangeCallback) override;
    uint8_t GetMaxNetworks() override { return kMaxNetworks; }
    DeviceLayer::NetworkCommissioning::NetworkIterator * GetNetworks() override;
    CHIP_ERROR SetEnabled(bool enabled) override;
    bool GetEnabled() override { return mEnabled; }

    // Internal::WirelessDriver
    CHIP_ERROR CommitConfiguration() override;
    bool ConfigWasCommitted();
    CHIP_ERROR RevertConfiguration() override;
    bool ConfigWasReverted();
    uint8_t GetScanNetworkTimeoutSeconds() override { return scanNetworkTimeoutSeconds; }
    uint8_t GetConnectNetworkTimeoutSeconds() override { return connectNetworkTimeoutSeconds; }

    NetworkCommissioningStatusEnum RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText,
                                                 uint8_t & outNetworkIndex) override;
    NetworkCommissioningStatusEnum ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText) override;
    void ConnectNetwork(ByteSpan networkId, ConnectCallback * callback) override;
    void FinaliseConnectNetwork(bool success = true);

#if CHIP_DEVICE_CONFIG_SUPPORTS_CONCURRENT_CONNECTION
    CHIP_ERROR DisconnectFromNetwork() override;
#endif

    // WifiDriver
    NetworkCommissioningStatusEnum AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                                      uint8_t & outNetworkIndex) override;
    void ScanNetworks(ByteSpan ssid, ScanCallback * callback) override;

    void FinaliseScanNetworks(bool success = true);

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
    bool SupportsPerDeviceCredentials() override { return PDCEnabled; };
    CHIP_ERROR AddOrUpdateNetworkWithPDC(ByteSpan ssid, ByteSpan networkIdentity,
                                                 Optional<uint8_t> clientIdentityNetworkIndex, NetworkCommissioningStatusEnum & outStatus,
                                                 MutableCharSpan & outDebugText, MutableByteSpan & outClientIdentity,
                                                 uint8_t & outNetworkIndex) override;
    CHIP_ERROR GetNetworkIdentity(uint8_t networkIndex, MutableByteSpan & outNetworkIdentity) override;
    CHIP_ERROR GetClientIdentity(uint8_t networkIndex, MutableByteSpan & outClientIdentity) override;
    CHIP_ERROR SignWithClientIdentity(uint8_t networkIndex, const ByteSpan & message,
                                              Crypto::P256ECDSASignature & outSignature) override;
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC

    uint32_t GetSupportedWiFiBandsMask() const override;

public:
    Optional<int32_t> testErrorValue{ 123 };
    uint8_t scanNetworkTimeoutSeconds = 2;
    uint8_t connectNetworkTimeoutSeconds = 2;
    bool setEnabledAllowed = false;
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
    bool PDCEnabled        = false;
#endif
    Span<WifiBandEnum> supportedWifiBands { mDefaultSupportedBands, 2 };

private:
    NetworkStatusChangeCallback * mNetworkStatusChangeCallback{ nullptr };
    FakeNetworkIterator mNetworkIterator;
    constexpr static size_t kMaxNetworks = 3;
    std::vector<WifiNetwork> mNetworks;
    bool mEnabled           = true;

    bool configWasCommitted = false;
    bool configWasReverted  = false;
    ByteSpan mCurrentlyConnectingNetworkId;
    ConnectCallback * mConnectCallback = nullptr;

    ByteSpan mCurrentlyScanningSsid;
    ScanCallback * mScanCallback = nullptr;

    WifiBandEnum mDefaultSupportedBands[2] = { WifiBandEnum::k2g4, WifiBandEnum::k5g };
};

class FakeThreadDriver : public DeviceLayer::NetworkCommissioning::ThreadDriver
{
public:
    using NetworkCommissioningStatusEnum = app::Clusters::NetworkCommissioning::NetworkCommissioningStatusEnum;
    using ThreadCapabilities = DeviceLayer::NetworkCommissioning::ThreadCapabilities;

    struct ThreadNetwork
    {
        Thread::OperationalDataset operationalDataset;
        bool connected = false;
    };

    class FakeNetworkIterator : public DeviceLayer::NetworkCommissioning::NetworkIterator
    {
    public:
        void Set(Span<ThreadNetwork> networks);
        bool Next(DeviceLayer::NetworkCommissioning::Network & item) override;
        size_t Count() override { return mNetworks.size(); }
        void Release() override {}

    private:
        Span<ThreadNetwork> mNetworks;
        size_t currentindex = 0;
    };

    template <size_t N>
    class TestResponseIterator : public DeviceLayer::NetworkCommissioning::ThreadScanResponseIterator
    {
    public:
        template <typename... Args>
        TestResponseIterator(Args &&... args) : mItems{ std::forward<Args>(args)... }
        {}

        size_t Count() override { return N; }
        bool Next(DeviceLayer::NetworkCommissioning::ThreadScanResponse & item) override
        {
            if (mIndex < N)
            {
                item = mItems[mIndex];
                mIndex++;
                return true;
            }
            return false;
        }
        void Release() override {}

    private:
        Span<DeviceLayer::NetworkCommissioning::ThreadScanResponse> mItems;
        size_t mIndex = 0;
    };

    FakeThreadDriver() { mNetworks.reserve(kMaxNetworks); }

    // Internal::BaseDriver
    CHIP_ERROR Init(NetworkStatusChangeCallback * networkStatusChangeCallback) override;
    uint8_t GetMaxNetworks() override { return kMaxNetworks; };
    DeviceLayer::NetworkCommissioning::NetworkIterator * GetNetworks() override;
    CHIP_ERROR SetEnabled(bool enabled) override;
    bool GetEnabled() override { return mEnabled; }

    // Internal::WirelessDriver
    CHIP_ERROR CommitConfiguration() override;
    bool ConfigWasCommitted();
    CHIP_ERROR RevertConfiguration() override;
    bool ConfigWasReverted();
    uint8_t GetScanNetworkTimeoutSeconds() override { return scanNetworkTimeoutSeconds; }
    uint8_t GetConnectNetworkTimeoutSeconds() override { return connectNetworkTimeoutSeconds; }

    NetworkCommissioningStatusEnum RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText,
                                                 uint8_t & outNetworkIndex) override;
    NetworkCommissioningStatusEnum ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText) override;
    void ConnectNetwork(ByteSpan networkId, ConnectCallback * callback) override;
    void FinaliseConnectNetwork(bool success = true);

#if CHIP_DEVICE_CONFIG_SUPPORTS_CONCURRENT_CONNECTION
    CHIP_ERROR DisconnectFromNetwork() override;
#endif

    // ThreadDriver
    NetworkCommissioningStatusEnum AddOrUpdateNetwork(ByteSpan operationalDataset, MutableCharSpan & outDebugText,
                                                      uint8_t & outNetworkIndex) override;
    void ScanNetworks(ScanCallback * callback) override { mScanCallback = callback; }
    void FinaliseScanNetworks(bool success = true);
    ThreadCapabilities GetSupportedThreadFeatures() override { return threadCapabilities; }
    uint16_t GetThreadVersion() override { return threadVersion; }

public:
    Optional<int32_t> testErrorValue{ 123 };
    uint8_t scanNetworkTimeoutSeconds = 2;
    uint8_t connectNetworkTimeoutSeconds = 2;
    bool setEnabledAllowed = false;
    ThreadCapabilities threadCapabilities {
        static_cast<uint16_t>(ThreadCapabilities::kIsFullThreadDevice) |
        static_cast<uint16_t>(ThreadCapabilities::kIsSynchronizedSleepyEndDeviceCapable)
    };
    uint16_t threadVersion = 2;

private:
    NetworkStatusChangeCallback * mNetworkStatusChangeCallback{ nullptr };
    FakeNetworkIterator mNetworkIterator;
    constexpr static size_t kMaxNetworks = 3;
    std::vector<ThreadNetwork> mNetworks;
    bool mEnabled           = true;

    bool configWasCommitted = false;
    bool configWasReverted  = false;
    ByteSpan mCurrentlyConnectingNetworkId;
    ConnectCallback * mConnectCallback = nullptr;

    ScanCallback * mScanCallback = nullptr;
};

} // namespace Testing
} // namespace chip
