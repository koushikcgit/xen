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
#include <poll.h>
#include <semaphore.h>
#include <signal.h>
#include "libxl_internal.h"
#include "libxl_test_asynctaskcanc.h"

extern bool sync_test;

/*thread function that monitors the cancellation point, using a busy wait and then signals the trigger of cancellation*/
void* thread_fn_for_cancellation(void* args)
{
   task_canc_ctx* ctx = (task_canc_ctx*)args;
   /*Look for the ao*/
   printf("In: %s: Waiting for AO_CREATE\n",__FUNCTION__);
   while(LIBXL_LIST_EMPTY(&ctx->ctx->aos_inprogress));

   libxl__ao *ao = LIBXL_LIST_FIRST(&ctx->ctx->aos_inprogress); /*There is only one ao being tested at a time*/
   while(1)
   {
      sem_wait(&ao->canc_point_check);
      if(ao->curr_nr_of_canc_points == LIBXL__AO_MAGIC_DESTROYED)
         break;
      if(ctx->target_canc_point <= ao->curr_nr_of_canc_points)
      {
         printf("In: %s: ************ AO CANCELLATION POINT REACHED **************\n",__FUNCTION__);
         break;
      }
      else
      {
         printf("In: %s: ************ AO CANCELLATION PENDING **************\n",__FUNCTION__);
/*         if(sync_test)
            sem_post(&ao->canc_point_go);
*/
      }
   }

   /*cancel the operation*/
/*
   sem_post(&ao->canc_point_go);
*/
   sem_post(&ctx->trigger_canc);

   return NULL;
   
}

/************************* END OF FILE **************************************/
