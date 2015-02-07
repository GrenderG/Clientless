/*
 * Copyright (C) 2015 Dehravor <dehravor@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldSession.h"
#include <functional>
#include <limits>
#include <iostream>
#include <future>
#include "EventMgr.h"

struct WorldOpcodeHandler
{
    Opcodes opcode;
    std::function<void(WorldPacket&)> callback;
};

WorldSession::WorldSession(std::shared_ptr<Session> session) : session_(session), socket_(this), serverSeed_(0), ping_(0), lastPingTime_(0)
{
    clientSeed_ = static_cast<uint32>(time(nullptr));
}

WorldSession::~WorldSession()
{
}

#define BIND_OPCODE_HANDLER(a, b) { a, std::bind(&WorldSession::b, this, std::placeholders::_1) }

const std::vector<WorldOpcodeHandler> WorldSession::GetOpcodeHandlers()
{
    return {
        // AuthHandler.cpp
        BIND_OPCODE_HANDLER(SMSG_AUTH_CHALLENGE, HandleAuthenticationChallenge),
        BIND_OPCODE_HANDLER(SMSG_AUTH_RESPONSE, HandleAuthenticationResponse),

        // CharacterHandler.cpp
        BIND_OPCODE_HANDLER(SMSG_CHAR_ENUM, HandleCharacterEnum)
    };
}

void WorldSession::HandlePacket(std::shared_ptr<WorldPacket> recvPacket)
{
    static const std::vector<WorldOpcodeHandler> handlers = GetOpcodeHandlers();

    auto itr = std::find_if(handlers.begin(), handlers.end(), [recvPacket](const WorldOpcodeHandler& handler) {
        return recvPacket->GetOpcode() == handler.opcode;
    });

    if (itr == handlers.end())
        return;

    try
    {
        itr->callback(*recvPacket);
    }
    catch (ByteBufferException const& exception)
    {
        error("%s", "ByteBufferException occured while handling a world packet!");
        error("Opcode: 0x%04x", recvPacket->GetOpcode());
        error("%s", exception.what());
    }
}

void WorldSession::SendPacket(WorldPacket &packet)
{
    socket_.EnqueuePacket(packet);
}

void WorldSession::Enter()
{
    if (!socket_.Connect(session_->GetRealm().Address))
        return;
    
    eventMgr_.Stop();
    {
        std::shared_ptr<Event> packetProcessEvent(new Event(EVENT_PROCESS_INCOMING));
        packetProcessEvent->SetPeriod(10);
        packetProcessEvent->SetEnabled(true);
        packetProcessEvent->SetCallback([this]() {
            while (std::shared_ptr<WorldPacket> packet = socket_.GetNextPacket())
                std::async(&WorldSession::HandlePacket, this, packet);
        });

        eventMgr_.AddEvent(packetProcessEvent);
    }
    eventMgr_.Start();
}

void WorldSession::HandleConsoleCommand(std::string cmd)
{
    if (cmd == "quit" || cmd == "disconnect" || cmd == "logout")
        socket_.Disconnect();
}

WorldSocket* WorldSession::GetSocket()
{
    return &socket_;
}