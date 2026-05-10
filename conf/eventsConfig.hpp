#ifndef EVENTS_CONFIG_HPP
#define EVENTS_CONFIG_HPP

#include <string>

class eventsConfig
{
    public:
        eventsConfig();
        eventsConfig(const eventsConfig& obj);
        eventsConfig& operator=(const eventsConfig& obj);
        ~eventsConfig();
        void set_worker_connections(int work_connets);
        void set_event_model(std::string event_model);
        int get_worker_connections();
        std::string get_event_model();
        void set_default_conf();
    private:
        int  _worker_connections;
        std::string  _event_model;
};




#endif