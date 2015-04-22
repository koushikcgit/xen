#include <pthread.h>
#include "test_common.h"
#include "libxl_test_asynctaskcanc.h"
#include "test_asynctaskcanc.h"
#include "libxl.h"

int TC1_demo(libxl_ctx *ctx)
{
   int rc = 0;
   pthread_t tid[2];

   printf("In: %s\n",__FUNCTION__);
   /*invoke the test*/
   /*poll to invoke cancel*/
   /*invoke cancel*/
      
   task_canc_ctx *task_ctx = (task_canc_ctx*)malloc(sizeof(task_canc_ctx));
   if(!task_ctx)
      return rc;
  
   task_ctx->ctx = ctx;
   task_ctx->trigger_canc = false;

   /*create a thread that will invoke the cancellation*/
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
      goto error;
   
   libxl_asyncop_how *how = (libxl_asyncop_how*) malloc(sizeof(libxl_asyncop_how));
   how->callback = async_op_callback;
   how->u.for_callback = (void*)10; /*som random data*/
   task_ctx->how = how;

   rc = demo_run(task_ctx);
   if(rc)
      goto error;

   while(!task_ctx->trigger_canc);
 
   printf("In: %s: Triggering ao_cancel\n",__FUNCTION__);
   rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
  
error:
   free(how);
   pthread_join(tid[0],NULL);
   free(task_ctx);
   return rc;

}

/*demo test case to show the task cancellation strategy*/
int demo_run(task_canc_ctx *task_ctx)
{
   int rc = 0;
   libxl_domain_config dc;
   libxl_device_nic nic;
   uint32_t domid;
   char *json = NULL;
   char *name = "badger";
   char *kernel = "/boot/guest/mirage-testvm.xen";
   libxl_ctx *ctx = task_ctx->ctx;

   printf("In: %s\n",__FUNCTION__);
   
   
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

   rc = libxl_domain_create_new(ctx, &dc, &domid, task_ctx->how, 0);
   assert (rc == 0);
   json = libxl_domain_config_to_json(ctx, &dc);
   printf("dc structure after libxl_domain_create_new(): %s\n", json);
   free(json);

   /* libxl_domain_config_dispose(&dc); */
   printf("created domain %d\n", domid);
  
   libxl_device_nic_init(&nic);
   json = libxl_device_nic_to_json(ctx, &nic);
   printf("nic structure before libxl_device_nic_add(): %s\n", json);
   free(json);

   libxl_device_nic_add(ctx, domid, &nic, NULL);
   json = libxl_device_nic_to_json(ctx, &nic);
   printf("nic structure after libxl_device_nic_add(): %s\n", json);
   free(json);
   libxl_device_nic_dispose(&nic);
 
   libxl_domain_suspend(ctx, domid, 0, 0, task_ctx->how); 

   return rc;

}

void async_op_callback(libxl_ctx *ctx, int rc, void *for_callback)
{
    UNUSED(ctx);
    UNUSED(rc);
    printf("IN: %s \n",__FUNCTION__);
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
int main(int argc, char **argv) {
    int rc;

    test_common_setup(XTL_DEBUG);

    rc = TC1_demo(ctx);
    assert(!rc);

    return 0;
}
