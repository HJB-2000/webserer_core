NAME    = webserv

CXX      = c++
CXXFLAGS = -g3 -std=c++98 -Wall -Wextra -Werror -I. -I conf

# ── your sources ──────────────────────────────────────────────
SRCS    = src/main.cpp \
          src/API_conf.cpp \
          src/Connection.cpp \
          src/EventLoop.cpp \
          src/HttpParser.cpp \
          src/Buffer.cpp \
          src/HttpRequest.cpp \
          src/ConnectionManager.cpp \
          src/ConnectionState.cpp \
          src/ResponseHandler.cpp \
          cgi/CgiHandler.cpp
        #   src/CgiStarter.cpp

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

# ── targets ───────────────────────────────────────────────────

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
