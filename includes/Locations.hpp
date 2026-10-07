#pragma once
#include "test.hpp"

class Locations
{
private:
    std::string _path;
    std::vector<std::string> _allowed_methods;
    std::string _redirect;
    int         _redirect_code;
    std::string _root;
    std::vector<std::string> _indexes;
    bool        _autoindex;
    std::string _upload_store;
    std::vector<std::string> _cgi_extensions;
    std::string _cgi_interpreter;
public:
    Locations();
    ~Locations();

    const std::string&              getPath() const;
    const std::vector<std::string>& getAllowedMethods() const;

    const std::string&  getRedirect() const;
    int                 getRedirectCode() const;

    void                setRedirect(const std::string &target, int code);

    const std::string&                  getRoot() const;
    const std::vector<std::string>&     getIndexes() const;
    bool                                getAutoindex() const;

    void                                setRoot(const std::string &root);
    void                                addIndex(const std::string &index);
    void                                setAutoindex(bool on);

    const std::string&                  getUploadStore() const;
    void                                setUploadStore(const std::string &dir);

    const std::vector<std::string>&     getCgiExtensions() const;
    const std::string&                  getCgiInterpreter() const;
    void                                addCgiExtension(const std::string &ext);
    void                                setCgiInterpreter(const std::string &cmd);

    void setPath(const std::string &path);
    void addAllowedMethod(const std::string &method);
};
