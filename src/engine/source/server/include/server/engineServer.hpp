#ifndef _SERVER_SERVER_H
#define _SERVER_SERVER_H

#include <functional>
#include <memory>
#include <string>

namespace engineserver
{

class EngineServer
{
public:
    EngineServer();
    ~EngineServer();

    // No response is expected
    void addEndpoint_UnixDatagram_woResponse(const std::string& address, std::function<void(std::string&&)> callback);

    void start();
    void request_stop();

private:
    void stop();

    class Impl;                  // Declaración adelantada
    std::unique_ptr<Impl> pimpl; // Puntero único a la implementación
};

} // namespace server

#endif // _SERVER_SERVER_H
