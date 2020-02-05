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
#include <set>

class ftpDataUnit;

namespace fs=std::filesystem;

class Server
{
public:
    explicit Server(int Port);

    void Run();

    void EnableCli();

    void Stop();

private:
    int listenControlPort = 0;
    std::map<int, std::string> monthToEn;
    std::set<ftpDataUnit *> setUnit;
    bool debugInfo = true;
    event *eventCli = nullptr;
    event_base *base = nullptr;
    evconnlistener *controlEvconn = nullptr;
    std::string currentUser;
    std::string currentIP;
    std::string RESPONSE_502 = "502 Command not implemented\r\n";
    std::string RESPONSE_220 = "220 (Tiny ftpServer)\r\n";
    std::string RESPONSE_530 = "530 Please login with USER and PASS\r\n";
    std::string RESPONSE_331 = "331 Please specify PASS\r\n";
    std::string RESPONSE_230 = "230 Login successful\r\n";
    std::string RESPONSE_215 = "215 Unix type: L8\r\n";
    std::string RESPONSE_257 = "257 ";
    std::string RESPONSE_200 = "200 Switching to binary mode.\r\n";
    std::string RESPONSE_227 = "227 Entering passive mode (";
    std::string RESPONSE_150 = "150 ";
    std::string RESPONSE_226 = "226 ";
    std::string RESPONSE_250 = "250 Directory successfully changed\r\n";
    std::string RESPONSE_550 = "550 Failed to change directory.\r\n";

    std::string getFileInfo(fs::path);

    std::string getFileInfoUseFS(fs::path);

    void initMonthToEn();

    void getCurrentUser();

    void getCurrentIP();

    void sendLISTbuf(ftpDataUnit *unit);

    void sendRETRbuf(ftpDataUnit *unit);

    void eventHandler(bufferevent *bev, ftpDataUnit *unit);

    static void listenControl(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int socklen, void *);

    static void listenTransfer(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int socklen, void *);

    static void readCB(struct bufferevent *bev, void *ctx);

    static void writeCB(struct bufferevent *bev, void *ctx);

    static void eventCB(struct bufferevent *bev, short what, void *ctx);

    static void readTransfer(struct bufferevent *bev, void *ctx);

    static void writeTransfer(struct bufferevent *bev, void *ctx);

    static void eventTRansfer(struct bufferevent *bev, short what, void *ctx);

    static void cliCB(evutil_socket_t, short, void *);

};


#endif //FILERECV_SERVER_HPP
