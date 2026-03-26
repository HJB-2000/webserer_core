#pragma once
#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>

/*lib to use memset*/
#include <string.h>
/**************** */

namespace socket_connection {
    class socket_
    {
        private :
        /*important data*/
        const char *node;
        const char *service;
        struct addrinfo hints;
        struct addrinfo *res;
        struct addrinfo *p;
        char ipstr[INET6_ADDRSTRLEN];
        int addrinfo_;




                
                
                

        /*make the obeject unique*/
        socket_(const socket_ &obj){};
        socket_ &operator=(const socket_ &obj){return *this;};
        /************/
        public :
        socket_();
        ~socket_();
/* make sure to find a good way to bring this node */
        void set_hints();
        struct addrinfo get_hints();

        void set_addrinfo_(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
        int get_addrinfo_();

        void get_pointer_address();

    };

};







#endif