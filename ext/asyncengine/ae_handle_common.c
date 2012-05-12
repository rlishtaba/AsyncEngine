#include "asyncengine_ruby.h"
#include "ae_handle_common.h"


// C variable holding current block number.
static long block_id;
// C variable holding current handle number.
static long handle_id;

// Ruby modules/classe.
static VALUE mKernel;

// Ruby attributes.
static ID att_blocks;
static ID att_handles;

// Ruby method names.
static ID method_call;
static ID method_handle_exception;
static ID method_raise;

// Ruby constanst.
static ID const_UV_ERRNOS;


void init_ae_handle_common()
{
  AE_TRACE();

  mKernel = rb_define_module("Kernel");
  cAsyncEngineCData = rb_define_class_under(mAsyncEngine, "CData", rb_cObject);

  att_blocks = rb_intern("@_blocks");
  att_handles = rb_intern("@_handles");
  att_cdata = rb_intern("@_cdata");
  att_handle_terminated = rb_intern("@_handle_terminated");

  method_call = rb_intern("call");
  method_handle_exception = rb_intern("handle_exception");
  method_raise = rb_intern("raise");

  const_UV_ERRNOS = rb_intern("UV_ERRNOS");

  block_id = 0;
  handle_id = 0;
}


VALUE ae_store_handle(VALUE rb_handle)
{
  AE_TRACE();

  VALUE rb_handle_id = LONG2FIX(++handle_id);

  rb_hash_aset(rb_ivar_get(mAsyncEngine, att_handles), rb_handle_id, rb_handle);
  return rb_handle_id;
}


VALUE ae_get_handle(VALUE rb_handle_id)
{
  AE_TRACE();

  return rb_hash_aref(rb_ivar_get(mAsyncEngine, att_handles), rb_handle_id);
}


VALUE ae_remove_handle(VALUE rb_handle_id)
{
  AE_TRACE();

  return rb_hash_delete(rb_ivar_get(mAsyncEngine, att_handles), rb_handle_id);
}


VALUE ae_store_block(VALUE rb_block)
{
  AE_TRACE();

  VALUE rb_block_id = LONG2FIX(++block_id);

  rb_hash_aset(rb_ivar_get(mAsyncEngine, att_blocks), rb_block_id, rb_block);
  return rb_block_id;
}


VALUE ae_get_block(VALUE rb_block_id)
{
  AE_TRACE();

  return rb_hash_aref(rb_ivar_get(mAsyncEngine, att_blocks), rb_block_id);
}


VALUE ae_remove_block(VALUE rb_block_id)
{
  AE_TRACE();

  return rb_hash_delete(rb_ivar_get(mAsyncEngine, att_blocks), rb_block_id);
}


void ae_uv_handle_close_callback(uv_handle_t* handle)
{
  AE_TRACE();

  xfree(handle);
}


VALUE ae_get_uv_error(void)
{
  AE_TRACE();

  VALUE ae_uv_error;

  if (NIL_P(ae_uv_error = rb_hash_aref(rb_const_get(mAsyncEngine, const_UV_ERRNOS), AE_FIXNUM_LAST_UV_ERRNO())))
    ae_uv_error = rb_hash_aref(rb_const_get(mAsyncEngine, const_UV_ERRNOS), INT2FIX(-1));

  return ae_uv_error;
}


void ae_raise_last_uv_errno(void)
{
  AE_TRACE();

  VALUE ae_uv_error = ae_get_uv_error();

  rb_funcall2(mKernel, method_raise, 1, &ae_uv_error);
}


VALUE ae_block_call_0(VALUE rb_block)
{
  AE_TRACE();

  return rb_funcall2(rb_block, method_call, 0, NULL);
}


VALUE ae_block_call_1(VALUE rb_block, VALUE param)
{
  AE_TRACE();

  return rb_funcall2(rb_block, method_call, 1, &param);
}


static
void ae_handle_exception(int exception_tag)
{
  AE_TRACE();

  // rb_errinfo() gives the current exception object in this thread.
  VALUE exception = rb_errinfo();

  // Just check the exception in the user provided AE.exception_handler block if
  // it is a StandardError. Otherwise raise it and terminate.
  if (rb_obj_is_kind_of(exception, rb_eStandardError) == Qtrue) {
    rb_funcall2(mAsyncEngine, method_handle_exception, 1, &exception);
    // Dissable the current thread exception.
    rb_set_errinfo(Qnil);
  }
  else
    rb_jump_tag(exception_tag);
}


typedef struct {
  function_with_gvl_and_protect function;
  VALUE rb_param;
} struct_function_with_gvl_and_protect_data;


static
VALUE execute_function_with_gvl(struct_function_with_gvl_and_protect_data *data)
{
  AE_TRACE();

  int exception_tag;
  VALUE ret;

  ret = rb_protect(data->function, data->rb_param, &exception_tag);

  if (exception_tag)
    ae_handle_exception(exception_tag);

  return ret;
}


/*
 * Executes the given function taking the GVL and using rb_protect(), and passes
 * rb_param as a single argument to the given function.
 */
VALUE ae_execute_function_with_gvl_and_protect(function_with_gvl_and_protect function, VALUE rb_param)
{
  AE_TRACE();

  struct_function_with_gvl_and_protect_data data;
  data.function = function;
  data.rb_param = rb_param;

  return rb_thread_call_with_gvl(execute_function_with_gvl, &data);
}