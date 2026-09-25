#include "test.hpp"
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
        return (std::cout << "Server expects a configuration file." << std::endl, 0);
    else if (argc > 2)
        return (std::cout << "Server can only handle one configuration file." << std::endl, 0);
    Config config;

    try
    {
        config.parse(argv[1]);
    }
    catch(std::runtime_error& e)
    {
        std::cout << e.what() << std::endl;
    }

    signal(SIGINT, sig_handler);
    Server server(config);

    if (!server.init())
         return (-1);
    server.run();
    server.shut_down();

}

//testing command:
//nc -vz localhost 8080