#include "rpcserver.h"



int main()
{   
    evthread_use_pthreads();
    RPCServer server(8081);
    server.start();
    return 0;
}