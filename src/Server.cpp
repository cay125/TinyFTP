//
// Created by xiangpu on 20-1-28.
//

#include "../inc/Server.hpp"
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <event2/buffer.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>

Server::Server(int Port)
{
    controlPort = Port;
    currentPath = fs::path(std::getenv("HOME"));
    std::ostringstream ostr;
    ostr << transferPort / 256 << "," << transferPort % 256 << ").\r\n";
    RESPONSE_227 += ostr.str();
    initMonthToEn();
    base = event_base_new();
    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(controlPort);

    controlEvconn = evconnlistener_new_bind(base, Server::listenControl, this,
                                            LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1, (sockaddr *) &sin,
                                            sizeof(sin));
    sin.sin_port = htons(transferPort);
    transferEvconn = evconnlistener_new_bind(base, Server::listenTransfer, this,
                                             LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1, (sockaddr *) &sin,
                                             sizeof(sin));

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
    if (srv_p->controlClientPort == -1)
    {
        sockaddr_in *clientAddr = (sockaddr_in *) addr;
        srv_p->controlClientPort = ntohs(clientAddr->sin_port);
    }
    bufferevent_setcb(bev, Server::readCB, Server::writeCB, Server::eventCB, arg);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
    bufferevent_write(bev, srv_p->RESPONSE_220.c_str(), srv_p->RESPONSE_220.length());
    srv_p->controlBuff = bev;
}

void Server::listenTransfer(struct evconnlistener *listener, int fd, struct sockaddr *, int socklen, void *arg)
{
    auto srv_p = static_cast<Server *>(arg);
    if (srv_p->controlClientPort == -1)
        return;
    event_base *_base = evconnlistener_get_base(listener);
    srv_p->transferBuff = bufferevent_socket_new(_base, fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(srv_p->transferBuff, Server::readTransfer, Server::writeTransfer, Server::eventTRansfer, arg);
    bufferevent_enable(srv_p->transferBuff, EV_READ | EV_WRITE);
    if (srv_p->currentStatus == "onLIST")
    {
        std::string response_buf = srv_p->RESPONSE_150 + "Here comes the directory listing.\r\n";
        bufferevent_write(srv_p->controlBuff, response_buf.c_str(), response_buf.length());
        srv_p->sendLISTbuf();
    }
    else if (srv_p->currentStatus == "onRETR")
    {
        fs::path filePath = srv_p->currentPath / srv_p->currentBody;
        if (!fs::exists(filePath))
        {
            std::cout << "file transfer error: file not exists\n";
            return;
        }
        std::ostringstream ostr;
        ostr << srv_p->RESPONSE_150 + "Opening BINARY mode data connection for " + srv_p->currentBody << " ("
             << fs::file_size(filePath) << " bytes).\r\n";
        std::string response_buf = ostr.str();
        bufferevent_write(srv_p->controlBuff, response_buf.c_str(), response_buf.length());
        srv_p->sendRETRbuf();
    }
    else if (srv_p->currentStatus == "onSTOR")
    {
        std::string response_buf = srv_p->RESPONSE_150 + "Ok to send data.\r\n";
        bufferevent_write(srv_p->controlBuff, response_buf.c_str(), response_buf.length());
    }
}

void Server::sendRETRbuf()
{
    fs::path filePath = currentPath / currentBody;
    int fd = open(filePath.string().c_str(), O_RDONLY);
    if (fd == -1)
        std::cout << "open file failed\n";
    if (evbuffer_add_file(bufferevent_get_output(transferBuff), fd, 0, -1))
        std::cout << "send file failed\n";
}

void Server::sendLISTbuf()
{
    int offset = 0;
    std::vector<std::string> temp;
    for (auto &it:fs::directory_iterator(currentPath))
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
    if (transferBuff != nullptr)
    {
        bufferevent_write(transferBuff, infobuf, offset);
        std::cout << "write dirs info successfully\n";
    }
    else
        std::cout << "write dirs info failed\n";
    delete[]infobuf;
}

void Server::readCB(struct bufferevent *bev, void *ctx)
{
    std::cout << "recv coming\n";
    auto srv_p = static_cast<Server *>(ctx);
    srv_p->eventHandler(bev);
}

void Server::eventHandler(bufferevent *bev)
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
        bufferevent_write(bev, RESPONSE_331.c_str(), RESPONSE_331.length());
    }
    else if (request == "PASS")
    {
        //std::cout << "Get command PASS\n";
        bufferevent_write(bev, RESPONSE_230.c_str(), RESPONSE_230.length());
    }
    else if (request == "SYST")
    {
        //std::cout << "Get command SYST\n";
        bufferevent_write(bev, RESPONSE_215.c_str(), RESPONSE_215.length());
    }
    else if (request == "PWD")
    {
        //std::cout << "Get command PWD\n";
        std::string path = RESPONSE_257 + "\"" + currentPath.string() + "\"" + " is the current directory.\r\n";
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
        bufferevent_write(bev, RESPONSE_227.c_str(), RESPONSE_227.length());
    }
    else if (request == "LIST")
    {
        //std::cout << "Get command LIST\n";
        currentStatus = "onLIST";
    }
    else if (request == "CWD")
    {
        //std::cout << "Get command CWD\n";
        fs::path desiredPath;
        if (fs::path(body).is_absolute())
            desiredPath = fs::path(body);
        else
            desiredPath = currentPath / body;
        if (body == "..")
            desiredPath = fs::canonical(desiredPath);
        if (fs::exists(desiredPath) && fs::is_directory(desiredPath))
        {
            currentPath = desiredPath;
            std::cout << "current dir is: " << currentPath.string() << "\n";
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
        currentStatus = "onRETR";
        currentBody = body;
    }
    else if (request == "STOR")
    {
        //std::cout << "Get command STOR\n";
        currentStatus = "onSTOR";
        currentBody = body;
    }
    else if (request == "MKD")
    {
        //std::cout << "Get command MKD\n";
        fs::path desiredPath = currentPath / body;
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

std::string Server::getFileInfo(fs::path p)
{
    const char *filename = p.string().c_str();
    std::ostringstream ostr;
    std::string res = "";
    struct stat buf;
    stat(filename, &buf);
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
    if (what | BEV_EVENT_EOF)
        std::cout << "control connction closed\n";
    else if (what | BEV_EVENT_ERROR)
        std::cout << "one error happend\n";
    bufferevent_free(bev);
    auto srv_p = static_cast<Server *>(ctx);
    srv_p->controlClientPort = -1;
}

void Server::readTransfer(struct bufferevent *bev, void *ctx)
{
    auto srv_p = static_cast<Server *>(ctx);
    std::cout << "transfer coming\n";
    fs::path filePath = srv_p->currentPath / srv_p->currentBody;
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
    auto srv_p = static_cast<Server *>(ctx);
    if (srv_p->currentStatus == "onLIST")
    {
        std::string response_buf = srv_p->RESPONSE_226 + "Directory send ok.\r\n";
        bufferevent_write(srv_p->controlBuff, response_buf.c_str(), response_buf.length());
        srv_p->currentStatus = "finishLIST";
        std::cout << "LIST command finished\n";
        bufferevent_free(srv_p->transferBuff);
        srv_p->transferBuff = nullptr;
    }
    else if (srv_p->currentStatus == "onRETR")
    {
        std::string response_buf = srv_p->RESPONSE_226 + "Transfer complete.\r\n";
        bufferevent_write(srv_p->controlBuff, response_buf.c_str(), response_buf.length());
        srv_p->currentStatus = "finishRETR";
        std::cout << "RETR command finished\n";
        bufferevent_free(srv_p->transferBuff);
        srv_p->transferBuff = nullptr;
    }
}

void Server::eventTRansfer(struct bufferevent *bev, short what, void *ctx)
{
    auto srv_p = static_cast<Server *>(ctx);
    if (what | BEV_EVENT_EOF)
    {
        std::cout << "transfer connction closed\n";
        if (srv_p->currentStatus == "onSTOR")
        {
            std::string response_buf = srv_p->RESPONSE_226 + "Transfer complete.\r\n";
            bufferevent_write(srv_p->controlBuff, response_buf.c_str(), response_buf.length());
            srv_p->currentStatus = "finishSTOR";
            std::cout << "STOR command finished\n";
        }

    }
    else if (what | BEV_EVENT_ERROR)
    {
        std::cout << "one error happend\n";
    }
    bufferevent_free(bev);
    srv_p->transferBuff = nullptr;
}

void Server::Run()
{
    std::cout << "server start running on port: " << controlPort << std::endl;
    event_base_dispatch(base);
}

void Server::Stop()
{
    event_base_loopexit(base, nullptr);
    event_base_free(base);
    if (controlEvconn != nullptr)
        evconnlistener_free(controlEvconn);
    if (transferBuff != nullptr)
        evconnlistener_free(transferEvconn);
}