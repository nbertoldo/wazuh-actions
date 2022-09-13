/* Copyright (C) 2015-2021, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#ifndef _OP_BUILDER_WDB_SYNC_H
#define _OP_BUILDER_WDB_SYNC_H

#include <any>

#include <baseTypes.hpp>

#include "expression.hpp"
#include <utils/stringUtils.hpp>
#include <wdb/wdb.hpp>

namespace builder::internals::builders
{

/**
 * @brief Executes query on WDB returning the status.
 * @param definition The filter definition.
 * @return base::Expression true when executes without any problem, false otherwise.
 */
base::Expression opBuilderWdbUpdate(const std::any& definition);

/**
 * @brief Executes query on WDB returning the payload.
 * @param definition The filter definition.
 * @param tr Tracer
 * @return base::Expression when true returns string of payload, false none.
 */
base::Expression opBuilderWdbQuery(const std::any& definition);

} // namespace builder::internals::builders

#endif // _OP_BUILDER_WDB_SYNC_H
