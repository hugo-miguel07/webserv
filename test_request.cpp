#include "includes/Request.hpp"
#include "includes/Client.hpp"
#include <iostream>
#include <string>
#include <stdexcept>

void testRequest(const std::string &name, const std::string &buffer)
{
    std::cout << "\n=== " << name << " ===" << std::endl;

    try
    {
        Request req;
        Client client;
        bool header_status;

        header_status = req.checkHeader(buffer);

        //std::cout << "Header status " << header_status << std::endl;

        /*std::cout << "Content?" << req.getHasContentStatus() << std::endl;
        std::cout << "Transfer Encoding??" << req.getTransferEncodingStatus() << std::endl;*/
        if (req.getHasContentStatus() && req.getTransferEncodingStatus())
            throw std::runtime_error("Invalid request format");
    
        if (header_status && req.getHasContentStatus())
        {
            req.readingBody(buffer);

            if (req.checkingBody_framing(buffer))
                std::cout << "Framing complete" << std::endl;
            else
                std::cout << "Framing not complete" << std::endl;
        }
        else if (header_status && req.getTransferEncodingStatus())
        {
            req.readingBody(buffer);

            if (req.checkingBody_chuncked(buffer))
                std::cout << "Chuncked complete" << std::endl;
            else 
                std::cout << "Chuncked not complete" << std::endl;
        }
        else if (!header_status)
            std::cout << "Request not complete" << std::endl;
        else
            std::cout << "Request (no body) complete" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cout << "ERROR: " << e.what() << std::endl;
    }
}

int main()
{
    std::string get_request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string content_complete =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    std::string content_incomplete =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "hello";

    std::string chunked_complete =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\n"
        "Wiki\r\n"
        "5\r\n"
        "pedia\r\n"
        "0\r\n"
        "\r\n";

    std::string chunked_incomplete =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\n"
        "Wi";

    std::string invalid_both =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "hello";

    std::string invalid_content_length =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: abc\r\n"
        "\r\n"
        "hello";

    testRequest("GET without body", get_request);
    testRequest("Content-Length complete", content_complete);
    testRequest("Content-Length incomplete", content_incomplete);
    testRequest("Chunked complete", chunked_complete);
    testRequest("Chunked incomplete", chunked_incomplete);
    testRequest("Content-Length + Transfer-Encoding", invalid_both);
    testRequest("Invalid Content-Length", invalid_content_length);

    return 0;
}