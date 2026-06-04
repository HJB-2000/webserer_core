NAME    = webserv

CXX      = c++
CXXFLAGS = -g3 -std=c++98 -Wall -Wextra -Werror -fsanitize=address -I. -I conf 
# CXXFLAGS = -g3 -std=c++98 -Wall -Wextra -fsanitize=undefined -I, -I conf #-fsanitize=address,undefined -I. -I conf
# ── your sources ──────────────────────────────────────────────
SRCS    = src/main.cpp \
          src/make_listener.cpp \
          src/API_conf.cpp \
          src/Connection.cpp \
          src/HttpParser.cpp \
          src/Buffer.cpp \
          src/HttpRequest.cpp \
          src/ConnectionManager.cpp \
          src/ConnectionState.cpp \
          src/Logger.cpp \
          src/ResponseHandler.cpp \
          cgi/CgiHandler.cpp \
          src/EventLoop/EventLoop.cpp \
          src/EventLoop/EventLoop_helper_handlers.cpp \
          src/EventLoop/EventLoop_helper.cpp \
          src/EventLoop/EventLoop_helper_cgi.cpp \

# ── teammate config-parser sources (Phase 1) ──────────────────
SRCS   += conf/parsing.cpp \
          conf/LexerConfig.cpp \
          conf/eventsConfig.cpp \
          conf/serverConfig.cpp \
          conf/locationConfig.cpp \
          conf/httpConfig.cpp \
          conf/parserConf.cpp \
          conf/server_parser.cpp \
          conf/location_parser.cpp 

OBJS    = $(SRCS:.cpp=.o)

# ── headers ─────────────────────────────────
HEADERS = Headers/API_conf.hpp \
          Headers/Logger.hpp \
          Headers/EventLoop.hpp \
          Headers/CgiStarter.hpp \
          Headers/CgiRequestInfo.hpp \
          Headers/ResponseHandler.hpp \
          Headers/ConnectionManager.hpp \
          Headers/EventRef.hpp \
          Headers/HttpRequest.hpp \
          Headers/ConnectionState.hpp \
          Headers/CgiJob.hpp \
          Headers/HttpParser.hpp \
          Headers/Connection.hpp \
          Headers/buffer.hpp \
          cgi/CgiHandler.hpp \
          conf/serverConfig.hpp \
          conf/LexerConfig.hpp \
          conf/parserConf.hpp \
          conf/locationConfig.hpp \
          conf/eventsConfig.hpp \
          conf/parsing.hpp \
          conf/httpConfig.hpp

# ── targets ───────────────────────────────────────────────────

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
