#include "fd_util.h"
#include "tcp.h"
#include "epoll_util.h"
#include "socks.h"
#include <cstdlib> // exit()
#include <sys/epoll.h>
#include <iostream> // perror()

#define SOCKS5_PORT "1080"
#define MAX_QUEUED 100
#define MAX_EVENTS 10


int main()
{
    int nfds;
    File_Descriptor listener;
    Epoll_Instance epoll_handle(MAX_EVENTS);

    listener = start_tcp_server(SOCKS5_PORT, MAX_QUEUED);
    if (listener == -1)
    {
        perror("tcpserv");
        exit(EXIT_FAILURE);
    }

    epoll_handle.add_to_interest(listener, FILE_DESCRIPTOR_REF, &listener, EPOLLIN | EPOLLET);

    

    for (;;) 
    {
        for (std::unordered_map<epoll_event_data*, bool>::iterator i =  epoll_handle.active_map.begin(); i != epoll_handle.active_map.end(); /* No increment condition here */)
        {
            if (!i->second) continue;
            epoll_event_data_type& type = i->first->type;
            if (type == FILE_DESCRIPTOR_REF || type == TARGET_CONNECTION_REF || type == TARGET_TIMER_REF) continue;
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

                    if (ret == S5_EAGAIN) i->second = false;
                    else if (ret == S5_RDAGAIN || ret == S5_WRAGAIN) continue;
                    else if (ret == S5_ADD_TARGET_TO_EPOLL)
                        epoll_handle.add_to_interest(*client->target_host_fd, TARGET_HOST_REF, client, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
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
                    }
                }
            }
            else if (type == CLIENT_REF)
            {
                if (client->cevents & S5E_IN)
                {
                    SOCKS_Returns ret = client->handle_communication(true, true);

                    if (ret == S5_SUCCESS) i->second = false;
                    else if (ret == S5_RDAGAIN) continue;
                    else if (ret == S5_CONNECTION_CLOSED && !(client->cevents & S5E_WRNEED) && !(client->hevents & S5E_WRNEED))
                    {
                        if (client->timer != nullptr)
                            epoll_handle.remove_from_interest(*client->timer);
                        if (client->target_host_fd != nullptr)
                            epoll_handle.remove_from_interest(*client->target_host_fd);
                        epoll_handle.remove_from_interest(client->get_file_descriptor());
                        delete client;
                        erase_this = true;
                    }
                }
            }


            if (erase_this)
                i = epoll_handle.active_map.erase(i);
            else
                i++;
        }
        
        nfds = epoll_handle.get_events(-1);

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
                SOCKS_Client *client = (SOCKS_Client*)ep_edata->ptr;
                client->register_events(rcv_events, true);
                epoll_handle.active_map[ep_edata] = true;
            }
            else if (ep_edata->type == TARGET_CONNECTION_REF)
            {
                SOCKS_Client *client = (SOCKS_Client*)ep_edata->ptr;
                if (client->timer == nullptr) continue;
                
                if (rcv_events & EPOLLOUT)
                {
                    SOCKS_Returns ret = client->check_target_connection(false);
                    if (ret == S5_SUCCESS)
                        epoll_handle.add_to_interest(*client->target_host_fd, TARGET_HOST_REF, client, EPOLLIN | EPOLLOUT | EPOLLET);
                    else
                        epoll_handle.remove_from_interest(*client->target_host_fd);
                }
                if (rcv_events & (EPOLLERR | EPOLLHUP))
                {
                    client->check_target_connection(true);
                    epoll_handle.remove_from_interest(*client->target_host_fd);
                }
                epoll_handle.remove_from_interest(*client->timer);
                delete client->timer;
                client->timer = nullptr;
                epoll_handle.active_map.erase(ep_edata);
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