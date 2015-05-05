
#ifndef TEST_ASYNCTASKCANC_H
#define TEST_ASYNCTASKCANC_H

#define UNUSED(x) x=x

/*Test cases*/
int TC1_domain_create(task_canc_ctx *ctx);
int TC2_domain_suspend(task_canc_ctx *ctx);
int TC3_domain_destroy(task_canc_ctx *ctx);
int TC4_domain_resume(task_canc_ctx *ctx);

/*internal APIs*/
int TC_run(libxl_ctx *task_ctx);

void async_op_callback(libxl_ctx *ctx, int rc, void *for_callback);

#endif /*TEST_ASYNCTASKCANC_H*/
