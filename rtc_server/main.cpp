#include <string>
#include <vector>
#include <iostream>
#include "hstring.h"
#include "Ap4.h"
using namespace std;

int main(int argc, char** argv) {
    char* cfg = nullptr;
    if (argc > 1) {
        cfg = argv[1];
    }
    AP4::Initialize();

    std::string line, cmd;
    std::vector<std::string> cmds;
    printf("press quit key to exit\n");
    while (getline(cin, line))
    {
        cmds = hv::split(line);
        if (cmds.empty()) {
            continue;
        }
        cmd = cmds[0];
        if (cmd == "quit" || cmd == "q") {
            cout << "use quit app" << endl;
            break;
        }
        else if (cmd == "count" || cmd == "c") {         
        }
    }
    
    AP4::Terminate();
}