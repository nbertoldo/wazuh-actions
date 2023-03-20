#include <server/engineServer.hpp>

#include <cstring>      // Unix  socket datagram bind
#include <fcntl.h>      // Unix socket datagram bind
#include <sys/socket.h> // Unix socket datagram bind
#include <sys/un.h>     // Unix socket datagram bind
#include <unistd.h>     // Unix socket datagram bind

#include <exception>

#include <logging/logging.hpp>
#include <uvw.hpp>
#include <uvw/async.hpp>
namespace engineserver
{

class EngineServer::Impl
{
public:
    std::shared_ptr<uvw::AsyncHandle> m_stopHandle;
    std::shared_ptr<uvw::Loop> m_loop;
};

EngineServer::EngineServer()
    : pimpl(std::make_unique<Impl>())
{

    pimpl->m_loop = uvw::Loop::getDefault();
    pimpl->m_stopHandle = pimpl->m_loop->resource<uvw::AsyncHandle>();
    pimpl->m_stopHandle->on<uvw::AsyncEvent>([this](const uvw::AsyncEvent&, uvw::AsyncHandle&) { this->stop(); });
}

EngineServer::~EngineServer() = default;

void EngineServer::start()
{
    pimpl->m_loop->run();
}

void EngineServer::stop()
{
    // Get the default loop
    auto loop = pimpl->m_loop;

    // Closes all the handles
    loop->walk([](auto& handle) { handle.close(); });
    loop->stop();
}

void EngineServer::request_stop()
{
    // Send the stop request
    pimpl->m_stopHandle->send();
}

/*************************************************** DATAGRAM SOCKET **************************************************/
namespace
{

constexpr unsigned int MAX_MSG_SIZE {65536 + 512}; ///< Maximum message size (TODO: I think this should be 65507)

/**
 * @brief This function opens, binds and configures a Unix datagram socket.
 * @param path Contains the absolute path to the Unix datagram socket. The path must be less than 108 bytes.
 * @return Returns either the file descriptor value
 * @throw std::runtime_error if the path is too long or the socket cannot be created or bound.
 */
inline int bindUnixDatagramSocket(const std::string& path)
{
    sockaddr_un n_us;

    // Check the path length
    if (path.length() >= sizeof(n_us.sun_path))
    {
        auto msg = fmt::format("Path '{}' too long, maximum length is {} ", path, sizeof(n_us.sun_path));
        throw std::runtime_error(std::move(msg));
    }

    // Remove the socket file if it already exists
    unlink(path.c_str());

    memset(&n_us, 0, sizeof(n_us));
    n_us.sun_family = AF_UNIX;
    strncpy(n_us.sun_path, path.c_str(), sizeof(n_us.sun_path) - 1);

    const int socketFd {socket(PF_UNIX, SOCK_DGRAM, 0)};
    if (0 > socketFd)
    {
        auto msg = fmt::format("Cannot create the socket '{}': {} ({})", path, strerror(errno), errno);
        throw std::runtime_error(std::move(msg));
    }

    if (bind(socketFd, reinterpret_cast<sockaddr*>(&n_us), SUN_LEN(&n_us)) < 0)
    {

        auto msg = fmt::format("Cannot bind the socket '{}': {} ({})", path, strerror(errno), errno);
        close(socketFd);
        throw std::runtime_error(std::move(msg));
    }

    // Change permissions
    if (chmod(path.c_str(), 0660) < 0) // TODO: Save the permissions in a constant
    {
        auto msg = fmt::format("Cannot change permissions of the socket '{}': {} ({})", path, strerror(errno), errno);
        close(socketFd);
        throw std::runtime_error(std::move(msg));
    }

    int len;
    socklen_t optlen {sizeof(len)};

    // Get current maximum size
    if (-1 == getsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<void*>(&len), &optlen))
    {
        len = 0;
    }

    // Set maximum message size
    if (MAX_MSG_SIZE > len)
    {
        len = MAX_MSG_SIZE;
        if (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const void*>(&len), optlen) < 0)
        {
            auto msg = fmt::format(
                "Cannot set maximum message size of the socket '{}': {} ({})", path, strerror(errno), errno);
            close(socketFd);
            throw std::runtime_error(std::move(msg));
        }
    }

    // Set close-on-exec
    if (-1 == fcntl(socketFd, F_SETFD, FD_CLOEXEC))
    {
        WAZUH_LOG_WARN("Cannot set close-on-exec flag to socket: {} ({})", strerror(errno), errno);
    }

    return socketFd;
}
} // namespace

void EngineServer::addEndpoint_UnixDatagram_woResponse(const std::string& address,
                                                       std::function<void(std::string&&)> callback) // packet proccessor
{
    // Get the default loop
    auto loop = pimpl->m_loop;

    // Create a new handle (Same as uv_udp_init for UDP handles)
    auto handle = loop->resource<uvw::UDPHandle>();

    // Listen for incoming data
    handle->on<uvw::UDPDataEvent>(
        [callback](const uvw::UDPDataEvent& event, uvw::UDPHandle& handle)
        {
            // Get the data
            auto data = std::string {event.data.get(), event.length};
            // Call the callback
            callback(std::move(data));
        });

    // Listen for errors
    handle->on<uvw::ErrorEvent>(
        [address](const uvw::ErrorEvent& event, uvw::UDPHandle& handle)
        {
            // Log the error
            WAZUH_LOG_WARN("Engine event endpoints: Event error on datagram socket "
                           "({}): code=[{}]; name=[{}]; message=[{}].",
                           address,
                           event.code(),
                           event.name(),
                           event.what());
        });

    // Bind the socket
    auto socketFd = bindUnixDatagramSocket(address);
    handle->open(socketFd);
    handle->recv();
    // Clean the socket file on exit (TODO)
}
/************************************************ END DATAGRAM SOCKET *************************************************/

} // namespace server
