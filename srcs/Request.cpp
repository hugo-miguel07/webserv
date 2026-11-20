#include "Request.hpp"
#include <cctype>

Request::Request()
    : Client(),
      _TransferMethod(""),
      _nextRequestBytes(0),
      _contentLen(0),
      _finisedHeader(false),
      _hasContent(false),
      _hasTransferEncoding(false),
      _headerSearchOffset(0),
      _bodyStart(0),
      _chunkState(CHUNK_SIZE),
      _chunkPosition(0),
      _chunkSize(0),
      _chunkTotal(0)
{
}

Request::Request(std::vector<ServerConfig> parsedServers)
    : Client(),
      _TransferMethod(""),
      _nextRequestBytes(0),
      _contentLen(0),
      _finisedHeader(false),
      _hasContent(false),
      _hasTransferEncoding(false),
      _headerSearchOffset(0),
      _bodyStart(0),
      _chunkState(CHUNK_SIZE),
      _chunkPosition(0),
      _chunkSize(0),
      _chunkTotal(0),
      _parsedServers(parsedServers)
{
}

Request::~Request()
{
}

bool Request::getHasContentStatus()
{
    return _hasContent;
}

bool Request::getTransferEncodingStatus()
{
    return _hasTransferEncoding;
}

std::string Request::getHeader(const std::string &name) const
{
    std::string requested_name = name;
    for (size_t i = 0; i < requested_name.size(); ++i)
    {
        const unsigned char character = static_cast<unsigned char>(requested_name[i]);
        requested_name[i] = static_cast<char>(std::tolower(character));
    }

    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it)
    {
        std::string stored_name = it->first;
        for (size_t i = 0; i < stored_name.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(stored_name[i]);
            stored_name[i] = static_cast<char>(std::tolower(character));
        }

        if (stored_name == requested_name)
        {
            return it->second;
        }
    }

    return "";
}

size_t Request::getContentLength() const
{
    const std::string length_header = getHeader("content-length");
    if (length_header.empty())
    {
        return _contentLen;
    }

    const bool contains_non_digit =
        length_header.find_first_not_of("0123456789") != std::string::npos;
    if (contains_non_digit)
    {
        throw std::runtime_error("Invalid content length");
    }

    size_t content_length = 0;
    std::istringstream length_stream(length_header);
    length_stream >> content_length;

    if (length_stream.fail())
    {
        throw std::runtime_error("Invalid content length");
    }

    return content_length;
}

const ServerConfig &Request::getServerConfig() const
{
    return _server;
}

bool Request::checkHeader(const std::string& buffer)
{
    if (_finisedHeader)
        return true;
    _hasContent = false;
    _hasTransferEncoding = false;
    headers.clear();

    const size_t header_end = buffer.find("\r\n\r\n", _headerSearchOffset);
    if (header_end == std::string::npos)
    {
        _headerSearchOffset = buffer.size() > 3 ? buffer.size() - 3 : 0;
        return false;
    }
    _bodyStart = header_end + 4;

    std::string header_text = buffer.substr(0, header_end);
    for (size_t i = 0; i < header_text.size(); ++i)
    {
        const unsigned char character = static_cast<unsigned char>(header_text[i]);
        header_text[i] = static_cast<char>(std::tolower(character));
    }

    _hasContent = header_text.find("\r\ncontent-length:") != std::string::npos;
    _hasTransferEncoding = header_text.find("\r\ntransfer-encoding:") != std::string::npos;

    if (_hasContent && _hasTransferEncoding)
    {
        throw std::runtime_error("Ambiguous request framing");
    }

    _finisedHeader = true;
    return true;
}

void Request::readingBody(const std::string& buffer)
{
    if (headers.empty())
    {
        HeadersToMap(buffer);
    }

    _contentLen = 0;
    _TransferMethod.clear();

    if (_hasContent)
    {
        const std::string length_header = getHeader("content-length");
        if (length_header.empty())
        {
            throw std::runtime_error("Invalid content length");
        }

        for (size_t i = 0; i < length_header.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(length_header[i]);
            if (!std::isdigit(character))
            {
                throw std::runtime_error("Invalid content length");
            }
        }

        std::istringstream length_stream(length_header);
        length_stream >> _contentLen;
        if (length_stream.fail())
        {
            throw std::runtime_error("Invalid content length");
        }
    }
    else if (_hasTransferEncoding)
    {
        std::string transfer_method = getHeader("transfer-encoding");
        for (size_t i = 0; i < transfer_method.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(transfer_method[i]);
            transfer_method[i] = static_cast<char>(std::tolower(character));
        }

        if (transfer_method != "chunked")
        {
            throw std::runtime_error("Invalid transfer encoding");
        }

        _TransferMethod = transfer_method;
    }
}

bool Request::checkingBody_framing(const std::string& buffer)
{
    const std::string header_separator = "\r\n\r\n";
    if (_bodyStart == 0)
    {
        const size_t header_end = buffer.find(header_separator);
        if (header_end == std::string::npos)
            return false;
        _bodyStart = header_end + header_separator.size();
    }
    const size_t body_start = _bodyStart;
    const size_t received_body_bytes = buffer.size() - body_start;
    if (received_body_bytes < _contentLen)
    {
        return false;
    }

    _nextRequestBytes = received_body_bytes - _contentLen;
    return true;
}

bool Request::checkingBody_chuncked(const std::string& buffer, size_t maxBodySize)
{
    const std::string header_separator = "\r\n\r\n";
    const std::string line_ending = "\r\n";
    const std::string hexadecimal_digits = "0123456789abcdefABCDEF";
    if (_chunkPosition == 0)
    {
        const size_t header_end = buffer.find(header_separator);
        if (header_end == std::string::npos)
            return false;
        _chunkPosition = header_end + header_separator.size();
    }
    if (_chunkTotal > maxBodySize)
        throw 413;

    while (_chunkState == CHUNK_SIZE || _chunkState == CHUNK_DATA)
    {
        if (_chunkState == CHUNK_SIZE)
        {
            const size_t size_line_end = buffer.find(line_ending, _chunkPosition);
            if (size_line_end == std::string::npos)
            {
                return false;
            }

            std::string size_text = buffer.substr(_chunkPosition, size_line_end - _chunkPosition);
            const size_t extension_start = size_text.find(';');
            if (extension_start != std::string::npos)
            {
                size_text = size_text.substr(0, extension_start);
            }

            if (size_text.empty())
            {
                throw std::runtime_error("Invalid chunk size");
            }

            const bool contains_non_hex_digit =
                size_text.find_first_not_of(hexadecimal_digits) != std::string::npos;
            if (contains_non_hex_digit)
            {
                throw std::runtime_error("Invalid chunk size");
            }

            size_t chunk_size = 0;
            std::istringstream size_stream(size_text);
            size_stream >> std::hex >> chunk_size;
            if (size_stream.fail())
            {
                throw std::runtime_error("Invalid chunk size");
            }

            // Check the announced payload before waiting for the chunk data.
            if (chunk_size > maxBodySize - _chunkTotal)
                throw 413;
            _chunkTotal += chunk_size;

            _chunkPosition = size_line_end + line_ending.size();
            _chunkSize = chunk_size;
            if (chunk_size == 0)
            {
                _chunkState = CHUNK_TRAILERS;
                break;
            }
            _chunkState = CHUNK_DATA;
        }

        const size_t available_bytes = buffer.size() - _chunkPosition;
        if (_chunkSize > available_bytes)
        {
            return false;
        }

        const size_t data_end = _chunkPosition + _chunkSize;
        const size_t bytes_after_data = buffer.size() - data_end;
        if (bytes_after_data < line_ending.size())
        {
            return false;
        }

        if (buffer.compare(data_end, line_ending.size(), line_ending) != 0)
        {
            throw std::runtime_error("Invalid chunk ending");
        }

        _chunkPosition = data_end + line_ending.size();
        _chunkState = CHUNK_SIZE;
    }

    const std::string valid_name_characters =
        "!#$%&'*+-.^_`|~0123456789abcdefghijklmnopqrstuvwxyz";
    while (_chunkState == CHUNK_TRAILERS)
    {
        const size_t trailer_end = buffer.find(line_ending, _chunkPosition);
        if (trailer_end == std::string::npos)
        {
            return false;
        }
        if (trailer_end == _chunkPosition)
        {
            _chunkState = CHUNK_DONE;
            return true;
        }

        const std::string trailer_line =
            buffer.substr(_chunkPosition, trailer_end - _chunkPosition);
        const size_t colon = trailer_line.find(':');
        if (colon == std::string::npos || colon == 0)
        {
            throw std::runtime_error("Invalid trailer");
        }

        std::string trailer_name = trailer_line.substr(0, colon);
        for (size_t i = 0; i < trailer_name.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(trailer_name[i]);
            trailer_name[i] = static_cast<char>(std::tolower(character));
        }

        const bool invalid_trailer_name =
            trailer_name.find_first_not_of(valid_name_characters) != std::string::npos;
        if (invalid_trailer_name)
        {
            throw std::runtime_error("Invalid trailer name");
        }

        const bool changes_request_framing = trailer_name == "content-length"
            || trailer_name == "transfer-encoding" || trailer_name == "host";
        if (changes_request_framing)
        {
            throw std::runtime_error("Invalid framing trailer");
        }

        const std::string trailer_value = trim(trailer_line.substr(colon + 1));
        for (size_t i = 0; i < trailer_value.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(trailer_value[i]);
            const bool control_character = character < 32 || character == 127;
            if (control_character && character != '\t')
            {
                throw std::runtime_error("Invalid trailer value");
            }
        }

        _chunkPosition = trailer_end + line_ending.size();
    }
    return _chunkState == CHUNK_DONE;
}

void Request::start_parsing(const std::string& requestbuffer)
{
    _requestLine.clear();

    RequestLineParsing(requestbuffer);
    HeadersToMap(requestbuffer);

    method = _requestLine["Method"];
    path = _requestLine["Path"];
    query = _requestLine["Query"];
    version = _requestLine["Version"];

    MethodParsing(method);
    readingBody(requestbuffer);
    parseBody(requestbuffer);
}

void Request::RequestLineParsing(const std::string& request)
{
    const size_t request_line_end = request.find("\r\n");
    if (request_line_end == std::string::npos)
    {
        throw std::runtime_error("Invalid request line ending");
    }

    const std::string request_line = request.substr(0, request_line_end);
    if (request_line.empty())
    {
        throw std::runtime_error("Empty request line");
    }

    const size_t method_end = request_line.find(' ');
    if (method_end == std::string::npos)
    {
        throw std::runtime_error("Missing request method separator");
    }

    const std::string parsed_method = request_line.substr(0, method_end);
    const bool supported_method =
        parsed_method == "GET" || parsed_method == "POST"
        || parsed_method == "DELETE" || parsed_method == "HEAD";
    if (!supported_method)
    {
        throw std::runtime_error("Invalid method");
    }
    _requestLine.insert(std::make_pair("Method", parsed_method));

    const size_t target_start = method_end + 1;
    const size_t target_end = request_line.find(' ', target_start);
    if (target_end == std::string::npos)
    {
        throw std::runtime_error("Missing request target separator");
    }

    const std::string request_target = request_line.substr(target_start, target_end - target_start);
    if (request_target.empty())
    {
        throw std::runtime_error("Empty request target");
    }
    if (request_target[0] != '/')
    {
        throw std::runtime_error("Invalid request target");
    }

    for (size_t i = 0; i < request_target.size(); ++i)
    {
        const unsigned char character = static_cast<unsigned char>(request_target[i]);
        if (character <= 32 || character == 127)
        {
            throw std::runtime_error("Invalid request target");
        }
    }

    const std::string parsed_version = request_line.substr(target_end + 1);
    if (parsed_version != "HTTP/1.1")
    {
        throw std::runtime_error("Invalid HTTP version");
    }
    _requestLine.insert(std::make_pair("Version", parsed_version));

    if (request_target.find('?') != std::string::npos)
    {
        QueryParsing(request_target);
    }
    else
    {
        _requestLine.insert(std::make_pair("Path", request_target));
    }

    method = _requestLine["Method"];
    path = _requestLine["Path"];
}

void Request::QueryParsing(const std::string request_target)
{
    const size_t query_position = request_target.find('?');
    if (query_position == std::string::npos)
    {
        throw std::runtime_error("Query not found");
    }

    const std::string parsed_path = request_target.substr(0, query_position);
    const std::string query_string = request_target.substr(query_position + 1);
    if (parsed_path.empty())
    {
        throw std::runtime_error("Invalid path");
    }

    _requestLine.insert(std::make_pair("Path", parsed_path));
    _requestLine.insert(std::make_pair("Query", query_string));
}

void Request::HeadersToMap(const std::string& request)
{
    const size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        throw std::runtime_error("Incomplete headers");
    }

    headers.clear();
    _hasContent = false;
    _hasTransferEncoding = false;

    const std::string valid_name_characters =
        "!#$%&'*+-.^_`|~0123456789abcdefghijklmnopqrstuvwxyz";
    std::istringstream header_stream(request.substr(0, header_end));
    std::string header_line;
    std::getline(header_stream, header_line);

    while (std::getline(header_stream, header_line))
    {
        const size_t colon = header_line.find(':');
        if (colon == std::string::npos)
        {
            throw std::runtime_error("Invalid header");
        }

        std::string header_name = header_line.substr(0, colon);
        for (size_t i = 0; i < header_name.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(header_name[i]);
            header_name[i] = static_cast<char>(std::tolower(character));
        }

        const bool invalid_header_name = header_name.empty()
            || header_name.find_first_not_of(valid_name_characters) != std::string::npos;
        if (invalid_header_name)
        {
            throw std::runtime_error("Invalid header name");
        }

        const bool is_host_header = header_name == "host";
        const bool defines_body_framing =
            header_name == "content-length" || header_name == "transfer-encoding";
        const bool already_present = headers.count(header_name) != 0;
        if (already_present && (is_host_header || defines_body_framing))
        {
            throw std::runtime_error("Duplicate Host or framing header");
        }

        const std::string header_value = trim(header_line.substr(colon + 1));
        for (size_t i = 0; i < header_value.size(); ++i)
        {
            const unsigned char character = static_cast<unsigned char>(header_value[i]);
            const bool control_character = character < 32 || character == 127;
            if (control_character && character != '\t')
            {
                throw std::runtime_error("Invalid header value");
            }
        }

        headers.insert(std::make_pair(header_name, header_value));
    }

    const std::string host_header = getHeader("host");
    if (host_header.empty())
    {
        throw std::runtime_error("No host");
    }

    _hasContent = headers.count("content-length") != 0;
    _hasTransferEncoding = headers.count("transfer-encoding") != 0;
    if (_hasContent && _hasTransferEncoding)
    {
        throw std::runtime_error("Ambiguous request framing");
    }

    HostParsing(host_header);
}

void Request::HostParsing(std::string host)
{
    host = trim(host);
    for (size_t i = 0; i < host.size(); ++i)
    {
        const unsigned char character = static_cast<unsigned char>(host[i]);
        host[i] = static_cast<char>(std::tolower(character));
    }

    const size_t port_separator = host.find(':');
    if (port_separator != std::string::npos)
    {
        host = host.substr(0, port_separator);
    }

    const std::vector<ServerConfig> &servers = _parsedServers;
    for (size_t server_index = 0; server_index < servers.size(); ++server_index)
    {
        const std::vector<std::string> server_names = servers[server_index].getServerName();
        for (size_t name_index = 0; name_index < server_names.size(); ++name_index)
        {
            std::string configured_name = server_names[name_index];
            for (size_t i = 0; i < configured_name.size(); ++i)
            {
                const unsigned char character = static_cast<unsigned char>(configured_name[i]);
                configured_name[i] = static_cast<char>(std::tolower(character));
            }

            if (host == configured_name)
            {
                _server = servers[server_index];
                _serverName = configured_name;
                return;
            }
        }
    }

    throw std::runtime_error("Invalid host");
}

void Request::parseBody(const std::string &buffer)
{
    const std::string header_separator = "\r\n\r\n";
    const size_t header_end = buffer.find(header_separator);
    if (header_end == std::string::npos)
    {
        throw std::runtime_error("Invalid body");
    }

    const size_t body_start = header_end + header_separator.size();
    if (_hasContent)
    {
        if (!checkingBody_framing(buffer))
        {
            throw std::runtime_error("Incomplete body");
        }

        body = buffer.substr(body_start, _contentLen);
    }
    else if (_hasTransferEncoding)
    {
        parseChunkedBody(buffer, body_start);
    }
    else
    {
        body.clear();
    }
}

void Request::MethodParsing(std::string method)
{
    const Locations location = _server.findLocation(_requestLine["Path"], method);
    const std::vector<std::string> allowed_methods = location.getAllowedMethods();
    if (allowed_methods.empty())
    {
        return;
    }

    for (size_t i = 0; i < allowed_methods.size(); ++i)
    {
        if (method == allowed_methods[i])
        {
            return;
        }
    }

    throw 405;
}

void Request::parseChunkedBody(const std::string &buffer, size_t pos)
{
    if (!checkingBody_chuncked(buffer))
    {
        throw std::runtime_error("Incomplete chunked body");
    }

    body.clear();
    const std::string line_ending = "\r\n";

    while (true)
    {
        const size_t size_line_end = buffer.find(line_ending, pos);
        if (size_line_end == std::string::npos)
        {
            throw std::runtime_error("Invalid chunk");
        }

        const std::string size_line = buffer.substr(pos, size_line_end - pos);
        size_t chunk_size = 0;
        std::istringstream size_stream(size_line);
        size_stream >> std::hex >> chunk_size;
        if (size_stream.fail())
        {
            throw std::runtime_error("Invalid chunk size");
        }

        const size_t data_start = size_line_end + line_ending.size();
        pos = data_start;
        if (chunk_size == 0)
        {
            break;
        }

        const size_t available_bytes = buffer.size() - data_start;
        if (chunk_size > available_bytes)
        {
            throw std::runtime_error("Incomplete chunk");
        }

        const size_t data_end = data_start + chunk_size;
        const size_t bytes_after_data = buffer.size() - data_end;
        if (bytes_after_data < line_ending.size())
        {
            throw std::runtime_error("Incomplete chunk");
        }
        if (buffer.compare(data_end, line_ending.size(), line_ending) != 0)
        {
            throw std::runtime_error("Invalid chunk ending");
        }

        body.append(buffer, data_start, chunk_size);
        pos = data_end + line_ending.size();
    }
}
