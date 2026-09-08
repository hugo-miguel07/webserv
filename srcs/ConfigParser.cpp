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

size_t ConfigParser::parseBodySizeWeight(const std::string& str)
{
    if (str.empty())
        std::runtime_error("Empty body size");
    size_t i = 0;
    size_t bodySize = 0;
    while (i < str.size() && str[i] >= '0' && str[i] <= '9')
    {
        size_t digit = str[i] - '0';

        if (bodySize > (SIZE_MAX - digit) / 10)
            throw std::runtime_error("Body size overflow");

        bodySize = bodySize * 10 + digit;
        i++;
    }
    if (bodySize == 0)
        std::runtime_error("Invalid body size");
    if (i == str.length())
        return (bodySize);

    size_t multiplier;

    if (str[i] == 'K')
        multiplier = 1000;
    else if (str[i] == 'M')
        multiplier = 1000000;
    else if (str[i] == 'G')
        multiplier = 1000000000;
    else
        throw std::runtime_error("invalid character in clientBodySize");

    if (bodySize > SIZE_MAX / multiplier)
        throw std::runtime_error("Body size overflow");

    bodySize *= multiplier;
    return (bodySize);
}

void ConfigParser::handle_location(const std::vector<std::string> &tokens, size_t &i)
{
    Locations loc;
    size_t    tSize = tokens.size();
    // bool open_brackets = false;
    if (!tokens[i].empty())
    {
        loc.setPath(tokens[i]);
        i++;
    }
    if (i < tSize && tokens[i] == "{")
    {
        // open_brackets = true;
        ++i;
        while (i < tSize && tokens[i] != "}")
        {
            if (tokens[i] == "allow_methods" && i + 1 < tSize) 
            {
                ++i;
                while (tokens[i] != ";" && i < tSize)
                    loc.setAllowedMethod(tokens[i++]);
            }
            if (tokens[i] == "upload_store" && i + 1 < tSize)
            {
                ++i;
                loc.setUploadStore(tokens[i]);
            }
            if (tokens[i] == "cgi_extension" && i + 1 < tSize)
            {
                ++i;
                loc.setCgiExtension(tokens[i]);
            }
            i++;
        }
    }
    std::cout << "=========================\n";
    std::vector<std::string> methods = loc.getAllowedMethods();
    std::cout << "Loc path: " << loc.getPath() << std::endl;
    std::cout << "methods:\n";
    for (size_t j = 0; j < methods.size(); j++)
        std::cout << methods[j] << std::endl;
    if (!loc.getUploadStore().empty())
        std::cout << "upload store: " << loc.getUploadStore() << std::endl;
    if (!loc.getCgiExtension().empty())
        std::cout << "cgi extension: " << loc.getCgiExtension() << std::endl;
    
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
        if ((tokens[i] == "client_max_body_size") && (i + 1 < tokens.size()))
        {
            ++i;
            try
            {
                size_t bodySize = parseBodySizeWeight(tokens[i]);
                serv.setBodySize(bodySize);
            }
            catch (std::runtime_error &e)
            {
                std::cout << e.what() << std::endl; 
            }
        }
        if (tokens[i] == "server_name" && (i + 1 < tokens.size()))
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
            while (tokens[i] != ";" && i < tokens.size())
            {
                serv.setIndex(tokens[i]);
                i++;
            }
        }
        if ((tokens[i] == "error_page") && (i + 1 < tokens.size()))
        {
            ++i;
            if (i + 1 < tokens.size() && tokens[i + 1] != ";")
            {
                serv.setErrorPages(atoi(tokens[i].c_str()), tokens[i + 1]);
                i++;
            }
        }
        if ((tokens[i] == "location") && (i + 1 < tokens.size()))
        {
            ++i;
            handle_location(tokens, i);
        }
        i++;
    }

    // PARSING DEBUG
    // std::cout << "Port: " << serv.getPort() << "\n";
    // std::vector<std::string> names = serv.getServerName();
    // std::vector<std::string> indexes = serv.getIndex();
    // std::map<int, std::string> errorPages = serv.getErrorPages();
    // std::cout << "Server names:\n";
    // for (size_t k = 0; k < names.size(); k++)
    //     std::cout << names[k] << "\n";
    // std::cout << "root: " << serv.getRoot() << "\n";
    // std::cout << "Server indexes:\n";
    // for (size_t k = 0; k < indexes.size(); k++)
    //     std::cout << indexes[k] << "\n";
    // std::cout << "Max Body Size: " << serv.getBodySize() << "\n";
    // std::cout << "Error Pages:\n";
    // for (std::map<int, std::string>::iterator it = errorPages.begin(); it != errorPages.end(); ++it)
    //     std::cout << it->first << " = " << it->second << std::endl;
    
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


