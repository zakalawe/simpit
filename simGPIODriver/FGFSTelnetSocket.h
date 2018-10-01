#ifndef FGFS_TELNET_SOCKET_H
#define FGFS_TELNET_SOCKET_H

#include <string>
#include <functional>

class FGFSTelnetSocket
{
public:
    bool connect(const std::string& host, const int port);

    using LineHandler = std::function<void(std::string)>;

    bool poll(LineHandler handler, int timeoutMsec = 0);

    bool isConnected() const;

    void close();

    void subscribe(const std::string& path);

    void set(const std::string& path, const std::string& value);

    bool syncGetDouble(const std::string& path, double& result);


    void processReadLines(const std::string& buf, LineHandler handler);

    bool write(const std::string& msg);
private:
    bool checkForClose();

    int _rawSocket = -1;
    bool _connected = false;
    uint32_t _timeoutMsec = 100;
    std::string _residualBytes;
};

#endif
