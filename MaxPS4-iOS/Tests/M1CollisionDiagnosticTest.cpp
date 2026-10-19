#include "../Native/M1CollisionDiagnostic.hpp"
#include <cassert>
#include <iostream>
int main() {
    for (int i=0;i<3;++i) {
        maxps4_m1_collision_report report{};
        assert(maxps4::m1_collision_diagnostic(report)==0);
        assert(report.candidate_status==0 || report.candidate_status==-EADDRINUSE);
        assert(report.controlled_direct_status==-EADDRINUSE);
        assert(report.translated_status==0 && report.marker_preserved && report.recreated);
        std::cout<<"PASS owned direct collision="<<report.controlled_direct_status
                 <<" translated="<<report.translated_status<<" preserved/recreated=1; no CPU execution\n";
    }
}
