#include "libs.hpp"

#pragma once
class Tokenize
{
private:
    
public:
    Tokenize();
    ~Tokenize();

    std::vector<Token> tokenize(const std::string& input) const;
    void debug(std::vector<Token> storage) const;
};

bool isSpace(char c);

