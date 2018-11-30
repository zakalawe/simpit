#include "FGFSTelnetSocket.h"

#include <iostream>
#include <cstdio>
#include <cstring>
#include <exception>

#include <errno.h>
#include <netdb.h> // for gethostbyname
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

const int bufferLength = 32 * 1024;

#if 0

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>


const char *HOST = "localhost";
const unsigned PORT = 5501;
const int BUFLEN = 256;


class FGFSSocket {
public:
	FGFSSocket(const char *name, unsigned port);
	~FGFSSocket();

	int		write(const char *msg, ...);
	const char	*read(void);
	inline void	flush(void);
	void		settimeout(unsigned t) { _timeout = t; }

private:
	int		close(void);

	int		_sock;
	bool		_connected;
	unsigned	_timeout;
	char		_buffer[BUFLEN];
};


FGFSSocket::FGFSSocket(const char *hostname = HOST, unsigned port = PORT) :
	_sock(-1),
	_connected(false),
	_timeout(1)
{
	_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_sock < 0)
		throw("FGFSSocket/socket");

	struct hostent *hostinfo;
	hostinfo = gethostbyname(hostname);
	if (!hostinfo) {
		close();
		throw("FGFSSocket/gethostbyname: unknown host");
	}

	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr = *(struct in_addr *)hostinfo->h_addr;

	if (connect(_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		close();
		throw("FGFSSocket/connect");
	}
	_connected = true;
	try {
		write("data");
	} catch (...) {
		close();
		throw;
	}
}


FGFSSocket::~FGFSSocket()
{
	close();
}


int FGFSSocket::close(void)
{
	if (_connected)
		write("quit");
	if (_sock < 0)
		return 0;
	int ret = ::close(_sock);
	_sock = -1;
	return ret;
}


int FGFSSocket::write(const char *msg, ...)
{
	va_list va;
	ssize_t len;
	char buf[BUFLEN];
	fd_set fd;
	struct timeval tv;

	FD_ZERO(&fd);
	FD_SET(_sock, &fd);
	tv.tv_sec = _timeout;
	tv.tv_usec = 0;
	if (!select(FD_SETSIZE, 0, &fd, 0, &tv))
		throw("FGFSSocket::write/select: timeout exceeded");

	va_start(va, msg);
	vsnprintf(buf, BUFLEN - 2, msg, va);
	va_end(va);
	std::cout << "SEND: " << buf << std::endl;
	strcat(buf, "\015\012");

	len = ::write(_sock, buf, strlen(buf));
	if (len < 0)
		throw("FGFSSocket::write");
	return len;
}


const char *FGFSSocket::read(void)
{
	char *p;
	fd_set fd;
	struct timeval tv;
	ssize_t len;

	FD_ZERO(&fd);
	FD_SET(_sock, &fd);
	tv.tv_sec = _timeout;
	tv.tv_usec = 0;
	if (!select(FD_SETSIZE, &fd, 0, 0, &tv)) {
		if (_timeout == 0)
			return 0;
		else
			throw("FGFSSocket::read/select: timeout exceeded");
	}

	len = ::read(_sock, _buffer, BUFLEN - 1);
	if (len < 0)
		throw("FGFSSocket::read/read");
	if (len == 0)
		return 0;

	for (p = &_buffer[len - 1]; p >= _buffer; p--)
		if (*p != '\015' && *p != '\012')
			break;
	*++p = '\0';
	return strlen(_buffer) ? _buffer : 0;
}


inline void FGFSSocket::flush(void)
{
	int i = _timeout;
	_timeout = 0;
	while (read())
		;
	_timeout = i;
}

#endif

bool FGFSTelnetSocket::connect(const std::string &host, const int port)
{
    if (_connected) {
        std::cerr << "already connected" << std::endl;
        return false;
    }

    _rawSocket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_rawSocket < 0) {
        std::cerr << "failed to create TCP socket" << std::endl;
        return false;
    }

    struct hostent *hostinfo;
    hostinfo = ::gethostbyname(host.c_str());
    if (!hostinfo) {
        close();
        std::cerr << "gethostbyname failed for " << host << std::endl;
        return false;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

    if (::connect(_rawSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close();
        std::cerr << "connect failed for " << host << std::endl;
        return false;
    }

    _connected = true;
    return write("data");
}

void FGFSTelnetSocket::processReadLines(const std::string& buf, LineHandler handler)
{
    if (buf.empty())
        return;

    bool atEnd = false;
    auto p = 0;
    const auto endPos = buf.size();

    while (!atEnd) {
        auto crlfPos = buf.find("\r\n", p);
        if (crlfPos == -1) {
            // no complete line, asave for later
            _residualBytes = buf.substr(p);
            return;
        }

        std::string msg = buf.substr(p, crlfPos - p);
        handler(msg);

        atEnd = ((crlfPos + 2) == endPos); // does this line terminate the buffer?
        p = crlfPos + 2;
    }
}

bool FGFSTelnetSocket::checkForClose()
{
    // use recv() to check for the socket being closed
	char buffer[32];
	if (recv(_rawSocket, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT) == 0) {
        std::cerr << "saw close of the socket" << std::endl;
        _connected = false;
        close();
        return true;
    }

    return false;
}

bool FGFSTelnetSocket::poll(LineHandler handler, int timeoutMsec)
{
    fd_set readFDs, errorFDs;
    struct timeval tv;

    FD_ZERO(&readFDs);
    FD_SET(_rawSocket, &readFDs);
    FD_ZERO(&errorFDs);
    FD_SET(_rawSocket, &errorFDs);

    int64_t microSecs =  timeoutMsec * 1000;
    tv.tv_sec = microSecs / 1000000;
    tv.tv_usec = microSecs % 1000000;

    int readyFDs = ::select(FD_SETSIZE, &readFDs, nullptr, &errorFDs, &tv);
    if (readyFDs < 0) {
        perror("Select failed doing poll()");
        _connected = false;
        close();
        return false;
    }

    if (readyFDs == 0) {
        checkForClose();
        return true; // timeout
    }

    if (FD_ISSET(_rawSocket, &errorFDs)) {
        std::cerr << "socket error during poll" << std::endl;
        perror("have error socket");
        _connected = false;
        close();
        return false;
    }

    if (FD_ISSET(_rawSocket, &readFDs)) {
        std::string buf;
        buf.resize(bufferLength);

        int len = ::read(_rawSocket, (void*) buf.data(), bufferLength - 1);
        if (len < 0) {
            std::cerr << "reading from socket failed" << std::endl;
            _connected = false;
            close();
            return false;
        }

        buf.resize(len); // trunate to what we actually got
        buf.insert(0, _residualBytes); // prepend our residual
        _residualBytes.clear();
        processReadLines(buf, handler);
    }

    return true;
}

bool FGFSTelnetSocket::isConnected() const
{
    return _connected;
}

void FGFSTelnetSocket::close()
{
    if (_connected) {
        write("quit");
        _connected = false;
    }

    if (_rawSocket != -1) {
        if (::close(_rawSocket) != 0) {
            perror("Closing socket failed");
        }
        _rawSocket = -1;
    }
}

void FGFSTelnetSocket::subscribe(const std::string &path)
{
    write("subscribe " + path);

}

void FGFSTelnetSocket::set(const std::string &path, const std::string &value)
{
    write("set " + path + " " + value);
}

bool FGFSTelnetSocket::syncGetDouble(const std::string &path, double& result)
{
    write("get " + path);
    bool ok = false;

    poll([&result, &ok](const std::string& line) {
        result = std::stod(line);
        ok = true;
    }, 1000);
    return ok;
}

bool FGFSTelnetSocket::syncGetBool(const std::string& path, bool& result)
{
    write("get " + path);
    bool ok = false;

    poll([&result, &ok](const std::string& line) {
        result = (line == "true") || (line == "1");
        ok = true;
    }, 1000);
    

    return ok;
}


bool FGFSTelnetSocket::write(const std::string &msg)
{
    char buf[bufferLength];
    fd_set fd;
    struct timeval tv;

    FD_ZERO(&fd);
    FD_SET(_rawSocket, &fd);
    tv.tv_sec = 0;
    tv.tv_usec = _timeoutMsec * 1000;
    if (!::select(FD_SETSIZE, 0, &fd, 0, &tv)) {
        perror("Selected failed doing write");
        return false;
    }

    strcpy(buf, msg.c_str());
    strcat(buf, "\015\012");
    int len = ::write(_rawSocket, buf, strlen(buf));
    if (len < 0) {
        perror("socket write failed, closing");
        _connected = false; // don't re-enter write()
        close();
        return false;
    }

    return true;
}
