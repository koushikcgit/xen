#include "test_common.h"
#include "libxl_test_asynctaskcanc.h"

int main(int argc, char **argv) {
    int rc;

    test_common_setup(XTL_DEBUG);

    rc = libxl_test_asynctaskcanc(ctx);
    assert(!rc);

    return 0;
}
