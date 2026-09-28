#include "libs.hpp"
#include "Client.hpp"
#include "Server.hpp" 
#include "Config.hpp"

volatile sig_atomic_t g_running = 1;

void sig_handler(int sig)
{
    if (sig == SIGINT)
        g_running = 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return (std::cerr << "Server expects a configuration file." << std::endl, -1);
    else if (argc > 2)
        return (std::cerr << "Server can only handle one configuration file." << std::endl, -1);
    Config config;

    try
    {
        config.parse(argv[1]);
        signal(SIGINT, sig_handler);
        Server server(config);

        if (!server.init())
            return (-1);
        server.run();
    }
    catch(std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        return (-1);
    }
    return (0);
}

//testing command:
//nc -vz localhost 8080