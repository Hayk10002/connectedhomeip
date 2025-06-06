/*
 *   Copyright (c) 2025 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#pragma once

#include <commands/common/CHIPCommand.h>

class LiveViewStartCommand : public CHIPCommand
{
public:
    LiveViewStartCommand(CredentialIssuerCommands * credIssuerCommands) : CHIPCommand("start", credIssuerCommands)
    {
        AddArgument("node-id", 0, UINT64_MAX, &mPeerNodeId);
        AddArgument("stream-usage", 0, UINT8_MAX, &mStreamUsage);
    }

    /////////// CHIPCommand Interface /////////
    CHIP_ERROR RunCommand() override;

    chip::System::Clock::Timeout GetWaitDuration() const override { return chip::System::Clock::Seconds16(1); }

private:
    chip::NodeId mPeerNodeId = chip::kUndefinedNodeId;
    chip::Optional<uint8_t> mStreamUsage;
};

class LiveViewStopCommand : public CHIPCommand
{
public:
    LiveViewStopCommand(CredentialIssuerCommands * credIssuerCommands) : CHIPCommand("stop", credIssuerCommands)
    {
        AddArgument("node-id", 0, UINT64_MAX, &mPeerNodeId);
        AddArgument("video-stream-id", 0, UINT16_MAX, &mVideoStreamID);
    }

    /////////// CHIPCommand Interface /////////
    CHIP_ERROR RunCommand() override;

    chip::System::Clock::Timeout GetWaitDuration() const override { return chip::System::Clock::Seconds16(1); }

private:
    chip::NodeId mPeerNodeId = chip::kUndefinedNodeId;
    uint16_t mVideoStreamID  = 0;
};
