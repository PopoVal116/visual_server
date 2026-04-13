#include "common.h"
#include <sstream>

using namespace std;

double server_start_time = 0.0;
std::deque<SignalPoint> signal_history;

std::vector<std::string> split_string(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}