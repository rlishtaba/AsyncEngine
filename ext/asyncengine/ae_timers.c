#include "asyncengine_ruby.h"
#include "ae_timers.h"


// Global variables defined in asyncengine_ruby.c.
extern VALUE cAsyncEngineCData;
extern ID att_c_data;
extern ID att_handle_terminated;
extern ID id_method_call;


typedef struct {
  uv_timer_t *_uv_handle;
  int periodic;
  VALUE rb_handle_id;
} struct_AsyncEngine_timer_handle;


static
void timer_close_cb(uv_handle_t* handle)
{
  AE_TRACE();
  xfree(handle);
}


static
void deallocate(struct_AsyncEngine_timer_handle* cdata)
{
  AE_TRACE();

  // Let the GC work.
  AsyncEngine_remove_handle(cdata->rb_handle_id);
  // Close the timer so it's unreferenced by uv.
  uv_close((uv_handle_t *)cdata->_uv_handle, timer_close_cb);
  // Free memory.
  xfree(cdata);
}


static
void execute_callback_with_gvl(VALUE rb_handle_id)
{
  AE_TRACE();
  rb_funcall(AsyncEngine_get_handle(rb_handle_id), id_method_call, 0, 0);
}


static
void AsyncEngine_timer_callback(uv_timer_t* handle, int status)
{
  AE_TRACE();
  struct_AsyncEngine_timer_handle* cdata = (struct_AsyncEngine_timer_handle*)handle->data;

  // Run callback.
  rb_thread_call_with_gvl(execute_callback_with_gvl, cdata->rb_handle_id);

  // Terminate the timer if it is not periodic.
  if (cdata->periodic == 0)
    deallocate(cdata);
}


VALUE AsyncEngine_c_add_timer(VALUE self, VALUE rb_delay, VALUE rb_interval, VALUE callback)
{
  AE_TRACE();
  uv_timer_t* _uv_handle = ALLOC(uv_timer_t);
  struct_AsyncEngine_timer_handle* cdata = ALLOC(struct_AsyncEngine_timer_handle);
  long delay, interval;

  cdata->_uv_handle = _uv_handle;

  delay = NUM2LONG(rb_delay);
  if (NIL_P(rb_interval)) {
    interval = 0;
    cdata->periodic = 0;
  }
  else {
    interval = NUM2LONG(rb_interval);
    if (interval == 0)  interval = 1;
    cdata->periodic = 1;
  }

  // Save the callback from being GC'd.
  cdata->rb_handle_id = AsyncEngine_store_handle(callback);

  // Initialize.
  uv_timer_init(uv_default_loop(), _uv_handle);
  _uv_handle->data = cdata;

  uv_timer_start(_uv_handle, AsyncEngine_timer_callback, delay, interval);

  return Data_Wrap_Struct(cAsyncEngineCData, NULL, NULL, cdata);
}


VALUE AsyncEngineTimer_cancel(VALUE self)
{
  AE_TRACE();
  struct_AsyncEngine_timer_handle* cdata;

  if (! NIL_P(rb_ivar_get(self, att_handle_terminated)))
    return Qfalse;
  rb_ivar_set(self, att_handle_terminated, Qtrue);

  Data_Get_Struct(rb_ivar_get(self, att_c_data), struct_AsyncEngine_timer_handle, cdata);

  // Stop timer.
  uv_timer_stop(cdata->_uv_handle);

  // Terminate the timer.
  deallocate(cdata);

  return Qtrue;
}


VALUE AsyncEngineTimer_c_set_interval(VALUE self, VALUE rb_interval)
{
  AE_TRACE();
  struct_AsyncEngine_timer_handle* cdata;
  long interval;
  
  if (! NIL_P(rb_ivar_get(self, att_handle_terminated)))
    return Qfalse;

  Data_Get_Struct(rb_ivar_get(self, att_c_data), struct_AsyncEngine_timer_handle, cdata);

  interval = NUM2LONG(rb_interval);
  if (interval == 0)  interval = 1;
  
  uv_timer_set_repeat(cdata->_uv_handle, interval);
  return rb_interval;
}