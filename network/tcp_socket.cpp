#include "tcp_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace
{
    bool isValidEndpoint(const std::string &host, int port)
    {
        return !host.empty() && port > 0 && port <= 65535;
    }

    bool isValidPort(int port)
    {
        return port > 0 && port <= 65535;
    }

    bool isTimeoutErrno(int code)
    {
        return code == EAGAIN || code == EWOULDBLOCK;
    }

    bool setSocketTimeout(int fd, int option, int timeoutMs)
    {
        if(timeoutMs < 0)
            return true;

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        return setsockopt(fd, SOL_SOCKET, option, &tv, sizeof(tv)) == 0;
    }

    bool connectWithTimeout(int fd, const struct sockaddr *addr, socklen_t addrLen, int timeoutMs)
    {
        if(timeoutMs < 0)
            return ::connect(fd, addr, addrLen) == 0;

        const int oldFlags = fcntl(fd, F_GETFL, 0);
        if(oldFlags < 0)
            return false;

        if(fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK) != 0)
            return false;

        int ret = ::connect(fd, addr, addrLen);
        if(ret == 0)
        {
            (void)fcntl(fd, F_SETFL, oldFlags);
            return true;
        }

        if(errno != EINPROGRESS)
        {
            (void)fcntl(fd, F_SETFL, oldFlags);
            return false;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(fd, &writeSet);

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        ret = select(fd + 1, NULL, &writeSet, NULL, &tv);
        if(ret <= 0)
        {
            (void)fcntl(fd, F_SETFL, oldFlags);
            return false;
        }

        int soError = 0;
        socklen_t soLen = sizeof(soError);
        if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0)
        {
            (void)fcntl(fd, F_SETFL, oldFlags);
            return false;
        }

        (void)fcntl(fd, F_SETFL, oldFlags);
        return soError == 0;
    }

    bool readCurrentTimeout(int fd, int option, struct timeval &out)
    {
        socklen_t len = sizeof(out);
        return getsockopt(fd, SOL_SOCKET, option, &out, &len) == 0;
    }

    void restoreTimeoutIfNeeded(int fd, int option, bool hasPrevious, const struct timeval &previous)
    {
        if(hasPrevious)
            (void)setsockopt(fd, SOL_SOCKET, option, &previous, sizeof(previous));
    }
}

TcpClientSocket::TcpClientSocket()
{
    host = "";
    port = 0;
    sockFd = -1;
    sockStatus = STATUS_DISCONNECTED;
}

TcpClientSocket::TcpClientSocket(const std::string &host, int port)
{
    this->host = host;
    this->port = port;
    sockFd = -1;
    sockStatus = STATUS_DISCONNECTED;
}

TcpClientSocket::TcpClientSocket(int acceptedFd)
{
    host = "";
    port = 0;
    sockFd = acceptedFd;
    sockStatus = acceptedFd >= 0 ? STATUS_CONNECTED : STATUS_DISCONNECTED;
}

TcpClientSocket::~TcpClientSocket()
{
    close();
}

bool TcpClientSocket::connect(int timeoutMs)
{
    close();

    if(!isValidEndpoint(host, port))
    {
        sockStatus = STATUS_INVALID_ENDPOINT;
        return false;
    }

    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portBuffer[16];
    snprintf(portBuffer, sizeof(portBuffer), "%d", port);

    const int gaiRet = getaddrinfo(host.c_str(), portBuffer, &hints, &result);
    if(gaiRet != 0 || result == NULL)
    {
        sockStatus = STATUS_DNS_ERROR;
        return false;
    }

    bool connected = false;
    for(rp = result; rp != NULL; rp = rp->ai_next)
    {
        const int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(fd < 0)
            continue;

        if(connectWithTimeout(fd, rp->ai_addr, rp->ai_addrlen, timeoutMs))
        {
            sockFd = fd;
            connected = true;
            break;
        }

        ::close(fd);
    }

    freeaddrinfo(result);

    if(!connected)
    {
        sockStatus = STATUS_CONNECT_ERROR;
        return false;
    }

    sockStatus = STATUS_CONNECTED;
    return true;
}

bool TcpClientSocket::connect(const std::string &host, int port, int timeoutMs)
{
    this->host = host;
    this->port = port;
    return connect(timeoutMs);
}

bool TcpClientSocket::send(const std::string &data, int timeoutMs)
{
    if(sockFd < 0)
    {
        sockStatus = STATUS_DISCONNECTED;
        return false;
    }

    const int totalSize = (int)data.size();
    if(totalSize <= 0)
        return true;

    const char *buffer = data.c_str();
    int totalSent = 0;
    struct timeval previousTimeout;
    bool hasPreviousTimeout = false;

    if(timeoutMs >= 0)
    {
        hasPreviousTimeout = readCurrentTimeout(sockFd, SO_SNDTIMEO, previousTimeout);
        if(!setSocketTimeout(sockFd, SO_SNDTIMEO, timeoutMs))
        {
            sockStatus = STATUS_OPTION_ERROR;
            return false;
        }
    }

    while(totalSent < totalSize)
    {
        const ssize_t bytes = ::send(sockFd, buffer + totalSent, totalSize - totalSent, 0);

        if(bytes < 0)
        {
            if(errno == EINTR)
                continue;

            if(isTimeoutErrno(errno))
            {
                restoreTimeoutIfNeeded(sockFd, SO_SNDTIMEO, hasPreviousTimeout, previousTimeout);
                sockStatus = STATUS_TIMEOUT;
                return false;
            }

            restoreTimeoutIfNeeded(sockFd, SO_SNDTIMEO, hasPreviousTimeout, previousTimeout);
            sockStatus = STATUS_SEND_ERROR;
            return false;
        }

        if(bytes == 0)
        {
            restoreTimeoutIfNeeded(sockFd, SO_SNDTIMEO, hasPreviousTimeout, previousTimeout);
            close();
            sockStatus = STATUS_REMOTE_CLOSED;
            return false;
        }

        totalSent += (int)bytes;
    }

    restoreTimeoutIfNeeded(sockFd, SO_SNDTIMEO, hasPreviousTimeout, previousTimeout);
    sockStatus = STATUS_CONNECTED;
    return true;
}

std::string TcpClientSocket::recv(int maxBytes, int timeoutMs)
{
    std::string result;

    if(sockFd < 0)
    {
        sockStatus = STATUS_DISCONNECTED;
        return result;
    }

    if(maxBytes <= 0)
    {
        sockStatus = STATUS_RECV_ERROR;
        return result;
    }

    std::vector<char> buffer(maxBytes);
    ssize_t bytes = 0;
    struct timeval previousTimeout;
    bool hasPreviousTimeout = false;

    if(timeoutMs >= 0)
    {
        hasPreviousTimeout = readCurrentTimeout(sockFd, SO_RCVTIMEO, previousTimeout);
        if(!setSocketTimeout(sockFd, SO_RCVTIMEO, timeoutMs))
        {
            sockStatus = STATUS_OPTION_ERROR;
            return result;
        }
    }

    do
    {
        bytes = ::recv(sockFd, buffer.data(), maxBytes, 0);
    } while(bytes < 0 && errno == EINTR);

    if(bytes > 0)
    {
        result.assign(buffer.data(), (size_t)bytes);
        restoreTimeoutIfNeeded(sockFd, SO_RCVTIMEO, hasPreviousTimeout, previousTimeout);
        sockStatus = STATUS_CONNECTED;
        return result;
    }

    if(bytes == 0)
    {
        restoreTimeoutIfNeeded(sockFd, SO_RCVTIMEO, hasPreviousTimeout, previousTimeout);
        close();
        sockStatus = STATUS_REMOTE_CLOSED;
        return result;
    }

    if(isTimeoutErrno(errno))
    {
        restoreTimeoutIfNeeded(sockFd, SO_RCVTIMEO, hasPreviousTimeout, previousTimeout);
        sockStatus = STATUS_TIMEOUT;
        return result;
    }

    restoreTimeoutIfNeeded(sockFd, SO_RCVTIMEO, hasPreviousTimeout, previousTimeout);
    sockStatus = STATUS_RECV_ERROR;
    return result;
}

int TcpClientSocket::status()
{
    return sockStatus;
}

bool TcpClientSocket::close()
{
    if(sockFd >= 0)
    {
        shutdown(sockFd, SHUT_RDWR);
        ::close(sockFd);
        sockFd = -1;
    }

    sockStatus = STATUS_DISCONNECTED;
    return true;
}

TcpServerSocket::TcpServerSocket()
{
    host = "";
    port = 0;
    sockFd = -1;
    clientFd = -1;
    sockStatus = STATUS_DISCONNECTED;
    acceptRunning = false;
}

TcpServerSocket::TcpServerSocket(const std::string &host, int port)
{
    this->host = host;
    this->port = port;
    sockFd = -1;
    clientFd = -1;
    sockStatus = STATUS_DISCONNECTED;
    acceptRunning = false;
}

TcpServerSocket::~TcpServerSocket()
{
    close();
}

bool TcpServerSocket::listen(ConnectionHandler handler)
{
    close();

    if(!isValidPort(port))
    {
        sockStatus = STATUS_INVALID_ENDPOINT;
        return false;
    }

    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    char portBuffer[16];
    snprintf(portBuffer, sizeof(portBuffer), "%d", port);

    const char *bindHost = host.empty() ? NULL : host.c_str();
    const int gaiRet = getaddrinfo(bindHost, portBuffer, &hints, &result);
    if(gaiRet != 0 || result == NULL)
    {
        sockStatus = STATUS_DNS_ERROR;
        return false;
    }

    bool listening = false;
    for(rp = result; rp != NULL; rp = rp->ai_next)
    {
        const int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(fd < 0)
            continue;

        int reuse = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        if(bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 && ::listen(fd, SOMAXCONN) == 0)
        {
            sockFd = fd;
            listening = true;
            break;
        }

        ::close(fd);
    }

    freeaddrinfo(result);

    if(!listening)
    {
        sockStatus = STATUS_CONNECT_ERROR;
        return false;
    }

    sockStatus = STATUS_CONNECTED;

    if(handler != NULL)
    {
        acceptRunning = true;
        const int listeningFd = sockFd;
        acceptThread = std::thread([this, listeningFd, handler]()
        {
            while(acceptRunning)
            {
                int acceptedFd;
                do
                {
                    acceptedFd = accept(listeningFd, NULL, NULL);
                } while(acceptedFd < 0 && errno == EINTR && acceptRunning);

                if(acceptedFd < 0)
                    break;

                if(!acceptRunning)
                {
                    ::close(acceptedFd);
                    break;
                }

                std::thread([handler, acceptedFd]()
                {
                    try
                    {
                        handler(acceptedFd);
                    }
                    catch(...)
                    {
                    }

                    ::shutdown(acceptedFd, SHUT_RDWR);
                    ::close(acceptedFd);
                }).detach();
            }
        });
    }

    return true;
}

bool TcpServerSocket::listen(int port, ConnectionHandler handler)
{
    this->port = port;
    return listen(handler);
}

bool TcpServerSocket::listen(SocketHandler handler, void *context)
{
    if(handler == NULL)
        return listen();

    if(!listen())
        return false;

    acceptRunning = true;
    const int listeningFd = sockFd;
    acceptThread = std::thread([this, listeningFd, handler, context]()
    {
        while(acceptRunning)
        {
            int acceptedFd;
            do
            {
                acceptedFd = accept(listeningFd, NULL, NULL);
            } while(acceptedFd < 0 && errno == EINTR && acceptRunning);

            if(acceptedFd < 0)
                break;

            if(!acceptRunning)
            {
                ::close(acceptedFd);
                break;
            }

            std::thread([handler, context, acceptedFd]()
            {
                TcpClientSocket client(acceptedFd);

                try
                {
                    handler(client, context);
                }
                catch(...)
                {
                }
            }).detach();
        }
    });

    return true;
}

bool TcpServerSocket::listen(int port, SocketHandler handler, void *context)
{
    this->port = port;
    return listen(handler, context);
}

bool TcpServerSocket::listen(const std::string &host, int port, SocketHandler handler, void *context)
{
    this->host = host;
    this->port = port;
    return listen(handler, context);
}

std::string TcpServerSocket::recv(int maxBytes)
{
    std::string result;

    if(sockFd < 0)
    {
        sockStatus = STATUS_DISCONNECTED;
        return result;
    }

    if(maxBytes <= 0)
    {
        sockStatus = STATUS_RECV_ERROR;
        return result;
    }

    if(clientFd < 0)
    {
        do
        {
            clientFd = accept(sockFd, NULL, NULL);
        } while(clientFd < 0 && errno == EINTR);

        if(clientFd < 0)
        {
            sockStatus = STATUS_CONNECT_ERROR;
            return result;
        }
    }

    std::vector<char> buffer(maxBytes);
    ssize_t bytes = 0;
    do
    {
        bytes = ::recv(clientFd, buffer.data(), maxBytes, 0);
    } while(bytes < 0 && errno == EINTR);

    if(bytes > 0)
    {
        result.assign(buffer.data(), (size_t)bytes);
        sockStatus = STATUS_CONNECTED;
        return result;
    }

    ::shutdown(clientFd, SHUT_RDWR);
    ::close(clientFd);
    clientFd = -1;

    if(bytes == 0)
    {
        sockStatus = STATUS_REMOTE_CLOSED;
        return result;
    }

    sockStatus = STATUS_RECV_ERROR;
    return result;
}
bool TcpServerSocket::sendResponse(const std::string &data){
    size_t total = 0;

    while (total < data.size())
    {
        ssize_t sent = send(
            clientFd,
            data.data() + total,
            data.size() - total,
            0
        );
        if (sent <= 0)
            return false;
        total += sent;
    }
    return true;
}
bool TcpServerSocket::close()
{
    acceptRunning = false;

    if(clientFd >= 0)
    {
        ::shutdown(clientFd, SHUT_RDWR);
        ::close(clientFd);
        clientFd = -1;
    }

    if(sockFd >= 0)
    {
        ::shutdown(sockFd, SHUT_RDWR);
        ::close(sockFd);
        sockFd = -1;
    }

    if(acceptThread.joinable())
        acceptThread.join();

    sockStatus = STATUS_DISCONNECTED;
    return true;
}
