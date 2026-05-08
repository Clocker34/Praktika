// RPC-клиент GUI: ncalrpc/ALPC, эндпоинт совпадает с Praktika_Stop.idl.
#include "service_client.h"

#include <windows.h>
#include <rpc.h>

extern "C" {
#include "Praktika_Stop.h"
}

#pragma comment(lib, "Rpcrt4.lib")

namespace {
RPC_BINDING_HANDLE g_binding = nullptr;

RPC_STATUS InitBinding() {
  if (g_binding) {
    RpcBindingFree(&g_binding);
    g_binding = nullptr;
  }
  static wchar_t kProt[] = L"ncalrpc";
  static wchar_t kEp[] = L"PraktikaSvcStop";
  RPC_WSTR str = nullptr;
  const RPC_STATUS s = RpcStringBindingComposeW(
      nullptr, reinterpret_cast<RPC_WSTR>(kProt), nullptr,
      reinterpret_cast<RPC_WSTR>(kEp), nullptr, &str);
  if (s != RPC_S_OK) {
    return s;
  }
  const RPC_STATUS b = RpcBindingFromStringBindingW(str, &g_binding);
  RpcStringFreeW(&str);
  return b;
}
}  // namespace

void Praktika_CallRequestStop() {
  if (!g_binding && InitBinding() != RPC_S_OK) {
    return;
  }
  // Вызов RPC может бросить SEH при недоступном сервере — глушим.
  RpcTryExcept {
    RequestStop(g_binding);
  } RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
    /* нет сервера / закрыт — игнор */
  } RpcEndExcept
}
