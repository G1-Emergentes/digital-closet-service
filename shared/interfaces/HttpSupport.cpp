#include "HttpSupport.h"
#include <cstring>
using namespace drogon;
namespace mirage::http
{
namespace
{
const char *defaultMessage(const char *code)
{
    if (!std::strcmp(code, "INVALID_ARGUMENT"))
        return "Parámetros, cursor o cuerpo inválidos.";
    if (!std::strcmp(code, "UNAUTHENTICATED"))
        return "Token ausente, inválido o sesión rechazada.";
    if (!std::strcmp(code, "GARMENT_NOT_FOUND"))
        return "Prenda inexistente o no accesible.";
    if (!std::strcmp(code, "IMAGE_NOT_FOUND"))
        return "La prenda no tiene imagen.";
    if (!std::strcmp(code, "RATE_LIMITED"))
        return "Demasiadas solicitudes.";
    if (!std::strcmp(code, "SERVICE_UNAVAILABLE"))
        return "Dependencia temporalmente no disponible.";
    return "Fallo inesperado.";
}
} // namespace
void reply(const Callback &callback, HttpStatusCode status, const Json::Value &body)
{
    auto response = HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    callback(response);
}

void noContent(const Callback &callback)
{
    auto response = HttpResponse::newHttpResponse();
    response->setStatusCode(k204NoContent);
    response->addHeader("Cache-Control", "no-store");
    callback(response);
}

void error(const Callback &callback, HttpStatusCode status, const char *code, const char *message)
{
    Json::Value body;
    body["error"]["code"] = code;
    body["error"]["message"] = message ? message : defaultMessage(code);
    reply(callback, status, body);
}

bool json(const HttpRequestPtr &req, Json::Value &body, const Callback &callback)
{
    if (req->body().size() > 16384)
    {
        error(callback, k413RequestEntityTooLarge, "INVALID_ARGUMENT", "El cuerpo es demasiado grande.");
        return false;
    }
    auto value = req->getJsonObject();
    if (!value || !value->isObject())
    {
        error(callback, k400BadRequest, "INVALID_ARGUMENT", "El cuerpo debe ser un objeto JSON.");
        return false;
    }
    body = *value;
    return true;
}

bool bearer(const HttpRequestPtr &req, std::string &token, const Callback &callback)
{
    token = req->getHeader("authorization");
    if (token.size() <= 7 || token.size() > 8192 || token.compare(0, 7, "Bearer ") != 0 ||
        token.find_first_of("\r\n\t") != std::string::npos)
    {
        error(callback, k401Unauthorized, "UNAUTHENTICATED");
        return false;
    }
    return true;
}
} // namespace mirage::http
