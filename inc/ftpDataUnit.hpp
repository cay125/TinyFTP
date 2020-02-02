//
// Created by xiangpu on 20-2-1.
//

#ifndef FILERECV_FTPDATAUNIT_HPP
#define FILERECV_FTPDATAUNIT_HPP

#include "Server.hpp"

class ftpDataUnit
{
public:
    evconnlistener *transferEvconn;
    bufferevent *controlBuff;
    bufferevent *transferBuff;
    int clientControlPort = 0;
    int listenTransferPort = 0;
    fs::path currentPath;
    std::string currentStatus;
    std::string currentBody;
    Server *srv_p;

    ftpDataUnit(Server *, int);
};

#endif //FILERECV_FTPDATAUNIT_HPP
