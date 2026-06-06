#include "Proxy.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>

namespace
{
    const int BUFFER_SIZE = 16384;
    const int IO_TIMEOUT_MS = 250;

 

    void forwardTraffic(TcpClientSocket &source,
                        TcpClientSocket &destination,
                        std::atomic<bool> &running)
    {
        while(running)
        {
            const std::string data = source.recv(BUFFER_SIZE, IO_TIMEOUT_MS);

            if(!data.empty())
            {
                if(!destination.send(data, IO_TIMEOUT_MS))
                    running = false;
                continue;
            }

            if(source.status() != TcpClientSocket::STATUS_TIMEOUT)
                running = false;
        }
    }
}

NetProxy::NetProxy()
{
    remotePort = 0;
}

void NetProxy::handleConnection(TcpClientSocket &client, void *context)
{
    NetProxy *proxy = static_cast<NetProxy *>(context);
    if(proxy == NULL)
        return;

    TcpClientSocket remote(proxy->remoteAddress, proxy->remotePort);
    if(!remote.connect())
        return;

    std::atomic<bool> running(true);
    std::thread clientToRemote(forwardTraffic,
                               std::ref(client),
                               std::ref(remote),
                               std::ref(running));
    std::thread remoteToClient(forwardTraffic,
                               std::ref(remote),
                               std::ref(client),
                               std::ref(running));

    clientToRemote.join();
    remoteToClient.join();
}

void NetProxy::proxyBind(std::string localAddress,
                         int localPort,
                         std::string remoteAddress,
                         int remotePort)
{

    this->remoteAddress = remoteAddress;
    this->remotePort = remotePort;

    if(!listener.listen(localAddress, localPort, &NetProxy::handleConnection, this))
        return;

    // proxyBind owns the proxy lifecycle and remains active while the listener runs.
    std::mutex waitMutex;
    std::unique_lock<std::mutex> lock(waitMutex);
    std::condition_variable waitCondition;
    waitCondition.wait(lock, []() { return false; });
}
