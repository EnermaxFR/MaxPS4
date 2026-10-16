#include "../Native/maxps4_fex_runtime.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(maxps4_v4_prx_request)==40, "64-bit V4 ABI layout");
_Static_assert(offsetof(maxps4_v4_prx_request, entry)==24, "V4 guest entry offset");
_Static_assert(offsetof(maxps4_v4_prx_request, abi)==32, "V4 ABI selector offset");
int main(void) {
    const maxps4_v4_prx_request request={NULL,0,0,0,MAXPS4_V4_ABI_SYNTHETIC_HLT,3};
    uint64_t result=123;
    uint32_t backend=123;
    int32_t status=maxps4_fex_engine_run_v4_prx(NULL,&request,&result,&backend);
    if (status!=-EINVAL || result || backend) return 1;
    puts("PASS linked C V4 ABI: layout, unmangled symbol, invalid engine and zero failure outputs; no JIT execution");
    return 0;
}
