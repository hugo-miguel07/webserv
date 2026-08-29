#pragma once    
#include "test.hpp"

class ConfigFile
{
private:
    std::string _inputText;
    int _fd;
public:
                    ConfigFile();
                    ~ConfigFile();
    bool            openFile (const std::string& filename);
    bool            read_and_append(const std::string& filename);
    std::string&    get_str(); 
};


