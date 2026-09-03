#include "ConfigFile.hpp"

ConfigFile::ConfigFile() : _inputText(""), _fd(-1) {}

ConfigFile::~ConfigFile() {}

bool ConfigFile::openFile(const std::string& filename)
{
    if (filename.empty())
        return (false);

    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0)
        return (false);
    this->_fd = fd;
    return (true);
}

bool ConfigFile::read_and_append(const std::string& filename)
{
    if (!ConfigFile::openFile(filename))
        return (false);

    std::string file_input;
    char buffer[1024];
    while (true)
    {
        ssize_t bytes = read(_fd, buffer, sizeof(buffer));
        if (bytes <= 0)
        {
            if (bytes < 0)
                std::cout << "Error\nFailed to read file\n";
            break;
        }
        file_input.append(buffer, static_cast<size_t>(bytes));
    }
    close(_fd);

    this->_inputText = file_input;
    // CONFIG FILE PRINTING DEBUG
    // std::cout << this->_inputText << "\n";
    return (true);
}

std::string& ConfigFile::get_str()
{
    return (this->_inputText);
}
