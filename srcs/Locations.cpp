#include "Locations.hpp"

Locations::Locations() : _path(""), _uploadStore(""), _cgiExtension(""), _allowed_methods() {}

Locations::~Locations() {}

std::string Locations::getPath() const 
{
    return (_path);
}

std::string Locations::getUploadStore() const 
{
    return (_uploadStore);
}

std::string Locations::getCgiExtension() const
{
    return (_cgiExtension);
}

std::vector<std::string> Locations::getAllowedMethods() const
{
    return (_allowed_methods);
}

void Locations::setPath(const std::string& path)
{
    if (path.empty())
        return ;
    _path = path;
}

void Locations::setUploadStore(const std::string& uploadPath)
{
    if (uploadPath.empty())
        return ;
    _uploadStore = uploadPath;
}

void Locations::setCgiExtension(const std::string& cgiExtension)
{
    if (cgiExtension.empty())
        return ;
    _cgiExtension = cgiExtension;
}

void Locations::setAllowedMethod(const std::string& method)
{
    if (method.empty())
        return ;
    _allowed_methods.push_back(method);

}
