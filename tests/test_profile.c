#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "tu104_profile.h"

int main(void)
{
    uint32_t class_revision =
        (TU104_PROFILE_CLASS_CODE << 8) | UINT32_C(0xa1);

    assert(TU104_PROFILE_PCI_ID == UINT32_C(0x1e8410de));
    assert(TU104_PROFILE_SUBSYSTEM == UINT32_C(0x3ff51458));

    assert(tu104_location_matches(0, 1, 0, 0));
    assert(!tu104_location_matches(0, 2, 0, 0));
    assert(!tu104_location_matches(0, 1, 0, 1));
    assert(!tu104_location_matches(1, 1, 0, 0));

    assert(tu104_parent_location_matches(0, 0, 1, 0));
    assert(!tu104_parent_location_matches(0, 0, 2, 0));

    assert(tu104_identity_matches(UINT32_C(0x1e8410de),
                                  UINT32_C(0x3ff51458),
                                  class_revision));
    assert(!tu104_identity_matches(UINT32_C(0x1e0710de),
                                   UINT32_C(0x3ff51458),
                                   class_revision));
    assert(!tu104_identity_matches(UINT32_C(0x1e8410de),
                                   UINT32_C(0x866a1043),
                                   class_revision));
    assert(!tu104_identity_matches(UINT32_C(0x1e8410de),
                                   UINT32_C(0x3ff51458),
                                   UINT32_C(0x040300a1)));

    puts("profile tests: 16 checks passed");
    return 0;
}
