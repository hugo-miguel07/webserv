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
    void            openFile (const std::string& filename);
    void            read_and_append(const std::string& filename);
    std::string&    get_str(); 
};


