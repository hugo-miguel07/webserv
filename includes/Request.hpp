#ifndef REQUEST_HPP
#define REQUEST_HPP
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "Client.hpp"

class Request: public Client {
    private:
        std::string                         _TransferMethod;
        int                                 _nextRequestBytes;
        size_t                              _contentLen;
        bool                                _finisedHeader;
        bool                                _hasContent;
        bool                                _hasTransferEncoding;
        std::string                         _body;
        std::map<std::string, std::string>  _requestLine;
        std::map<std::string, std::string>  _headers;
    public:
        Request();
        ~Request();
        bool    getHasContentStatus();
        bool    getTransferEncodingStatus();

        /*Checkers*/
        bool    checkHeader(const std::string requestBuffer);
        void    readingBody(const std::string requestBuffer);
        bool    checkingBody_framing(const std::string requestBuffer);
        bool    checkingBody_chuncked(const std::string requestBuffer);

        /*Parsing*/
        void    start_parsing(const std::string requestBuffer);
        void    QueryParsing(std::string requestTarget);
        void    RequestLineParsing(const std::string requestBuffer);
        void    MethodParsing(std::string method);
        void    HeadersToMap(const std::string requestBuffer);
        void    parseBody(const std::string& buffer);
        void    HostParsing(std::string value);
        void    parseChunkedBody(const std::string& buffer, size_t pos);
};

std::string trim(const std::string &str);


#endif