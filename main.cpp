#include <iostream>
#include <string.h>
#include "inc/Server.hpp"
#include <fstream>
#include <string>

void printSyntax()
{
    std::cout << "You must indicate client or server\n";
}

void runAsServer()
{
    Server srv(8080);
    srv.Run();
}

void runAsClient()
{

}

int main(int argc, char **argv)
{
    if (argc != 2 || strlen(argv[1]) != 1 || (argv[1][0] != 's' && argv[1][0] != 'c'))
    {
        printSyntax();
        return 0;
    }
    std::cout << "Hello World!!\n";
    if (argv[1][0] == 's')
        runAsServer();
    else
        runAsClient();
    return 0;
}