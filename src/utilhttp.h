#ifndef UTILHTTP_H
#define UTILHTTP_H

#include <sstream>
#include <string>

//! Does a HTTP GET request for <host>:<port><target>. Throws on error. Throws
//! on http response code != 200.
std::stringstream http_get(const std::string &host, int port, const std::string &target /* ex: /index.html */);

#endif
