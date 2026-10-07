#include "Locations.hpp"

Locations::Locations()
    : _path(""),
      _allowed_methods(),
      _redirect(""),
      _redirect_code(0),
      _root(""),
      _indexes(),
      _autoindex(false),
      _upload_store(""),
      _cgi_extensions(),
      _cgi_interpreter("")
{
}

Locations::~Locations()
{
}

const std::string& Locations::getPath() const
{
    return (_path);
}

const std::vector<std::string>& Locations::getAllowedMethods() const
{
    return (_allowed_methods);
}

void Locations::setPath(const std::string &path)
{
    _path = path;
}

void Locations::addAllowedMethod(const std::string &method)
{
    _allowed_methods.push_back(method);
}


const std::string& Locations::getRedirect() const
{
    return (_redirect);
}

int Locations::getRedirectCode() const
{
    return (_redirect_code);
}

void Locations::setRedirect(const std::string &target, int code)
{
    _redirect = target;
    _redirect_code = code;
}


const std::string& Locations::getRoot() const
{
    return (_root);
}

const std::vector<std::string>& Locations::getIndexes() const
{
    return (_indexes);
}

bool Locations::getAutoindex() const
{
    return (_autoindex);
}

void Locations::setRoot(const std::string &root)
{
    _root = root;
}

void Locations::addIndex(const std::string &index)
{
    _indexes.push_back(index);
}

void Locations::setAutoindex(bool on)
{
    _autoindex = on;
}


const std::string& Locations::getUploadStore() const
{
    return (_upload_store);
}

void Locations::setUploadStore(const std::string &dir)
{
    _upload_store = dir;
}


const std::vector<std::string>& Locations::getCgiExtensions() const
{
    return (_cgi_extensions);
}

void Locations::addCgiExtension(const std::string &ext)
{
    _cgi_extensions.push_back(ext);
}

const std::string& Locations::getCgiInterpreter() const
{
    return (_cgi_interpreter);
}

void Locations::setCgiInterpreter(const std::string &cmd)
{
    _cgi_interpreter = cmd;
}
