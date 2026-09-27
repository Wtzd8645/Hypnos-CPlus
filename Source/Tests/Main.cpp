#include "Network/NetworkTest.hpp"
#include <cstdlib>
#include <iostream>

using namespace Blanketmen;

static int ReadInput(int argc, char* argv[])
{
    if (argc > 1)
    {
        return std::atoi(argv[1]);
    }

    int input = 0;
    std::cin >> input;
    return input;
}

int main(int argc, char* argv[])
{
    Logging::Info("1: Network");

    int input = ReadInput(argc, argv);
    switch (input)
    {
        case 1:
        {
            Hypnos::Tests::NetworkPasses();
            break;
        }
    }
    return 0;
}
