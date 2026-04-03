NAME    = webserv

CXX     = c++
CXXFLAGS = -std=c++98 -Wall -Wextra -Werror

SRCS    = src/main.cpp \
          src/Connection.cpp \
          src/EventLoop.cpp \
          src/HttpParser.cpp \
          src/Buffer.cpp \
          src/HttpRequest.cpp \
          src/ConnectionManager.cpp \
          src/ConnectionState.cpp \
          src/ServerConfig.cpp \
          src/ResponseHandler.cpp

OBJS    = $(SRCS:.cpp=.o)

# ── targets ───────────────────────────────────────────────────

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
