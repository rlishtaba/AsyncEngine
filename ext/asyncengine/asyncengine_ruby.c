#include "asyncengine_ruby.h"
#include "ae_handle_common.h"
#include "ae_async.h"
#include "ae_timer.h"
#include "ae_next_tick.h"
#include "ae_udp.h"
#include "ae_utils.h"
#include "ae_ip_utils.h"


static ID att_handles;
static ID att_blocks;
static ID const_UV_ERRNOS;

static int initialized;
static int is_ready_for_handles;
static int do_stop;


// TODO: Temporal function for debugging purposes, since active_handles
// will be removed soon from UV.
static
long ae_uv_num_active_handlers(void)
{
  ngx_queue_t *q;
  long num_active_handlers = 0;

  if (! initialized)
    return 0;

  ngx_queue_foreach(q, &AE_uv_loop->active_handles) {
    num_active_handlers++;
  }
  return num_active_handlers;
}


VALUE AsyncEngine_num_uv_active_handles(VALUE self)
{
  AE_TRACE();

  return LONG2FIX(ae_uv_num_active_handlers());
}


VALUE AsyncEngine_init(VALUE self)
{
  AE_TRACE();

  if (initialized)
    return Qfalse;

  AE_handles = rb_ivar_get(mAsyncEngine, att_handles);
  AE_blocks = rb_ivar_get(mAsyncEngine, att_blocks);
  AE_UV_ERRNOS = rb_const_get(mAsyncEngine, const_UV_ERRNOS);

  /* Set the UV loop. */
  AE_uv_loop = uv_default_loop();

  initialized = 1;
  return Qtrue;
}


VALUE AsyncEngine_pre_run(VALUE self)
{
  AE_TRACE();

  /* Load the UV idle (next tick) now. */
  load_ae_next_tick_uv_idle();

  /* Set is_ready_for_handles=1 so handles can be added. */
  is_ready_for_handles = 1;
  do_stop = 0;

  return Qtrue;
}


static
void ae_ubf_uv_async_callback(uv_async_t* handle, int status)
{
  AE_TRACE();

  // TODO: testing, can be error?
  AE_ASSERT(! status);

  uv_close((uv_handle_t *)handle, ae_uv_handle_close_callback);

  AE_DEBUG("call rb_thread_call_with_gvl(rb_thread_check_ints, NULL)");
  rb_thread_call_with_gvl(rb_thread_check_ints, NULL);
}


static
void ae_ubf(void)
{
  AE_TRACE();

  /*
   * When a signal is received by a Ruby process running a blocking code (without GVL)
   * Ruby calls the ubf() function. But this ubf() function could be called also from
   * other thread (i.e. ae_tread.kill) so we don't know if the ubf() is being executing
   * in AE thread, nor if it has been called due a received signal or a Thread#kill.
   * Therefore, do nothing but check interrupts in Ruby land via a thread safe uv_async.
   */

  uv_async_t* ae_ubf_uv_async = ALLOC(uv_async_t);

  AE_ASSERT(! uv_async_init(AE_uv_loop, ae_ubf_uv_async, ae_ubf_uv_async_callback));
  AE_ASSERT(! uv_async_send(ae_ubf_uv_async));
}


static
VALUE run_uv_without_gvl(void)
{
  AE_TRACE();

  /* Run UV loop until there are no more active handles or do_stop
   * has been set to 1 (by AsyncEngine.stop). */
  AE_DEBUG("uv_run_once() loop starts...");

  // TODO: for testing.
  AE_ASSERT(do_stop == 0);

  while(!do_stop && uv_run_once(AE_uv_loop));

  AE_DEBUG("uv_run_once() loop terminates");

  do_stop = 0;
  is_ready_for_handles = 0;

  return Qtrue;
}


VALUE AsyncEngine_run_uv(VALUE self)
{
  AE_TRACE();

  rb_thread_call_without_gvl(run_uv_without_gvl, NULL, ae_ubf, NULL);

  AE_DEBUG("function terminates");

  return Qtrue;
}


static
VALUE run_uv_once_without_gvl(void)
{
  AE_TRACE();

  /* There MUST NOT be UV active handles at this time, we enter here just to
   * iterate once for freeing closed UV handles not freed yet (it's required
   * a UV iteration for uv_close callbacks to be called).
   * NOTE: If the blocks just contains a next_tick and raises, next_tick idle is
   * not removed by AE.destroy_ae_handles:
   *    AE.run { AE.next_tick { } ; RAISE_1 }
   * Therefore here we check that there are 0 or 1 UV active handles.
   */
  AE_ASSERT(ae_uv_num_active_handlers() <= 1);

  /* Run UV loop (it blocks if there were handles in the given block). */
  AE_DEBUG("uv_run_once() starts...");
  uv_run_once(AE_uv_loop);
  AE_DEBUG("uv_run_once() terminates");

  /* Close the UV idle (next tick) now. */
  unload_ae_next_tick_uv_idle();

  do_stop = 0;
  is_ready_for_handles = 0;

  return Qtrue;
}


/*
 * This method is called in the _ensure_ block of AsyncEngine.run() method
 * after AsyncEngine.destroy_ae_handles() to cause a single UV iteration in order
 * to invoke all the uv_close callbacks and free() the uv_handles.
 */
VALUE AsyncEngine_run_uv_once(VALUE self)
{
  AE_TRACE();

  rb_thread_call_without_gvl(run_uv_once_without_gvl, NULL, NULL, NULL);

  AE_DEBUG("function terminates");

  return Qtrue;
}


VALUE AsyncEngine_stop_uv(VALUE self)
{
  AE_TRACE();

  do_stop = 1;
}


static
int ae_is_ready_for_handles(void)
{
  AE_TRACE();

  // TODO: Not sure which is better, theorically second one is "safer".
  //return is_ready_for_handles;
  return (is_ready_for_handles && !do_stop);
}


VALUE AsyncEngine_is_ready_for_handles(VALUE self)
{
  AE_TRACE();

  return ae_is_ready_for_handles();
}


VALUE AsyncEngine_ensure_ready_for_handles(VALUE self)
{
  AE_TRACE();

  if (ae_is_ready_for_handles())
    return Qtrue;
  else
    rb_raise(eAsyncEngineError, "AsyncEngine is not ready yet");
}




void Init_asyncengine_ext()
{
  AE_TRACE();

  mAsyncEngine = rb_define_module("AsyncEngine");
  cAsyncEngineHandle = rb_define_class_under(mAsyncEngine, "Handle", rb_cObject);
  eAsyncEngineError = rb_define_class_under(mAsyncEngine, "Error", rb_eStandardError);

  rb_define_module_function(mAsyncEngine, "init", AsyncEngine_init, 0);
  rb_define_module_function(mAsyncEngine, "pre_run", AsyncEngine_pre_run, 0);
  rb_define_module_function(mAsyncEngine, "run_uv", AsyncEngine_run_uv, 0);
  rb_define_module_function(mAsyncEngine, "run_uv_once", AsyncEngine_run_uv_once, 0);
  rb_define_module_function(mAsyncEngine, "stop_uv", AsyncEngine_stop_uv, 0);
  rb_define_module_function(mAsyncEngine, "ready_for_handles?", AsyncEngine_is_ready_for_handles, 0);
  rb_define_module_function(mAsyncEngine, "ensure_ready_for_handles", AsyncEngine_ensure_ready_for_handles, 0);
  rb_define_module_function(mAsyncEngine, "num_uv_active_handles", AsyncEngine_num_uv_active_handles, 0);

  att_handles = rb_intern("@_handles");
  att_blocks = rb_intern("@_blocks");
  const_UV_ERRNOS = rb_intern("UV_ERRNOS");

  init_ae_handle_common();
  init_ae_async();
  init_ae_timer();
  init_ae_next_tick();
  init_ae_udp();
  init_ae_utils();
  init_ae_ip_utils();
  init_rb_utilities();

  initialized = 0;
  is_ready_for_handles = 0;
  do_stop = 0;
  AE_uv_loop = NULL;
}
