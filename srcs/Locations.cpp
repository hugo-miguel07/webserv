#include "Locations.hpp"

Locations::Locations() : _path(""), _root(""), _uploadStore(""), _cgiExtension(""), _autoindex(false), _allowed_methods(), _indexes(), _return() {}

Locations::~Locations() {}

std::string Locations::getPath() const 
{
    return (_path);
}

std::string Locations::getRoot() const 
{
    return (_root);
}

std::string Locations::getUploadStore() const 
{
    return (_uploadStore);
}

std::string Locations::getCgiExtension() const
{
    return (_cgiExtension);
}

std::string Locations::getCgiPath() const
{
    return (_cgiPath);
}

bool Locations::getAutoIndex() const
{
    return (_autoindex);
}

std::vector<std::string> Locations::getAllowedMethods() const
{
    return (_allowed_methods);
}

std::vector<std::string> Locations::getIndexes() const
{
    return (_indexes);
}

std::map<int, std::string> Locations::getReturn() const
{
    return (_return);
}

void Locations::setPath(const std::string& path)
{
    if (path.empty())
        return ;
    _path = path;
}

void Locations::setRoot(const std::string& root)
{
    if (root.empty())
        return ;
    _root = root;
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

void Locations::setCgiPath(const std::string& cgiPath)
{
    if (cgiPath.empty())
        return ;
    _cgiPath = cgiPath;
}

void Locations::setAutoIndex(const bool &flag)
{
    _autoindex = flag;
}

void Locations::setAllowedMethod(const std::string& method)
{
    if (method.empty())
        return ;
    _allowed_methods.push_back(method);
}

void Locations::setIndex(const std::string& index)
{
    if (index.empty())
        return ;
    _indexes.push_back(index);
}

void Locations::setReturn(const int &returnCode, const std::string& returnTarget)
{
    _return.insert(std::make_pair(returnCode, returnTarget));
}

void Locations::erase()
{
    _path = "";
    _root = "";
    _uploadStore = "";
    _cgiExtension = "";
    _cgiPath = "";
    _autoindex = false;
    _indexes.clear();
    _allowed_methods.clear();
    _return.clear();
}

