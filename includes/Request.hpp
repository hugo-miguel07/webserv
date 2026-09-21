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

#include "ServerConfig.hpp"
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
        ServerConfig                        _server;
        std::string                         _serverName;
        std::map<std::string, std::string>  _requestLine;
        std::map<std::string, std::string>  _headers;

        std::vector<ServerConfig>   _parsedServers;
        
    public:
        Request();
        Request(std::vector<ServerConfig> parsed_server);
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

RequestLineParsing: estruturalmente OK.

QueryParsing: quase OK; query vazia não precisa necessariamente de erro.

HeadersToMap: funciona, mas precisas de normalizar nomes dos headers e tratar duplicados.

HostParsing: estruturalmente correto; só move o parsing da porta para fora dos loops.
seleção de ServerConfig: correta.

MethodParsing: falta escolher o longest matching Location.
readingBody: funcional, mas estás a fazer parsing duas vezes; usa _headers.

Content-Length + Transfer-Encoding: falta rejeitar conflito.
checkingBody_framing: ideia correta.

checkingBody_chuncked: precisa de validação mais forte do chunk size.

parseBody: correto se só for chamado quando a request estiver completa.



#endif