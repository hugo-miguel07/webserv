#include "Tokenize.hpp"

Tokenize::Tokenize() {}

Tokenize::~Tokenize() {}

void Tokenize::debug(std::vector<Token> storage) const
{
    int i = 0;
    for (std::vector<Token>::iterator it = storage.begin(); it != storage.end(); ++it)
    {
        std::cout << "[" << i << "]: " << it->content << "\n";
        i++;
    }
}

std::vector<Token> Tokenize::tokenize(const std::string& input) const
{
    std::string temp_storage;
    std::vector<Token> storage;
    size_t line = 1;

    for (size_t i = 0; i < input.size(); i++)
    {
        char c = input[i];
        if (c == '\n')
        {
            if (!temp_storage.empty())
            {
                Token token;
                token.content = temp_storage;
                token.line = line;
                storage.push_back(token);
                temp_storage.clear();
            }
            line++;
            continue;
        }
        else if (c == '#')
        {
            if (!temp_storage.empty())
            {
                Token token;
                token.content = temp_storage;
                token.line = line;
                storage.push_back(token);
                temp_storage.clear();
            }
            while (i < input.size() && input[i] != '\n')
                i++;
            continue;
        }
        else if (c == '{' || c == '}' || c == ';')
        {
            if (!temp_storage.empty())
            {
                Token token;
                token.content = temp_storage;
                token.line = line;
                storage.push_back(token);
                temp_storage.clear();
            }
            Token token;
            token.content = std::string(1, c);
            token.line = line;
            storage.push_back(token);
            continue;
        }
        else if (isspace(c))
        {
            if (!temp_storage.empty())
            {
                Token token;
                token.content = temp_storage;
                token.line = line;
                storage.push_back(token);
                temp_storage.clear();
            }
            continue;
        }
        else
            temp_storage += c;
    }
    if (!temp_storage.empty())
    {   
        Token token;
        token.content = temp_storage;
        token.line = line;
        storage.push_back(token);
    }
    // TOKENS DEBUG
    // Tokenize::debug(storage);
    return (storage);
}