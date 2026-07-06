#define _POSIX_X_SOURCE

#include "fd_util.h"
#include "tcp.h"
#include "epoll_util.h"
#include "socks.h"
#include <sys/epoll.h>
#include <signal.h> // sigaction(), struct sigaction
#include <iostream> // perror()

#define SOCKS5_PORT "1080"
#define MAX_QUEUED 100
#define MAX_EVENTS 10


int main()
{
    // Ignore the SIGPIPE signal so that we may safely return EPIPE when write triggers this signal
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, nullptr);

    int nfds;
    File_Descriptor listener;
    Epoll_Instance epoll_handle(MAX_EVENTS);

    listener = start_tcp_server(SOCKS5_PORT, MAX_QUEUED);
    if (listener == -1)
    {
        perror("tcpserv");
        return 1;
    }

    if (epoll_handle.is_valid())
    {
        if (epoll_handle.add_to_interest(listener, FILE_DESCRIPTOR_REF, &listener, EPOLLIN | EPOLLET) == -1){
            perror("Epoll_Instance: add_to_interest(): epoll_ctl: EPOLL_CTL_ADD");
            return 1;
        }
        // printf("added listener to epoll\n");
    }
    else
        return 1;
    

    for (;;) 
    {
        for (std::unordered_map<epoll_event_data*, bool>::iterator i =  epoll_handle.active_map.begin(); i != epoll_handle.active_map.end(); /* No increment condition here */)
        {
            if (!i->second){
                i++;
                continue;
            }
            epoll_event_data_type& type = i->first->type;
            if (type == FILE_DESCRIPTOR_REF || type == TARGET_CONNECTION_REF || type == TARGET_TIMER_REF){
                i++;
                continue;
            }
            SOCKS_Client* client = (SOCKS_Client*)i->first->ptr;
            bool erase_this = false;

            if (type == SOCKS_CLIENT_REF)
            {
                if (client->cevents & S5E_WRHUP)
                {
                    if (client->timer != nullptr)
                            epoll_handle.remove_from_interest(*client->timer);
                    if (client->target_host_fd != nullptr)
                        epoll_handle.remove_from_interest(*client->target_host_fd);
                    epoll_handle.remove_from_interest(client->get_file_descriptor());
                    delete client;
                    erase_this = true;
                }
                else if ((client->cevents & S5E_IN) || (client->cevents & S5E_OUT))
                {
                    // is timer nullptr or what ? do we need to check it ?
                    SOCKS_Returns ret = client->handle_request();

                    // std::cout<< "handle_request returned " << (int)ret << std::endl;
                    if (ret == S5_EAGAIN && 
                        (
                            (!(client->cevents & S5E_IN) && (client && S5E_RDNEED)) || 
                            (!(client->cevents & S5E_OUT) && (client && S5E_WRNEED))
                        ))
                        i->second = false;
                    else if (ret == S5_RDAGAIN || ret == S5_WRAGAIN);
                    else if (ret == S5_ADD_TARGET_CONNECT_TIMER)
                    {
                        epoll_handle.add_to_interest(*client->target_host_fd, TARGET_CONNECTION_REF, client, EPOLLOUT | EPOLLET);
                        Timer_File_Descriptor* tfd = new Timer_File_Descriptor(CLOCK_MONOTONIC, TFD_NONBLOCK);

                        // need to handle the error of timerfd_create()

                        struct itimerspec new_value = { { 0, 0 }, { 120, 0 } }; // 2 min single expiry timer
                        tfd->settime(0, &new_value, nullptr);
                        epoll_handle.add_to_interest(*tfd, TARGET_TIMER_REF, client, EPOLLIN | EPOLLET);
                        client->timer = tfd;
                    }
                    else if (ret == S5_CONNECTION_CLOSED) 
                    {
                        if (client->timer != nullptr)
                            epoll_handle.remove_from_interest(*client->timer);
                        if (client->target_host_fd != nullptr)
                            epoll_handle.remove_from_interest(*client->target_host_fd);
                        epoll_handle.remove_from_interest(client->get_file_descriptor());
                        delete client;
                        erase_this = true;
                    }
                    else if (ret == S5_SUCCESS)
                    {
                        i->first->type = CLIENT_REF;
                        epoll_handle.modify_event(client->get_file_descriptor(), i->first, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
                        epoll_handle.add_to_interest(*client->target_host_fd, TARGET_HOST_REF, client, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
                    }
                }
            }
            else if (type == CLIENT_REF || type == TARGET_HOST_REF)
            {
                if ((client->cevents & S5E_CLOSED) && (client->hevents & S5E_CLOSED))
                {
                    epoll_handle.remove_from_interest(*client->target_host_fd);
                    epoll_handle.remove_from_interest(client->get_file_descriptor());
                    delete client;
                    erase_this = true;
                }
                else 
                {
                    uint8_t& ev = type == CLIENT_REF ? client->cevents : client->hevents;
                    const File_Descriptor& fd = type == CLIENT_REF ? client->get_file_descriptor() : *client->target_host_fd;

                    if (ev & S5E_IN)
                    {
                        SOCKS_Returns ret = client->handle_communication(type == CLIENT_REF, true);
                        // std::cout<< "handle_communication returned " << (int)ret << std::endl;
                        if (ret == S5_SUCCESS || ret == S5_RDAGAIN || ret == S5_WRAGAIN);
                        else if (ret == S5_CONNECTION_CLOSED)
                        {
                            epoll_handle.remove_from_interest(fd);
                            erase_this = true;
                        }
                    }
                    if (ev & S5E_OUT)
                    {
                        SOCKS_Returns ret = client->handle_communication(type == CLIENT_REF, false);
                        // std::cout<< "handle_communication returned " << (int)ret << std::endl;
                        if (ret == S5_SUCCESS || ret == S5_RDAGAIN || ret == S5_WRAGAIN);
                        else if (ret == S5_CONNECTION_CLOSED)
                        {
                            epoll_handle.remove_from_interest(fd);
                            erase_this = true;
                        }
                    }
                    if (!(ev & S5E_IN) && !(ev & S5E_OUT))
                        i->second = false;
                }
            }


            if (erase_this){
                delete i->first;
                i = epoll_handle.active_map.erase(i);
            }
            else
                i++;
        }
        
        nfds = epoll_handle.get_events(1000);
        if (nfds == -1)
        {
            perror("Epoll_Instance: epoll_wait()");
            return 1;
        }

        for (int i = 0; i < nfds; i++) 
        {
            // Check for returned epoll events flags missing
            struct epoll_event_data* ep_edata= (struct epoll_event_data*)(epoll_handle.events_buff[i].data.ptr);
            uint32_t rcv_events = epoll_handle.events_buff[i].events;

            if (ep_edata->type == FILE_DESCRIPTOR_REF)
            {
                if (rcv_events & EPOLLIN)
                {
                    File_Descriptor *triggered = (File_Descriptor*)ep_edata->ptr;
                    if (*triggered == listener)
                        epoll_handle.nonblocking_socks_client_accept(*triggered);
                        // Need to handle the many requests from the same client for same thing so that the client doesn't clutter up
                    // std::cout<< "came outside" << std::endl;
                }
                if (rcv_events & EPOLLPRI)
                {
                    // handle out of band data (no need since TCP RFC 9293 suggest against using urgent mechanism and OOB data is urgent data in TCP)
                    // ioctl thing also need not be handled since it makes the application non-portable
                    // cgroups.events: file which handles the resources in the cgroupfs (is it needed to be handled ??) 
                }
            }
            else if (ep_edata->type == SOCKS_CLIENT_REF)
            {
                // std::cout<< "socks man" << std::endl;
                SOCKS_Client *client = (SOCKS_Client*)ep_edata->ptr;
                client->register_events(rcv_events, true);
                epoll_handle.active_map[ep_edata] = true;
            }
            else if (ep_edata->type == TARGET_CONNECTION_REF)
            {
                SOCKS_Client *client = (SOCKS_Client*)ep_edata->ptr;
                if (client->timer == nullptr) continue;
                bool erase = false; 
                
                if (rcv_events & EPOLLOUT)
                {
                    SOCKS_Returns ret = client->check_target_connection(false);
                    // if (ret == S5_SUCCESS)
                    // {
                    //     // std::cout<< "connection successfull" << std::endl;
                    //     ep_edata->type = TARGET_HOST_REF;
                    //     epoll_handle.modify_event(*client->target_host_fd, ep_edata, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
                    // }
                    // else
                    // {
                        epoll_handle.remove_from_interest(*client->target_host_fd);
                        erase = true;
                    // }
                }
                if (rcv_events & (EPOLLERR | EPOLLHUP))
                {
                    client->check_target_connection(true);
                    epoll_handle.remove_from_interest(*client->target_host_fd);
                    erase = true;
                }
                epoll_handle.remove_from_interest(*client->timer);
                delete client->timer;
                client->timer = nullptr;
                if (erase)
                {
                    epoll_handle.active_map.erase(ep_edata);
                    delete ep_edata;
                }
            }
            else if (ep_edata->type == TARGET_TIMER_REF)
            {
                SOCKS_Client *associated_client = (SOCKS_Client*)ep_edata->ptr;
                if (associated_client->timer == nullptr) continue;

                SOCKS_Returns ret = associated_client->check_target_connection(true);
                if (ret != S5_SUCCESS)
                    epoll_handle.remove_from_interest(*associated_client->target_host_fd);

                epoll_handle.remove_from_interest(*associated_client->timer);
                delete associated_client->timer;
                associated_client->timer = nullptr;
                
                epoll_handle.active_map.erase(ep_edata);
                delete ep_edata;
            }
            else if (ep_edata->type == TARGET_HOST_REF)
            {
                SOCKS_Client *associated_client = (SOCKS_Client*)ep_edata->ptr;
                associated_client->register_events(rcv_events, false);
                epoll_handle.active_map[ep_edata] = true;
            }
            else if (ep_edata->type == CLIENT_REF)
            {
                SOCKS_Client *client = (SOCKS_Client*)ep_edata->ptr;
                client->register_events(rcv_events, true);
                epoll_handle.active_map[ep_edata] = true;
            }
        }
    }
}