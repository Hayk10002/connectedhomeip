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
#include <pw_unit_test/framework.h>

#include <app/AttributePathParams.h>
#include <app/AttributeValueDecoder.h>
#include <app/clusters/general-commissioning-server/BreadCrumbTracker.h>
#include <app/clusters/network-commissioning/NetworkCommissioningCluster.h>
#include <app/data-model-provider/MetadataTypes.h>
#include <app/data-model-provider/tests/TestConstants.h>
#include <app/data-model-provider/tests/WriteTesting.h>
#include <app/server/Server.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <app/server-cluster/testing/AttributeTesting.h>
#include <app/server-cluster/testing/ClusterTester.h>
#include <app/server-cluster/testing/TestServerClusterContext.h>
#include <app/server-cluster/testing/ValidateGlobalAttributes.h>
#include <clusters/GeneralCommissioning/Attributes.h>
#include <clusters/NetworkCommissioning/Commands.h>
#include <clusters/NetworkCommissioning/Enums.h>
#include <clusters/NetworkCommissioning/Ids.h>
#include <clusters/NetworkCommissioning/Metadata.h>
#include <clusters/NetworkCommissioning/Structs.h>
#include <credentials/tests/CHIPCert_test_vectors.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/ReadOnlyBuffer.h>
#include <lib/support/ThreadOperationalDataset.h>
#include <platform/NetworkCommissioning.h>

#include "FakeDrivers.h"

namespace {

using namespace chip;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::NetworkCommissioning;

using chip::app::AttributeValueDecoder;
using chip::app::ClusterShutdownType;
using chip::app::DataModel::AttributeEntry;
using chip::Testing::kAdminSubjectDescriptor;
using chip::Testing::WriteOperation;
using chip::Thread::OperationalDataset;

class NoopBreadcrumbTracker : public BreadCrumbTracker
{
public:
    void SetBreadCrumb(uint64_t v) override {}
};
// initialize memory as ReadOnlyBufferBuilder may allocate
struct TestNetworkCommissioningClusterEthernet : public ::testing::Test
{
    static void SetUpTestSuite()
    {
        ASSERT_EQ(chip::Platform::MemoryInit(), CHIP_NO_ERROR);
        ASSERT_EQ(DeviceLayer::SystemLayer().Init(), CHIP_NO_ERROR);
        ASSERT_EQ(DeviceLayer::PlatformMgr().StartEventLoopTask(), CHIP_NO_ERROR);
    }
    static void TearDownTestSuite()
    {
        EXPECT_EQ(DeviceLayer::PlatformMgr().StopEventLoopTask(), CHIP_NO_ERROR);
        DeviceLayer::SystemLayer().Shutdown();
        chip::Platform::MemoryShutdown();
    }

    template<class DriverT>
    void TestAttributes()
    {
        constexpr bool is_ethernet = std::is_same_v<DriverT, Testing::FakeEthernetDriver>;
        constexpr bool is_wifi     = std::is_same_v<DriverT, Testing::FakeWiFiDriver>;
        constexpr bool is_thread   = std::is_same_v<DriverT, Testing::FakeThreadDriver>;
        static_assert(is_ethernet || is_wifi || is_thread, "DriverT must be one of the supported fake drivers");

        // this is done to be able to check for this without checking CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
        bool supportsPDC = false;
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
        if constexpr (is_wifi)
        {
            // enable PDC if the macro is set
            fakeDriver.PDCEnabled = true;
            supportsPDC = fakeDriver.SupportsPerDeviceCredentials();
        }
#endif

        NoopBreadcrumbTracker tracker;
        DriverT fakeDriver;
        ByteSpan testEthernetInterfaceName(Uint8::from_const_char("eth0_test"), 9);
        if constexpr (is_ethernet)
        {
            fakeDriver.SetNetwork(testEthernetInterfaceName, false);
        }
        NetworkCommissioningCluster cluster(kRootEndpointId, &fakeDriver, tracker);
        chip::Testing::ClusterTester tester(cluster);
        ASSERT_EQ(cluster.Init(), CHIP_NO_ERROR);
        ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

        // Cluster Revision
        {
            Attributes::ClusterRevision::TypeInfo::DecodableType value{};
            ASSERT_TRUE(tester.ReadAttribute(Attributes::ClusterRevision::Id, value).IsSuccess());
            ASSERT_EQ(value, NetworkCommissioning::kRevision);
        }

        // Feature Map
        {
            Attributes::FeatureMap::TypeInfo::DecodableType value;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::FeatureMap::Id, value).IsSuccess());
            if constexpr (is_ethernet)
            {
                EXPECT_EQ(value, BitFlags<Feature>{ Feature::kEthernetNetworkInterface }.Raw());
            }
            else if constexpr (is_wifi)
            {
                BitFlags<Feature> expectedFeatures{ Feature::kWiFiNetworkInterface };
                if (supportsPDC)
                {
                    expectedFeatures.Set(Feature::kPerDeviceCredentials);
                }
                EXPECT_EQ(value, expectedFeatures.Raw());
            }
            else if constexpr (is_thread)
            {
                EXPECT_EQ(value, BitFlags<Feature>{ Feature::kThreadNetworkInterface }.Raw());
            }
        }

        // Attribute List
        if constexpr (is_ethernet)
        {
            ASSERT_TRUE(Testing::IsAttributesListEqualTo(cluster,
                                                        {
                                                            Attributes::MaxNetworks::kMetadataEntry,
                                                            Attributes::Networks::kMetadataEntry,
                                                            Attributes::InterfaceEnabled::kMetadataEntry,
                                                            Attributes::LastNetworkingStatus::kMetadataEntry,
                                                            Attributes::LastNetworkID::kMetadataEntry,
                                                            Attributes::LastConnectErrorValue::kMetadataEntry,
                                                        }));
        }
        else if constexpr (is_wifi)
        {
            ASSERT_TRUE(Testing::IsAttributesListEqualTo(cluster,
                                                        {
                                                            Attributes::MaxNetworks::kMetadataEntry,
                                                            Attributes::Networks::kMetadataEntry,
                                                            Attributes::ScanMaxTimeSeconds::kMetadataEntry,
                                                            Attributes::ConnectMaxTimeSeconds::kMetadataEntry,
                                                            Attributes::InterfaceEnabled::kMetadataEntry,
                                                            Attributes::LastNetworkingStatus::kMetadataEntry,
                                                            Attributes::LastNetworkID::kMetadataEntry,
                                                            Attributes::LastConnectErrorValue::kMetadataEntry,
                                                            Attributes::SupportedWiFiBands::kMetadataEntry,
                                                        }));
        }
        else if constexpr (is_thread)
        {
            ASSERT_TRUE(Testing::IsAttributesListEqualTo(cluster,
                                                        {
                                                            Attributes::MaxNetworks::kMetadataEntry,
                                                            Attributes::Networks::kMetadataEntry,
                                                            Attributes::ScanMaxTimeSeconds::kMetadataEntry,
                                                            Attributes::ConnectMaxTimeSeconds::kMetadataEntry,
                                                            Attributes::InterfaceEnabled::kMetadataEntry,
                                                            Attributes::LastNetworkingStatus::kMetadataEntry,
                                                            Attributes::LastNetworkID::kMetadataEntry,
                                                            Attributes::LastConnectErrorValue::kMetadataEntry,
                                                            Attributes::SupportedThreadFeatures::kMetadataEntry,
                                                            Attributes::ThreadVersion::kMetadataEntry,
                                                        }));
        }

        // Accepted Commands List
        if constexpr (is_ethernet)
        {
            ASSERT_TRUE(Testing::IsAcceptedCommandsListEqualTo(cluster, {}));
        }
        else if constexpr (is_wifi)
        {
            if (supportsPDC)
            {
                ASSERT_TRUE(Testing::IsAcceptedCommandsListEqualTo(
                    cluster,
                    {
                        Commands::ScanNetworks::kMetadataEntry,
                        Commands::AddOrUpdateWiFiNetwork::kMetadataEntry,
                        Commands::RemoveNetwork::kMetadataEntry,
                        Commands::ConnectNetwork::kMetadataEntry,
                        Commands::ReorderNetwork::kMetadataEntry,
                        Commands::QueryIdentity::kMetadataEntry,
                    }));
            }
            else
            {
                ASSERT_TRUE(Testing::IsAcceptedCommandsListEqualTo(
                    cluster,
                    {
                        Commands::ScanNetworks::kMetadataEntry,
                        Commands::AddOrUpdateWiFiNetwork::kMetadataEntry,
                        Commands::RemoveNetwork::kMetadataEntry,
                        Commands::ConnectNetwork::kMetadataEntry,
                        Commands::ReorderNetwork::kMetadataEntry,
                    }));
            }
        }
        else if constexpr (is_thread)
        {
            ASSERT_TRUE(Testing::IsAcceptedCommandsListEqualTo(
                cluster,
                {
                    Commands::ScanNetworks::kMetadataEntry,
                    Commands::AddOrUpdateThreadNetwork::kMetadataEntry,
                    Commands::RemoveNetwork::kMetadataEntry,
                    Commands::ConnectNetwork::kMetadataEntry,
                    Commands::ReorderNetwork::kMetadataEntry
                }));
        }

        // Generated Commands List
        if constexpr (is_ethernet)
        {
            ASSERT_TRUE(Testing::IsGeneratedCommandsListEqualTo(cluster, {}));
        }
        else if (is_wifi && supportsPDC)
        {
            ASSERT_TRUE(Testing::IsGeneratedCommandsListEqualTo(
                cluster,
                {
                    Commands::ScanNetworksResponse::Id,
                    Commands::NetworkConfigResponse::Id,
                    Commands::ConnectNetworkResponse::Id,
                    Commands::QueryIdentityResponse::Id,
                }));
        }
        else // if wifi without PDC or Thread
        {
            ASSERT_TRUE(Testing::IsGeneratedCommandsListEqualTo(
                cluster,
                {
                    Commands::ScanNetworksResponse::Id,
                    Commands::NetworkConfigResponse::Id,
                    Commands::ConnectNetworkResponse::Id,
                }));
        }

        // Max Networks
        {
            Attributes::MaxNetworks::TypeInfo::DecodableType value;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::MaxNetworks::Id, value).IsSuccess());
            ASSERT_EQ(value, fakeDriver.GetMaxNetworks());
        }

        if constexpr (is_ethernet)
        {
            // connect the ethernet cable
            fakeDriver.SetNetworkConnected(true);
        }

        // Networks
        {
            Attributes::Networks::TypeInfo::DecodableType value;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::Networks::Id, value).IsSuccess());
            auto it = value.begin();
            // by default only ethernet network will have networks attribute populated
            // wifi and thread can also have netowkrs attribute populated if they connect to a network automatically. But this is not tested here.
            // ethernet is required to always have exactly one network in the list, which indicates the existence of ethernet port, and the connected state indicates if there is a cable attached to it.
            if constexpr (is_ethernet)
            {
                ASSERT_TRUE(it.Next());
                const auto & network = it.GetValue();
                EXPECT_TRUE(testEthernetInterfaceName.data_equal(network.networkID));
                EXPECT_TRUE(network.connected);
            }
            EXPECT_FALSE(it.Next());
            EXPECT_EQ(it.GetStatus(), CHIP_NO_ERROR);
        }

        // Last Networking Status, Last Network ID, Last Connect Error Value
        {
            Attributes::LastNetworkingStatus::TypeInfo::DecodableType status;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::LastNetworkingStatus::Id, status).IsSuccess());

            Attributes::LastNetworkID::TypeInfo::DecodableType id;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::LastNetworkID::Id, id).IsSuccess());

            Attributes::LastConnectErrorValue::TypeInfo::DecodableType err;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::LastConnectErrorValue::Id, err).IsSuccess());

            // because we connected the ethernet cable above, we expect success status
            if constexpr (is_ethernet)
            {
                ASSERT_FALSE(status.IsNull());
                EXPECT_EQ(status.Value(), NetworkCommissioningStatusEnum::kSuccess);

                ASSERT_FALSE(id.IsNull());
                EXPECT_TRUE(testEthernetInterfaceName.data_equal(id.Value()));

                ASSERT_FALSE(err.IsNull());
                EXPECT_EQ(err.Value(), fakeDriver.testErrorValue.Value());
            }
            else // wifi and thread
            {
                EXPECT_TRUE(status.IsNull());
                EXPECT_TRUE(id.IsNull());
                EXPECT_TRUE(err.IsNull());
            }
        }

        // Interface Enabled
        {
            Attributes::InterfaceEnabled::TypeInfo::DecodableType value;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::InterfaceEnabled::Id, value).IsSuccess());
            EXPECT_TRUE(value); // default enabled
        }

        // Try enabling the interface when it is alread enabled and disabling is not supported
        // This should be success
        fakeDriver.setEnabledAllowed = false;
        ASSERT_TRUE(tester.WriteAttribute(Attributes::InterfaceEnabled::Id, true).IsSuccess());

        // Try disabling the interface when it is not supported
        // This should fail with InvalidAction
        ASSERT_EQ(tester.WriteAttribute(Attributes::InterfaceEnabled::Id, false).GetStatusCode().GetStatus(),
                Protocols::InteractionModel::Status::InvalidAction);

        // Now try disabling when it is supported
        // This should succeed and mark the attribute as dirty
        fakeDriver.setEnabledAllowed = true;
        ASSERT_TRUE(tester.WriteAttribute(Attributes::InterfaceEnabled::Id, false).IsSuccess());
        ASSERT_FALSE(tester.GetDirtyList().empty());
        auto val = tester.GetDirtyList().back();
        EXPECT_EQ(val.mEndpointId, kRootEndpointId);
        EXPECT_EQ(val.mClusterId, NetworkCommissioning::Id);
        EXPECT_EQ(val.mAttributeId, Attributes::InterfaceEnabled::Id);

        // Verify persistence
        {
            Attributes::InterfaceEnabled::TypeInfo::DecodableType value;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::InterfaceEnabled::Id, value).IsSuccess());
            EXPECT_FALSE(value); // disabled
        }

        cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
        cluster.Deinit();

        ASSERT_EQ(cluster.Init(), CHIP_NO_ERROR);
        ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

        {
            Attributes::InterfaceEnabled::TypeInfo::DecodableType value;
            ASSERT_TRUE(tester.ReadAttribute(Attributes::InterfaceEnabled::Id, value).IsSuccess());
            EXPECT_FALSE(value); // stays disabled
        }

        cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
        cluster.Deinit();
    }
    template<class DriverT>
    void TestCommands()
    {
        constexpr bool is_wifi = std::is_same_v<DriverT, Testing::FakeWiFiDriver>;
        constexpr bool is_thread = std::is_same_v<DriverT, Testing::FakeThreadDriver>;
        static_assert(is_wifi || is_thread, "DriverT must be one of FakeWifiDriver or FakeThreadDriver");

        // this is done to be able to check for this without checking CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
        [[maybe_unused]] bool supportsPDC = false;
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
        if constexpr (is_wifi)
        {
            // enable PDC if the macro is set
            fakeDriver.PDCEnabled = true;
            supportsPDC = fakeDriver.SupportsPerDeviceCredentials();
        }
#endif

        NoopBreadcrumbTracker tracker;
        DriverT fakeDriver;
        NetworkCommissioningCluster cluster(kRootEndpointId, &fakeDriver, tracker);
        chip::Testing::ClusterTester tester(cluster);
        ASSERT_EQ(cluster.Init(), CHIP_NO_ERROR);
        ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

        // Scan Networks
        {
            auto response = tester.Invoke(Commands::ScanNetworks::Id, Commands::ScanNetworks::Type{}, true);

            DeviceLayer::PlatformMgr().LockChipStack();
            fakeDriver.FinaliseScanNetworks();
            DeviceLayer::PlatformMgr().UnlockChipStack();

            response.HandleCommandResponse();

            ASSERT_TRUE(response.IsSuccess());
            ASSERT_TRUE(response.response.has_value());
            Commands::ScanNetworksResponse::DecodableType scanResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_EQ(scanResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
            if constexpr (is_wifi)
            {
                ASSERT_TRUE(scanResponse.wiFiScanResults.HasValue());
                EXPECT_FALSE(scanResponse.threadScanResults.HasValue());
                auto & scanResults = scanResponse.wiFiScanResults.Value();
                {
                    auto it = scanResults.begin();

                    ASSERT_TRUE(it.Next());
                    const auto & sr1 = it.GetValue();
                    EXPECT_TRUE(sr1.ssid.data_equal(ByteSpan(Uint8::from_const_char("First"), 5)));
                    EXPECT_TRUE(sr1.bssid.data_equal(ByteSpan(Uint8::from_const_char("BSSID1"), 6)));

                    ASSERT_TRUE(it.Next());
                    const auto & sr2 = it.GetValue();
                    EXPECT_TRUE(sr2.ssid.data_equal(ByteSpan(Uint8::from_const_char("Second"), 6)));
                    EXPECT_TRUE(sr2.bssid.data_equal(ByteSpan(Uint8::from_const_char("BSSID2"), 6)));

                    ASSERT_TRUE(it.Next());
                    const auto & sr3 = it.GetValue();
                    EXPECT_TRUE(sr3.ssid.data_equal(ByteSpan(Uint8::from_const_char("Third"), 5)));
                    EXPECT_TRUE(sr3.bssid.data_equal(ByteSpan(Uint8::from_const_char("BSSID3"), 6)));

                    ASSERT_TRUE(it.Next());
                    const auto & sr4 = it.GetValue();
                    EXPECT_TRUE(sr4.ssid.data_equal(ByteSpan(Uint8::from_const_char("Fourth"), 6)));
                    EXPECT_TRUE(sr4.bssid.data_equal(ByteSpan(Uint8::from_const_char("BSSID4"), 6)));

                    EXPECT_FALSE(it.Next());
                    EXPECT_EQ(it.GetStatus(), CHIP_NO_ERROR);
                }
            }
            else if constexpr (is_thread)
            {
                ASSERT_TRUE(scanResponse.threadScanResults.HasValue());
                EXPECT_FALSE(scanResponse.wiFiScanResults.HasValue());
                auto & scanResults = scanResponse.threadScanResults.Value();
                {
                    auto it = scanResults.begin();

                    ASSERT_TRUE(it.Next());
                    const auto & sr1 = it.GetValue();
                    EXPECT_EQ(sr1.extendedPanId, 1234u);

                    ASSERT_TRUE(it.Next());
                    const auto & sr2 = it.GetValue();
                    EXPECT_EQ(sr2.extendedPanId, 2345u);

                    ASSERT_TRUE(it.Next());
                    const auto & sr3 = it.GetValue();
                    EXPECT_EQ(sr3.extendedPanId, 3456u);

                    ASSERT_TRUE(it.Next());
                    const auto & sr4 = it.GetValue();
                    EXPECT_EQ(sr4.extendedPanId, 4567u);

                    EXPECT_FALSE(it.Next());
                    EXPECT_EQ(it.GetStatus(), CHIP_NO_ERROR);
                }
            }
        }

        // Scan Networks with specific SSID (only for WiFi)
        {
            if constexpr (is_wifi)
            {
                Commands::ScanNetworks::Type request
                {
                    .ssid = MakeOptional(ByteSpan(Uint8::from_const_char("Third"), 5))
                };
                auto response = tester.Invoke(Commands::ScanNetworks::Id, request, true);

                DeviceLayer::PlatformMgr().LockChipStack();
                fakeDriver.FinaliseScanNetworks();
                DeviceLayer::PlatformMgr().UnlockChipStack();

                response.HandleCommandResponse();

                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::ScanNetworksResponse::DecodableType scanResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(scanResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
                ASSERT_TRUE(scanResponse.wiFiScanResults.HasValue());
                auto & scanResults = scanResponse.wiFiScanResults.Value();
                {
                    auto it = scanResults.begin();
                    ASSERT_TRUE(it.Next());
                    const auto & sr = it.GetValue();
                    EXPECT_TRUE(sr.ssid.data_equal(ByteSpan(Uint8::from_const_char("Third"), 5)));
                    EXPECT_TRUE(sr.bssid.data_equal(ByteSpan(Uint8::from_const_char("BSSID3"), 6)));

                    EXPECT_FALSE(it.Next());
                    EXPECT_EQ(it.GetStatus(), CHIP_NO_ERROR);
                }
            }
        }

        //Scan Networks fail
        {
            auto response = tester.Invoke(Commands::ScanNetworks::Id, Commands::ScanNetworks::Type{}, true);

            DeviceLayer::PlatformMgr().LockChipStack();
            fakeDriver.FinaliseScanNetworks(false);
            DeviceLayer::PlatformMgr().UnlockChipStack();

            response.HandleCommandResponse();

            ASSERT_TRUE(response.IsSuccess());
            ASSERT_TRUE(response.response.has_value());
            Commands::ScanNetworksResponse::DecodableType scanResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
            EXPECT_EQ(scanResponse.networkingStatus, NetworkCommissioningStatusEnum::kUnknownError);
            ASSERT_TRUE(scanResponse.debugText.HasValue());
            EXPECT_TRUE(scanResponse.debugText.Value().data_equal("Scan/Failure"_span));
        }

        // AddOrUpdateNetwork without failsafe
        {
            if constexpr (is_wifi)
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("FailSafeTest"), 12),
                    .credentials = ByteSpan(),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);
                ASSERT_FALSE(response.IsSuccess());
                ASSERT_TRUE(response.status.has_value());
                EXPECT_EQ(response.status.value(), Protocols::InteractionModel::Status::FailsafeRequired);
            }
            else
            {
                OperationalDataset dataset;
                static constexpr uint8_t kExtendedPanId[] = { 'F', 'a', 'i', 'l', 'S', 'a', 'f', 'e' };
                ASSERT_EQ(dataset.SetExtendedPanId(kExtendedPanId), CHIP_NO_ERROR);
                Commands::AddOrUpdateThreadNetwork::Type request
                {
                    .operationalDataset = dataset.AsByteSpan(),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateThreadNetwork::Id, request);
                ASSERT_FALSE(response.IsSuccess());
                ASSERT_TRUE(response.status.has_value());
                EXPECT_EQ(response.status.value(), Protocols::InteractionModel::Status::FailsafeRequired);
            }
        }

        // Arm the failsaife
        constexpr uint16_t kFailSafeTimeout = 20;

        DeviceLayer::PlatformMgr().LockChipStack();
        ASSERT_EQ(chip::Server::GetInstance().GetFailSafeContext().ArmFailSafe(chip::Testing::kTestFabricIndex, chip::System::Clock::Seconds16(kFailSafeTimeout)), CHIP_NO_ERROR);
        DeviceLayer::PlatformMgr().UnlockChipStack();

        // AddOrUpdateWifiNetwork
        if constexpr (is_wifi)
        {
            // No networks in Networks attribute initially
            {
                Attributes::Networks::TypeInfo::DecodableType networks;
                ASSERT_TRUE(tester.ReadAttribute(Attributes::Networks::Id, networks).IsSuccess());
                auto it = networks.begin();
                EXPECT_FALSE(it.Next());
                EXPECT_EQ(it.GetStatus(), CHIP_NO_ERROR);
            }

            // Add an open network (no credentials, no NetworkIdentity)
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("First"), 5),
                    .credentials = ByteSpan(),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);

                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::NetworkConfigResponse::DecodableType netResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(netResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
                ASSERT_TRUE(netResponse.networkIndex.HasValue());
                EXPECT_EQ(netResponse.networkIndex.Value(), 0u);
            }

            // Add a secured network (with credentials, no NetworkIdentity)
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("Second"), 6),
                    .credentials = ByteSpan(Uint8::from_const_char("password"), 8),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);

                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::NetworkConfigResponse::DecodableType netResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(netResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
                ASSERT_TRUE(netResponse.networkIndex.HasValue());
                EXPECT_EQ(netResponse.networkIndex.Value(), 1u);
            }

            // If supported, Add a network with NetworkIdentity (PDC), else add another network with credentials only
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
            if (supportsPDC)
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("Third"), 5),
                    .credentials = ByteSpan(),
                    .networkIdentity = MakeOptional(TestCerts::sTestCert_PDCID01_ChipCompact);
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);
                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::NetworkConfigResponse::DecodableType netResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(netResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
                ASSERT_TRUE(netResponse.networkIndex.HasValue());
                EXPECT_EQ(netResponse.networkIndex.Value(), 2u);
                EXPECT_TRUE(netResponse.clientIdentity.HasValue());
            }
            else
#endif
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("Third"), 5),
                    .credentials = ByteSpan(Uint8::from_const_char("password3"), 9),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);
                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::NetworkConfigResponse::DecodableType netResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(netResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
                ASSERT_TRUE(netResponse.networkIndex.HasValue());
                EXPECT_EQ(netResponse.networkIndex.Value(), 2u);
            }

            // Networks attribute should now have 3 networks (this is max for FakeWiFiDriver)
            {
                Attributes::Networks::TypeInfo::DecodableType networks;
                ASSERT_TRUE(tester.ReadAttribute(Attributes::Networks::Id, networks).IsSuccess());
                auto it = networks.begin();

                ASSERT_TRUE(it.Next());
                const auto & net1 = it.GetValue();
                EXPECT_TRUE(net1.networkID.data_equal(ByteSpan(Uint8::from_const_char("First"), 5)));

                ASSERT_TRUE(it.Next());
                const auto & net2 = it.GetValue();
                EXPECT_TRUE(net2.networkID.data_equal(ByteSpan(Uint8::from_const_char("Second"), 6)));

                ASSERT_TRUE(it.Next());
                const auto & net3 = it.GetValue();
                EXPECT_TRUE(net3.networkID.data_equal(ByteSpan(Uint8::from_const_char("Third"), 5)));

                EXPECT_FALSE(it.Next());
                EXPECT_EQ(it.GetStatus(), CHIP_NO_ERROR);
            }

            // Try to Add a new network - should fail
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("Fourth"), 6),
                    .credentials = ByteSpan(),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);

                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::NetworkConfigResponse::DecodableType netResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(netResponse.networkingStatus, NetworkCommissioningStatusEnum::kBoundsExceeded);
            }

            // Update an the password of the second network
            {
                Commands::AddOrUpdateWiFiNetwork::Type request
                {
                    .ssid = ByteSpan(Uint8::from_const_char("Second"), 6),
                    .credentials = ByteSpan(Uint8::from_const_char("new_password"), 12),
                };

                auto response = tester.Invoke(Commands::AddOrUpdateWiFiNetwork::Id, request);

                ASSERT_TRUE(response.IsSuccess());
                ASSERT_TRUE(response.response.has_value());
                Commands::NetworkConfigResponse::DecodableType netResponse = response.response.value(); // NOLINT(bugprone-unchecked-optional-access)
                EXPECT_EQ(netResponse.networkingStatus, NetworkCommissioningStatusEnum::kSuccess);
                ASSERT_TRUE(netResponse.networkIndex.HasValue());
                EXPECT_EQ(netResponse.networkIndex.Value(), 1u);
            }
        }

        cluster.Shutdown(ClusterShutdownType::kClusterShutdown);
        cluster.Deinit();
    }
};

TEST_F(TestNetworkCommissioningClusterEthernet, TestAttributes)
{
    TestAttributes<Testing::FakeEthernetDriver>();
    TestAttributes<Testing::FakeWiFiDriver>();
    TestAttributes<Testing::FakeThreadDriver>();
}

TEST_F(TestNetworkCommissioningClusterEthernet, TestCommands)
{
    TestCommands<Testing::FakeWiFiDriver>();
    ASSERT_FALSE(HasFailure());

    TestCommands<Testing::FakeThreadDriver>();
    ASSERT_FALSE(HasFailure());
}

} // namespace
