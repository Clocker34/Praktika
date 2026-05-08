// RPC-клиент GUI: Praktika_Api по ncalrpc/ALPC (задание 3).
#include "api_client.h"
#include "app_config.h"

#include <windows.h>
#include <rpc.h>

extern "C" {
#include "Praktika_Api.h"
}

#pragma comment(lib, "Rpcrt4.lib")

namespace {

RPC_BINDING_HANDLE g_apiBinding = nullptr;

RPC_STATUS InitApiBinding() {
  if (g_apiBinding) {
    RpcBindingFree(&g_apiBinding);
    g_apiBinding = nullptr;
  }
  static wchar_t kProt[] = L"ncalrpc";
  static wchar_t kEp[] = PRAKTIKA_RPC_API_ENDPOINT;
  RPC_WSTR str = nullptr;
  const RPC_STATUS s = RpcStringBindingComposeW(
      nullptr, reinterpret_cast<RPC_WSTR>(kProt), nullptr,
      reinterpret_cast<RPC_WSTR>(kEp), nullptr, &str);
  if (s != RPC_S_OK) return s;
  const RPC_STATUS b = RpcBindingFromStringBindingW(str, &g_apiBinding);
  RpcStringFreeW(&str);
  return b;
}

RPC_STATUS EnsureBinding() {
  if (g_apiBinding) return RPC_S_OK;
  return InitApiBinding();
}

}  // namespace

long Praktika_RpcGetAuthInfo(std::wstring& username) {
  if (EnsureBinding() != RPC_S_OK) return CYCL_SERVER_ERROR;
  wchar_t buf[256]{};
  long result = CYCL_SERVER_ERROR;
  RpcTryExcept {
    result = GetAuthInfo(g_apiBinding, 256, buf);
  } RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
    return CYCL_SERVER_ERROR;
  } RpcEndExcept
  username = buf;
  return result;
}

long Praktika_RpcLogin(const wchar_t* username, const wchar_t* password) {
  if (EnsureBinding() != RPC_S_OK) return CYCL_SERVER_ERROR;
  long result = CYCL_SERVER_ERROR;
  RpcTryExcept {
    result = Login(g_apiBinding, username, password);
  } RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
    return CYCL_SERVER_ERROR;
  } RpcEndExcept
  return result;
}

long Praktika_RpcLogout() {
  if (EnsureBinding() != RPC_S_OK) return CYCL_SERVER_ERROR;
  long result = CYCL_SERVER_ERROR;
  RpcTryExcept {
    result = Logout(g_apiBinding);
  } RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
    return CYCL_SERVER_ERROR;
  } RpcEndExcept
  return result;
}

long Praktika_RpcGetLicenseInfo(bool& isLicensed, std::wstring& expiryDate) {
  if (EnsureBinding() != RPC_S_OK) return CYCL_SERVER_ERROR;
  long isLic = 0;
  wchar_t buf[64]{};
  long result = CYCL_SERVER_ERROR;
  RpcTryExcept {
    result = GetLicenseInfo(g_apiBinding, &isLic, 64, buf);
  } RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
    return CYCL_SERVER_ERROR;
  } RpcEndExcept
  isLicensed = (isLic != 0);
  expiryDate = buf;
  return result;
}

long Praktika_RpcActivateProduct(const wchar_t* activationCode) {
  if (EnsureBinding() != RPC_S_OK) return CYCL_SERVER_ERROR;
  long result = CYCL_SERVER_ERROR;
  RpcTryExcept {
    result = ActivateProduct(g_apiBinding, activationCode);
  } RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
    return CYCL_SERVER_ERROR;
  } RpcEndExcept
  return result;
}
