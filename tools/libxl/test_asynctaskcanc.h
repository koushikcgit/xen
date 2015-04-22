
#ifndef TEST_ASYNCTASKCANC_H
#define TEST_ASYNCTASKCANC_H

#define UNUSED(x) x=x

int TC1_demo(libxl_ctx *ctx);
void async_op_callback(libxl_ctx *ctx, int rc, void *for_callback);

/*Test cases*/
int demo_run_tc(task_canc_ctx *ctx);

/*internal APIs*/
int demo_run(task_canc_ctx *task_ctx);

#endif /*TEST_ASYNCTASKCANC_H*/
