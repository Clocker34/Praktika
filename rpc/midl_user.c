/* Аллокаторы для MIDL-стабов (RPC). */
#include <stdlib.h>
#include <rpc.h>

void __RPC_FAR* __RPC_USER MIDL_user_allocate(size_t cb) { return malloc(cb); }
void __RPC_USER MIDL_user_free(void __RPC_FAR* p) { free(p); }
