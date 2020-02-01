//
// Created by xiangpu on 20-1-28.
//

#ifndef FILERECV_SERVER_HPP
#define FILERECV_SERVER_HPP

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <filesystem>
#include <string>
#include <map>

namespace fs=std::filesystem;

class Server
{
public:
    explicit Server(int Port);

    void Run();

    void Stop();

private:
    std::map<int, std::string> monthToEn;
    std::string currentStatus;
    std::string currentBody;
    int controlPort = -1;
    int transferPort = 46301;
    int controlClientPort = -1;
    event_base *base = nullptr;
    evconnlistener *controlEvconn = nullptr;
    evconnlistener *transferEvconn = nullptr;
    bufferevent *controlBuff = nullptr;
    bufferevent *transferBuff = nullptr;
    fs::path currentPath;
    std::string RESPONSE_502 = "502 Command not implemented\r\n";
    std::string RESPONSE_220 = "220 (Tiny ftpServer)\r\n";
    std::string RESPONSE_530 = "530 Please login with USER and PASS\r\n";
    std::string RESPONSE_331 = "331 Please specify PASS\r\n";
    std::string RESPONSE_230 = "230 Login successful\r\n";
    std::string RESPONSE_215 = "215 Unix type: L8\r\n";
    std::string RESPONSE_257 = "257 ";
    std::string RESPONSE_200 = "200 Switching to binary mode.\r\n";
    std::string RESPONSE_227 = "227 Entering passive mode (192,168,124,4,";
    std::string RESPONSE_150 = "150 ";
    std::string RESPONSE_226 = "226 ";
    std::string RESPONSE_250 = "250 Directory successfully changed\r\n";
    std::string RESPONSE_550 = "550 Failed to change directory.\r\n";

    std::string getFileInfo(fs::path);

    void initMonthToEn();

    void sendLISTbuf();

    void sendRETRbuf();

    void eventHandler(bufferevent *bev);

    static void listenControl(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int socklen, void *);

    static void listenTransfer(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int socklen, void *);

    static void readCB(struct bufferevent *bev, void *ctx);

    static void writeCB(struct bufferevent *bev, void *ctx);

    static void eventCB(struct bufferevent *bev, short what, void *ctx);

    static void readTransfer(struct bufferevent *bev, void *ctx);

    static void writeTransfer(struct bufferevent *bev, void *ctx);

    static void eventTRansfer(struct bufferevent *bev, short what, void *ctx);

};


#endif //FILERECV_SERVER_HPP