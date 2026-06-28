#include "serverConfig.hpp"
#include "Headers/Connection.hpp"
#include <cstring>  
#include <iostream>

Connection::Connection(int fd, ServerConfig* config)
    : conn_num(0)
    , _fd(fd)
    , _config(config)
    , _read_buffer(config->getMaxBody())
    , _write_buffer(config->getMaxBody())
    , _request(config->getMaxBody())
    , _state(CSTATE_READING)
    , _last_active(std::time(NULL))
    , _peer_half_closed(false)
{}




Connection::~Connection()
{
    if (_fd >= 0)
    {
        ::close(_fd);
        _fd = -1;
    }
}

ssize_t Connection::recv()
{
    char    tmp[16 * 1024];
    ssize_t n = ::recv(_fd, tmp, sizeof(tmp), 0);

    if (n > 0)
    {
        _read_buffer.append(tmp, static_cast<size_t>(n));
        _touchActive();
    }
    return n;
}

ssize_t Connection::send()
{
    if (_write_buffer.empty())
        return 0;

    ssize_t n = ::send(_fd,
                       _write_buffer.data(),
                       _write_buffer.size(),
                       MSG_NOSIGNAL);
    if (n > 0)
    {
        _write_buffer.consume(static_cast<size_t>(n));
        _touchActive();
    }
    return n;
}

void Connection::reset()
{   
    _read_buffer.reset();
    _write_buffer.reset();
     size_t server_default = _config->getMaxBody();  
    _read_buffer.setMaxSize(server_default);  
    _write_buffer.setMaxSize(server_default);  
    _request.reset();
    _request.body.setMaxSize(server_default);
    _state             = CSTATE_READING;
    _peer_half_closed  = false;
    _touchActive();
}

bool Connection::isTimedOut(time_t timeout_seconds) const
{
    return (std::time(NULL) - _last_active) > timeout_seconds;
}

int                 Connection::fd()          const { return _fd;           }
ConnectionState     Connection::state()       const { return _state;        }
time_t              Connection::lastActive()  const { return _last_active;  }
const ServerConfig* Connection::config()      const { return _config;       }
void Connection::overide_conf(ServerConfig* c) {
    _config = c;
}

Buffer&       Connection::readBuffer()        { return _read_buffer;  }
const Buffer& Connection::readBuffer()  const { return _read_buffer;  }
Buffer&       Connection::writeBuffer()       { return _write_buffer; }
const Buffer& Connection::writeBuffer() const { return _write_buffer; }

HttpRequest&       Connection::request()       { return _request; }
const HttpRequest& Connection::request() const { return _request; }

void Connection::setProcessing() { _state = CSTATE_PROCESSING; }
void Connection::setWriting()    { _state = CSTATE_WRITING;    }
void Connection::setClosing()    { _state = CSTATE_CLOSING;    }
void Connection::setCgiRunning() {_state = CSTATE_CGI_RUNNING;}
void Connection::setReading()
{
    reset();
    _state = CSTATE_READING;
}


void Connection::setPeerHalfClosed() { _peer_half_closed = true; }
bool Connection::peerHalfClosed() const { return _peer_half_closed; }

void Connection::_touchActive() { _last_active = std::time(NULL); }


epoll_event Connection::buildEpollEvent()
{
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = this;
    ev.events   = EPOLLET | EPOLLRDHUP;

    switch (_state)
    {
        case CSTATE_READING:
            ev.events |= EPOLLIN;
            break;
        case CSTATE_WRITING:
            ev.events |= EPOLLOUT;
            break;
        default:
            break;
    }
    return ev;
}

void Connection::updateBufferSizes(size_t new_max)  
{  
    _read_buffer.setMaxSize(new_max);  
    _write_buffer.setMaxSize(new_max);  
}

const std::string& Connection::get_clientIp() const
{
    return _client_ip; 
}

void Connection::setClientIp(const std::string& ip) 
{
    _client_ip = ip; 
}

void Connection::hold_cofiguration(std::vector<const ServerConfig *> confs){
    configuration = confs;
}

void Connection::resize_buffers() {
    _read_buffer.setMaxSize(_config->getMaxBody());
    _write_buffer.setMaxSize(_config->getMaxBody());
    _request.body.setMaxSize(_config->getMaxBody());
}