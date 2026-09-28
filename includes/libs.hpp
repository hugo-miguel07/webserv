#pragma once

#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdexcept>
#include "utils.hpp"

extern volatile sig_atomic_t g_running;

enum DirectiveType
{
    DIRECTIVE_LISTEN,
    DIRECTIVE_SERVER_NAME,
    DIRECTIVE_ROOT,
    DIRECTIVE_INDEX,
    DIRECTIVE_CLIENT_MAX_BODY_SIZE,
    DIRECTIVE_ERROR_PAGE,
    DIRECTIVE_LOCATION,
    DIRECTIVE_ALLOW_METHODS,
    DIRECTIVE_RETURN,
    DIRECTIVE_AUTOINDEX,
    DIRECTIVE_UPLOAD_STORE,
    DIRECTIVE_CGI_EXTENSION,
    DIRECTIVE_CGI_PATH,
    DIRECTIVE_UNKNOWN
};

struct Token
{
    std::string content;
    size_t      line;
};