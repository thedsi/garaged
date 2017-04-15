#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include "garaged.h"
using namespace std;

int main(int argc, char** argv)
{
    if(geteuid() != 0)
    {
        cerr << "Must be root.\n";
        return 1;
    }
    bool startDaemon = false;
    
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-d") == 0)
        {
            startDaemon = true;
        }
        else
        {
            cerr << "Unknown option: <" << argv[i] << ">" << endl;
            return 2;
        }
    }
    if(startDaemon)
    {
        cout << "Starting garaged daemon..." << endl;
        if(daemon(0, 0) == -1)
        {
            int errsv = errno;
            cerr << "Failed to daemonize (" << strerror(errsv) << ")" << endl;
            return errsv;
        }
    }            
    Garaged& garaged = Garaged::Instance();
    garaged.SetLogFileName("/var/log/garaged.log");
    garaged.Exec();
    return 0;
}