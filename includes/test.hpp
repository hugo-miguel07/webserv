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
extern volatile sig_atomic_t g_running;