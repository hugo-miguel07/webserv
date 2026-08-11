#include "test.hpp"

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        return (std::cout << "Error" << std::endl, -1);
    sockaddr_in address;
    
    int port = 8080;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) == -1)
        return (close(server_fd), std::cout << "Error" << std::endl, -1);
    if (listen(server_fd, SOMAXCONN) == -1)
        return (close(server_fd), std::cout << "Error" << std::endl, -1);

    sockaddr_in client_addr;
    socklen_t   client_addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);
    if (client_fd == -1)
        return (close(server_fd), std::cout << "Error" << std::endl, -1);

    std::cout << "Success!" << std::endl;
    close(server_fd);
    close(client_fd);
}

//testing command:
//nc -vz localhost 8080