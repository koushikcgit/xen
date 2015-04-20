#ifndef TEST_ASYNCTASKCANC_H
#define TEST_ASYNCTASKCANC_H

#include <pthread.h>

typedef struct _task_canc_ctx_
{
   libxl_ctx *ctx;
   libxl_asyncop_how *how;
   uint32_t target_canc_point;
}task_canc_ctx;

int libxl_test_asynctaskcanc(libxl_ctx *ctx);
int demo_run(libxl_ctx *ctx);
void* thread_fn_for_cancellation(void* args);

#endif /*TEST_ASYNCTASKCANC_H*/
