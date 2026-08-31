#ifndef REQUEST_HPP
#define REQUEST_HPP
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "Client.hpp"

class Request: public Client {
    private:
        std::string _TransferMethod;
        int     _nextRequestBytes;
        size_t  _contentLen;
        bool    _finisedHeader;
        bool    _hasContent;
        bool    _hasTransferEncoding;
    public:
        Request();
        ~Request();
        bool    getHasContentStatus();
        bool    getTransferEncodingStatus();
        bool    checkHeader(const std::string requestBuffer);
        void    readingBody(const std::string requestBuffer);
        bool    checkingBody_framing(const std::string requestBuffer);
        bool    checkingBody_chuncked(const std::string requestBuffer);
};


#endif