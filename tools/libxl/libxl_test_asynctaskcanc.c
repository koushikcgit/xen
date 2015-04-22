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
#if 0
#include <pthread.h>
#endif
#include <poll.h>
#include <signal.h>
#include "libxl_internal.h"
#include "libxl_test_asynctaskcanc.h"

/*thread function that invokes cancellation*/
void* thread_fn_for_cancellation(void* args)
{
   task_canc_ctx* ctx = (task_canc_ctx*)args;
   /*Look for the ao*/
   printf("In: %s: Waiting for AO_CREATE\n",__FUNCTION__);
   while(LIBXL_LIST_EMPTY(&ctx->ctx->aos_inprogress));

#if 0
   libxl__ao *ao = LIBXL_LIST_FIRST(&ctx->ctx->aos_inprogress);
   
   while(ctx->target_canc_point != ao->curr_nr_of_canc_points);
#endif
   /*cancel the operation*/
   ctx->trigger_canc = true;
   printf("In: %s: Invoking cancellation\n",__FUNCTION__);

   return NULL;
   
}
#if 0
/*demo test case to show the task cancellation strategy*/
int demo_run(task_canc_ctx *task_ctx)
{
   int rc = 0;
   pthread_t tid;
   libxl_domain_config dc;
   uint32_t domid;
   char *json = NULL;
   char *name = "badger";
   char *kernel = "/boot/guest/mirage-testvm.xen";
   libxl_ctx *ctx = task_ctx->ctx;

   printf("In: %s\n",__FUNCTION__);
   
   /*create a thread that will invoke the cancellation*/
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid, NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc != 0)
      return rc;
   
   /*DO the LibXL CALL*/
   printf("In: %s: Creating domain\n",__FUNCTION__);
   libxl_domain_config_init(&dc);
   /* should we be using xlu_cfg_replace_string? */
   dc.c_info.name = name;
   dc.c_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.u.pv.kernel = kernel;
   json = libxl_domain_config_to_json(ctx, &dc);
   printf("dc structure before libxl_domain_create_new(): %s\n", json);
   free(json);
   rc = libxl_domain_create_new(ctx, &dc, &domid, 0, 0);
   assert (rc == 0);
   json = libxl_domain_config_to_json(ctx, &dc);
   printf("dc structure after libxl_domain_create_new(): %s\n", json);
   free(json);

   /* libxl_domain_config_dispose(&dc); */
   printf("created domain %d\n", domid);
   
   pthread_join(tid,NULL);
   return rc;

}

int demo_run_tc(task_canc_ctx *ctx)
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
#endif
