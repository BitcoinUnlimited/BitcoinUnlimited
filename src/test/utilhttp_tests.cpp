// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilhttp.h"
#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <iostream>

template <typename T>
class RAII
{
public:
    RAII(T *o, std::function<void(T *)> d) : obj(o), destroy(d) { assert(o != nullptr); }
    ~RAII() { destroy(obj); }
    T *get() { return obj; }
private:
    T *obj;
    std::function<void(T *)> destroy;
};

BOOST_FIXTURE_TEST_SUITE(utilhttp_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(http_get_test)
{
    const char HOST[] = "127.0.0.1";
    const int PORT = 23456;
    auto server_response = [](struct evhttp_request *req, void *) -> void {
        if (evhttp_request_get_command(req) != EVHTTP_REQ_GET)
        {
            evhttp_send_error(req, 400, "not a GET request");
        }
        RAII<struct evbuffer> buffer(evbuffer_new(), evbuffer_free);
        evbuffer_add(buffer.get(), "magic", std::string("magic").size());
        evhttp_send_reply(req, 200, "OK", buffer.get());
    };

    // bind a http server to HOST:PORT
    RAII<struct event_base> base(event_base_new(), event_base_free);
    RAII<struct evhttp> server(evhttp_new(base.get()), evhttp_free);
    evhttp_set_gencb(server.get(), server_response, nullptr);

    if (evhttp_bind_socket(server.get(), HOST, PORT) != 0)
    {
        std::cerr << __func__ << ": could not bind to " << HOST << ":" << PORT << ", skipping test" << std::endl;
        return;
    }

    std::atomic<bool> done(false);
    std::thread dispatch_thread([&]() {
        while (!done)
        {
            event_base_loop(base.get(), EVLOOP_NONBLOCK);
        }
    });

    // test http_get
    std::string result;
    BOOST_CHECK_NO_THROW(result = http_get(HOST, PORT, "/").str());
    BOOST_CHECK_EQUAL("magic", result);

    done = true;
    dispatch_thread.join();
}

BOOST_AUTO_TEST_SUITE_END()
