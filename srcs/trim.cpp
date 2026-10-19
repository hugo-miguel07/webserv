#include "../includes/Request.hpp"

std::string trim(const std::string &str)
{
    size_t start = str.find_first_not_of(" \t\r");
    size_t end = str.find_last_not_of(" \t\r");

    if (start == std::string::npos)
        return "";

    return str.substr(start, end - start + 1);
}