#ifndef _LIN_NETWORK_PROXY_
#define _LIN_NETWORK_PROXY_

#include <string>

#include "tcp_socket.h"

class NetProxy{
    private:
        TcpServerSocket listener;
        std::string remoteAddress;
        int remotePort;

        static void handleConnection(TcpClientSocket &client, void *context);

    public:
        NetProxy();
        virtual void proxyBind(std::string localAddress, int localPort, std::string remoteAddress, int remotePort);
};
#endif
