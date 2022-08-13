#include <string>
#include <vector>
#include <iostream>
#include "hstring.h"
#include "Ap4.h"
#include "WebRtcServer.h"
#include "Util/util.h"
using namespace std;
using mediakit::WebRtcServer;

int main(int argc, char** argv) {
    std::string cfg = toolkit::exeDir() + "config.ini";
    if (argc > 1) {
        cfg = argv[1];
    }
    AP4::Initialize();
    WebRtcServer::Instance().start(cfg.c_str());

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

    WebRtcServer::Instance().stop();
    AP4::Terminate();
}