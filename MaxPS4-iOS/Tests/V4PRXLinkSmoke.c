#include "../Native/maxps4_fex_runtime.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(maxps4_v4_prx_request)==40, "64-bit V4 ABI layout");
_Static_assert(offsetof(maxps4_v4_prx_request, entry)==24, "V4 guest entry offset");
_Static_assert(offsetof(maxps4_v4_prx_request, abi)==32, "V4 ABI selector offset");
_Static_assert(MAXPS4_V4_ABI_SYNTHETIC_HLT==1 && MAXPS4_V4_ABI_STATIC_EXEC_HLT==2,
               "Distinct stable PRX and static ELF contracts");
_Static_assert(MAXPS4_V4_ABI_STARTUP_RETURN==3,"Distinct startup subset");
_Static_assert(MAXPS4_V4_ABI_IMPORT_THREAD_SELF==4,"Distinct bounded import subset");
_Static_assert(MAXPS4_V4_ABI_IMPORT_CLOCK_GETRES==5,"Distinct bounded guest-memory service");
int main(void) {
    maxps4_m1_collision_report collision;
    if (maxps4_fex_m1_collision_diagnostic(&collision) ||
        collision.controlled_direct_status!=-EADDRINUSE || collision.translated_status ||
        !collision.marker_preserved || !collision.recreated) return 1;
    const maxps4_v4_prx_request request={NULL,0,0,0,MAXPS4_V4_ABI_SYNTHETIC_HLT,3};
    uint64_t result=123;
    uint32_t backend=123;
    int32_t status=maxps4_fex_engine_run_v4_prx(NULL,&request,&result,&backend);
    if (status!=-EINVAL || result || backend) return 1;
    char diagnostic[256];
    if (maxps4_fex_v4_preflight(NULL,diagnostic,sizeof(diagnostic))!=-EINVAL || !diagnostic[0]) return 1;
    if (maxps4_fex_eboot_preflight(NULL,0,diagnostic,sizeof(diagnostic))!=-EINVAL || !diagnostic[0]) return 1;
    result=123; backend=123;
    if (maxps4_fex_engine_run_eboot(NULL,NULL,0,&result,&backend)!=-EINVAL || result || backend) return 1;
    result=123; backend=123;
    if (maxps4_fex_engine_run_eboot_experimental(NULL,NULL,0,&result,&backend)!=-EINVAL || result || backend) return 1;
    if (maxps4_fex_eboot_experimental_preflight(NULL,0,diagnostic,sizeof(diagnostic))!=-ENOTSUP) return 1;
    puts("PASS linked C V4 ABI: layout, unmangled symbol, invalid engine and zero failure outputs; no JIT execution");
    return 0;
}
