#include "ConfigFile.hpp"

ConfigFile::ConfigFile() : _inputText(""), _fd(-1) {}

ConfigFile::~ConfigFile() {}

void ConfigFile::openFile(const std::string& filename)
{
    if (filename.empty())
        return ;
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0)
        throw (std::runtime_error("Could not open file"));
    this->_fd = fd;
}

void ConfigFile::read_and_append(const std::string& filename)
{
    openFile(filename);
    std::string file_input;
    char buffer[1024];
    while (true)
    {
        ssize_t bytes = read(_fd, buffer, sizeof(buffer));
        if (bytes <= 0)
        {
            if (bytes < 0)
                throw (std::runtime_error("Failed at reading file"));
            break;
        }
        file_input.append(buffer, static_cast<size_t>(bytes));
    }
    close(_fd);

    this->_inputText = file_input;
    // CONFIG FILE PRINTING DEBUG
    // std::cout << this->_inputText << "\n";
}

std::string& ConfigFile::get_str()
{
    return (this->_inputText);
}
