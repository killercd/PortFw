#ifndef _LIN_TCP_SOCKET_
#define _LIN_TCP_SOCKET_

#include <atomic>
#include <string>
#include <thread>

class TcpClientSocket
{
    friend class TcpServerSocket;

    private:
        std::string host;
        int port;
        int sockFd;
        std::atomic<int> sockStatus;

        explicit TcpClientSocket(int acceptedFd);

    public: 
        enum SocketStatus
        {
            STATUS_DISCONNECTED = 0,
            STATUS_CONNECTED = 1,
            STATUS_INVALID_ENDPOINT = -1,
            STATUS_DNS_ERROR = -2,
            STATUS_CONNECT_ERROR = -3,
            STATUS_SEND_ERROR = -4,
            STATUS_RECV_ERROR = -5,
            STATUS_REMOTE_CLOSED = -6,
            STATUS_TIMEOUT = -7,
            STATUS_OPTION_ERROR = -8
        };

        TcpClientSocket();
        TcpClientSocket(const std::string &host, int port);
        virtual ~TcpClientSocket();

        virtual bool connect(int timeoutMs = -1);
        virtual bool connect(const std::string &host, int port, int timeoutMs = -1);
        virtual bool send(const std::string &data, int timeoutMs = -1);
        virtual std::string recv(int maxBytes = 4096, int timeoutMs = -1);
        virtual int status();
        virtual bool close();
};


class TcpServerSocket{
    public:
        // clientFd is valid for the duration of the callback and is closed automatically afterward.
        typedef void (*ConnectionHandler)(int clientFd);
        typedef void (*SocketHandler)(TcpClientSocket &client, void *context);

    private:
        std::string host;
        int port;
        int sockFd;
        int clientFd;
        int sockStatus;
        std::atomic<bool> acceptRunning;
        std::thread acceptThread;

    public:
        enum SocketStatus
        {
            STATUS_DISCONNECTED = 0,
            STATUS_CONNECTED = 1,
            STATUS_INVALID_ENDPOINT = -1,
            STATUS_DNS_ERROR = -2,
            STATUS_CONNECT_ERROR = -3,
            STATUS_SEND_ERROR = -4,
            STATUS_RECV_ERROR = -5,
            STATUS_REMOTE_CLOSED = -6,
            STATUS_TIMEOUT = -7,
            STATUS_OPTION_ERROR = -8
        };
        TcpServerSocket();
        TcpServerSocket(const std::string &host, int port);
        virtual ~TcpServerSocket();


        virtual bool listen(ConnectionHandler handler = NULL);
        virtual bool listen(int port, ConnectionHandler handler = NULL);
        virtual bool listen(SocketHandler handler, void *context);
        virtual bool listen(int port, SocketHandler handler, void *context);
        virtual bool listen(const std::string &host, int port, SocketHandler handler, void *context);
        virtual std::string recv(int maxBytes = 4096);
        virtual bool sendResponse(const std::string &data);
        virtual bool close();

};
#endif
