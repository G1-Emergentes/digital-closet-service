#pragma once
#include <functional>
#include <json/json.h>
namespace mirage
{
struct ApiResult
{
    int status;
    Json::Value body;
};
using Completion = std::function<void(ApiResult)>;
inline ApiResult failure(int status, const char *code)
{
    Json::Value body;
    body["error"] = code;
    return {status, std::move(body)};
}
} // namespace mirage
