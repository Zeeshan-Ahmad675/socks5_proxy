#include "tcp.h"
#include <iostream> // perror()
#include <cstring> // memset()
#include <cstdint> // uint8_t
#include <unistd.h> // close()
#include <sys/socket.h> // socket(), setsockopt()
#include <netdb.h> // struct addrinfo, getaddrinfo(), gai_strerror(), freeaddrinfo()
#include <errno.h>
#include <arpa/inet.h>

#define freeaddrinfo_and_return(result, ret_value) \
    do { \
        freeaddrinfo(result); \
        return ret_value; \
    } while(0)



File_Descriptor start_tcp_server (const char* port, int max_queued) {
    int sockfd, n;
    struct addrinfo hints;
    struct addrinfo *result, *rp;


    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // Allows IPv4 or IPv6 (although the documentation mentions all network families are possible, but in implementation only IP is used and other families became obsolete over time.)
    hints.ai_socktype = SOCK_STREAM; // Byte Steam Socket
    hints.ai_flags = AI_PASSIVE; // Wildcard IP address
    hints.ai_protocol = IPPROTO_TCP; // TCP
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    if ((n = getaddrinfo(nullptr, port, &hints, &result)) != 0)
    {
        std::cerr << "getaddrinfo: " << gai_strerror(n) << std::endl;
        return File_Descriptor();
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next)
    {
        if ((sockfd = socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK, rp->ai_protocol)) < 0)
        {
            perror("socket");
            continue;
        }

        // For tesing and debugging. Not for prodution, since it makes TCP less reliable
        n = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) < 0)
        {  
            perror("SO_REUSEADDR");
            close(sockfd);
            continue;
        }
        // ..

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) < 0)
        {
            perror("bind");
            close(sockfd);
            continue;
        }
        
        break;
    }

    if (rp == nullptr)
    {
        std::cerr << "Server failed to bind" << std::endl;
        return File_Descriptor();
    }

    freeaddrinfo(result);
    std::cout << "Server bound to port " << port << std::endl;
    
    if (listen(sockfd, max_queued) < 0)
    {
        perror("listen");
        close(sockfd);
        return File_Descriptor();
    }

    return File_Descriptor(sockfd);
}


int open_new_connection_to_target(SOCKS_Client& client)
{
    const uint8_t* target_addr_port = client.get_target_host_address_and_port();
    if (target_addr_port == nullptr) return TCP_SERVER_ERROR;

    int sockfd, n;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    char name[256] = { 0 }, service[3] = { 0 };

    switch(target_addr_port[0]) {
    case 0x01:
        memcpy(name, target_addr_port + 1, 4);
        memcpy(service, target_addr_port + 5, 2);
        break;
    case 0x03:
        memcpy(name, target_addr_port + 2, target_addr_port[1]);
        memcpy(service, target_addr_port + 2 + target_addr_port[1], 2);
        break;
    case 0x04:
        memcpy(name, target_addr_port + 1, 16);
        memcpy(service, target_addr_port + 17, 2);
        break;
    default:
        delete[] target_addr_port;
        return TCP_ATYP_UNSUPPORTED;
    }
    delete[] target_addr_port;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // Allows IPv4 or IPv6 (although the documentation mentions all network families are possible, but in implementation only IP is used and other families became obsolete over time.)
    hints.ai_socktype = SOCK_STREAM; // Byte Steam Socket
    hints.ai_protocol = IPPROTO_TCP; // TCP
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    if ((n = getaddrinfo(name, service, &hints, &result)) != 0)
    {
        int err = errno;
        std::cerr << "getaddrinfo: " << gai_strerror(n) << std::endl;
        switch (n){
            case EAI_SYSTEM:
                if (err == ENETUNREACH)
                    return TCP_NETWORK_UNREACHABLE;
                else freeaddrinfo_and_return(result, TCP_EAGAIN);
            case EAI_ADDRFAMILY:
            case EAI_BADFLAGS:
            case EAI_FAMILY:    
            case EAI_MEMORY:
            case EAI_SOCKTYPE:
                freeaddrinfo_and_return(result, TCP_SERVER_ERROR);
            case EAI_FAIL:
            case EAI_NODATA:
            case EAI_NONAME:
                freeaddrinfo_and_return(result, TCP_HOST_UNREACHABLE);
            case EAI_SERVICE:
                freeaddrinfo_and_return(result, TCP_CONNECTION_REFUSED);
            case EAI_AGAIN:
                freeaddrinfo_and_return(result, TCP_EAGAIN);
        }
    }


    for (rp = result; rp != nullptr; rp = rp->ai_next)
    {
        if ((sockfd = socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK, rp->ai_protocol)) < 0)
        {
            perror("socket");
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) < 0)
        {
            switch (errno) {
                case EINPROGRESS:
                    client.target_host_fd = File_Descriptor(sockfd);
                    freeaddrinfo_and_return(result, TCP_INPROGRESS);
                case ENETUNREACH:
                    freeaddrinfo_and_return(result, TCP_NETWORK_UNREACHABLE);
                case ECONNREFUSED:
                    freeaddrinfo_and_return(result, TCP_CONNECTION_REFUSED);
                default:
                    close(sockfd);
                    continue;
            }
        }
        break;
    }
    
    if (rp == nullptr)
    {
        std::cerr << "Failed to connect to remote application" << std::endl;
        freeaddrinfo_and_return(result, TCP_SERVER_ERROR);
    }
    else
        client.target_host_fd = File_Descriptor(sockfd);

    freeaddrinfo(result);


    sockaddr bind_addr;
    socklen_t addrlen;
    addrlen = sizeof(bind_addr);
    if (getsockname(client.target_host_fd.get_fd(), &bind_addr, &addrlen) == -1)
    {
        perror("SOCKS_Client: EVALUATION_AND_REPLY: getsockname");
        return TCP_SERVER_ERROR;
    }

    client.set_bind_address_and_port(&bind_addr, addrlen);
    return TCP_SUCCESS;
}