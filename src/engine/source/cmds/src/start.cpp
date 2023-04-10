#include "cmds/start.hpp"

#include <atomic>
#include <csignal>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <api/api.hpp>
#include <api/catalog/catalog.hpp>
#include <api/catalog/handlers.hpp>
#include <api/config/config.hpp>
#include <api/kvdb/handlers.hpp>
#include <api/router/handlers.hpp>
#include <builder/builder.hpp>
#include <builder/register.hpp>
#include <cmds/details/stackExecutor.hpp>
#include <hlp/logpar.hpp>
#include <hlp/registerParsers.hpp>
#include <kvdb/kvdbManager.hpp>
#include <logging/logging.hpp>
#include <parseEvent.hpp> // Event
#include <router/router.hpp>
#include <rxbk/rxFactory.hpp>
#include <server/endpoints/unixDatagram.hpp> // Event
#include <server/endpoints/unixStream.hpp>   //API
#include <server/engineServer.hpp>
#include <server/protocolHandlers/wStream.hpp> //API
#include <store/drivers/fileDriver.hpp>

#include "base/utils/getExceptionStack.hpp"
#include "defaultSettings.hpp"
#include "register.hpp"
#include "registry.hpp"

namespace
{
cmd::details::StackExecutor g_exitHanlder {};

void sigint_handler(const int signum)
{
    g_exitHanlder.execute();
}

struct Options
{
    // Server
    int serverThreads;
    std::string serverEventSock;
    int serverEventQueueSize;
    std::string serverApiSock;
    int serverApiQueueSize;
    int serverApiTimeout;
    // Store
    std::string fileStorage;
    // KVDB
    std::string kvdbPath;
    // Router
    std::vector<std::string> environment;
    int routerThreads;
    bool forceRouterArg;
    // Queue
    int queueSize;
    std::string queueFloodFile;
    int queueFloodAttempts;
    int queueFloodSleep;
    // Loggin
    int logLevel;
    std::string logOutput;
};

} // namespace

namespace cmd::server
{
void runStart(ConfHandler confManager)
{
    // Get needed configuration on main function
    const auto confPath = confManager->get<std::string>("config");

    // Log config
    const auto logLevel = confManager->get<int>("server.log_level");
    const auto logOutput = confManager->get<std::string>("server.log_output");

    // Server config
    const auto serverThreads = confManager->get<int>("server.server_threads");
    const auto serverEventSock = confManager->get<std::string>("server.event_socket");
    const auto serverEventQueueSize = confManager->get<int>("server.event_queue_tasks");
    const auto serverApiSock = confManager->get<std::string>("server.api_socket");
    const auto serverApiQueueSize = confManager->get<int>("server.api_queue_tasks");
    const auto serverApiTimeout = confManager->get<int>("server.api_timeout");

    // Store config
    const auto fileStorage = confManager->get<std::string>("server.store_path");

    // KVDB config
    const auto kvdbPath = confManager->get<std::string>("server.kvdb_path");

    // Router Config
    const auto routerThreads = confManager->get<int>("server.router_threads");

    // Queue config
    const auto queueSize = confManager->get<int>("server.queue_size");
    const auto queueFloodFile = confManager->get<std::string>("server.queue_flood_file");
    const auto queueFloodAttempts = confManager->get<int>("server.queue_flood_attempts");
    const auto queueFloodSleep = confManager->get<int>("server.queue_flood_sleep");

    // Start environment
    const auto environment = confManager->get<std::vector<std::string>>("server.start.environment");
    const auto routeName = environment[0];
    int routePriority;
    try
    {
        routePriority = std::stoi(environment[1]);
    }
    catch (const std::exception& e)
    {
        WAZUH_LOG_ERROR("Invalid route priority '{}'", environment[1]);
        exit(EXIT_FAILURE); // TODO Change whens add the LOG_CRITICAL / LOG_FATAL
    }
    const auto routeFilter = environment[2];
    const auto routeEnvironment = environment[3];
    const auto forceRouterArg = confManager->get<bool>("server.start.force_router_arg");

    // Set Crt+C handler
    {
        // Set the signal handler for SIGINT
        struct sigaction sigIntHandler;
        sigIntHandler.sa_handler = sigint_handler;
        sigemptyset(&sigIntHandler.sa_mask);
        sigIntHandler.sa_flags = 0;
        sigaction(SIGINT, &sigIntHandler, nullptr);
    }

    // Init logging
    logging::LoggingConfig logConfig;
    switch (logLevel)
    {
        case 0: logConfig.logLevel = logging::LogLevel::Debug; break;
        case 1: logConfig.logLevel = logging::LogLevel::Info; break;
        case 2: logConfig.logLevel = logging::LogLevel::Warn; break;
        case 3: logConfig.logLevel = logging::LogLevel::Error; break;
        default: logging::LogLevel::Error;
    }

    if (logging::LogLevel::Debug != logConfig.logLevel)
    {
        logConfig.header = "{YmdHMSe} {t} {l}: ";
    }

    logConfig.filePath = logOutput.c_str();
    logging::loggingInit(logConfig);
    g_exitHanlder.add([]() { logging::loggingTerm(); });
    WAZUH_LOG_INFO("Logging initialized");
    // WAZUH_LOG_DEBUG("Log output in '{}'", logConfig.filePath);
    WAZUH_LOG_DEBUG("Logging poll interval '{}'", logConfig.pollInterval);

    // Init modules
    std::shared_ptr<api::Api> api;
    std::shared_ptr<engineserver::EngineServer> server;
    std::shared_ptr<store::FileDriver> store;
    std::shared_ptr<builder::Builder> builder;
    std::shared_ptr<api::catalog::Catalog> catalog;
    std::shared_ptr<router::Router> router;
    std::shared_ptr<hlp::logpar::Logpar> logpar;
    std::shared_ptr<kvdb_manager::KVDBManager> kvdb;
    std::shared_ptr<base::queue::ConcurrentQueue<base::Event>> eventQueue;

    try
    {
        // API
        {
            api = std::make_shared<api::Api>();
            WAZUH_LOG_DEBUG("API created.");
        }

        // Queue
        {
            eventQueue = std::make_shared<base::queue::ConcurrentQueue<base::Event>>(
                queueSize, queueFloodFile, queueFloodAttempts, queueFloodSleep);
            WAZUH_LOG_DEBUG("Event queue created.");
        }

        // KVDB
        {
            kvdb = std::make_shared<kvdb_manager::KVDBManager>(kvdbPath);
            WAZUH_LOG_INFO("KVDB initialized.");
            g_exitHanlder.add(
                [kvdb]()
                {
                    WAZUH_LOG_INFO("KVDB terminated.");
                    kvdb->clear();
                });

            api::kvdb::handlers::registerHandlers(kvdb, api);
            WAZUH_LOG_DEBUG("KVDB API registered.")
        }

        // Store
        {
            store = std::make_shared<store::FileDriver>(fileStorage);
            WAZUH_LOG_INFO("Store initialized.");
        }

        // HLP
        {
            base::Name hlpConfigFileName({"schema", "wazuh-logpar-types", "0"});
            auto hlpParsers = store->get(hlpConfigFileName);
            if (std::holds_alternative<base::Error>(hlpParsers))
            {
                WAZUH_LOG_ERROR("Could not retreive configuration file [{}] needed by the "
                                "HLP module, error: {}",
                                hlpConfigFileName.fullName(),
                                std::get<base::Error>(hlpParsers).message);

                g_exitHanlder.execute();
                return;
            }
            logpar = std::make_shared<hlp::logpar::Logpar>(std::get<json::Json>(hlpParsers));
            hlp::registerParsers(logpar);
            WAZUH_LOG_INFO("HLP initialized.");
        }

        // Builder and registry
        {
            auto registry = std::make_shared<builder::internals::Registry>();
            builder::internals::registerBuilders(registry, {0, logpar, kvdb});
            WAZUH_LOG_DEBUG("Builders registered.");

            builder = std::make_shared<builder::Builder>(store, registry);
            WAZUH_LOG_INFO("Builder initialized.");
        }

        // Catalog
        {
            api::catalog::Config catalogConfig {
                store,
                builder,
                fmt::format("schema{}wazuh-asset{}0", base::Name::SEPARATOR_S, base::Name::SEPARATOR_S),
                fmt::format("schema{}wazuh-environment{}0", base::Name::SEPARATOR_S, base::Name::SEPARATOR_S)};

            catalog = std::make_shared<api::catalog::Catalog>(catalogConfig);
            WAZUH_LOG_INFO("Catalog initialized.");

            api::catalog::handlers::registerHandlers(catalog, api);
            WAZUH_LOG_DEBUG("Catalog API registered.")
        }

        // Router
        {
            router = std::make_shared<router::Router>(builder, store, routerThreads);

            router->run(eventQueue);
            g_exitHanlder.add([router]() { router->stop(); });
            WAZUH_LOG_INFO("Router initialized.");

            // Register the API command
            api::router::handlers::registerHandlers(router, api);
            WAZUH_LOG_DEBUG("Router API registered.")

            // If the router table is empty or the force flag is passed, load from the command line
            if (router->getRouteTable().empty())
            {
                router->addRoute(routeName, routePriority, routeFilter, routeEnvironment);
            }
            else if (forceRouterArg)
            {
                router->clear();
                router->addRoute(routeName, routePriority, routeFilter, routeEnvironment);
            }
        }

        // Configuration manager
        api::config::handlers::registerHandlers(api, confManager);
        WAZUH_LOG_DEBUG("Configuration manager API registered.");

        // Server
        {
            using namespace engineserver;
            server = std::make_shared<EngineServer>();
            g_exitHanlder.add([server]() { server->request_stop(); });

            // API Endpoint
            auto apiHandler = std::bind(&api::Api::processRequest, api, std::placeholders::_1);
            auto apiClientFactory = std::make_shared<ph::WStreamFactory>(apiHandler); // API endpoint
            apiClientFactory->setErrorResponse(base::utils::wazuhProtocol::WazuhResponse::unknownError().toString());
            apiClientFactory->setBusyResponse(base::utils::wazuhProtocol::WazuhResponse::busyServer().toString());

            auto apiEndpointCfg = std::make_shared<endpoint::UnixStream>(
                serverApiSock, apiClientFactory, serverApiQueueSize, serverApiTimeout);
            server->addEndpoint("API", apiEndpointCfg);

            // Event Endpoint
            auto eventHandler = std::bind(&router::Router::fastEnqueueEvent, router, std::placeholders::_1);
            auto eventEndpointCfg =
                std::make_shared<endpoint::UnixDatagram>(serverEventSock, eventHandler, serverEventQueueSize);
            server->addEndpoint("EVENT", eventEndpointCfg);
            WAZUH_LOG_DEBUG("Server configured.");
        }
    }
    catch (const std::exception& e)
    {
        const auto msg = utils::getExceptionStack(e);
        WAZUH_LOG_ERROR("While initializing modules: {}", msg);
        g_exitHanlder.execute();
        return;
    }

    // Start server
    try
    {
        server->start();
    }
    catch (const std::exception& e)
    {
        WAZUH_LOG_ERROR("While server running: {}.", utils::getExceptionStack(e));
        g_exitHanlder.execute();
        return;
    }
    g_exitHanlder.execute();
}

void configure(CLI::App_p app)
{
    auto serverApp = app->add_subcommand("server", "Start/Stop a Wazuh engine instance.");
    serverApp->require_subcommand(1);
    auto options = std::make_shared<Options>();

    // Loggin module
    serverApp
        ->add_option(
            "--log_level", options->logLevel, "Sets the logging level: 0 = Debug, 1 = Info, 2 = Warning, 3 = Error")
        ->default_val(ENGINE_LOG_LEVEL)
        ->check(CLI::Range(0, 3))
        ->envname(ENGINE_LOG_LEVEL_ENV);

    serverApp->add_option("--log_output", options->logOutput, "Sets the logging output")
        ->default_val(ENGINE_LOG_OUTPUT)
        ->envname(ENGINE_LOG_OUTPUT_ENV);

    // Server module
    serverApp
        ->add_option("--server_threads", options->serverThreads, "Sets the number of threads for server worker pool.")
        ->default_val(ENGINE_SRV_PULL_THREADS)
        ->check(CLI::Range(1, 128))
        ->envname(ENGINE_SRV_PULL_THREADS_ENV);
    serverApp->add_option("--event_socket", options->serverEventSock, "Sets the events server socket address.")
        ->default_val(ENGINE_SRV_EVENT_SOCK)
        ->envname(ENGINE_SRV_EVENT_SOCK_ENV);
    serverApp
        ->add_option("--event_queue_tasks",
                     options->serverEventQueueSize,
                     "Sets the size of the event task queue of the server (0 = disable, process asynchonously).")
        ->default_val(ENGINE_SRV_EVENT_QUEUE_TASK)
        ->check(CLI::NonNegativeNumber)
        ->envname(ENGINE_SRV_EVENT_QUEUE_TASK_ENV);
    serverApp->add_option("--api_socket", options->serverApiSock, "Sets the API server socket address.")
        ->default_val(ENGINE_SRV_API_SOCK)
        ->envname(ENGINE_SRV_API_SOCK_ENV);
    serverApp
        ->add_option("--api_queue_tasks",
                     options->serverApiQueueSize,
                     "Sets the size of the API task queue of the server. (0 = disable, process asynchonously).")
        ->default_val(ENGINE_SRV_API_QUEUE_TASK)
        ->check(CLI::NonNegativeNumber)
        ->envname(ENGINE_SRV_API_QUEUE_TASK_ENV);
    serverApp
        ->add_option("--api_timeout", options->serverApiTimeout, "Sets the timeout for the API socket in miliseconds.")
        ->default_val(ENGINE_SRV_API_TIMEOUT)
        ->check(CLI::NonNegativeNumber)
        ->envname(ENGINE_SRV_API_TIMEOUT_ENV);

    // Store Module
    serverApp
        ->add_option(
            "--store_path", options->fileStorage, "Sets the path to the folder where the assets are located (store).")
        ->default_val(ENGINE_STORE_PATH)
        ->check(CLI::ExistingDirectory)
        ->envname(ENGINE_STORE_PATH_ENV);

    // KVDB Module
    serverApp->add_option("--kvdb_path", options->kvdbPath, "Sets the path to the KVDB folder.")
        ->default_val(ENGINE_KVDB_PATH)
        ->check(CLI::ExistingDirectory)
        ->envname(ENGINE_KVDB_PATH_ENV);

    // Router module
    serverApp
        ->add_option("--router_threads", options->routerThreads, "Sets the number of threads to be used by the router.")
        ->default_val(ENGINE_ROUTER_THREADS)
        ->check(CLI::PositiveNumber)
        ->envname(ENGINE_ROUTER_THREADS_ENV);

    // Queue module
    serverApp
        ->add_option(
            "--queue_size", options->queueSize, "Sets the number of events that can be queued to be processed.")
        ->default_val(ENGINE_QUEUE_SIZE)
        ->check(CLI::PositiveNumber)
        ->envname(ENGINE_QUEUE_SIZE_ENV);

    serverApp
        ->add_option("--queue_flood_file",
                     options->queueFloodFile,
                     "Sets the path to the file where the flood events will be stored.")
        ->default_val(ENGINE_QUEUE_FLOOD_FILE)
        ->envname(ENGINE_QUEUE_FLOOD_FILE_ENV);

    serverApp
        ->add_option("--queue_flood_attempts",
                     options->queueFloodAttempts,
                     "Sets the number of attempts to try to push an event to the queue.")
        ->default_val(ENGINE_QUEUE_FLOOD_ATTEMPTS)
        ->check(CLI::PositiveNumber)
        ->envname(ENGINE_QUEUE_FLOOD_ATTEMPTS_ENV);

    serverApp
        ->add_option("--queue_flood_sleep",
                     options->queueFloodSleep,
                     "Sets the number of microseconds to sleep between attempts to push an event to the queue.")
        ->default_val(ENGINE_QUEUE_FLOOD_SLEEP)
        ->check(CLI::PositiveNumber)
        ->envname(ENGINE_QUEUE_FLOOD_SLEEP_ENV);

    // Start subcommand
    // Router module
    auto startApp = serverApp->add_subcommand("start", "Start a Wazuh engine instance");
    startApp
        ->add_option("--environment",
                     options->environment,
                     "Sets the environment to be used the first time an engine instance is started.")
        ->default_val(ENGINE_ENVIRONMENT)
        ->expected(4)
        ->delimiter(':')
        ->envname(ENGINE_ENVIRONMENT_ENV);
    startApp
        ->add_flag("--force_router_arg",
                   options->forceRouterArg,
                   "Use the router parameter, even if there is previous configuration.")
        ->default_val(false);

    // Register callback
    startApp->callback(
        [app, options]()
        {
            auto confManager = std::make_shared<conf::IConf<conf::CliConf>>(conf::CliConf(app));
            runStart(confManager);
        });
}
} // namespace cmd::server
