/* Copyright (C) 2015-2022, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#ifndef _ENGINE_SERVER_H
#define _ENGINE_SERVER_H

#include <map>
#include <string>

#include <api/api.hpp>
#include <baseTypes.hpp>
#include "../../src/endpoints/baseEndpoint.hpp"

/**
 * @brief Defines all related server functionality.
 *
 */
namespace engineserver
{
// (ceil(N / BLOCK_SIZE) - 1 + 2 * MAX_NUM_PRODUCERS) * BLOCK_SIZE
constexpr uint32_t DEFAULT_QUEUE_SIZE = 1024;


/**
 * @brief Class that handles all endpoints and exposes Server functionality.
 *
 */
class EngineServer
{
private:
    std::unordered_map<EndpointType, std::shared_ptr<endpoints::BaseEndpoint>>
        m_endpoints;

public:
    /**
     * @brief Construct a new Engine Server object
     *
     * @param config <type>:<path> string describing endpoint type with it
     * associated configuration.
     *
     * @param bufferSize Events queue buffer size.
     */
    explicit EngineServer(const std::string& apiEndpointPath,
                          std::shared_ptr<api::Registry> registry,
                          const std::string& eventEndpointPath,
                          std::optional<std::string> pathFloodedFile,
                          const int bufferSize);

    /**
     * @brief Start server.
     *
     */
    void run(void);

    /**
     * @brief Close and liberate resources used by server.
     *
     */
    void close(void);

    /**
     * @brief Get server output queue
     *
     * @return std::shared_ptr<concurrentQueue>
     */
    std::shared_ptr<base::queue::ConcurrentQueue<base::Event>>
    getEventQueue() const;

    /**
     * @brief Get the API Registry asociated with the server
     *
     * @return The shared pointer to the API Registry
     */
    std::shared_ptr<api::Registry> getRegistry() const;
};

} // namespace engineserver

#endif // _ENGINE_SERVER_H
