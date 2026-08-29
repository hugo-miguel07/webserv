#include "ConfigParser.hpp"

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

bool isspace(char c)
{
    if (c == '\f' || c == '\n' || c == ' ' || c == '\r' || c == '\t' || c == '\v')
        return (true);
    return (false);
}

std::vector<std::string> ConfigParser::tokenize(const std::string& input) const
{
    std::string temp_storage;
    std::vector<std::string> storage;

    for (size_t i = 0; i < input.size(); i++)
    {
        char c = input[i];
        if (c == '{' || c == '}' || c == ';')
        {
            if (!temp_storage.empty())
            {
                storage.push_back(temp_storage);
                temp_storage.clear();
            }
            storage.push_back(std::string(1, c));
        }
        else if (isspace(c))
        {
            if (!temp_storage.empty())
            {
                storage.push_back(temp_storage);
                temp_storage.clear();
            }
        }
        else
            temp_storage += c;
    }
    if (!temp_storage.empty())
        storage.push_back(temp_storage);


    //TOKENS DEBUG
    // int i = 0;
    // for (std::vector<std::string>::iterator it = storage.begin(); it != storage.end(); ++it)
    // {
    //         std::cout << "[" << i << "]: " << *it << "\n";
    //         i++;
    // }
    return (storage);
}

ServerConfig ConfigParser::parseServerInfo(const std::vector<std::string>& tokens, size_t &i)
{
    ServerConfig serv;
    int bracket_count = 0;

    ++i;
    while (i < tokens.size())
    {
        if (tokens[i] == "{")
        {
            bracket_count++;
            i++;
            continue;
        }
        if (tokens[i] == "}")
        {
            bracket_count--;
            i++;
            if (bracket_count == 0)
                break;
            continue;
        }

        if ((tokens[i] == "listen") && (i + 1 < tokens.size()))
        {
            ++i;
            int port = atoi(tokens[i].c_str());
            if (port < 1 || port > 65535)
            {
                std::cout << "Error\nInvalid port";
            }
            else
            {
                serv.setPort(port);
            }
        }
        if (tokens[i] == "server_name")
        {
            ++i;
            while (tokens[i] != ";" && i < tokens.size())
            {
                serv.setServerName(tokens[i]);
                i++;
            }
        }
        if ((tokens[i] == "root") && (i + 1 < tokens.size()))
        {
            ++i;
            serv.setRoot(tokens[i]); 
        }
        if ((tokens[i] == "index") && (i + 1 < tokens.size()))
        {
            ++i;
            serv.setIndex(tokens[i]);
        }
        i++;
    }

    //PARSING DEBUG
    std::cout << "Port: " << serv.getPort() << "\n";
    std::vector<std::string> names = serv.getServerName();
    std::cout << "Server names:\n";
    for (size_t k = 0; k < names.size(); k++)
        std::cout << names[k] << "\n";
    std::cout << "root: " << serv.getRoot() << "\n";
    std::cout << "index: " << serv.getIndex() << "\n";
    //
    return (serv);
}

std::vector<ServerConfig> ConfigParser::parseServers(const std::vector<std::string>& tokens)
{
    std::vector<ServerConfig> servConfigs;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] == "server")
        {
            servConfigs.push_back(parseServerInfo(tokens, i));
        }
        // if (tokens[i] == "server")
        // {
        //     i++;
        //     ServerConfig server_info;
        //     for (std::vector<std::string>::const_iterator it = tokens.begin() + i; it != tokens.end(); ++it)
        //     {
        //         if (*it == "{")
        //             bracket_count++;
        //         else
        //             server_info = parseServerInfo(it);
        //         if (*it == "}")
        //             bracket_count--;
        //         if (bracket_count == 0)
        //         {
        //             servConfigs.push_back(server_info);
        //             break ;   
        //         }
        //     }

        }
    // }
    return (servConfigs);
}


