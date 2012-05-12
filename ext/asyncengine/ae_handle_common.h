#ifndef AE_HANDLE_COMMON_H
#define AE_HANDLE_COMMON_H


#include "libuv/include/uv.h"  // NOTE: Not needed if dir_config() line is enabled in extconf.rb.
//#include <uv.h>              // so this line becomes enough.
#include <assert.h>


#define AE_FIXNUM_UV_LAST_ERROR()  INT2FIX(uv_last_error(uv_default_loop()).code)


VALUE cAsyncEngineCData;

ID att_cdata;
ID att_handle_terminated; // TODO: Fuera, hacer como en UDP y meterlo en el cdata struct.

void init_ae_handle_common();

VALUE ae_store_handle(VALUE);
VALUE ae_get_handle(VALUE);
VALUE ae_remove_handle(VALUE);

VALUE ae_store_block(VALUE);
VALUE ae_get_block(VALUE);
VALUE ae_remove_block(VALUE);

void ae_handle_exception(int);
void ae_uv_handle_close_callback(uv_handle_t*);
VALUE ae_protect_block_call(VALUE block, int *exception_tag);

typedef VALUE (*execute_method_with_protect)(VALUE param);
void execute_method_with_gvl_and_protect(execute_method_with_protect method_with_protect, void *param);


#endif  /* AE_HANDLE_COMMON_H */