#include "common.h"

double server_start_time = 0.0;
std::deque<SignalPoint> signal_history;

std::vector<std::string> split_string(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    for (char c : s)
    {
        if (c == delimiter)
        {
            if (!token.empty())
                tokens.push_back(token);
            token.clear();
        }
        else
        {
            token += c;
        }
    }
    if (!token.empty())
        tokens.push_back(token);
    return tokens;
}