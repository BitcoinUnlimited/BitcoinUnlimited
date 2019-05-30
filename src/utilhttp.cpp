// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilhttp.h"
#include "util.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>

// RAII object to clean up after libevent
struct EventRAII
{
    struct event_base *base = nullptr;
    struct evhttp_connection *http_conn = nullptr;
    struct evhttp_request *req = nullptr;

    ~EventRAII()
    {
        if (http_conn == nullptr && req != nullptr)
        {
            // http_conn takes ownership of req, so only release if http_conn
            // failed to initialize.
            evhttp_request_free(req);
            req = nullptr;
        }
        if (http_conn != nullptr)
        {
            evhttp_connection_free(http_conn);
            http_conn = nullptr;
        }
        if (base != nullptr)
        {
            event_base_free(base);
            base = nullptr;
        }
    }
};

namespace
{
static const int CB_UNKNOWN_ERROR = -1;
}

// The callback function writes the response to this.
struct CallbackArgs
{
    int &http_code;
    std::stringstream &res;
};

// Callback for fetching the http response.
void response_cb(struct evhttp_request *req, void *args_)
{
    CallbackArgs *args(static_cast<CallbackArgs *>(args_));
    if (req == nullptr)
    {
        args->http_code = CB_UNKNOWN_ERROR;
        return;
    }
    args->http_code = evhttp_request_get_response_code(req);
    char buffer[1024];
    int s = 0;
    do
    {
        // evbuffer_remove should not have written more than the size
        // of the buffer we gave it
        DbgAssert(static_cast<size_t>(s) <= sizeof buffer, return );

        args->res.write(buffer, s);
        s = evbuffer_remove(req->input_buffer, &buffer, sizeof buffer);
    } while (s > 0);
};


std::stringstream http_get(const std::string &host, const int port, const std::string &target)
{
    EventRAII event;
    std::stringstream res;
    int http_code = CB_UNKNOWN_ERROR;
    CallbackArgs cbargs{http_code, res};

    // Initiate request
    event.base = event_base_new();
    if (event.base == nullptr)
    {
        throw std::runtime_error("event_base_new");
    }

    event.http_conn = evhttp_connection_base_new(event.base, nullptr, host.c_str(), port);
    if (event.http_conn == nullptr)
    {
        throw std::runtime_error("evhttp_connection_base_new");
    }

    event.req = evhttp_request_new(response_cb, static_cast<void *>(&cbargs));
    if (event.req == nullptr)
    {
        throw std::runtime_error("evhttp_request_new");
    }

    struct evkeyvalq *output_headers = evhttp_request_get_output_headers(event.req);
    if (output_headers == nullptr)
    {
        throw std::runtime_error("evhttp_request_get_output_headers");
    }
    if (evhttp_add_header(output_headers, "Host", host.c_str()) != 0)
    {
        throw std::runtime_error("evhttp_add_header failed");
    }
    if (evhttp_add_header(output_headers, "Connection", "close") != 0)
    {
        throw std::runtime_error("evhttp_add_header failed");
    }

    // Perform request
    int rc = evhttp_make_request(event.http_conn, event.req, EVHTTP_REQ_GET, target.c_str());
    if (rc != 0)
    {
        throw std::runtime_error("evhttp_make_request failed");
    }
    rc = event_base_dispatch(event.base);

    // Check response
    switch (rc)
    {
    case -1:
        throw std::runtime_error("event_base_dispatch failed");
    case 0: // OK
    case 1: // No events pending -- OK
        break;
    default:
        throw std::runtime_error("unknown rc from event_base_dispatch");
    }
    if (http_code == 0)
    {
        throw std::runtime_error("http_get failed (invalid host/port?)");
    }
    if (http_code == CB_UNKNOWN_ERROR)
    {
        throw std::runtime_error("http_get failed with unknown error");
    }
    if (http_code != 200)
    {
        std::stringstream err;
        err << "http_get failed with error " << http_code;
        throw std::runtime_error(err.str());
    }

    return res;
}
