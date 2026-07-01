NAME    = webserv

CXX     = c++
CXXFLAGS = -g3 -std=c++98 -Wall -Wextra -Werror -MMD -MP  -I. -I conf
# CXXFLAGS = -g3 -std=c++98 -Wall -Wextra -Werror -MMD -MP -fsanitize=thread -I. -I conf

OBJ_DIR = objects

# ── sources ──────────────────────────────────────────────
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
          cgi/CgiHelpers.cpp \
          cgi/CgiCleanning.cpp \
          src/ResponseHandler_helper.cpp \
          src/ResponseLoader.cpp \
          conf/parsing.cpp \
          conf/LexerConfig.cpp \
          conf/eventsConfig.cpp \
          conf/serverConfig.cpp \
          conf/locationConfig.cpp \
          conf/httpConfig.cpp \
          conf/parserConf.cpp \
          conf/server_parser.cpp \
          conf/location_parser.cpp 

# ── objects ──────────────────────────────────────────────
OBJS = $(addprefix $(OBJ_DIR)/, $(SRCS:.cpp=.o))

# ── targets ──────────────────────────────────────────────

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)


$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# include dependency files
-include $(OBJS:.o=.d)

# ── cleaning ─────────────────────────────────────────────

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re