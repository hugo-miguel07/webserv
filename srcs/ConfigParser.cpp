#include "ConfigParser.hpp"

ConfigParser::ConfigParser(): _serv(), _loc() {}

ConfigParser::~ConfigParser() {}
//HELPERS 
bool isspace(char c)
{
    if (c == '\f' || c == '\n' || c == ' ' || c == '\r' || c == '\t' || c == '\v')
        return (true);
    return (false);
}

bool isAllDigit(const std::string &str)
{
    if (str.empty())
        return (false);
    for (size_t i = 0; i < str.size(); i++)
    {
        if (isdigit(str[i]))
            continue;
        else
            return (false);
    }
    return (true);
}

void checkSemiColon(const std::vector<Token>& tokens, size_t i)
{
    if (i + 1 < tokens.size() && tokens[i + 1].content != ";")
        throw std::runtime_error("Missing ';' at line " + to_string(tokens[i].line));
}
//////////////////////////////////


size_t ConfigParser::parseBodySizeWeight(const std::string& str, const std::vector<Token>& tokens, size_t index)
{
    if (str.empty())
        throw std::runtime_error("Empty client_max_body_size  (line: " + to_string(tokens[index].line) + ")");
    size_t i = 0;
    size_t bodySize = 0;
    while (i < str.size() && str[i] >= '0' && str[i] <= '9')
    {
        size_t digit = str[i] - '0';
        if (bodySize > (SIZE_MAX - digit) / 10)
            throw std::runtime_error("Client_max_body_size overflow (line: " + to_string(tokens[index].line) + ")");
        bodySize = bodySize * 10 + digit;
        i++;
    }
    if (bodySize == 0)
        throw std::runtime_error("Invalid client_max_body_size (line: " + to_string(tokens[index].line) + ")");
    if (i == str.length())
        return (bodySize);

    size_t multiplier;

    if (str[i] == 'K')
        multiplier = 1000;
    else if (str[i] == 'M')
        multiplier = 1000000;
    else if (str[i] == 'G')
        multiplier = 1000000000;
    else
        throw std::runtime_error("Invalid character in client_max_body_size directive (line: " + to_string(tokens[index].line) + ")");
    if (bodySize > SIZE_MAX / multiplier)
        throw std::runtime_error("Client_max_body_size overflow (line: " + to_string(tokens[index].line) + ")");
    bodySize *= multiplier;
    return (bodySize);
}

bool   ConfigParser::isLocationDirective(const std::string& token)
{
    return (token == "index"
    || token == "allow_methods"
    || token == "root"
    || token == "autoindex"
    || token == "upload_store"
    || token == "cgi_extension"
    || token == "location"
    || token == "return");
}

bool   ConfigParser::isDirective(const std::string& token)
{
    return (token == "index"
    || token == "server_name"
    || token == "listen"
    || token == "root"
    || token == "client_max_body_size"
    || token == "error_page"
    || token == "location");
}

void ConfigParser::debugLocations()
{
    std::cout << "=========================\n";
    std::vector<std::string> methods = _loc.getAllowedMethods();
    std::vector<std::string> indexes = _loc.getIndexes();
    std::map<int, std::string> returns = _loc.getReturn();
    std::cout << "Loc path: " << _loc.getPath() << std::endl;
    if (!_loc.getRoot().empty())
        std::cout << "Root: " << _loc.getRoot() << std::endl;
    bool ai = _loc.getAutoIndex();
    if (ai)
        std::cout << "AutoIndex: on" << std::endl;
    if (!methods.empty())
    {
        std::cout << "methods: ";
        for (size_t j = 0; j < methods.size(); j++)
            std::cout << methods[j] << " ";
        std::cout << std::endl;
    }
    if (!indexes.empty())
    {
        std::cout << "indexes:\n";
        for (size_t j = 0; j < indexes.size(); j++)
            std::cout << indexes[j] << std::endl;
    }
    for (std::map<int, std::string>::iterator it = returns.begin(); it != returns.end(); ++it)
        std::cout << it->first << " -> " << it->second << std::endl;
    if (!_loc.getUploadStore().empty())
        std::cout << "upload store: " << _loc.getUploadStore() << std::endl;
    if (!_loc.getCgiExtension().empty())
        std::cout << "cgi extension: " << _loc.getCgiExtension() << std::endl;
    if (!_loc.getCgiPath().empty())
        std::cout << "cgi path: " << _loc.getCgiPath() << std::endl;
}

Locations ConfigParser::handleLocation(const std::vector<Token> &tokens, size_t &i)
{
    _loc.erase();
    size_t    tSize = tokens.size();
    size_t bracket_count = 0;
    if (!tokens[i].content.empty() && tokens[i].content != "{")
    {
        _loc.setPath(tokens[i].content);
        i++;
    }
    else
        throw std::runtime_error("Location path is missing (line: " + to_string(tokens[i].line) + ")");
    if (i < tSize && tokens[i].content == "{")
    {
        bracket_count++;
        ++i;
        while (i < tSize && tokens[i].content != "}")
        {
            DirectiveType directive = getLocationDirectiveType(tokens[i].content);

            switch (directive)
            {
                case DIRECTIVE_ALLOW_METHODS:
                    handleAllowMethodsDirective(tokens, i);
                    break;
                case DIRECTIVE_RETURN:
                    handleReturnDirective(tokens, i);
                    break;
                case DIRECTIVE_ROOT:
                    handleRootDirective(tokens, i, true);
                    break;
                case DIRECTIVE_INDEX:
                    handleIndexDirective(tokens, i, true);
                    break;
                case DIRECTIVE_AUTOINDEX:
                    handleAutoindexDirective(tokens, i);
                    break;
                case DIRECTIVE_UPLOAD_STORE:
                    handleUploadStoreDirective(tokens, i);
                    break;
                case DIRECTIVE_CGI_EXTENSION:
                    handleCgiExtensionDirective(tokens, i);
                    break;
                case DIRECTIVE_CGI_PATH:
                    handleCgiPathDirective(tokens, i);
                    break;
                default:
                    if (directive == DIRECTIVE_UNKNOWN && tokens[i].content == ";")
                    {
                        i++;
                        continue;
                    }
                    else if (directive == DIRECTIVE_UNKNOWN && tokens[i].content == "}")
                    {
                        bracket_count--;
                        if (bracket_count == 0)
                            break;
                        else
                            throw std::runtime_error("Unexpected token '"+ tokens[i].content+ "' (line: " + to_string(tokens[i].line) + ")");
                    }
                    else
                        throw std::runtime_error("Directive not allowed in location (line: " + to_string(tokens[i].line) + ")");
            }
            i++;
        }
    }
    else
        throw std::runtime_error("Unexpected token '" + tokens[i].content + "' expecting -> '}' (Line " + to_string(tokens[i].line) + ")");
    // debugLocations();
    return (_loc);
}

DirectiveType   ConfigParser::getDirectiveType(const std::string& token)
{
    DirectiveType type;
    if (token == "listen")
        type = DIRECTIVE_LISTEN;
    else if (token == "root")
        type = DIRECTIVE_ROOT;
    else if (token == "server_name")
        type = DIRECTIVE_SERVER_NAME;
    else if (token == "index")
        type = DIRECTIVE_INDEX;
    else if (token == "client_max_body_size")
        type = DIRECTIVE_CLIENT_MAX_BODY_SIZE;
    else if (token == "error_page")
        type = DIRECTIVE_ERROR_PAGE;
    else if (token == "location")
        type = DIRECTIVE_LOCATION;
    else
        type = DIRECTIVE_UNKNOWN;
    return (type);
}

DirectiveType   ConfigParser::getLocationDirectiveType(const std::string& token)
{
    DirectiveType type;

    if (token == "root")
        type = DIRECTIVE_ROOT;
    else if (token == "index")
        type = DIRECTIVE_INDEX;
    else if (token == "allow_methods")
        type = DIRECTIVE_ALLOW_METHODS;
    else if (token == "return")
        type = DIRECTIVE_RETURN;
    else if (token == "autoindex")
        type = DIRECTIVE_AUTOINDEX;
    else if (token == "upload_store")
        type = DIRECTIVE_UPLOAD_STORE;
    else if (token == "cgi_extension")
        type = DIRECTIVE_CGI_EXTENSION;
    else if (token == "cgi_path")
        type = DIRECTIVE_CGI_PATH;
    else
        type = DIRECTIVE_UNKNOWN;
    return (type);
}

void ConfigParser::debugServers()
{
    std::cout << "Port: " << _serv.getPort() << "\n";
    std::vector<std::string> names = _serv.getServerName();
    std::vector<std::string> indexes = _serv.getIndex();
    std::map<int, std::string> errorPages = _serv.getErrorPages();
    std::cout << "Server names:\n";
    for (size_t k = 0; k < names.size(); k++)
        std::cout << names[k] << "\n";
    std::cout << "root: " << _serv.getRoot() << "\n";
    std::cout << "Server indexes:\n";
    for (size_t k = 0; k < indexes.size(); k++)
        std::cout << indexes[k] << "\n";
    std::cout << "Max Body Size: " << _serv.getBodySize() << "\n";
    std::cout << "Error Pages:\n";
    for (std::map<int, std::string>::iterator it = errorPages.begin(); it != errorPages.end(); ++it)
        std::cout << it->first << " = " << it->second << std::endl;
}


ServerConfig ConfigParser::parseServerInfo(const std::vector<Token>& tokens, size_t &i)
{
    _serv = ServerConfig();
    int bracket_count = 0;
    size_t tSize = tokens.size();
    size_t server_line = tokens[i].line;
    ++i;
    if (i >= tSize || tokens[i].content != "{")
        throw std::runtime_error("Expected '{' after server (line: " + to_string(server_line) + ")");
    while (i < tSize)
    {
        if (tokens[i].content == "{")
        {
            bracket_count++;
            i++;
            continue;
        }
        if (tokens[i].content == "}")
        {
            bracket_count--;
            i++;
            if (bracket_count == 0)
                break;
            continue;
        }

        if (isDirective(tokens[i].content))
        {
            DirectiveType directive = getDirectiveType(tokens[i].content);
            switch (directive)
            {
                case DIRECTIVE_LISTEN:
                    handleListenDirective(tokens, i);
                    break;
                case DIRECTIVE_ROOT:
                    handleRootDirective(tokens, i, false);
                    break;
                case DIRECTIVE_INDEX:
                    handleIndexDirective(tokens, i, false);
                    break;
                case DIRECTIVE_SERVER_NAME:
                    handleServerNameDirective(tokens, i);
                    break;
                case DIRECTIVE_CLIENT_MAX_BODY_SIZE:
                    handleClientMaxBodySizeDirective(tokens, i);
                    break;
                case DIRECTIVE_ERROR_PAGE:
                    handleErrorPageDirective(tokens, i);
                    break;
                case DIRECTIVE_LOCATION:
                    handleLocationDirective(tokens, i);
                    break;
                default:
                    throw std::runtime_error("Directive not allowed in server (line: " + to_string(tokens[i].line) + ")");
            }
        }
        i++;
    }
    if (bracket_count != 0)
    {
        size_t error_line = tokens.empty() ? 1 : tokens.back().line;
        throw std::runtime_error("Missing '}' for server (line: " + to_string(error_line) + ")");
    }
    // debugServers();    
    return (_serv);
}

std::vector<ServerConfig> ConfigParser::parseServers(const std::vector<Token>& tokens)
{
    std::vector<ServerConfig> servConfigs;
    size_t i = 0;
    while (i < tokens.size())
    {
        if (tokens[i].content != "server")
            throw std::runtime_error("Expected 'server' block (line: " + to_string(tokens[i].line) + ")");
        servConfigs.push_back(parseServerInfo(tokens, i));
    }
    if (servConfigs.empty())
        throw std::runtime_error("Expected 'server' block");
    return (servConfigs);
}

void ConfigParser::handleListenDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Listen directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    i++;
    int port = atoi(tokens[i].content.c_str());
    if (!isAllDigit(tokens[i].content) || port < 1 || port > 65535)
        throw std::runtime_error("Invalid port (line: " + to_string(tokens[i].line) + ")");
    else
        _serv.setPort(port);
    checkSemiColon(tokens, i);
}
void ConfigParser::handleRootDirective(const std::vector<Token>& tokens, size_t& i, bool is_location)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Root directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    if (is_location)
        _loc.setRoot(tokens[i].content);
    else
        _serv.setRoot(tokens[i].content);
    checkSemiColon(tokens, i);
}
void ConfigParser::handleServerNameDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens [i + 1].content == ";")
        throw std::runtime_error("Server_name directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    size_t prev_line = tokens[i].line;
    while (tokens[i].content != ";" && i < tSize)
    {
        if (tokens[i].content == "}" || isDirective(tokens[i].content))
            throw (std::runtime_error("Missing ';' on server_name directive (line: " + to_string(prev_line) + ")"));
        _serv.setServerName(tokens[i].content);
        prev_line = tokens[i].line;
        i++;
    }
}
void ConfigParser::handleIndexDirective(const std::vector<Token>& tokens, size_t& i, bool is_location)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Index directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    size_t prev_line = tokens[i].line;
    while ( i < tSize && tokens[i].content != ";")
    {
        if (is_location)
        {
            if (tokens[i].content == "}" || isLocationDirective(tokens[i].content))
                throw (std::runtime_error("Missing ';' on index directive (line: " + to_string(prev_line) + ")"));
            _loc.setIndex(tokens[i].content);
        }
        else
        {
            if (tokens[i].content == "}" || isDirective(tokens[i].content))
                throw (std::runtime_error("Missing ';' on index directive (line: " + to_string(prev_line) + ")"));
            _serv.setIndex(tokens[i].content);
        }
        prev_line = tokens[i].line;
        i++;
    }
}
void ConfigParser::handleClientMaxBodySizeDirective(const std::vector<Token>& tokens, size_t &i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Client_max_body_size directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    size_t bodySize = parseBodySizeWeight(tokens[i].content, tokens, i);
    _serv.setBodySize(bodySize);
    checkSemiColon(tokens, i);
}

bool ConfigParser::isHttpStatusCodeSuported(const std::string& token)
{
    if (token == "400")
        return (true);
    else if (token == "403")
        return (true);
    else if (token == "404")
        return (true);
    else if (token == "405")
        return (true);
    else if (token == "413")
        return (true);
    else if (token == "500")
        return (true);
    else if (token == "501")
        return (true);
    else
        return (false);
}

void ConfigParser::handleErrorPageDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Error_page directive expects arguments (line: " + to_string(tokens[i].line) + ")");
    ++i;
    if (i + 1 < tokens.size() && tokens[i + 1].content != ";")
    {
        if (!isAllDigit(tokens[i].content))
            throw std::runtime_error("First argument of error_page directive must be a HTTP status code (line: " + to_string(tokens[i].line) + ")");
        if (!isHttpStatusCodeSuported(tokens[i].content))
            throw std::runtime_error("HTTP status code not supported on error_page directive (line: " + to_string(tokens[i].line) + ")");
        int res = atoi(tokens[i].content.c_str());
        if (res == 0)
            throw std::runtime_error("Invalid HTTP status code on error_page directive (line: " + to_string(tokens[i].line) + ")");
        _serv.setErrorPages(res, tokens[i + 1].content);
        i++;
    }
    else
        throw std::runtime_error("Error_page directive expects two arguments (line: " + to_string(tokens[i].line) + ")");
    checkSemiColon(tokens, i);
}
void ConfigParser::handleLocationDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Location directive expects a path (line: " + to_string(tokens[i].line) + ")");
    ++i;
    _serv.addLocation(handleLocation(tokens, i));
}
void ConfigParser::handleAllowMethodsDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    size_t directive_line = tokens[i].line;
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Allow_methods directive expects an argument (line: " + to_string(directive_line) + ")");
    ++i;
    while (i < tSize && tokens[i].content != ";")
    {
        if (tokens[i].content == "}" || isLocationDirective(tokens[i].content))
            throw std::runtime_error("Missing ';' on allow_methods directive (line: " + to_string(directive_line) + ")");
        _loc.setAllowedMethod(tokens[i].content);
        i++;
    }
    if (i == tSize)
        throw std::runtime_error("Missing ';' on allow_methods directive (line: " + to_string(directive_line) + ")");
}
void ConfigParser::handleUploadStoreDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Upload_store directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    _loc.setUploadStore(tokens[i].content);
    checkSemiColon(tokens, i);
}
void ConfigParser::handleCgiExtensionDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Cgi_extension directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    _loc.setCgiExtension(tokens[i].content);
    checkSemiColon(tokens, i);
}
void ConfigParser::handleCgiPathDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Cgi_path directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    _loc.setCgiPath(tokens[i].content);
    checkSemiColon(tokens, i);
}
void ConfigParser::handleAutoindexDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Autoindex directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    ++i;
    if (tokens[i].content == "on" )
        _loc.setAutoIndex(true);
    else
        _loc.setAutoIndex(false);
    checkSemiColon(tokens, i);
}
void ConfigParser::handleReturnDirective(const std::vector<Token>& tokens, size_t& i)
{
    size_t tSize = tokens.size();
    if (i + 1 >= tSize || tokens[i + 1].content == ";")
        throw std::runtime_error("Return directive expects an argument (line: " + to_string(tokens[i].line) + ")");
    i++;
    int nb;
    if (isAllDigit(tokens[i].content))
    {
        nb = atoi(tokens[i].content.c_str());
        if (!nb)
            throw std::runtime_error("Invalid HTTP status code on return directive (line: " + to_string(tokens[i].line) + ")");
    }
    else 
        throw std::runtime_error("Invalid HTTP status code on return directive (line: " + to_string(tokens[i].line) + ")");
    if (i + 1 < tSize && tokens[i + 1].content != ";")
    {
        i++;
        _loc.setReturn(nb, tokens[i].content);
        checkSemiColon(tokens, i);
    }
    else
        throw std::runtime_error("Return directive expects a second argument (line: " + to_string(tokens[i].line) + ")");

}
