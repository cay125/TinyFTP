//
// Created by xiangpu on 20-1-28.
//

#include "../inc/Server.hpp"
#include "../inc/ftpDataUnit.hpp"
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <event2/buffer.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>

Server::Server(int Port)
{
    initMonthToEn();
    getCurrentUser();
    getCurrentIP();
    listenControlPort = Port;
    base = event_base_new();
    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(Port);

    controlEvconn = evconnlistener_new_bind(base, Server::listenControl, this,
                                            LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1, (sockaddr *) &sin,
                                            sizeof(sin));
}

void Server::getCurrentUser()
{
    uid_t userid = getuid();
    passwd *pwd = getpwuid(userid);
    currentUser = pwd->pw_name;
}

void Server::getCurrentIP()
{
    char ipaddr[256];
    struct sockaddr_in *sin;
    struct ifreq ifr_ip;
    int sock_get_ip = socket(AF_INET, SOCK_STREAM, 0);
    memset(&ifr_ip, 0, sizeof(ifr_ip));
    strncpy(ifr_ip.ifr_name, "eth0", sizeof(ifr_ip.ifr_name) - 1);
    ioctl(sock_get_ip, SIOCGIFADDR, &ifr_ip);
    sin = (struct sockaddr_in *) &ifr_ip.ifr_addr;
    strcpy(ipaddr, inet_ntoa(sin->sin_addr));
    close(sock_get_ip);
    currentIP = ipaddr;
}

void Server::initMonthToEn()
{
    const char p[][5] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++)
        monthToEn[i] = std::string(p[i]);
}

void Server::listenControl(struct evconnlistener *listener, int fd, struct sockaddr *addr, int socklen, void *arg)
{
    event_base *_base = evconnlistener_get_base(listener);
    bufferevent *bev = bufferevent_socket_new(_base, fd, BEV_OPT_CLOSE_ON_FREE);
    auto srv_p = static_cast<Server *>(arg);
    sockaddr_in *clientAddr = (sockaddr_in *) addr;
    auto *unit = new ftpDataUnit(srv_p, ntohs(clientAddr->sin_port));
    bufferevent_setcb(bev, Server::readCB, Server::writeCB, Server::eventCB, unit);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
    bufferevent_write(bev, srv_p->RESPONSE_220.c_str(), srv_p->RESPONSE_220.length());
    unit->controlBuff = bev;
    std::cout << "one control connection established on: " << ntohs(clientAddr->sin_port) << std::endl;
}

void Server::listenTransfer(struct evconnlistener *listener, int fd, struct sockaddr *, int socklen, void *arg)
{
    auto unit = static_cast<ftpDataUnit *>(arg);
    auto srv_p = unit->srv_p;
    event_base *_base = evconnlistener_get_base(listener);
    if (unit->transferBuff != nullptr)
        std::cout << "error when get one connection (transferBuff should be null).\r";
    unit->transferBuff = bufferevent_socket_new(_base, fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(unit->transferBuff, Server::readTransfer, Server::writeTransfer, Server::eventTRansfer, arg);
    bufferevent_enable(unit->transferBuff, EV_READ | EV_WRITE);
    if (unit->currentStatus == "onLIST")
    {
        std::string response_buf = srv_p->RESPONSE_150 + "Here comes the directory listing.\r\n";
        bufferevent_write(unit->controlBuff, response_buf.c_str(), response_buf.length());
        srv_p->sendLISTbuf(unit);
    }
    else if (unit->currentStatus == "onRETR")
    {
        fs::path filePath = unit->currentPath / unit->currentBody;
        if (!fs::exists(filePath))
        {
            std::cout << "file transfer error: file not exists\n";
            return;
        }
        std::ostringstream ostr;
        ostr << srv_p->RESPONSE_150 + "Opening BINARY mode data connection for " + unit->currentBody << " ("
             << fs::file_size(filePath) << " bytes).\r\n";
        std::string response_buf = ostr.str();
        bufferevent_write(unit->controlBuff, response_buf.c_str(), response_buf.length());
        srv_p->sendRETRbuf(unit);
    }
    else if (unit->currentStatus == "onSTOR")
    {
        std::string response_buf = srv_p->RESPONSE_150 + "Ok to send data.\r\n";
        bufferevent_write(unit->controlBuff, response_buf.c_str(), response_buf.length());
    }
}

void Server::sendRETRbuf(ftpDataUnit *unit)
{
    fs::path filePath = unit->currentPath / unit->currentBody;
    int fd = open(filePath.string().c_str(), O_RDONLY);
    if (fd == -1)
        std::cout << "open file failed\n";
    if (evbuffer_add_file(bufferevent_get_output(unit->transferBuff), fd, 0, -1))
        std::cout << "send file failed\n";
}

void Server::sendLISTbuf(ftpDataUnit *unit)
{
    int offset = 0;
    std::vector<std::string> temp;
    for (auto &it:fs::directory_iterator(unit->currentPath))
    {
        std::string info = getFileInfo(it.path());
        offset += info.length();
        temp.push_back(info);
    }
    char *infobuf = new char[offset];
    offset = 0;
    for (auto &it:temp)
    {
        memcpy(infobuf + offset, it.c_str(), it.length());
        offset += it.length();
    }
    if (unit->transferBuff != nullptr)
    {
        bufferevent_write(unit->transferBuff, infobuf, offset);
        //std::cout << "write dirs info successfully\n";
    }
    else
    {
        std::cout << "write dirs info failed\n";
    }
    delete[]infobuf;
}

void Server::readCB(struct bufferevent *bev, void *ctx)
{
    std::cout << "recv coming\n";
    auto unit = static_cast<ftpDataUnit *>(ctx);
    unit->srv_p->eventHandler(bev, unit);
}

void Server::eventHandler(bufferevent *bev, ftpDataUnit *unit)
{
    char buf[4096];
    int n = 0;
    std::string request = "", body = "";
    while ((n = evbuffer_remove(bufferevent_get_input(bev), buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n; i++)
            request += buf[i];
    }
    if (request.length() < 3)
    {
        bufferevent_write(bev, RESPONSE_502.c_str(), RESPONSE_502.length());
        return;
    }
    for (int i = 0; i < request.length(); i++)
    {
        if (request[i] == ' ' || request[i] == '\r')
        {
            if (request[i] == '\r')
            {
                body = "";
                request = request.substr(0, i);
                break;
            }
            for (int ii = i + 1; ii < request.length(); ii++)
            {
                if (request[ii] == '\r')
                {
                    body = request.substr(i + 1, ii - i - 1);
                    request = request.substr(0, i);
                    break;
                }
            }
            break;
        }
    }
    if (request == "AUTH")
    {
        //std::cout << "Get command AUTH\n";
        bufferevent_write(bev, RESPONSE_530.c_str(), RESPONSE_530.length());
    }
    else if (request == "USER")
    {
        //std::cout << "Get command USER\n";
        unit->inputUser = body;
        bufferevent_write(bev, RESPONSE_331.c_str(), RESPONSE_331.length());
    }
    else if (request == "PASS")
    {
        //std::cout << "Get command PASS\n";
        unit->inputPass = body;
        if (unit->inputUser == currentUser)
        {
            bufferevent_write(bev, RESPONSE_230.c_str(), RESPONSE_230.length());
            unit->Verifyed = true;
        }
        else
        {
            const char *response_buf = "530 Login incorrect.\r\n";
            bufferevent_write(bev, response_buf, strlen(response_buf));
        }
    }
    else if (request == "SYST")
    {
        //std::cout << "Get command SYST\n";
        bufferevent_write(bev, RESPONSE_215.c_str(), RESPONSE_215.length());
    }
    else if (request == "PWD")
    {
        //std::cout << "Get command PWD\n";
        std::string path = RESPONSE_257 + "\"" + unit->currentPath.string() + "\"" + " is the current directory.\r\n";
        bufferevent_write(bev, path.c_str(), path.length());
    }
    else if (request == "TYPE")
    {
        //std::cout << "Get command TYPE\n";
        bufferevent_write(bev, RESPONSE_200.c_str(), RESPONSE_200.length());
    }
    else if (request == "PASV")
    {
        //std::cout << "Get command PASV\n";
        sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(0);
        if (unit->transferEvconn != nullptr)
        {
            evconnlistener_free(unit->transferEvconn);
            unit->transferEvconn = nullptr;
        }
        unit->transferEvconn = evconnlistener_new_bind(base, listenTransfer, unit,
                                                       LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1, (sockaddr *) &sin,
                                                       sizeof(sin));
        int fd = evconnlistener_get_fd(unit->transferEvconn);
        socklen_t sinlen = sizeof(sin);
        getsockname(fd, (sockaddr *) &sin, &sinlen);
        unit->listenTransferPort = ntohs(sin.sin_port);
        std::cout << "one transfer connection listen on: " << unit->listenTransferPort << std::endl;
        std::string temp_ip = currentIP;
        std::replace(temp_ip.begin(), temp_ip.end(), '.', ',');
        std::ostringstream ostr;
        ostr << RESPONSE_227 << temp_ip << "," << unit->listenTransferPort / 256 << ","
             << unit->listenTransferPort % 256 << ").\r\n";
        std::string response_buf = ostr.str();
        bufferevent_write(bev, response_buf.c_str(), response_buf.length());
    }
    else if (request == "LIST")
    {
        //std::cout << "Get command LIST\n";
        unit->currentStatus = "onLIST";
    }
    else if (request == "CWD")
    {
        //std::cout << "Get command CWD\n";
        fs::path desiredPath;
        if (fs::path(body).is_absolute())
            desiredPath = fs::path(body);
        else
            desiredPath = unit->currentPath / body;
        if (body == "..")
            desiredPath = fs::canonical(desiredPath);
        if (fs::exists(desiredPath) && fs::is_directory(desiredPath))
        {
            unit->currentPath = desiredPath;
            std::cout << "client port: " << unit->clientControlPort << " current dir is: " << unit->currentPath.string()
                      << "\n";
            bufferevent_write(bev, RESPONSE_250.c_str(), RESPONSE_250.length());
        }
        else
        {
            std::cout << "CWD failed\n";
            bufferevent_write(bev, RESPONSE_550.c_str(), RESPONSE_550.length());
        }
    }
    else if (request == "RETR")
    {
        //std::cout << "Get command RETR\n";
        unit->currentStatus = "onRETR";
        unit->currentBody = body;
    }
    else if (request == "STOR")
    {
        //std::cout << "Get command STOR\n";
        unit->currentStatus = "onSTOR";
        unit->currentBody = body;
    }
    else if (request == "MKD")
    {
        //std::cout << "Get command MKD\n";
        fs::path desiredPath = unit->currentPath / body;
        fs::create_directories(desiredPath);
        if (fs::exists(desiredPath) && fs::is_directory(desiredPath))
        {
            std::string response_buf = RESPONSE_257 + "\"" + desiredPath.string() + "\" created.\r\n";
            bufferevent_write(bev, response_buf.c_str(), response_buf.length());
        }
        else
        {
            bufferevent_write(bev, RESPONSE_550.c_str(), RESPONSE_550.length());
        }
    }
    else
    {
        std::cout << "Get unrecognized command: " << request << std::endl;
        bufferevent_write(bev, RESPONSE_502.c_str(), RESPONSE_502.length());
    }
}

std::string Server::getFileInfoUseFS(fs::path p)
{
    std::string res;
    if (fs::is_directory(p)) res += 'd';
    else if (fs::is_block_file(p)) res += 'b';
    else if (fs::is_character_file(p)) res += 'c';
    else if (fs::is_symlink(p)) res += 'l';
    else if (fs::is_socket(p)) res += 's';
    else if (fs::is_fifo(p)) res += 'p';
    else res += '-';
    fs::perms filePerm = fs::status(p).permissions();
    res += ((filePerm & fs::perms::owner_read) != fs::perms::none ? "r" : "-");
    res += ((filePerm & fs::perms::owner_write) != fs::perms::none ? "w" : "-");
    res += ((filePerm & fs::perms::owner_exec) != fs::perms::none ? "x" : "-");
    res += ((filePerm & fs::perms::group_read) != fs::perms::none ? "r" : "-");
    res += ((filePerm & fs::perms::group_write) != fs::perms::none ? "w" : "-");
    res += ((filePerm & fs::perms::group_exec) != fs::perms::none ? "x" : "-");
    res += ((filePerm & fs::perms::others_read) != fs::perms::none ? "r" : "-");
    res += ((filePerm & fs::perms::others_write) != fs::perms::none ? "w" : "-");
    res += ((filePerm & fs::perms::others_exec) != fs::perms::none ? "x" : "-");
    res += ' ';

    std::ostringstream ostr;
    ostr << fs::hard_link_count(p) << " ";

    struct stat buf;
    stat(p.string().c_str(), &buf);
    ostr << buf.st_uid << " " << buf.st_gid << " " << buf.st_size << " ";
    res += ostr.str();

    //TODO: return the full file information

    return res;
}

std::string Server::getFileInfo(fs::path p)
{
    //const char *filename = p.string().c_str();
    std::ostringstream ostr;
    std::string res;
    struct stat buf;
    stat(p.string().c_str(), &buf);
    if ((buf.st_mode & S_IFMT) == S_IFDIR)
        res += 'd';
    else if ((buf.st_mode & S_IFMT) == S_IFBLK)
        res += 'b';
    else if ((buf.st_mode & S_IFMT) == S_IFCHR)
        res += 'c';
    else if ((buf.st_mode & S_IFMT) == S_IFLNK)
        res += 'l';
    else if ((buf.st_mode & S_IFMT) == S_IFSOCK)
        res += 's';
    else
        res += '-';

    res += buf.st_mode & S_IRUSR ? 'r' : '-';
    res += buf.st_mode & S_IWUSR ? 'w' : '-';
    res += buf.st_mode & S_IXUSR ? 'x' : '-';
    res += buf.st_mode & S_IRGRP ? 'r' : '-';
    res += buf.st_mode & S_IWGRP ? 'w' : '-';
    res += buf.st_mode & S_IXGRP ? 'x' : '-';
    res += buf.st_mode & S_IROTH ? 'r' : '-';
    res += buf.st_mode & S_IWOTH ? 'w' : '-';
    res += buf.st_mode & S_IXOTH ? 'x' : '-';
    res += " ";

    ostr << buf.st_nlink << " " << buf.st_uid << " " << buf.st_gid << " " << buf.st_size << " ";
    res += ostr.str();
    ostr.clear();
    ostr.str("");
    struct tm *p_tm = localtime(&buf.st_mtime);
    auto it = monthToEn.find(p_tm->tm_mon);
    res += it->second + ' ';
    ostr << p_tm->tm_mday << " " << p_tm->tm_hour << ":" << p_tm->tm_min << " ";
    res += ostr.str() + p.filename().string() + "\r\n";

    return res;
}

void Server::writeCB(struct bufferevent *bev, void *ctx)
{

}

void Server::eventCB(struct bufferevent *bev, short what, void *ctx)
{
    auto unit = static_cast<ftpDataUnit *>(ctx);
    if (what | BEV_EVENT_EOF)
        std::cout << "control connction closed: " << unit->clientControlPort << std::endl;
    else if (what | BEV_EVENT_ERROR)
        std::cout << "one error happend\n";
    bufferevent_free(bev);
    if (unit->transferEvconn != nullptr)
        evconnlistener_free(unit->transferEvconn);
    if (unit->transferBuff != nullptr)
        bufferevent_free(unit->transferBuff);
    delete unit;
}

void Server::readTransfer(struct bufferevent *bev, void *ctx)
{
    auto unit = static_cast<ftpDataUnit *>(ctx);
    std::cout << "transfer coming\n";
    fs::path filePath = unit->currentPath / unit->currentBody;
    std::fstream out(filePath.string(), std::ios::out | std::ios::binary | std::ios::app);
    int n = 0;
    char buf[8192] = {0};
    while ((n = evbuffer_remove(bufferevent_get_input(bev), buf, sizeof(buf))) > 0)
    {
        std::cout << "recv: " << n << "bytes\n";
        out.write(buf, n);
    }
    out.close();
}

void Server::writeTransfer(struct bufferevent *bev, void *ctx)
{
    auto unit = static_cast<ftpDataUnit *>(ctx);
    auto srv_p = unit->srv_p;
    if (unit->currentStatus == "onLIST")
    {
        std::string response_buf = srv_p->RESPONSE_226 + "Directory send ok.\r\n";
        bufferevent_write(unit->controlBuff, response_buf.c_str(), response_buf.length());
        unit->currentStatus = "finishLIST";
        std::cout << "LIST command finished\n";
        bufferevent_free(unit->transferBuff);
        unit->transferBuff = nullptr;
        std::cout << "one transfer connection closed: " << unit->listenTransferPort << std::endl;
    }
    else if (unit->currentStatus == "onRETR")
    {
        std::string response_buf = srv_p->RESPONSE_226 + "Transfer complete.\r\n";
        bufferevent_write(unit->controlBuff, response_buf.c_str(), response_buf.length());
        unit->currentStatus = "finishRETR";
        std::cout << "RETR command finished\n";
        bufferevent_free(unit->transferBuff);
        unit->transferBuff = nullptr;
        std::cout << "one transfer connection closed: " << unit->listenTransferPort << std::endl;
    }
}

void Server::eventTRansfer(struct bufferevent *bev, short what, void *ctx)
{
    auto unit = static_cast<ftpDataUnit *>(ctx);
    auto srv_p = unit->srv_p;
    if (what | BEV_EVENT_EOF)
    {
        std::cout << "one transfer connection closed: " << unit->listenTransferPort << std::endl;
        if (unit->currentStatus == "onSTOR")
        {
            std::string response_buf = srv_p->RESPONSE_226 + "Transfer complete.\r\n";
            bufferevent_write(unit->controlBuff, response_buf.c_str(), response_buf.length());
            unit->currentStatus = "finishSTOR";
            std::cout << "STOR command finished\n";
        }

    }
    else if (what | BEV_EVENT_ERROR)
    {
        std::cout << "one error happend\n";
    }
    bufferevent_free(bev);
    unit->transferBuff = nullptr;
}

void Server::Run()
{
    std::cout << "server start running on ip: " << currentIP << " port: " << listenControlPort << std::endl;
    event_base_dispatch(base);
}

void Server::Stop()
{
    event_base_loopexit(base, nullptr);
    event_base_free(base);
    if (controlEvconn != nullptr)
        evconnlistener_free(controlEvconn);
}

ftpDataUnit::ftpDataUnit(Server *_srv_p, int _port)
{
    srv_p = _srv_p;
    clientControlPort = _port;
    transferEvconn = nullptr;
    controlBuff = nullptr;
    transferBuff = nullptr;
    currentPath = fs::path(std::getenv("HOME"));
    Verifyed = false;
}