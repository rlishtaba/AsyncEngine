#ifndef AE_CALL_FROM_OTHER_THREAD_H
#define AE_CALL_FROM_OTHER_THREAD_H


void init_ae_call_from_other_thread(void);
void load_ae_call_from_other_thread_uv_async(void);
void unload_ae_call_from_other_thread_uv_async(void);

static VALUE AsyncEngine_call_from_other_thread(int argc, VALUE *argv, VALUE self);


#endif  /* AE_CALL_FROM_OTHER_THREAD_H */
