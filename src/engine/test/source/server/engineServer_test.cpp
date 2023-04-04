
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <rxcpp/rx.hpp>

#include <uvw/pipe.hpp>
#include <uvw/tcp.hpp>
#include <uvw/udp.hpp>

//#include "engineServer.hpp"

//using namespace engineserver;
using namespace std;
using namespace rxcpp;

#define GTEST_COUT cerr << "[          ] [ INFO ]"

TEST(ServerTest, InitializesTcp)
{
    //vector<string> config = {"tcp:localhost:5054"};
    //ASSERT_NO_THROW(EngineServer server(config));
    GTEST_SKIP();
}

// TEST(ServerTest, InitializesUdp)
// {
//     vector<string> config = {"udp:localhost:5054"};
//     ASSERT_NO_THROW(EngineServer server(config));
// }

// TEST(ServerTest, InitializesSocket)
// {
//     vector<string> config = {"socket:/tmp/testsocket"};
//     ASSERT_NO_THROW(EngineServer server(config));
// }

// TEST(ServerTest, InitializesErrorEndpointType)
// {
//     vector<string> config = {"error:localhost:5054"};
//     ASSERT_THROW(EngineServer server(config), invalid_argument);
// }

// TEST(ServerTest, RunStopTcp)
// {
//     vector<string> config = {"tcp:localhost:5054"};
//     EngineServer server(config);
//     ASSERT_NO_THROW(server.run());
//     // Give time to initialize before closing
//     this_thread::sleep_for(chrono::milliseconds(5));
//     ASSERT_NO_THROW(server.close());
// }

// TEST(ServerTest, RunStopUdp)
// {
//     vector<string> config = {"udp:localhost:5054"};
//     EngineServer server(config);
//     ASSERT_NO_THROW(server.run());
//     // Give time to initialize before closing
//     this_thread::sleep_for(chrono::milliseconds(5));
//     ASSERT_NO_THROW(server.close());
// }

// TEST(ServerTest, RunStopSocket)
// {
//     vector<string> config = {"socket:/tmp/testsocket"};
//     EngineServer server(config);
//     ASSERT_NO_THROW(server.run());
//     // Give time to initialize before closing
//     this_thread::sleep_for(chrono::milliseconds(5));
//     ASSERT_NO_THROW(server.close());
// }

// TEST(ServerTest, RunStopTcpUdpSocket)
// {
//     vector<string> config = {"socket:/tmp/testsocket", "udp:localhost:5054", "tcp:localhost:5054"};
//     EngineServer server(config);
//     ASSERT_NO_THROW(server.run());
//     // Give time to initialize before closing
//     this_thread::sleep_for(chrono::milliseconds(5));
//     ASSERT_NO_THROW(server.close());
// }
