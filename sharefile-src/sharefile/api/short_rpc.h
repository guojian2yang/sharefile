#ifndef SHORT_RPC_H
#define SHORT_RPC_H

#include <string>

void startRpcServer();
std::string convertToShortUrl(const std::string& fullUrl);
std::string resolveShortUrl(const std::string& shortUrl);
std::string handleresolveRequest(const std::string& requestStr);
std::string handleconvertRequest(const std::string& requestStr);

#endif // SHORT_RPC_H