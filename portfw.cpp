#include "network/Proxy.h"
#include <iostream>
#include <string>

using namespace std;

void usage(){
    cout << "Portfw <local_addr> <local_port> <remote_addr> <remote_port>" << endl;
}
int main(int argc, char *argv[]){

    NetProxy np;

    if(argc<4){
        usage();
        return -1;
    }
    np.proxyBind(argv[1], stoi(argv[2]), argv[3], stoi(argv[4]));

    return 0;

}