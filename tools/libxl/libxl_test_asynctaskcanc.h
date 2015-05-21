#ifndef LIBXL_TEST_ASYNCTASKCANC_H
#define LIBXL_TEST_ASYNCTASKCANC_H

typedef struct _task_canc_ctx_
{
   libxl_ctx *ctx;
   libxl_asyncop_how *how;
   uint32_t target_canc_point;
   sem_t trigger_canc;
}task_canc_ctx;

void* thread_fn_for_cancellation(void* args);

#endif /*LIBXL_TEST_ASYNCTASKCANC_H*/
