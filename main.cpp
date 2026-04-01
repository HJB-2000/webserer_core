#include "webserver.hpp"


int main(int ac, char *av[])
{
    (void)ac;
    (void)av;
    {
        socket_connection::socket_ conn_;
        if (conn_.create_socket() < 0)
            std::cerr << "\033[031mwe could not open a connection socket \033[0m"
                <<std::endl;
        while(true)
        {
            if (conn_.accept_connection() < 0)
                continue;

            // Temporary stop point for core-only phase.
            // Remove this break when the next server parts are integrated.
            break;
        }
    }
    /*i added brackets for to serve the purpose of scope so the desctructor get 
        called for cleeaning with the ability to continue execution
        after cleanup
    */

}