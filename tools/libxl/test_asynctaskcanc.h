
#ifndef TEST_ASYNCTASKCANC_H
#define TEST_ASYNCTASKCANC_H

#define UNUSED(x) x=x

#define TC_ASSERT_STREAM FILE *tc_fp; char tc_name[256];

#define TC_NAME(z) sprintf(tc_name,"%s",z);

#define INIT_TC_ASSERT \
tc_fp = fopen("TC_log","w");\
if(!tc_fp) \
{ \
   printf("Could not init TC Logger\n"); \
   exit(1); \
} \
fprintf(tc_fp, "********* TC RUN START ***********\n");


#define DEINIT_TC_ASSERT \
fprintf(tc_fp, "********* TC RUN END ***********\n"); \
fclose(tc_fp);

#define TC_ASSERT(exp) \
if(exp) \
fprintf(tc_fp, "TC: %20s \t\tPASSED\n",tc_name); \
else \
fprintf(tc_fp, "TC: %20s \t\tFAILED\n",tc_name); \


#define RUN_TEST(a) \
TC_fn = &a; \
rc = TC_run(ctx); 

/*Test cases*/
int TC1_1_domain_create_pv(task_canc_ctx *ctx);
int TC1_2_domain_create_pv(task_canc_ctx *ctx);
int TC1_3_domain_create_pv(task_canc_ctx *ctx);
int TC1_4_domain_create_pv(task_canc_ctx *ctx);


int TC2_1_domain_create_hv(task_canc_ctx *ctx);
int TC2_2_domain_create_hv(task_canc_ctx *ctx);

int TC3_1_domain_suspend(task_canc_ctx *ctx);
int TC3_2_domain_suspend(task_canc_ctx *ctx);
int TC3_3_domain_suspend(task_canc_ctx *ctx);
int TC3_4_domain_suspend(task_canc_ctx *ctx);
int TC3_5_domain_suspend(task_canc_ctx *ctx);
int TC3_6_domain_suspend(task_canc_ctx *ctx);
int TC3_7_domain_suspend(task_canc_ctx *ctx);

int TC4_domain_destroy(task_canc_ctx *ctx);

int TC5_cancel_after_operation(task_canc_ctx *ctx);

int TC6_1_device_add(task_canc_ctx *task_ctx);

/*internal APIs domain*/
int TC_run(libxl_ctx *task_ctx);
int TC_domain_create(task_canc_ctx*, const char* , int , void *);
int TC_domain_create_event(task_canc_ctx *, const char*, int, void *);

int TC_domain_suspend(task_canc_ctx* , int );
int TC_domain_suspend_event(task_canc_ctx* , int );

/*internal APIs helpers*/
void async_op_callback(libxl_ctx *ctx, int rc, void *for_callback);
void * thread_fn_for_triggering_cancel (void *args);
void * thread_fn_for_waiting_for_events (void *args);

/* Internal APIs device */
int TC_device_add(task_canc_ctx *, const char*, int);

#endif /*TEST_ASYNCTASKCANC_H*/
