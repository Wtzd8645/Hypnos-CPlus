#pragma once

#include "RequestBase.hpp"
#include <Hypnos-Core/Types.hpp>
#include <Hypnos-Core/Mediation/EventHandlerBase.hpp>

namespace Blanketmen {
namespace Hypnos {

class RequestProcessorBase
{
public:
    virtual ~RequestProcessorBase() = default;

    virtual void Process(uint8* buf, Connection* conn) = 0;
    virtual void Register(uint8 gid, EventHandlerBase<RequestBase*>* handler) = 0;
    virtual void Unregister(uint8 gid) = 0;
};

class RequestProcessor
{
public:
    RequestProcessor()
    {
        // TODO: Auto-generate this code.
        // TODO: Initialize request handlers size.
    }

    void Process(uint8* buf, Connection* conn)
    {
        std::memcpy(&group_id, buf, sizeof(uint8));
        buf += sizeof(uint8);
        std::memcpy(&message_id, buf, sizeof(uint16));
        buf += sizeof(uint16);
        if (group_id >= MAX_GROUP)
        {
            return;
        }

        // TODO: Auto-generate this code.
        RequestBase* request;
        switch (group_id)
        {
            switch (message_id)
            {
                // case 1: request = new (buffer) TestRequest(); break;
                default: return;
            }
            default: return;
        }

        request->conn = conn;
        request->Unpack(buf);
        if (request_handlers[group_id] == nullptr)
        {
            request_handlers[group_id]->Handle(request);
        }
    }

    void Register(uint8 gid, EventHandlerBase<RequestBase*>* handler)
    {
        if (gid < MAX_GROUP)
        {
            request_handlers[gid] = handler;
        }
    }

    void Unregister(uint8 gid)
    {
        if (gid < MAX_GROUP)
        {
            request_handlers[gid] = nullptr;
        }
    }

private:
    static constexpr int32 MAX_SIZE = 2048;
    static constexpr int32 MAX_GROUP = 2; // TODO: Auto-generate this code.

    static uint8 buffer[MAX_SIZE];

    uint8 group_id;
    uint16 message_id;
    EventHandlerBase<RequestBase*>* request_handlers[MAX_GROUP];
};

} // namespace Hypnos
} // namespace Blanketmen