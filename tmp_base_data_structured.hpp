#pragma once
#ifndef TMP_BASED_DATA_STRUCTURED
#define TMP_BASED_DATA_STRUCTURED

/****************BUFFER*************/

// std::vector<char>	    Page-aligned, no manual new/delete, C++98 OK
// Lazy _compact()          Only slides bytes when head ≥ 50% — avoids copy every consume
// _ensureCapacity()        doubles	Matches blueprint "double on full until cmbs"
// BufferOverflowException	Caller catches → sets response 413
// reset() on keep-alive	Reclaims memory cleanly between requests

/***********************************/

/********************HTTP_REQUEST ***/
// Field	        Consumer	            Note

// method	        Processing	            Validated against allowed_methods

// path	            Processing / CGI	    Raw, no percent-decode yet

// query_string	    CGI	                    → QUERY_STRING env var

// version	        Processing	            Enforces 1.0 / 1.1 policy

// headers	        Processing / CGI	    Lower-cased keys by Parser

// body	            Processing / CGI	    De-chunked before PS_COMPLETE

// parse_state	    Connection	            Drive the read loop state machine

// content_length	Parser → Connection	    Stop reading body at right byte

// chunked	        Parser → Connection	    Branch to de-chunk logic

/***********************************/

/***************ConnectionState */
// read_buffer (Buffer)
//       │
//       │  recv() appends raw bytes
//       ▼
//   HttpRequest (Parser fills)
//       │
//       │  parse_state == PS_COMPLETE
//       ▼
//   ConnectionState drives what epoll watches next
//       │
//       │  response bytes
//       ▼
// write_buffer (Buffer)
//       │
//       │  send() drains
//       ▼
//   CS_CLOSING or back to CS_READING

// State            epoll armed with	Triggered by								Transitions to

// CS_READING	    EPOLLIN				accept() / keep-alive reset					CS_PROCESSING

// CS_PROCESSING	nothing				PS_COMPLETE									CS_WRITING

// CS_WRITING	    EPOLLOUT			response ready								CS_READING or CS_CLOSING

// CS_CLOSING	    nothing				error / HUP / timeout / no keep-alive		destroyed

/***********************************/

/************connection.hpp */
// connection owns:
//   int               fd
//   time_t            last_active      ← update with time(NULL)
//   const ServerConfig* config
//   Buffer            read_buffer
//   Buffer            write_buffer
//   HttpParser        parser
//   HttpRequest       request
//   ResponseHandler   response_handler
//   ConnectionState   state

// ⚠️ epoll_event.data.ptr = raw pointer to Connection
//    map still OWNS the memory — epoll just BORROWS the pointer
//    NEVER use that pointer after erasing from map
//    delete BEFORE erase — always together, never separate

//								*************************

// Member			Type					Owned by		Note
// _fd				int						Connection		Closed only in destructor
// _config			const ServerConfig*		Server			Borrowed pointer, never deleted
// _read_buffer		Buffer					Connection		Grows up to cmbs
// _write_buffer	Buffer					Connection		Drained by send()
// _request			HttpRequest				Connection		Reset between keep-alive requests
// _state			ConnectionState			Connection		Only changed via explicit setters
// _last_active		time_t					Connection		Updated every I/O via _touchActive()

// Buffer.hpp
//     └── HttpRequest.hpp
//             └── ConnectionState.hpp
//                         └── Connection.hpp  ← we are here


/***********************************/

/****************ConnectionManager.hpp *******************/
// The ⚠️ danger — fully enforced now

// CORRECT — enforced inside _destroy()
// delete _connections[fd];   // 1st — destructor runs, fd closed
// _connections.erase(fd);    // 2nd — pointer gone from map

// epoll data.ptr for this fd is now dangling
// ConnectionManager is the ONLY place that touches this sequence

// Buffer.hpp
//     └── HttpRequest.hpp
//             └── ConnectionState.hpp
//                     └── Connection.hpp
//                             └── ConnectionManager.hpp  ← we are here

// Operation			Sequence									Why
// addConnection		accept → new → map insert → epoll ADD		Stable pointer before epoll sees it
// closeConnection		epoll DEL → delete → map erase				Stop events before object dies
// rearmEpoll			buildEpollEvent() → epoll MOD				State change reflected in epoll
// closeTimedOut		Collect → then close						Never modify map while iterating
// _destroy				delete then erase							The golden rule from the blueprint

/***********************************/
/***************EventLoop.hpp********************/
// epoll_wait()
// ├── fd == server_fd  → accept()    → new Connection
// ├── EPOLLERR/EPOLLHUP→ closeConnection()
// ├── EPOLLIN          → read → parse → maybe respond
// └── EPOLLOUT         → drain write buffer

// when epoll wakes up with an event we know two things:
//   • which fd fired
//   • what happened on that fd

// ⚠️ NEVER use the epoll raw pointer after removing
//    the Connection from the map — object already destroyed

// The ⚠️ danger — enforced in every handler
// cpp
// const int fd = conn->fd();   // save fd BEFORE any possible close

// _manager->closeConnection(fd);
// return;                       // ALWAYS return immediately
//                               // conn is dangling from this line on
//                               // NEVER touch conn after this

// Buffer.hpp
//     └── HttpRequest.hpp
//             └── ConnectionState.hpp
//                     └── Connection.hpp
//                             └── ConnectionManager.hpp
//                                     └── EventLoop.hpp  ← we are here


// Event				Handler				Behaviour
// fd == server_fd		_handleAccept		Loop accept() until EAGAIN
// EPOLLERR/EPOLLHUP	_handleError		Save fd, close, return
// EPOLLIN				_handleRead			Loop recv() until EAGAIN, parse, maybe respond
// EPOLLOUT				_handleWrite		Loop send() until EAGAIN or empty, keep-alive or close

/***********************************/
#endif