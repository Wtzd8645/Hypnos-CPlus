#pragma once

#include "Network/PacketFraming.hpp"
#include <Hypnos/Network/NetworkManager.hpp>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Blanketmen {
namespace Hypnos {
namespace Tests {

namespace NetworkTests {

class FakeMessage : public Network::IMessage
{
public:
    explicit FakeMessage(uint8 codec_id) :
        codec_id(codec_id)
    {
    }

    uint8 CodecId() const noexcept override
    {
        return codec_id;
    }

private:
    uint8 codec_id = 0;
};

class FakeAllocator : public Network::IMessageAllocator
{
public:
    Network::IMessage* Acquire(uint8 codec_id) override
    {
        ++acquire_count;
        return new FakeMessage(codec_id);
    }

    void Release(Network::IMessage& message) override
    {
        ++release_count;
        delete &message;
    }

    static inline uint32 acquire_count = 0;
    static inline uint32 release_count = 0;
};

class FakeCodec : public Network::ICodec
{
public:
    explicit FakeCodec(uint8 codec_id = 0) :
        codec_id(codec_id)
    {
    }

    uint8 Id() const noexcept override
    {
        return codec_id;
    }

    Status<Network::PacketSize> Encode(Network::IMessage& message, byte* buffer, Network::PacketSize capacity) override
    {
        (void)message;

        if (capacity == 0)
        {
            return Status<Network::PacketSize>::Error(ErrorCode::ResourceExhausted, "[NetworkTests] Fake codec buffer is full.");
        }

        buffer[0] = byte { 42 };
        return Status<Network::PacketSize>::Success(static_cast<Network::PacketSize>(1));
    }

    Status<Network::IMessage*> Decode(const byte* buffer, Network::PacketSize size, Network::IMessageAllocator& allocator) override
    {
        (void)buffer;
        (void)size;
        return Status<Network::IMessage*>::Success(allocator.Acquire(Id()));
    }

private:
    uint8 codec_id = 0;
};

class FakePipeline : public Network::IPacketPipeline
{
public:
    Status<Network::PacketSize> Encode(Network::ICodec& codec, Network::IMessage& message, byte* buffer, Network::PacketSize capacity) override
    {
        return codec.Encode(message, buffer, capacity);
    }

    Status<Network::IMessage*> Decode(Network::ICodec& codec, const byte* buffer, Network::PacketSize size, Network::IMessageAllocator& allocator) override
    {
        return codec.Decode(buffer, size, allocator);
    }
};

inline bool FailedWith(const Status<void>& status, Network::NetworkStatus network_status)
{
    return status.IsFailed() && status.ErrorCode() == static_cast<int32>(Network::ToErrorCode(network_status));
}

inline Network::ServerConfig MakeServerConfig(Network::EndpointId id)
{
    Network::ServerConfig config { };
    config.id = id;
    config.transport = Network::TransportProtocol::Tcp;
    config.bind.host = "127.0.0.1";
    config.bind.port = static_cast<uint16>(27000 + id);
    config.worker_id = 0;
    config.max_connections = 8;
    config.send_queue_capacity_per_connection = 4;
    config.receive_queue_capacity_per_connection = 4;
    config.delivery_queue_capacity = 8;
    config.codec_buffer_capacity = 1024;
    return config;
}

inline Network::ClientConfig MakeClientConfig(Network::EndpointId id)
{
    Network::ClientConfig config { };
    config.id = id;
    config.transport = Network::TransportProtocol::Tcp;
    config.remote.host = "127.0.0.1";
    config.remote.port = static_cast<uint16>(28000 + id);
    config.worker_id = 0;
    config.send_queue_capacity = 4;
    config.receive_queue_capacity = 4;
    config.delivery_queue_capacity = 8;
    config.codec_buffer_capacity = 1024;
    return config;
}

inline Network::NetworkConfig MakeConfig(bool has_codec_id_mismatch = false)
{
    Network::NetworkConfig config { };
    config.backend = Network::BackendType::Epoll;
    config.worker_count = 1;
    config.codecs.push_back(std::make_unique<FakeCodec>(has_codec_id_mismatch ? 1 : 0));
    config.message_allocator = std::make_unique<FakeAllocator>();
    config.packet_pipeline = std::make_unique<FakePipeline>();
    config.servers.push_back(MakeServerConfig(0));
    return config;
}

inline Network::NetworkConfig MakeLoopbackConfig(uint16 port)
{
    Network::NetworkConfig config { };
    config.backend = Network::BackendType::Epoll;
    config.worker_count = 1;
    config.codecs.push_back(std::make_unique<FakeCodec>());
    config.message_allocator = std::make_unique<FakeAllocator>();
    config.packet_pipeline = std::make_unique<FakePipeline>();
    config.servers.push_back(MakeServerConfig(0));
    config.clients.push_back(MakeClientConfig(1));
    config.servers[0].bind.port = port;
    config.clients[0].remote.port = port;
    return config;
}

inline bool Pump(Network::NetworkManager& manager, bool (*done)(), uint32 iterations = 200)
{
    for (uint32 i = 0; i < iterations; ++i)
    {
        Status<void> update_status = manager.Update();
        assert(!update_status.IsFailed());
        if (done())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return done();
}

inline void ConfigValidationPasses()
{
    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.worker_count = 0;
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.backend = Network::BackendType::IoUring;
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::Unsupported));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.servers.push_back(MakeServerConfig(0));
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig(true);
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.servers[0].id = 1;
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.codecs[0].reset();
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.message_allocator.reset();
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.servers[0].worker_id = 1;
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }

    {
        Network::NetworkManager manager { };
        Network::NetworkConfig config = MakeConfig();
        config.servers[0].delivery_queue_capacity = 0;
        assert(FailedWith(manager.Configure(std::move(config)), Network::NetworkStatus::InvalidConfig));
    }
}

inline void LifecyclePasses()
{
    Network::NetworkManager manager { };
    assert(FailedWith(manager.Start(), Network::NetworkStatus::NotReady));

    Network::NetworkConfig config = MakeConfig();
    Status<void> configure_status = manager.Configure(std::move(config));
    assert(!configure_status.IsFailed());
    assert(manager.State() == Network::ManagerState::Configured);

    assert(FailedWith(manager.Update(), Network::NetworkStatus::NotReady));
    assert(FailedWith(manager.Configure(MakeConfig()), Network::NetworkStatus::InvalidState));

    Network::Server* server = manager.GetServer(0);
    assert(server != nullptr);
    assert(server->State() == Network::ServerState::Stopped);
    assert(!server->Register(Network::MessageHandler { }).IsFailed());

    Status<void> start_status = manager.Start();
    assert(!start_status.IsFailed());
    assert(manager.State() == Network::ManagerState::Running);
    assert(server->State() == Network::ServerState::Listening);

    Status<void> update_status = manager.Update();
    assert(!update_status.IsFailed());

    Status<void> stop_status = manager.Stop();
    assert(!stop_status.IsFailed());
    assert(manager.State() == Network::ManagerState::Configured);
    assert(server->State() == Network::ServerState::Stopped);

    manager.Release();
    assert(manager.State() == Network::ManagerState::Unconfigured);
    assert(manager.GetServer(0) == nullptr);
}

inline void EndpointLookupPasses()
{
    Network::NetworkConfig config = MakeConfig();
    config.clients.push_back(MakeClientConfig(1));

    Network::NetworkManager manager { };
    Status<void> status = manager.Configure(std::move(config));
    assert(!status.IsFailed());

    assert(manager.GetServer(0) != nullptr);
    assert(manager.GetClient(1) != nullptr);
    assert(manager.GetServer(1) == nullptr);
    assert(manager.GetClient(0) == nullptr);
    assert(manager.GetServer(99) == nullptr);
}

inline void EndpointApiGuardPasses()
{
    Network::NetworkManager manager { };
    Network::NetworkConfig config = MakeConfig();
    Status<void> configure_status = manager.Configure(std::move(config));
    assert(!configure_status.IsFailed());

    Network::Server* server = manager.GetServer(0);
    assert(server != nullptr);

    FakeMessage message { 0 };
    Network::ConnectionHandle invalid_handle { };
    assert(FailedWith(server->Send(invalid_handle, message), Network::NetworkStatus::NotReady));

    Status<void> start_status = manager.Start();
    assert(!start_status.IsFailed());
    assert(FailedWith(server->Send(invalid_handle, message), Network::NetworkStatus::InvalidHandle));
    assert(FailedWith(server->Close(invalid_handle), Network::NetworkStatus::InvalidHandle));
}

inline void PacketFramingPasses()
{
    byte buffer[Network::PACKET_HEADER_SIZE] { };
    Network::WritePacketHeader(0x1234, 7, buffer, sizeof(buffer));
    assert(static_cast<uint8>(buffer[0]) == 0x12);
    assert(static_cast<uint8>(buffer[1]) == 0x34);
    assert(static_cast<uint8>(buffer[2]) == 7);
    assert(static_cast<uint8>(buffer[3]) == 0);

    Status<Network::PacketHeader> read_status = Network::ReadPacketHeader(buffer, sizeof(buffer));
    assert(!read_status.IsFailed());
    assert(read_status.Value().payload_size == 0x1234);
    assert(read_status.Value().codec_id == 7);
    assert(read_status.Value().flags == 0);
}

#if defined(__linux__)
inline bool server_connected = false;
inline bool client_connected = false;
inline bool server_received = false;
inline bool client_received = false;
inline Network::ConnectionHandle server_connection;

inline void ResetLoopbackTrace()
{
    server_connected = false;
    client_connected = false;
    server_received = false;
    client_received = false;
    server_connection = Network::ConnectionHandle { };
    FakeAllocator::acquire_count = 0;
    FakeAllocator::release_count = 0;
}

inline bool BothConnected()
{
    return server_connected && client_connected;
}

inline bool ServerReceived()
{
    return server_received;
}

inline bool ClientReceived()
{
    return client_received;
}

inline void OnServerConnection(const Network::ConnectionEvent& event)
{
    if (event.type == Network::ConnectionEventType::Connected)
    {
        server_connected = true;
        server_connection = event.connection;
    }
}

inline void OnClientConnection(const Network::ConnectionEvent& event)
{
    if (event.type == Network::ConnectionEventType::Connected)
    {
        client_connected = true;
    }
}

inline void OnServerMessage(const Network::MessageEvent& event)
{
    (void)event;
    server_received = true;
}

inline void OnClientMessage(const Network::MessageEvent& event)
{
    (void)event;
    client_received = true;
}

inline void TcpLoopbackPasses()
{
    ResetLoopbackTrace();

    Network::NetworkManager manager { };
    Network::NetworkConfig config = MakeLoopbackConfig(39101);
    Status<void> configure_status = manager.Configure(std::move(config));
    assert(!configure_status.IsFailed());

    Network::Server* server = manager.GetServer(0);
    Network::Client* client = manager.GetClient(1);
    assert(server != nullptr);
    assert(client != nullptr);

    assert(!server->Register(Network::ConnectionEventHandler::Bind<&OnServerConnection>()).IsFailed());
    assert(!server->Register(Network::MessageHandler::Bind<&OnServerMessage>()).IsFailed());
    assert(!client->Register(Network::ConnectionEventHandler::Bind<&OnClientConnection>()).IsFailed());
    assert(!client->Register(Network::MessageHandler::Bind<&OnClientMessage>()).IsFailed());

    Status<void> start_status = manager.Start();
    assert(!start_status.IsFailed());

    Status<void> connect_status = client->Connect();
    assert(!connect_status.IsFailed());
    assert(Pump(manager, &BothConnected));

    FakeMessage client_message { 0 };
    Status<void> client_send_status = client->Send(client_message);
    assert(!client_send_status.IsFailed());
    assert(Pump(manager, &ServerReceived));
    assert(FakeAllocator::acquire_count == 1);
    assert(FakeAllocator::release_count == 0);

    Status<void> release_update_status = manager.Update();
    assert(!release_update_status.IsFailed());
    assert(FakeAllocator::release_count == 1);

    FakeMessage server_message { 0 };
    Status<void> server_send_status = server->Send(server_connection, server_message);
    assert(!server_send_status.IsFailed());
    assert(Pump(manager, &ClientReceived));

    Status<void> stop_status = manager.Stop();
    assert(!stop_status.IsFailed());
}

inline bool server_codec_error = false;
inline bool server_resource_exhausted = false;

inline bool ServerCodecError()
{
    return server_codec_error;
}

inline void OnServerError(const Network::NetworkErrorEvent& event)
{
    if (event.status == Network::NetworkStatus::CodecError)
    {
        server_codec_error = true;
    }

    if (event.status == Network::NetworkStatus::ResourceExhausted)
    {
        server_resource_exhausted = true;
    }
}

inline void UnsupportedFlagsFailConnectionPasses()
{
    server_codec_error = false;

    Network::NetworkManager manager { };
    Network::NetworkConfig config = MakeConfig();
    config.servers[0].bind.port = 39102;
    Status<void> configure_status = manager.Configure(std::move(config));
    assert(!configure_status.IsFailed());

    Network::Server* server = manager.GetServer(0);
    assert(server != nullptr);
    assert(!server->Register(Network::ErrorHandler::Bind<&OnServerError>()).IsFailed());

    Status<void> start_status = manager.Start();
    assert(!start_status.IsFailed());

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(39102);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    byte packet[Network::PACKET_HEADER_SIZE] { byte { 0 }, byte { 0 }, byte { 0 }, byte { 1 } };
    assert(send(fd, packet, sizeof(packet), 0) == static_cast<ssize_t>(sizeof(packet)));
    assert(Pump(manager, &ServerCodecError));

    close(fd);
    Status<void> stop_status = manager.Stop();
    assert(!stop_status.IsFailed());
}

inline void UnknownCodecFailsConnectionPasses()
{
    server_codec_error = false;

    Network::NetworkManager manager { };
    Network::NetworkConfig config = MakeConfig();
    config.servers[0].bind.port = 39104;
    Status<void> configure_status = manager.Configure(std::move(config));
    assert(!configure_status.IsFailed());

    Network::Server* server = manager.GetServer(0);
    assert(server != nullptr);
    assert(!server->Register(Network::ErrorHandler::Bind<&OnServerError>()).IsFailed());

    Status<void> start_status = manager.Start();
    assert(!start_status.IsFailed());

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(39104);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    byte packet[Network::PACKET_HEADER_SIZE] { byte { 0 }, byte { 0 }, byte { 1 }, byte { 0 } };
    assert(send(fd, packet, sizeof(packet), 0) == static_cast<ssize_t>(sizeof(packet)));
    assert(Pump(manager, &ServerCodecError));

    close(fd);
    Status<void> stop_status = manager.Stop();
    assert(!stop_status.IsFailed());
}

inline bool ServerResourceExhausted()
{
    return server_resource_exhausted;
}

inline void DeliveryOverflowCoalescesTerminalErrorPasses()
{
    server_resource_exhausted = false;

    Network::NetworkManager manager { };
    Network::NetworkConfig config = MakeConfig();
    config.servers[0].bind.port = 39103;
    config.servers[0].delivery_queue_capacity = 1;
    Status<void> configure_status = manager.Configure(std::move(config));
    assert(!configure_status.IsFailed());

    Network::Server* server = manager.GetServer(0);
    assert(server != nullptr);
    assert(!server->Register(Network::ErrorHandler::Bind<&OnServerError>()).IsFailed());

    Status<void> start_status = manager.Start();
    assert(!start_status.IsFailed());

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(39103);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    byte packet[Network::PACKET_HEADER_SIZE] { byte { 0 }, byte { 0 }, byte { 0 }, byte { 1 } };
    assert(send(fd, packet, sizeof(packet), 0) == static_cast<ssize_t>(sizeof(packet)));
    assert(Pump(manager, &ServerResourceExhausted));

    close(fd);
    Status<void> stop_status = manager.Stop();
    assert(!stop_status.IsFailed());
}
#endif

} // namespace NetworkTests

inline void NetworkPasses()
{
    NetworkTests::ConfigValidationPasses();
    NetworkTests::LifecyclePasses();
    NetworkTests::EndpointLookupPasses();
    NetworkTests::EndpointApiGuardPasses();
    NetworkTests::PacketFramingPasses();
#if defined(__linux__)
    NetworkTests::TcpLoopbackPasses();
    NetworkTests::UnsupportedFlagsFailConnectionPasses();
    NetworkTests::UnknownCodecFailsConnectionPasses();
    NetworkTests::DeliveryOverflowCoalescesTerminalErrorPasses();
#endif
}

} // namespace Tests
} // namespace Hypnos
} // namespace Blanketmen
