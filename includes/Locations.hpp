#pragma once
#include "test.hpp"
class Locations
{
private:
    std::string _path; 
    // std::string _root; 
    // std::string _index;
    // bool         _autoindex;
    //return map prob

    std::string _uploadStore; 
    std::string _cgiExtension; 
    std::vector<std::string> _allowed_methods;
public:
    Locations();
    ~Locations();

    std::string getPath() const;
    std::string getUploadStore() const;
    std::string getCgiExtension() const;
    std::vector<std::string> getAllowedMethods() const;

    void setPath(const std::string& path);
    void setUploadStore(const std::string& upload_path);
    void setCgiExtension(const std::string& cgiExtension);
    void setAllowedMethod(const std::string& method);
};


