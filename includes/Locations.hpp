#pragma once
#include "test.hpp"
class Locations
{
private:
    std::string _path;
    std::vector<std::string> _allowed_methods;
public:
    Locations(/* args */);
    ~Locations();

    std::vector<std::string> getAllowedMethods();
};


