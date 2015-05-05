#include <pthread.h>
#include <stdlib.h>
#include "test_common.h"
#include "libxl_test_asynctaskcanc.h"
#include "test_asynctaskcanc.h"
#include "libxl.h"
#include "libxl_utils.h"

bool callback_data;
int vm_no = 0;

typedef int (*TC_Function)(task_canc_ctx *);
TC_Function TC_fn;

int TC_run(libxl_ctx *ctx)
{
   int rc = 0;
   printf("In: %s\n",__FUNCTION__);
   /*invoke the test*/
   /*poll to invoke cancel*/
   /*invoke cancel*/
      
   task_canc_ctx *task_ctx = (task_canc_ctx*)malloc(sizeof(task_canc_ctx));
   if(!task_ctx)
      return rc;
  
   task_ctx->ctx = ctx;
   task_ctx->trigger_canc = false;

   libxl_asyncop_how *how = (libxl_asyncop_how*) malloc(sizeof(libxl_asyncop_how));
   how->callback = async_op_callback;
   how->u.for_callback = (void*)&callback_data; /*som random data*/
   task_ctx->how = how;

   rc = TC_fn(task_ctx);
   if(rc)
      goto error;

error:
   if (how)
      free(how);
   free(task_ctx);
   return rc;

}

/*test case to show the task cancellation strategy*/
int TC1_domain_create(task_canc_ctx *task_ctx)
{
   int rc = 0;
   libxl_domain_config dc;
   uint32_t domid = -1;
   char name[20] = {0};
   sprintf(name, "AsyncTestVM_TC1_%d",vm_no++);

   char *kernel = "/boot/linux_pv/vmlinuz";
   char *ramdisk = "/boot/linux_pv/initrd.gz";
   libxl_ctx *ctx = task_ctx->ctx;
   pthread_t tid[2];

   printf("In: %s\n",__FUNCTION__);
   
   
   /*DO the LibXL CALL*/
   printf("In: %s: Creating domain\n",__FUNCTION__);

 
   libxl_domain_config_init(&dc);
   /* should we be using xlu_cfg_replace_string? */
   dc.c_info.name = name;
   dc.c_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.max_vcpus = 1;
   dc.b_info.max_memkb = 2048 * 1024;
   dc.b_info.target_memkb = dc.b_info.max_memkb;
   dc.b_info.u.pv.kernel = kernel;
   dc.b_info.u.pv.ramdisk = ramdisk;
   libxl_uuid_generate(&dc.c_info.uuid);


   /*create the thread that will monitor the cancellation points to signal the cancellation trigger*/
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
      return rc;
   sleep(1);
  
   char *json = libxl_domain_config_to_json(ctx, &dc);
   printf("dc structure before libxl_domain_create_new(): %s\n", json);
   free(json);
    
   callback_data = false;

   rc = libxl_domain_create_new(ctx, &dc, &domid, task_ctx->how, 0);
   assert (rc == 0);
   if(-1 == domid)
   {
      printf("*** domain creation failed ***\n");
   }

   /*Wait for the trigger from thread_fn_for_cancelllation function*/
   while(!task_ctx->trigger_canc && !callback_data);
   if(callback_data)
   {
      printf("In %s: AO_COMPLETE\n",__FUNCTION__);
      libxl_domain_destroy(ctx, domid, 0);
   }
   else
   {
      printf("In: %s: Triggering ao_cancel\n",__FUNCTION__);
      rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
      assert(!rc);
     
      /*Check if domain was created*/
      system("xenstore-ls -f /local/domain");
   }

   pthread_join(tid[0],NULL);
   return rc;

}


/*demo test case to show the task cancellation strategy*/
int TC2_domain_suspend(task_canc_ctx *task_ctx)
{
   int rc = 0;
   libxl_domain_config dc;
   uint32_t domid = -1;
/*
   char *json = NULL;
   libxl_device_nic nic;
*/
   char name[20] = {0};
   sprintf(name, "AsyncTestVM_%d",vm_no++);

   /*char *kernel = "/boot/mirage-testvm.xen";*/

   char *kernel = "/boot/linux_pv/vmlinuz";
   char *ramdisk = "/boot/linux_pv/initrd.gz";
   libxl_ctx *ctx = task_ctx->ctx;

   printf("In: %s\n",__FUNCTION__);
   
   
   /*DO the LibXL CALL*/
   printf("In: %s: Creating domain\n",__FUNCTION__);

 
   libxl_domain_config_init(&dc);
   /* should we be using xlu_cfg_replace_string? */
   dc.c_info.name = name;
   dc.c_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.max_vcpus = 1;
   dc.b_info.max_memkb = 2048 * 1024;
   dc.b_info.target_memkb = dc.b_info.max_memkb;
   dc.b_info.u.pv.kernel = kernel;
   dc.b_info.u.pv.ramdisk = ramdisk;
   libxl_uuid_generate(&dc.c_info.uuid);
/*
   json = libxl_domain_config_to_json(ctx, &dc);
   printf("dc structure before libxl_domain_create_new(): %s\n", json);
   free(json);
*/
   rc = libxl_domain_create_new(ctx, &dc, &domid, task_ctx->how, 0);
   assert (rc == 0);
/*
   json = libxl_domain_config_to_json(ctx, &dc);
   printf("dc structure after libxl_domain_create_new(): %s\n", json);
   free(json);
*/
   
   if(-1 == domid)
   {
      printf("*** domain creation failed ***\n");
   }

   /* libxl_domain_config_dispose(&dc); */
   printf("created domain %d\n", domid);
 /* 
   libxl_device_nic_init(&nic);
   json = libxl_device_nic_to_json(ctx, &nic);
   printf("nic structure before libxl_device_nic_add(): %s\n", json);
   free(json);

   libxl_device_nic_add(ctx, domid, &nic, NULL);
   json = libxl_device_nic_to_json(ctx, &nic);
   printf("nic structure after libxl_device_nic_add(): %s\n", json);
   free(json);
   libxl_device_nic_dispose(&nic);
 */
   pthread_t tid[2];
   /*create the thread that will monitor the cancellation points to signal the cancellation trigger*/
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
      return rc;
   sleep(1);
   
   callback_data = false;

   libxl_domain_suspend(ctx, domid, 0, 0, task_ctx->how);

   /*Wait for the trigger from thread_fn_for_cancelllation function*/
   while(!task_ctx->trigger_canc && !callback_data);
   if(callback_data)
      printf("In %s: AO_COMPLETE\n",__FUNCTION__);
   else
   {
      printf("In: %s: Triggering ao_cancel\n",__FUNCTION__);
      rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
      assert(!rc);
 
      /*Check if domain was suspended*/
      char domain[100] = {0};
      sprintf(domain,"xenstore-ls -f /local/domain/%d/control/shutdown",domid);
      system(domain);
   }
   pthread_join(tid[0],NULL);
   libxl_domain_destroy(ctx, domid, 0);
   return rc;

}


/*test case to show the task cancellation strategy*/
int TC3_domain_destroy(task_canc_ctx *task_ctx)
{
   int rc = 0;
   libxl_domain_config dc;
   uint32_t domid = -1;
   char name[20] = {0};
   sprintf(name, "AsyncTestVM_TC1_%d",vm_no++);

   char *kernel = "/boot/linux_pv/vmlinuz";
   char *ramdisk = "/boot/linux_pv/initrd.gz";
   libxl_ctx *ctx = task_ctx->ctx;
   pthread_t tid[2];

   printf("In: %s\n",__FUNCTION__);
   
   
   /*DO the LibXL CALL*/
   printf("In: %s: Creating domain\n",__FUNCTION__);

 
   libxl_domain_config_init(&dc);
   /* should we be using xlu_cfg_replace_string? */
   dc.c_info.name = name;
   dc.c_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.type = LIBXL_DOMAIN_TYPE_PV;
   dc.b_info.max_vcpus = 1;
   dc.b_info.max_memkb = 2048 * 1024;
   dc.b_info.target_memkb = dc.b_info.max_memkb;
   dc.b_info.u.pv.kernel = kernel;
   dc.b_info.u.pv.ramdisk = ramdisk;
   libxl_uuid_generate(&dc.c_info.uuid);

   rc = libxl_domain_create_new(ctx, &dc, &domid, NULL, 0);
   assert (rc == 0);
   if(-1 == domid)
   {
      printf("*** domain creation failed ***\n");
   }

   /*create the thread that will monitor the cancellation points to signal the cancellation trigger*/
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
      return rc;
   sleep(1);
  
   callback_data = false;

   libxl_domain_destroy(ctx, domid, task_ctx->how);

   /*Wait for the trigger from thread_fn_for_cancelllation function*/
   while(!task_ctx->trigger_canc && !callback_data);
   if(callback_data)
   {
      printf("In %s: AO_COMPLETE\n",__FUNCTION__);
   }
   else
   {
      printf("In: %s: Triggering ao_cancel\n",__FUNCTION__);
      rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
      assert(!rc);
     
      /*Check if domain was created*/
      system("xenstore-ls -f /local/domain");
   }

   pthread_join(tid[0],NULL);
   return rc;

}


/*test case to show the task cancellation strategy*/
int TC4_domain_resume(task_canc_ctx *task_ctx)
{
   int rc = 0;
   libxl_domain_config dc;
   uint32_t domid = -1;
   char name[20] = {0};
   void * config_data = NULL;
   int config_len = 0;
   sprintf(name, "AsyncTestVM_TC1_%d",vm_no++);

   libxl_ctx *ctx = task_ctx->ctx;
   pthread_t tid[2];
   char *filename = "myfirstdomU.cfg";

   printf("In: %s\n",__FUNCTION__);
   
   
   /*DO the LibXL CALL*/
   printf("In: %s: Creating domain\n",__FUNCTION__);

   rc = libxl_read_file_contents(ctx, filename,
                                           &config_data, &config_len);
   if (rc || !config_data) 
   { 
      printf("Failed to read config file: %s\n", filename); 
      return -1;
   }
   libxl_domain_config_init(&dc);
/*
   parse_config_data(filename, config_data, config_len, &dc);
*/

   /* should we be using xlu_cfg_replace_string? */
   libxl_uuid_generate(&dc.c_info.uuid);

   rc = libxl_domain_create_new(ctx, &dc, &domid, NULL, 0);
   assert (rc == 0);
   if(-1 == domid)
   {
      printf("*** domain creation failed ***\n");
      return -1;
   }

   libxl_domain_suspend(ctx, domid, 0, 0, NULL);

   /*create the thread that will monitor the cancellation points to signal the cancellation trigger*/
   task_ctx->target_canc_point = 1;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
      return rc;
   sleep(1);
  
   callback_data = false;

   libxl_domain_resume(ctx, domid, 1, task_ctx->how);

   /*Wait for the trigger from thread_fn_for_cancelllation function*/
   while(!task_ctx->trigger_canc && !callback_data);
   if(callback_data)
   {
      printf("In %s: AO_COMPLETE\n",__FUNCTION__);
   }
   else
   {
      printf("In: %s: Triggering ao_cancel\n",__FUNCTION__);
      rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
      assert(!rc);
     
      /*Check if domain was created*/
      system("xenstore-ls -f /local/domain");
      libxl_domain_resume(ctx, domid, 1, NULL);
   }

   libxl_domain_destroy(ctx, domid, NULL);

   pthread_join(tid[0],NULL);
   return rc;

}

void async_op_callback(libxl_ctx *ctx, int rc, void *for_callback)
{
    UNUSED(ctx);
    UNUSED(rc);
    printf("IN: %s \n",__FUNCTION__);
    bool *data = (bool*)for_callback;
    *data = true;
}

int main(int argc, char **argv) {
    int rc;

    vm_no = rand();
    test_common_setup(XTL_DEBUG);
    
    TC_fn = &TC1_domain_create;
    rc = TC_run(ctx);
    assert(!rc);

    TC_fn = &TC2_domain_suspend;
    rc = TC_run(ctx);
    assert(!rc);

    TC_fn = &TC3_domain_destroy;
    rc = TC_run(ctx);
    assert(!rc);

    TC_fn = &TC4_domain_resume;
    rc = TC_run(ctx);
    assert(!rc);
    return 0;
}
