#pragma once
#include "test.hpp"
class Locations
{
private:
    std::string _path; 
    std::string _root; 
    std::string _uploadStore; 
    std::string _cgiExtension;
    std::string _cgiPath;
    bool         _autoindex;
    std::vector<std::string> _allowed_methods;
    std::vector<std::string> _indexes;
    std::map<int, std::string> _return;

public:
    Locations();
    ~Locations();

    void erase();

    std::string getPath() const;
    std::string getRoot() const;
    std::string getUploadStore() const;
    std::string getCgiExtension() const;
    std::string getCgiPath() const;
    bool        getAutoIndex() const;
    std::vector<std::string> getAllowedMethods() const;
    std::vector<std::string> getIndexes() const;
    std::map<int, std::string> getReturn() const;

    void setPath(const std::string& path);
    void setRoot(const std::string& root);
    void setUploadStore(const std::string& upload_path);
    void setCgiExtension(const std::string& cgiExtension);
    void setCgiPath(const std::string& cgiPath);
    void setAutoIndex(const bool &flag);
    void setAllowedMethod(const std::string& method);
    void setIndex(const std::string& index);
    void setReturn(const int &returnCode, const std::string& returnTarget);
};


