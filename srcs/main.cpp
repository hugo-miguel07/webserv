#include "test.hpp"
#include "Client.hpp"
#include "Server.hpp" 

volatile sig_atomic_t g_running = 1;

void sig_handler(int sig)
{
    if (sig == SIGINT)
        g_running = 0;
}

int main()
{
    signal(SIGINT, sig_handler);
    Server server(8080);

    if (!server.init())
        return (-1);
    server.run();
    server.shut_down();
}

//testing command:
//nc -vz localhost 8080