#ifndef AE_HANDLE_COMMON_H
#define AE_HANDLE_COMMON_H


#define AE_CLOSE_UV_HANDLE(handle)  \
  uv_close((uv_handle_t *)handle, ae_uv_handle_close_callback);

// TODO: esto lo quitaré y haré que send_data() y send_datagram() devuelvan false y a correr.
// Get this uv_errno when the socket is not connected.
#define AE_UV_ERRNO_SOCKET_NOT_CONNECTED  31

#define AE_UV_ERRNO_ETIMEDOUT  40


void init_ae_handle_common(void);

VALUE ae_store_handle(VALUE);
VALUE ae_get_handle(VALUE);
VALUE ae_remove_handle(VALUE);

VALUE ae_store_block(VALUE);
VALUE ae_get_block(VALUE);
VALUE ae_remove_block(VALUE);

// TODO: Remove it from the API when all the handles use AE_CLOSE_UV_HANDLE(handle).
void ae_uv_handle_close_callback(uv_handle_t*);

int ae_get_last_uv_error_int(void);
VALUE ae_get_uv_error(int uv_errno);
VALUE ae_get_last_uv_error(void);
void ae_raise_uv_error(int uv_errno);
void ae_raise_last_uv_error(void);

VALUE ae_block_call_0(VALUE rb_block);
VALUE ae_block_call_1(VALUE rb_block, VALUE param);

VALUE ae_execute_in_ruby_land(void* function);


#endif  /* AE_HANDLE_COMMON_H */