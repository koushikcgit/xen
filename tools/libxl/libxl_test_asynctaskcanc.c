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
#include <signal.h>
#include "libxl_internal.h"
#include "libxl_test_asynctaskcanc.h"

/*thread function that monitors the cancellation point, using a busy wait and then signals the trigger of cancellation*/
void* thread_fn_for_cancellation(void* args)
{
   task_canc_ctx* ctx = (task_canc_ctx*)args;
   /*Look for the ao*/
   printf("In: %s: Waiting for AO_CREATE\n",__FUNCTION__);
   while(LIBXL_LIST_EMPTY(&ctx->ctx->aos_inprogress));

   libxl__ao *ao = LIBXL_LIST_FIRST(&ctx->ctx->aos_inprogress); /*There is only one ao being tested at a time*/
  /* while((ctx->target_canc_point > ao->curr_nr_of_canc_points) && (ao->magic != LIBXL__AO_MAGIC_DESTROYED));*/
   while((ctx->target_canc_point > ao->curr_nr_of_canc_points) && (!LIBXL_LIST_EMPTY(&ctx->ctx->aos_inprogress)));

   /*cancel the operation*/
   printf("In: %s: Signalling cancellation\n",__FUNCTION__);
   ctx->trigger_canc = true;

   return NULL;
   
}

/************************* END OF FILE **************************************/
