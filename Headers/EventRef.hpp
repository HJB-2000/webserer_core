#ifndef EVENT_REF_HPP
#define EVENT_REF_HPP


enum EventKind 
{
    EV_SERVER,
    EV_CLIENT,
    EV_CGI
};

struct EventRef
{
    EventKind   kind;
    int         fd;
    EventRef(EventKind k, int f)
    :   kind(k)
    ,   fd(f)
    {}
};

#endif