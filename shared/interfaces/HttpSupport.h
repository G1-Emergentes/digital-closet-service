#pragma once
#include <drogon/drogon.h>
namespace mirage::http
{
using Callback = std::function<void(const drogon::HttpResponsePtr &)>;
void reply(const Callback &, drogon::HttpStatusCode, const Json::Value &);
void noContent(const Callback &);
// Contract error envelope: {"error":{"code":"...","message":"..."}}. A null message
// selects the default text for the code.
void error(const Callback &, drogon::HttpStatusCode, const char *code,
           const char *message = nullptr);
bool json(const drogon::HttpRequestPtr &, Json::Value &, const Callback &);
bool bearer(const drogon::HttpRequestPtr &, std::string &, const Callback &);
} // namespace mirage::http
