#include "SocketBase.hpp"
#include <Thread.hpp>

namespace Blanketmen {
namespace Hypnos {

class TcpSocket : public SocketBase
{
public:
    TcpSocket();
    ~TcpSocket() override;

    void Connect() override;
    void Disconnect() override;
    void Listen() override;
    void Receive() override;
    void Send() override;
private:
    Thread* recvThread;
    Thread* sendThread;

    void ProcessEvents();
    void SendResponses();
};

} // namespace Hypnos
} // namespace Blanketmen