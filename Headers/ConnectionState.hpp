#ifndef CONNECTION_STATE_HPP
#define CONNECTION_STATE_HPP

enum ConnectionState
{
    CSTATE_READING    = 0,  
    CSTATE_PROCESSING,      
    CSTATE_CGI_RUNNING,
    CSTATE_WRITING,         
    CSTATE_CLOSING          
};

const char* connStateStr(ConnectionState s);

#endif
