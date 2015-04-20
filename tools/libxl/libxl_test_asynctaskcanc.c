/*
 * asynctaskcanc test case for the libxl event system
 *
 * To run this test:
 *    ./test_asynctaskcanc
 * Success:
 *    program takes a few seconds, prints some debugging output and exits 0
 * Failure:
 *
 */
#include <pthread.h>
#include "libxl_internal.h"
#include "libxl_test_asynctaskcanc.h"

/*thread function that invokes cancellation*/
void* thread_fn_for_cancellation(void* args)
{
   task_canc_ctx* ctx = (task_canc_ctx*)args;
   ctx = ctx;

   /*Look for the ao*/
   while (LIBXL_LIST_EMPTY(&ctx->ctx->aos_inprogress));

   libxl__ao *ao = LIBXL_LIST_FIRST(&ctx->ctx->aos_inprogress);
   
   while(ctx->target_canc_point != ao->curr_nr_of_canc_points);
   
   /*cancel the operation*/
/*
   libxl_ao_cancel(ctx->ctx, ctx->how); 
*/  

   return NULL;
   
}

/*demo test case to show the task cancellation strategy*/
int demo_run(libxl_ctx *ctx)
{
   int rc = 0;
   task_canc_ctx *task_ctx;
   pthread_t tid;

   printf("In: %s\n",__FUNCTION__);
   
   /*create a thread that will invoke the cancellation*/
   task_ctx = (task_canc_ctx*)malloc(sizeof(task_canc_ctx));
   task_ctx->ctx = ctx;
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid, NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc != 0)
      return rc;

   
   pthread_join(tid,NULL);
   return rc;

}

int libxl_test_asynctaskcanc(libxl_ctx *ctx)
{
    int rc = 0;
    printf("LibXL Async Task Cancellation Test \n");
    
    /*Demo run*/
    rc = demo_run(ctx);
    if(rc)
       printf("Demo Run Failed\n");
    else
       printf("Demo Run Passed\n");

    return 0;
}
