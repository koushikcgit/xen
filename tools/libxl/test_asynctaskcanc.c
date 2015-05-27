#include <pthread.h>
#include <execinfo.h>
#include <signal.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "test_common.h"
#include "libxl_osdeps.h"
#include "libxl_test_asynctaskcanc.h"
#include "test_asynctaskcanc.h"
#include "libxl.h"
#include "libxl_utils.h"
#include "libxlutil.h"
#include "xl.h"
#include "test_utils.h"


TC_ASSERT_STREAM

extern libxl_ctx *ctx;

bool callback_data;
typedef int (*TC_Function)(task_canc_ctx *);
TC_Function TC_fn;

int TC_domain_create(task_canc_ctx *task_ctx, const char*filename, int canc_point, void *how)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   pthread_t tid[2];

   libxl_ctx *ctx = task_ctx->ctx;

   printf("*************** In: %s ********************\n",__FUNCTION__);

   /*create the thread that will monitor the cancellation points to signal the cancellation trigger*/
   task_ctx->target_canc_point = canc_point;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
   {
      printf(" In: %s: FATAL: Could not create threads for testing\n", __FUNCTION__);
      return rc;
   }
   sleep(1);
   
   callback_data = false;
   if(!how)
   {
      /*create another thread to trigger the cancellation as the create is synchronous*/
      rc = pthread_create(&tid[1], NULL, thread_fn_for_triggering_cancel,(void*)task_ctx);
      if(rc)
      {
         printf(" In: %s: FATAL: Could not create threads for testing\n", __FUNCTION__);
         return rc;
       }
      sleep(1);
   }
   
   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = freemem(domid, &dc.b_info);
   
   rc = libxl_domain_create_new(ctx, &dc, &domid, how, 0);
   if(!how)
   {
      /*assert(rc != 0);*/
   }
   else
   {
      /*assert(rc == 0);*/
  
      /*Wait for the trigger from thread_fn_for_cancelllation function*/
      sem_wait(&task_ctx->trigger_canc);
      if(callback_data)
      {
         printf("In %s: ***********  AO_COMPLETE ****************\n",__FUNCTION__);
      }
      else
      {
         printf("In: %s: ******** TRIGGERING AO_CANCEL ********\n",__FUNCTION__);
         rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
         /*assert(rc == 0);*/
      }
   }
   /*Check if domain not created*/
   libxl_dominfo* info = libxl_list_domain(ctx, &rc);
   free(info);
   TC_ASSERT(rc == 1); /* dom0 only*/
   
   pthread_join(tid[0], NULL);

   if(!how)
      pthread_join(tid[1], NULL);

   if(rc != 1)
      libxl_domain_destroy(ctx,domid,NULL);
  
   rc = 0;
out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
 
}


int TC_domain_create_event(task_canc_ctx *task_ctx, const char*filename, int canc_point, void *how)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   bool cancelled = false;

   libxl_ctx *ctx = task_ctx->ctx;

   printf("*************** In: %s ********************\n",__FUNCTION__);

   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = libxl_domain_create_new(ctx, &dc, &domid, how, 0);

   int count = 0;
   libxl_event * event_r = NULL;
   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        /*
        if ((event_r)->domid != domid) {
            char *evstr = libxl_event_to_json(ctx, event_r);
            free(evstr);
            libxl_event_free(ctx, event_r);
            continue;
        }
        */
        if (count >= canc_point && !cancelled) {
        rc = libxl_ao_cancel(ctx,how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }
   /*Check if domain not created*/
   libxl_dominfo* info = libxl_list_domain(ctx, &rc);
   free(info);
   TC_ASSERT(rc == 1); /* dom0 only*/
   
   if(rc > 1)
      libxl_domain_destroy(ctx,domid,NULL);
   else
    rc = 0;
out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
 
}

int TC1_1_domain_create_pv(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_create(task_ctx, "myfirstdomU.cfg", 1, NULL);
}

int TC1_2_domain_create_pv(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_create(task_ctx, "myfirstdomU.cfg", 2, NULL);
}

int TC1_3_domain_create_pv(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_create(task_ctx, "myfirstdomU.cfg", 3, NULL);
}

int TC1_4_domain_create_pv(task_canc_ctx *task_ctx)
{
   int rc = 0;
   int point = 3;
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   {
       printf("*************** In: %s : CANC POINT [%d]********************\n",__FUNCTION__, point);
       callback_data = false;
       rc = TC_domain_create_event(task_ctx, "myfirstdomU.cfg", point++, task_ctx->how);
   }
   return rc;
}

int TC2_1_domain_create_hv(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_create(task_ctx, "xenwin7.cfg", 1, NULL);
}

int TC2_2_domain_create_hv(task_canc_ctx *task_ctx)
{
   int rc = 0;
   int point = 3;
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   
   callback_data = false;
   rc = TC_domain_create_event(task_ctx, "xenwin7.cfg", point++, task_ctx->how);
   
   return rc;
}

int TC_domain_suspend(task_canc_ctx *task_ctx, int canc_point)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   pthread_t tid[2];
   const char* filename = "myfirstdomU.cfg";

   libxl_ctx *ctx = task_ctx->ctx;

   printf("*************** In: %s ********************\n",__FUNCTION__);

   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = libxl_domain_create_new(ctx, &dc, &domid, 0, NULL);
   if(rc != 0 || domid == -1)
   {
      printf(" In: %s: domain_create failed\n", __FUNCTION__);
      goto out;
   } 
   libxl_domain_unpause(ctx, domid);

   /*create the thread that will monitor the cancellation points to signal the cancellation trigger*/
   task_ctx->target_canc_point = canc_point;
   rc = pthread_create(&tid[0], NULL, thread_fn_for_cancellation,(void*)task_ctx);
   if(rc)
   {
      printf(" In: %s: FATAL: Could not create threads for testing\n", __FUNCTION__);
      return rc;
   }
   sleep(1);
 
   callback_data = false;
   int fd = -1;
   fd = open("/root/domain-write",O_WRONLY|O_CREAT|O_TRUNC, 0644);
   libxl_domain_suspend(ctx, domid, fd, 0, task_ctx->how);

   
   /*Wait for the trigger from thread_fn_for_cancelllation function*/
   sem_wait(&task_ctx->trigger_canc);
   if(callback_data)
   {
      printf("In %s: ***********  AO_COMPLETE ****************\n",__FUNCTION__);
   }
   else
   {
      printf("In: %s: ******** TRIGGERING AO_CANCEL ********\n",__FUNCTION__);
      rc = libxl_ao_cancel(task_ctx->ctx, task_ctx->how);
      /*assert(rc == 0);*/
   }

   close(fd);
   /*Check if domain was suspended*/
   libxl_dominfo* info = libxl_list_domain(ctx, &rc);
   TC_ASSERT((info[1].blocked == true || info[1].running == true) && (rc == 2));
   rc = 0;
   free(info);

   pthread_join(tid[0],NULL);
   libxl_domain_destroy(ctx, domid, 0);

out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
}

int TC_domain_suspend_event(task_canc_ctx *task_ctx, int canc_point)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   const char* filename = "myfirstdomU.cfg";
   bool cancelled = false;

   libxl_ctx *ctx = task_ctx->ctx;

   printf("*************** In: %s ********************\n",__FUNCTION__);

   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = libxl_domain_create_new(ctx, &dc, &domid, 0, NULL);
   if(rc != 0 || domid == -1)
   {
      printf(" In: %s: domain_create failed\n", __FUNCTION__);
      goto out;
   } 
   libxl_domain_unpause(ctx, domid);
   sleep(1);
 
   callback_data = false;
   int fd = -1;
   fd = open("/root/domain-write",O_WRONLY|O_CREAT|O_TRUNC, 0644);
   libxl_domain_suspend(ctx, domid, fd, 0, task_ctx->how);

   int count = 0;
   libxl_event * event_r = NULL;
   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        if (count >= canc_point && !cancelled) {
        rc = libxl_ao_cancel(ctx,task_ctx->how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }
 
   close(fd);
   /*Check if domain was suspended*/
   libxl_dominfo* info = libxl_list_domain(ctx, &rc);
   TC_ASSERT((info[1].blocked == true || info[1].running == true) && (rc == 2));
   rc = 0;
   free(info);
   libxl_domain_destroy(ctx, domid, 0);

out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
}

int TC3_1_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend(task_ctx, 1);
}


int TC3_2_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend_event(task_ctx, 1);
}

int TC3_3_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend_event(task_ctx, 2);
}

int TC3_4_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend_event(task_ctx, 3);
}

int TC3_5_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend_event(task_ctx, 4);
}

int TC3_6_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend_event(task_ctx, 5);
}

int TC3_7_domain_suspend(task_canc_ctx *task_ctx)
{
   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_domain_suspend_event(task_ctx, 6);
}
/*test case to show the task cancellation strategy*/
int TC4_domain_destroy(task_canc_ctx *task_ctx)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   const char* filename = "myfirstdomU.cfg";
   bool cancelled = false;

   libxl_ctx *ctx = task_ctx->ctx;

   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);

   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = libxl_domain_create_new(ctx, &dc, &domid, 0, NULL);
   if(rc != 0 || domid == -1)
   {
      printf(" In: %s: domain_create failed\n", __FUNCTION__);
      goto out;
   } 
   libxl_domain_unpause(ctx, domid);
 
   sleep(1);

   callback_data = false;

   printf("*************** In: %s: domain destroy ******** \n",__FUNCTION__);
   libxl_domain_destroy(ctx, domid, task_ctx->how);

   int count = 0;
   libxl_event * event_r = NULL;
   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        if (!cancelled) {
        rc = libxl_ao_cancel(ctx,task_ctx->how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }
    
    libxl_dominfo* info = libxl_list_domain(ctx, &rc);
    free(info);
    TC_ASSERT(rc == 2);
    libxl_domain_destroy(ctx, domid, 0);
    rc = 0;

out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
}

int TC5_cancel_after_operation(task_canc_ctx *task_ctx)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   const char* filename = "myfirstdomU.cfg";

   TC_NAME(__FUNCTION__);
   libxl_ctx *ctx = task_ctx->ctx;

   printf("******************In: %s ***************************\n",__FUNCTION__);

   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = libxl_domain_create_new(ctx, &dc, &domid, 0, NULL);
   if(rc != 0 || domid == -1)
   {
      printf(" In: %s: domain_create failed\n", __FUNCTION__);
      goto out;
   } 
  
   rc = libxl_ao_cancel(ctx, NULL);
   TC_ASSERT(rc != 0); /*TC passed*/
  
   libxl_domain_destroy(ctx, domid, 0);
   rc = 0;

out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
 
}


int TC_device_add(task_canc_ctx * task_ctx, const char* filename, int canc_point)
{
   int rc = -1;
   libxl_domain_config dc;
   uint32_t domid = -1;
   void *config_data = 0;
   int config_len = 0;
   libxl_device_nic nic;

   libxl_ctx *ctx = task_ctx->ctx;

   printf("******************In: %s ***************************\n",__FUNCTION__);

   printf("In: %s: Creating domain\n",__FUNCTION__);

   libxl_domain_config_init(&dc);
   rc = libxl_read_file_contents(ctx, filename, &config_data, &config_len);
   if(rc)
   {
      printf(" In: %s: Cannot open file (%s) \n", __FUNCTION__, filename);
      goto out;
   }
   parse_config_data(filename, config_data, config_len, &dc);

   rc = libxl_domain_create_new(ctx, &dc, &domid, 0, NULL);
   if(rc != 0 || domid == -1)
   {
      printf(" In: %s: domain_create failed\n", __FUNCTION__);
      goto out;
   } 
   libxl_domain_unpause(ctx,domid); 

   printf(" \n ************ NIC ADD ************** \n");
   int old_nic_count = 0;
   libxl_device_nic *nic_info = libxl_device_nic_list(ctx, domid, &old_nic_count);
   free(nic_info);
   
   libxl_device_nic_init(&nic);
   rc = libxl_device_nic_add(ctx, domid, &nic, task_ctx->how);

   int count = 0;
   bool cancelled = false;
   callback_data = false;
   libxl_event * event_r = NULL;
   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        /*
        if ((event_r)->domid != domid) {
            char *evstr = libxl_event_to_json(ctx, event_r);
            free(evstr);
            libxl_event_free(ctx, event_r);
            continue;
        }
        */
        if (count >= canc_point && !cancelled) {
        rc = libxl_ao_cancel(ctx,task_ctx->how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }

   libxl_device_nic_dispose(&nic);
   /*TC assert*/
   int nic_count = 0;
   nic_info = libxl_device_nic_list(ctx, domid, &nic_count);
   TC_ASSERT(nic_count == old_nic_count);
   free(nic_info);
#if 0 
   printf(" \n ************ DISK ADD ************** \n");
   int old_disk_count = 0;
   libxl_device_disk *disk_info = libxl_device_disk_list(ctx, domid, &old_disk_count);
   free(disk_info);

   libxl_device_disk disk;
   libxl_device_disk_init(&disk);
   rc = libxl_device_disk_add(ctx, domid, &disk, task_ctx->how);

   count = 0;
   cancelled = false;
   callback_data = false;
   event_r = NULL;
   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        if (count >= canc_point && !cancelled) {
        rc = libxl_ao_cancel(ctx,task_ctx->how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }

   libxl_device_disk_dispose(&disk);
   /*TC assert*/
   int disk_count = 0;
   disk_info = libxl_device_disk_list(ctx, domid, &disk_count);
   TC_ASSERT(disk_count == old_disk_count);
   free(disk_info);

#endif
   printf(" \n ************ VTPM ADD ************** \n");
   int old_vtpm_count = 0;
   libxl_device_vtpm *vtpm_info = libxl_device_vtpm_list(ctx, domid, &old_vtpm_count);
   free(vtpm_info);

   libxl_device_vtpm vtpm;
   libxl_device_vtpm_init(&vtpm);
   vtpm.backend_domid = domid;
   libxl_uuid_generate(&vtpm.uuid);
   rc = libxl_device_vtpm_add(ctx, domid, &vtpm, task_ctx->how);

   count = 0;
   cancelled = false;
   callback_data = false;
   event_r = NULL;
   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        if (count >= canc_point && !cancelled) {
        rc = libxl_ao_cancel(ctx,task_ctx->how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }

   libxl_device_vtpm_dispose(&vtpm);
   /*TC assert*/
   int vtpm_count = 0;
   vtpm_info = libxl_device_vtpm_list(ctx, domid, &vtpm_count);
   TC_ASSERT(vtpm_count == old_vtpm_count);
   free(vtpm_info);


   printf(" \n ************ VKB ADD ************** \n");
   libxl_device_vkb vkb;
   libxl_device_vkb_init(&vkb);
   count = 0;
   cancelled = false;
   callback_data = false;
   event_r = NULL;
   rc = libxl_device_vkb_add(ctx, domid, &vkb, task_ctx->how);

   for (;;) {
        /* Stop looping through if callback is called and operation has exited*/
        if(callback_data)
           break;

        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ********* OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return rc;
        }  
        count++;
        if (count >= canc_point && !cancelled) {
        rc = libxl_ao_cancel(ctx,task_ctx->how);
        if(rc)
           printf(" In: %s: ******CANCELLATION FAILED [rc = %d] *********** \n", __FUNCTION__, rc);
        else {
           cancelled = true;
           printf(" In: %s: ******CANCELLATION SUCCESS *********** \n", __FUNCTION__);
        }
           
       }
       libxl_event_free(ctx, event_r);
       event_r = NULL;
    }

   libxl_device_vkb_dispose(&vkb);
   /*TC assert*/
 
   printf(" \n ************ Domain Destroy ************** \n");
   libxl_domain_destroy(ctx, domid, 0);
   rc = 0;

out:
   libxl_domain_config_dispose(&dc);
   free(config_data);
   return rc;
 
}

int TC6_1_device_add(task_canc_ctx *task_ctx)
{

   printf("*************** In: %s ********************\n",__FUNCTION__);
   TC_NAME(__FUNCTION__);
   return TC_device_add(task_ctx, "myfirstdomU.cfg", 1);
}


void * thread_fn_for_waiting_for_events(void *args)
{
   libxl_event * event_r;
   int rc = 0;
   libxl_ctx *ctx = (libxl_ctx*) args;
   for (;;) {

        printf(" In: %s: ****@@@@@@@@@@@@@@@@@@@@@@@@@*************** \n", __FUNCTION__);
        rc = libxl_event_wait(ctx, &event_r, LIBXL_EVENTMASK_ALL, 0,0);
        printf(" In: %s: ****@@@@@@@@@@@@@@@@@@@**** OUT OF LOOOP ********** \n", __FUNCTION__);
        if (rc) {
            return NULL;
        }  
    }
}

void * thread_fn_for_triggering_cancel (void *args)
{
   task_canc_ctx * task_ctx = (task_canc_ctx*) args; 
   int rc = -1;
   
   /*Wait for the trigger from thread_fn_for_cancelllation function*/
   sem_wait(&task_ctx->trigger_canc);
   
   printf("In: %s: ******** TRIGGERING AO_CANCEL ********\n",__FUNCTION__);
   rc = libxl_ao_cancel(task_ctx->ctx, NULL);
   /*assert(rc == 0);*/

   return NULL;
}

void async_op_callback(libxl_ctx *ctx, int rc, void *for_callback)
{
    UNUSED(ctx);
    UNUSED(rc);
    printf("*************IN: OPERATION CALLBACK (rc = %d) ********************\n",rc);
    sem_t *data = (sem_t*)for_callback;
    callback_data = true;
    sem_post(data);
}


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
   sem_init(&task_ctx->trigger_canc, 0, 0);

   libxl_asyncop_how *how = (libxl_asyncop_how*) malloc(sizeof(libxl_asyncop_how));
   how->callback = async_op_callback;
   how->u.for_callback = (void*)&task_ctx->trigger_canc;
   task_ctx->how = how;

   rc = TC_fn(task_ctx);
   if(rc)
      goto error;

error:
   if (how)
      free(how);
   sem_destroy(&task_ctx->trigger_canc);
   free(task_ctx);
   return rc;

}

void sigintsignalhndl(int sig_num);
void sigintsignalhndl(int sig_num)
{
    if(sig_num == SIGINT)
    {
       printf("\n User pressed Ctrl+C \n");
       DEINIT_TC_ASSERT;
       exit(1);
    }
    else if(sig_num == SIGSEGV)
    {
       printf("\n Test Crashed\n");
       void *bt_arr[10];
       size_t bt_size;
       char **bt;

       bt_size = backtrace (bt_arr, 10);
       bt = backtrace_symbols (bt_arr, bt_size);

       for (int itr = 0; itr < bt_size; ++itr)
          printf ("%s\n", bt[itr]);

       free (bt);
 
       DEINIT_TC_ASSERT;
       abort();
    }
}

int main(int argc, char **argv) {
    int rc;

    signal(SIGINT, sigintsignalhndl);
    signal(SIGSEGV, sigintsignalhndl);
   
    test_common_setup(XTL_DEBUG);
    INIT_TC_ASSERT;

    RUN_TEST(TC6_1_device_add);

#if 0
    RUN_TEST(TC1_1_domain_create_pv);
    RUN_TEST(TC1_2_domain_create_pv);
    RUN_TEST(TC1_3_domain_create_pv);
    RUN_TEST(TC1_4_domain_create_pv);
    

    RUN_TEST(TC3_1_domain_suspend);
    RUN_TEST(TC3_2_domain_suspend);
    RUN_TEST(TC3_3_domain_suspend);
/*
    RUN_TEST(TC3_4_domain_suspend);
    RUN_TEST(TC3_5_domain_suspend);
    RUN_TEST(TC3_6_domain_suspend);
    RUN_TEST(TC3_7_domain_suspend);
*/

    RUN_TEST(TC5_cancel_after_operation);
/*
    RUN_TEST(TC2_1_domain_create_hv);
*/
    RUN_TEST(TC2_2_domain_create_hv);

    RUN_TEST(TC4_domain_destroy);
#endif

    DEINIT_TC_ASSERT;
    libxl_ctx_free(ctx);
    return 0;
}

