#ifndef _EVENT_ENDPOINT_H
#define _EVENT_ENDPOINT_H

#include <uvw/udp.hpp>

#include "baseEndpoint.hpp"
#include <baseTypes.hpp>

namespace engineserver::endpoints
{

/**
 * @brief Implements Unix Datagram Socket endpoint by using uvw library.
 *
 */
class EventEndpoint : public BaseEndpoint
{
protected:
    using DatagramSocketEvent = uvw::UDPDataEvent;
    using DatagramSocketHandle = uvw::UDPHandle;
    using concurrentQueue = moodycamel::BlockingConcurrentQueue<base::Event>;

private:
    int m_socketFd;

    std::shared_ptr<uvw::Loop> m_loop;
    std::shared_ptr<DatagramSocketHandle> m_handle;
    std::shared_ptr<concurrentQueue> m_eventQueue;

public:
    /**
     * @brief Construct a new EventEndpoint object.
     *
     * @param path (std::string) Absolute path to the datagram socket.
     * @param eventBuffer (ServerOutput) Reference to the event queue.
     */
    explicit EventEndpoint(const std::string& path,
                           std::shared_ptr<concurrentQueue> eventQueue,
                           std::optional<std::string> pathFloodedFile = std::nullopt);

    ~EventEndpoint();

    void run(void);

    void configure() override;

    void close(void);

    std::shared_ptr<concurrentQueue> getEventQueue() const;
};

} // namespace engineserver::endpoints

#endif // _EVENT_ENDPOINT_H
