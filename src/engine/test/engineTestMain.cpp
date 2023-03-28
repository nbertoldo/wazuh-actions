#include <gtest/gtest.h>

#include <kvdb/kvdbManager.hpp>
#include <logging/logging.hpp>

int main(int argc, char** argv)
{
    logging::LoggingConfig logConfig;
    logConfig.logLevel = "off";
    logging::loggingInit(logConfig);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
