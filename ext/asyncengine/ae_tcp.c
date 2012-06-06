#include "asyncengine_ruby.h"
#include "ae_handle_common.h"
#include "ae_ip_utils.h"
#include "ae_tcp.h"


#define GET_CDATA_FROM_SELF  \
  struct_cdata* cdata;  \
  Data_Get_Struct(self, struct_cdata, cdata)

#define CHECK_UV_HANDLE_IS_OPEN  \
  if (! (cdata->_uv_handle))  \
    return Qfalse;

#define GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN  \
  GET_CDATA_FROM_SELF;  \
  CHECK_UV_HANDLE_IS_OPEN


static VALUE cAsyncEngineTcpSocket;

static ID method_on_connected;
static ID method_on_connection_error;
static ID method_on_data_received;
static ID method_on_disconnected;

static VALUE symbol_closed;
static VALUE symbol_connecting;
static VALUE symbol_connected;


enum status {
  CONNECTING = 0,
  CONNECTED
};

enum flags {
  PAUSED = 1,
  CLOSING_GRACEFULLY,
  CONNECT_TIMEOUT
};

typedef struct {
  uv_tcp_t *_uv_handle;
  VALUE ae_handle;
  VALUE ae_handle_id;
  enum_ip_type ip_type;
  unsigned int status;
  unsigned int flags;
  enum_string_encoding encoding;
  uv_timer_t *_uv_timer_connect_timeout;
} struct_cdata;

struct _uv_connect_callback_data {
  struct_cdata* cdata;
  int status;
};

struct _uv_read_callback_data {
  struct_cdata* cdata;
  ssize_t nread;
  uv_buf_t buf;
};

typedef struct {
  uv_buf_t buf;
  VALUE *on_write_block;
} struct_uv_write_req_data;

struct _uv_write_callback_data {
  VALUE *on_write_block;
  int status;
};

struct _uv_shutdown_callback_data {
  struct_cdata* cdata;
};

struct _uv_timer_connect_timeout_callback_data {
  struct_cdata* cdata;
};

// Used for storing information about the last TCP connect callback.
static struct _uv_connect_callback_data last_uv_connect_callback_data;

// Used for storing information about the last TCP read callback.
static struct _uv_read_callback_data last_uv_read_callback_data;

// Used for storing information about the last TCP write callback.
static struct _uv_write_callback_data last_uv_write_callback_data;

// Used for storing information about the last TCP shutdown callback.
static struct _uv_shutdown_callback_data last_uv_shutdown_callback_data;

// Used for storing information about the last TCP connection timeout callback.
static struct _uv_timer_connect_timeout_callback_data last_uv_timer_connect_timeout_callback_data;


/** Predeclaration of static functions. */

static void AsyncEngineTcpSocket_mark(void *ptr);
static void AsyncEngineTcpSocket_free(void* cdata);
static VALUE AsyncEngineTcpSocket_alloc(VALUE klass);
static void init_instance(VALUE self, enum_ip_type ip_type, char* dest_ip, int dest_port, char* bind_ip, int bind_port);
static void _uv_connect_callback(uv_connect_t* req, int status);
static uv_buf_t _uv_alloc_callback(uv_handle_t* handle, size_t suggested_size);
static void _uv_read_callback(uv_stream_t* stream, ssize_t nread, uv_buf_t buf);
static VALUE _ae_connect_callback(void);
static void _uv_connect_callback(uv_connect_t* req, int status);
static VALUE _ae_read_callback(void);
static void _uv_read_callback(uv_stream_t* stream, ssize_t nread, uv_buf_t buf);
static void _uv_write_callback(uv_write_t* req, int status);
static VALUE _ae_write_callback(void);
static void _uv_shutdown_callback(uv_shutdown_t* req, int status);
static VALUE _ae_shutdown_callback(void);
static void _uv_timer_connect_timeout_callback(uv_timer_t* handle, int status);
static void _ae_cancel_timer_connect_timeout(struct_cdata* cdata);


void init_ae_tcp()
{
  AE_TRACE();

  cAsyncEngineTcpSocket = rb_define_class_under(mAsyncEngine, "TCPSocket", cAsyncEngineHandle);

  rb_define_alloc_func(cAsyncEngineTcpSocket, AsyncEngineTcpSocket_alloc);
  rb_define_singleton_method(cAsyncEngineTcpSocket, "new", AsyncEngineTcpSocket_new, -1);

  rb_define_method(cAsyncEngineTcpSocket, "send_data", AsyncEngineTcpSocket_send_data, -1);
  rb_define_method(cAsyncEngineTcpSocket, "local_address", AsyncEngineTcpSocket_local_address, 0);
  rb_define_method(cAsyncEngineTcpSocket, "peer_address", AsyncEngineTcpSocket_peer_address, 0);
  rb_define_method(cAsyncEngineTcpSocket, "set_connect_timeout", AsyncEngineTcpSocket_set_connect_timeout, 1);
  rb_define_alias(cAsyncEngineTcpSocket, "connect_timeout=", "set_connect_timeout");
  rb_define_method(cAsyncEngineTcpSocket, "status", AsyncEngineTcpSocket_status, 0);
  rb_define_method(cAsyncEngineTcpSocket, "connected?", AsyncEngineTcpSocket_is_connected, 0);
  rb_define_method(cAsyncEngineTcpSocket, "alive?", AsyncEngineTcpSocket_is_alive, 0);
  rb_define_method(cAsyncEngineTcpSocket, "close", AsyncEngineTcpSocket_close, 0);
  rb_define_method(cAsyncEngineTcpSocket, "close_gracefully", AsyncEngineTcpSocket_close_gracefully, -1);
  rb_define_private_method(cAsyncEngineTcpSocket, "destroy", AsyncEngineTcpSocket_destroy, 0);

  method_on_connected = rb_intern("on_connected");
  method_on_connection_error = rb_intern("on_connection_error");
  method_on_data_received = rb_intern("on_data_received");
  method_on_disconnected = rb_intern("on_disconnected");

  symbol_closed = ID2SYM(rb_intern("closed"));
  symbol_connecting = ID2SYM(rb_intern("connecting"));
  symbol_connected = ID2SYM(rb_intern("connected"));
}


/** Class alloc and free functions. */

static
VALUE AsyncEngineTcpSocket_alloc(VALUE klass)
{
  AE_TRACE();

  struct_cdata* cdata = ALLOC(struct_cdata);

  return Data_Wrap_Struct(klass, AsyncEngineTcpSocket_mark, AsyncEngineTcpSocket_free, cdata);
}


static
void AsyncEngineTcpSocket_mark(void *ptr)
{
  AE_TRACE();

  struct_cdata* cdata = (struct_cdata*)ptr;
}


static void AsyncEngineTcpSocket_free(void *cdata)
{
  AE_TRACE2();

  xfree(cdata);
}


/** Class new() method.
 *
 * Arguments:
 * - destination IP (String).
 * - destination port (Fixnum).
 * - bind IP (String or Nil) (optional).
 * - bind port (Fixnum or Nil) (optional).
 */

VALUE AsyncEngineTcpSocket_new(int argc, VALUE *argv, VALUE self)
{
  AE_TRACE();

  char *dest_ip, *bind_ip;
  int dest_ip_len, bind_ip_len;
  int dest_port, bind_port;
  enum_ip_type ip_type, bind_ip_type;
  VALUE klass, instance;

  ae_ensure_ready_for_handles();

  AE_RB_CHECK_NUM_ARGS(2,4);

  // Parameter 1: destination IP.
  if (TYPE(argv[0]) != T_STRING)
    rb_raise(rb_eTypeError, "destination IP must be a String");
  dest_ip = StringValueCStr(argv[0]);
  dest_ip_len = RSTRING_LEN(argv[0]);

  // Parameter 2: destination port.
  if (! FIXNUM_P(argv[1]))
    rb_raise(rb_eTypeError, "destination port must be a Fixnum");
  dest_port = FIX2INT(argv[1]);
  if (! ae_ip_utils_is_valid_port(dest_port))
    rb_raise(rb_eArgError, "invalid destination port value");

  bind_ip = NULL;
  bind_port = 0;

  // Parameter 3: bind IP.
  if (argc >= 3) {
    switch(TYPE(argv[2])) {
      case T_STRING:
        bind_ip = StringValueCStr(argv[2]);
        bind_ip_len = RSTRING_LEN(argv[2]);
        break;
      case T_NIL:
        break;
      default:
        rb_raise(rb_eTypeError, "bind IP must be a String");
    }

    // Parameter 4: bind port.
    if (bind_ip && argc == 4) {
      switch(TYPE(argv[3])) {
        case T_FIXNUM:
          bind_port = FIX2INT(argv[3]);
          if (! ae_ip_utils_is_valid_port(bind_port))
            rb_raise(rb_eArgError, "invalid bind port value");
          break;
        case T_NIL:
          break;
        default:
          rb_raise(rb_eTypeError, "bind port must be a Fixnum");
          break;
      }
    }
  }

  // TODO: Allow domains y blablablablablablabla puto sag.
  // Parse destination IP.
  if ((ip_type = ae_ip_parser_execute(dest_ip, dest_ip_len)) == ip_type_no_ip)
    rb_raise(rb_eTypeError, "destination IP is not valid IPv4 or IPv6");

  // Parse bind IP.
  if (bind_ip) {
    if ((bind_ip_type = ae_ip_parser_execute(bind_ip, bind_ip_len)) == ip_type_no_ip)
      rb_raise(rb_eTypeError, "bind IP is not valid IPv4 or IPv6");
    else if (bind_ip_type != ip_type)
      rb_raise(rb_eTypeError, "bind IP does not belong to the same IP family of the destination IP");
  }

  // Allocate the Ruby instance.
  instance = rb_obj_alloc(self);

  // Init the UV stuff within the instance.
  init_instance(instance, ip_type, dest_ip, dest_port, bind_ip, bind_port);

  return instance;
}


static
void init_instance(VALUE self, enum_ip_type ip_type, char *dest_ip, int dest_port, char *bind_ip, int bind_port)
{
  AE_TRACE();

  uv_tcp_t *_uv_handle = NULL;
  uv_connect_t *_uv_tcp_connect_req = NULL;
  int ret;

  // Create and init the UV handle.
  _uv_handle = ALLOC(uv_tcp_t);
  if (uv_tcp_init(AE_uv_loop, _uv_handle)) {
    xfree(_uv_handle);  // TODO: OK?
    ae_raise_last_uv_error();
  }

  // Bind to the given IP (and optional port) if given.
  if (bind_ip) {
    switch(ip_type) {
      case ip_type_ipv4:
        ret = uv_tcp_bind(_uv_handle, uv_ip4_addr(bind_ip, bind_port));
        break;
      case ip_type_ipv6:
        ret = uv_tcp_bind6(_uv_handle, uv_ip6_addr(bind_ip, bind_port));
        break;
    }
    if (ret) {
      AE_CLOSE_UV_HANDLE(_uv_handle);
      ae_raise_last_uv_error();
    }
  }

  // Connect.
  _uv_tcp_connect_req = ALLOC(uv_connect_t);

  switch(ip_type) {
    case ip_type_ipv4:
      ret = uv_tcp_connect(_uv_tcp_connect_req, _uv_handle, uv_ip4_addr(dest_ip, dest_port), _uv_connect_callback);
      break;
    case ip_type_ipv6:
      ret = uv_tcp_connect6(_uv_tcp_connect_req, _uv_handle, uv_ip6_addr(dest_ip, dest_port), _uv_connect_callback);
      break;
  }
  if (ret) {
    xfree(_uv_tcp_connect_req);
    AE_CLOSE_UV_HANDLE(_uv_handle);
    ae_raise_last_uv_error();
  }

  // Start receiving (this will never return error).
  AE_ASSERT(! uv_read_start((uv_stream_t*)_uv_handle, _uv_alloc_callback, _uv_read_callback));

  GET_CDATA_FROM_SELF;

  // Fill cdata struct.
  cdata->_uv_handle = _uv_handle;
  cdata->ae_handle = self;
  cdata->ae_handle_id = ae_store_handle(self); // Avoid GC.
  cdata->ip_type = ip_type;
  cdata->status = CONNECTING;
  cdata->flags = 0;
  cdata->encoding = string_encoding_ascii;
  cdata->_uv_timer_connect_timeout = NULL;

  // Fill data field of the UV handle.
  cdata->_uv_handle->data = (void *)cdata;
}


/** TCP connect callback. */

static
void _uv_connect_callback(uv_connect_t* req, int status)
{
  AE_TRACE2();

  uv_tcp_t* _uv_handle = (uv_tcp_t*)req->handle;
  struct_cdata* cdata = (struct_cdata*)_uv_handle->data;

  xfree(req);

  last_uv_connect_callback_data.cdata = cdata;
  last_uv_connect_callback_data.status = status;

  if (! status) {
    cdata->status = CONNECTED;
  }
  // If it's a connection error and uv_is_closing() == 1 it means that the AE handle
  // was closed by the application, so don't close the UV handle again. Otherwise
  // close the UV handle.
  else if (! uv_is_closing((const uv_handle_t*)_uv_handle)) {
    AE_CLOSE_UV_HANDLE(_uv_handle);
    cdata->_uv_handle = NULL;
  }

  _ae_cancel_timer_connect_timeout(cdata);

  ae_execute_in_ruby_land(_ae_connect_callback);
}


static
VALUE _ae_connect_callback(void)
{
  AE_TRACE2();

  struct_cdata* cdata = last_uv_connect_callback_data.cdata;
  VALUE error;

  // Connection succedded, so call on_connected() callback in the instance.
  if (! last_uv_connect_callback_data.status) {
    rb_funcall2(cdata->ae_handle, method_on_connected, 0, NULL);
  }
  // Connection failed.
  else {
    ae_remove_handle(cdata->ae_handle_id);
    // Network error, remote rejection or client closed before connecting.
    if (! cdata->flags & CONNECT_TIMEOUT)
      error = ae_get_last_uv_error();
    // Connection timeout set by the user, so raise UV error 40: ETIMEDOUT, "connection timed out".
    else
      error = ae_get_uv_error(AE_UV_ERRNO_ETIMEDOUT);
    rb_funcall2(cdata->ae_handle, method_on_connection_error, 1, &error);
  }

  return Qnil;
}


/** TCP read callback. */

static
uv_buf_t _uv_alloc_callback(uv_handle_t* handle, size_t suggested_size)
{
  AE_TRACE();

  return uv_buf_init(ALLOC_N(char, suggested_size), suggested_size);
}


static
void _uv_read_callback(uv_stream_t* stream, ssize_t nread, uv_buf_t buf)
{
  AE_TRACE2();

  uv_tcp_t *_uv_handle = (uv_tcp_t *)stream;
  struct_cdata* cdata = (struct_cdata*)_uv_handle->data;

  last_uv_read_callback_data.cdata = cdata;
  last_uv_read_callback_data.nread = nread;
  last_uv_read_callback_data.buf = buf;

  /*
   * nread 0:  libuv requested a buffer through the alloc callback but then decided
   *           that it didn't need that buffer.
   * nread -1: disconnecton, so close the UV handle.
   *
   * In any case,  read alloc cb *may* be previously called so check the buffer pointer
   * and in case it's not NULL, free it.
   */
  if (nread == 0) {
    if (buf.base)
      xfree(buf.base);
  }
  else if (nread == -1) {
    if (buf.base)
      xfree(buf.base);
    // Once the connection is established, if the app calls to uv_close() this tcp_read_callback
    // could be called with nread=0, but never with nread=-1, so -1 means "network/remote disconnection".
    AE_CLOSE_UV_HANDLE(_uv_handle);
    cdata->_uv_handle = NULL;
    ae_execute_in_ruby_land(_ae_read_callback);
  }
  else {
    if (! cdata->flags & PAUSED)
      ae_execute_in_ruby_land(_ae_read_callback);
    else
      xfree(buf.base);
  }
}


static
VALUE _ae_read_callback(void)
{
  AE_TRACE2();

  struct_cdata* cdata = last_uv_read_callback_data.cdata;
  VALUE _rb_data;
  VALUE error;

  // Data received.
  if (last_uv_read_callback_data.nread > 0) {
    _rb_data = ae_rb_str_new(last_uv_read_callback_data.buf.base, last_uv_read_callback_data.nread, cdata->encoding, 1);
    xfree(last_uv_read_callback_data.buf.base);
    rb_funcall2(cdata->ae_handle, method_on_data_received, 1, &_rb_data);
  }
  // Otherwise it's -1 (EOF, disconnection).
  else {
    error = ae_get_last_uv_error();
    ae_remove_handle(cdata->ae_handle_id);
    rb_funcall2(cdata->ae_handle, method_on_disconnected, 1, &error);
  }

  return Qnil;
}


/** send_data() method. */

VALUE AsyncEngineTcpSocket_send_data(int argc, VALUE *argv, VALUE self)
{
  AE_TRACE2();

  char *data = NULL;
  int data_len;
  VALUE _rb_block;
  uv_write_t* _uv_write_req = NULL;
  uv_buf_t buf;
  struct_uv_write_req_data* _uv_write_req_data = NULL;

  AE_RB_CHECK_NUM_ARGS(1,2);
  AE_RB_GET_BLOCK_OR_PROC(2, _rb_block);

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  // Parameter 1: data.
  if (TYPE(argv[0]) != T_STRING)
    rb_raise(rb_eTypeError, "data must be a String");

  data_len = RSTRING_LEN(argv[0]);
  data = ALLOC_N(char, data_len);
  memcpy(data, RSTRING_PTR(argv[0]), data_len);

  buf = uv_buf_init(data, data_len);

  _uv_write_req_data = ALLOC(struct_uv_write_req_data);
  _uv_write_req_data->buf = buf;

  if (! NIL_P(_rb_block)) {
    _uv_write_req_data->on_write_block = ALLOC(VALUE);
    *(_uv_write_req_data->on_write_block) = _rb_block;
    //printf("--- AsyncEngineTcpSocket_send_data():  _uv_write_req_data->on_write_block = %x\n", _uv_write_req_data->on_write_block);
    rb_gc_register_address(_uv_write_req_data->on_write_block);
  }
  else
    _uv_write_req_data->on_write_block = NULL;

  _uv_write_req = ALLOC(uv_write_t);
  _uv_write_req->data = _uv_write_req_data;

  if (uv_write(_uv_write_req, (uv_stream_t *)cdata->_uv_handle, &buf, 1, _uv_write_callback)) {
    xfree(data);
    if (_uv_write_req_data->on_write_block) {
      rb_gc_unregister_address(_uv_write_req_data->on_write_block);
      xfree(_uv_write_req_data->on_write_block);
    }
    xfree(_uv_write_req_data);
    xfree(_uv_write_req);
    ae_raise_last_uv_error();
  }

  return Qtrue;
}


static
void _uv_write_callback(uv_write_t* req, int status)
{
  AE_TRACE2();

  uv_tcp_t *_uv_handle = (uv_tcp_t*)req->handle;
  struct_uv_write_req_data* _uv_write_req_data = (struct_uv_write_req_data*)req->data;
  int has_block = 0;

  // Block was provided so must go to Ruby land (thus don't free the req yet!).
  if (_uv_write_req_data->on_write_block) {
    last_uv_write_callback_data.on_write_block = _uv_write_req_data->on_write_block;
    last_uv_write_callback_data.status = status;
    has_block = 1;
  }

  xfree(_uv_write_req_data->buf.base);
  xfree(_uv_write_req_data);
  xfree(req);

  // NOTE: Even if the handle has been destroyed, we need to go to Ruby land to
  // unregister and free the write block.
  if (has_block)
    ae_execute_in_ruby_land(_ae_write_callback);
}


static
VALUE _ae_write_callback(void)
{
  AE_TRACE2();

  VALUE rb_on_write_block = *(last_uv_write_callback_data.on_write_block);

  //printf("--- ae_tcp_write_callback():  last_tcp_write_callback_data.on_write_block = %x\n", last_tcp_write_callback_data.on_write_block);

  rb_gc_unregister_address(last_uv_write_callback_data.on_write_block);
  xfree(last_uv_write_callback_data.on_write_block);

  if (! last_uv_write_callback_data.status)
    ae_block_call_1(rb_on_write_block, Qnil);
  else
    ae_block_call_1(rb_on_write_block, ae_get_last_uv_error());

  return Qnil;
}


/** local_address() and peer_address() methods. */

VALUE AsyncEngineTcpSocket_local_address(VALUE self)
{
  AE_TRACE();

  struct sockaddr_storage local_addr;
  int len = sizeof(local_addr);

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  // NOTE: If disconnection occurs just before the tcp read callback (with nread = -1)
  // this would fail with ENOTCONN, "socket is not connected" (UV_ERRNO: 31). In this case
  // return nil.
  if (uv_tcp_getsockname(cdata->_uv_handle, (struct sockaddr*)&local_addr, &len))
    return Qnil;

  return ae_ip_utils_get_ip_port(&local_addr, cdata->ip_type);
}


VALUE AsyncEngineTcpSocket_peer_address(VALUE self)
{
  AE_TRACE();

  struct sockaddr_storage peer_addr;
  int len = sizeof(peer_addr);
  VALUE _rb_array_ip_port;

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  if (! cdata->status == CONNECTED)
    return Qnil;

  // NOTE: Same as above.
  if (uv_tcp_getpeername(cdata->_uv_handle, (struct sockaddr*)&peer_addr, &len))
    return Qnil;

  return ae_ip_utils_get_ip_port(&peer_addr, cdata->ip_type);
}


/** set_connect_timeout() method. */

VALUE AsyncEngineTcpSocket_set_connect_timeout(VALUE self, VALUE _rb_timeout)
{
  AE_TRACE2();

  long delay;

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  // Return nil if the socket already connected or the connect timeout already set.
  if (cdata->status == CONNECTED || cdata->_uv_timer_connect_timeout)
    return Qnil;

  if (! (FIXNUM_P(_rb_timeout) || TYPE(_rb_timeout) == T_FLOAT))
    rb_raise(rb_eTypeError, "timeout must be a Fixnum or Float");

  delay = (long)(NUM2DBL(_rb_timeout) * 1000);
  if (delay <= 0)
    delay = 1;

  // Init and run the connect timeout timer.
  cdata->_uv_timer_connect_timeout = ALLOC(uv_timer_t);
  uv_timer_init(AE_uv_loop, cdata->_uv_timer_connect_timeout);
  cdata->_uv_timer_connect_timeout->data = (void*)cdata;

  if (uv_timer_start(cdata->_uv_timer_connect_timeout, _uv_timer_connect_timeout_callback, delay, 0)) {
    _ae_cancel_timer_connect_timeout(cdata);
    ae_raise_last_uv_error();
  }

  return Qtrue;
}


static
void _uv_timer_connect_timeout_callback(uv_timer_t* handle, int status)
{
  AE_TRACE2();

  struct_cdata* cdata = (struct_cdata*)handle->data;

  _ae_cancel_timer_connect_timeout(cdata);

  cdata->flags |= CONNECT_TIMEOUT;

  AE_CLOSE_UV_HANDLE(cdata->_uv_handle);
  cdata->_uv_handle = NULL;
}


static
void _ae_cancel_timer_connect_timeout(struct_cdata* cdata)
{
  AE_TRACE2();

  if (cdata->_uv_timer_connect_timeout) {
    AE_CLOSE_UV_HANDLE(cdata->_uv_timer_connect_timeout);
    cdata->_uv_timer_connect_timeout = NULL;
  }
}


/** status() method. */

VALUE AsyncEngineTcpSocket_status(VALUE self)
{
  AE_TRACE();

  GET_CDATA_FROM_SELF;

  if (! cdata->_uv_handle)
    return symbol_closed;

  switch(cdata->status) {
    case CONNECTING:
      return symbol_connecting;
      break;
    case CONNECTED:
      return symbol_connected;
      break;
  }
}


/** connected?() method. */

VALUE AsyncEngineTcpSocket_is_connected(VALUE self)
{
  AE_TRACE();

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  if (cdata->status == CONNECTED)
    return Qtrue;
  else
    return Qfalse;
}


/** alive?() method. */

VALUE AsyncEngineTcpSocket_is_alive(VALUE self)
{
  AE_TRACE();

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  return Qtrue;
}


/** close(), close_gracefully() and destroy() methods. */

VALUE AsyncEngineTcpSocket_close(VALUE self)
{
  AE_TRACE2();

  VALUE error = Qnil;

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  AE_CLOSE_UV_HANDLE(cdata->_uv_handle);
  cdata->_uv_handle = NULL;

  _ae_cancel_timer_connect_timeout(cdata);

  // If it's connected we must manually call to on_disconnected() callback.
  if (cdata->status == CONNECTED) {
    ae_remove_handle(cdata->ae_handle_id);
    // NOTE: This should be safe. Ruby wont GC the handle here since the object is
    // in the stack.
    rb_funcall2(cdata->ae_handle, method_on_disconnected, 1, &error);
  }

  return Qtrue;
}


// TODO: Si está conectando y, aunque no haya ningún write req, uv_shutdown no hace nada hasta
// que no se conecte. Así que no mola porque si intentamos conectar a 1.2.3.4 y llamamos a este
// método, no pasará NADA.
VALUE AsyncEngineTcpSocket_close_gracefully(int argc, VALUE *argv, VALUE self)
{
  AE_TRACE2();

  uv_shutdown_t *shutdown_req;

  AE_RB_CHECK_NUM_ARGS(0,1);

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  // Avoid this method to be called twice.
  if (cdata->flags & CLOSING_GRACEFULLY)
    return Qfalse;

  shutdown_req = ALLOC(uv_shutdown_t);

  // NOTE: If the handle is not connected yet, the uv_shutdown will occur after connection
  // is done (or failed). If it fails, _uv_shutdown_callback() detects it and will not
  // close the UV handle again, neither it will go to Ruby land.
  if (uv_shutdown(shutdown_req, (uv_stream_t*)cdata->_uv_handle, _uv_shutdown_callback)) {
    xfree(shutdown_req);
    ae_raise_last_uv_error();
  }

  cdata->flags |= CLOSING_GRACEFULLY;
  cdata->flags |= PAUSED;  // Avoid incoming data.

  return Qtrue;
}


static
void _uv_shutdown_callback(uv_shutdown_t* req, int status)
{
  AE_TRACE2();

  uv_tcp_t* _uv_handle = (uv_tcp_t*)req->handle;
  struct_cdata* cdata = (struct_cdata*)_uv_handle->data;

  xfree(req);

  // Check that the handle has not been closed between the call to #close_gracefully() and
  // its callback.
  if (cdata->_uv_handle && ! uv_is_closing((const uv_handle_t*)cdata->_uv_handle)) {
    AE_CLOSE_UV_HANDLE(cdata->_uv_handle);
    cdata->_uv_handle = NULL;
  }
  else {
    AE_WARN("handle was closed before _uv_shutdown_callback()");  // TODO: remove it.
    return;
  }

  last_uv_shutdown_callback_data.cdata = cdata;
  ae_execute_in_ruby_land(_ae_shutdown_callback);
}


static
VALUE _ae_shutdown_callback(void)
{
  AE_TRACE2();

  struct_cdata* cdata = last_uv_shutdown_callback_data.cdata;
  VALUE error = Qnil;

  // We must manually call to on_disconnected() callback.
  ae_remove_handle(cdata->ae_handle_id);
  rb_funcall2(cdata->ae_handle, method_on_disconnected, 1, &error);

  return Qnil;
}


VALUE AsyncEngineTcpSocket_destroy(VALUE self)
{
  AE_TRACE2();

  VALUE error = Qnil;

  GET_CDATA_FROM_SELF_AND_CHECK_UV_HANDLE_IS_OPEN;

  AE_CLOSE_UV_HANDLE(cdata->_uv_handle);
  cdata->_uv_handle = NULL;

  _ae_cancel_timer_connect_timeout(cdata);

  // If it's connected we must manually call to on_disconnected() callback.
  if (cdata->status == CONNECTED) {
    ae_remove_handle(cdata->ae_handle_id);
    rb_funcall2(cdata->ae_handle, method_on_disconnected, 1, &error);
  }

  return Qtrue;
}
