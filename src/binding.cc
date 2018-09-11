#define NDEBUG

#include "KcpuvSess.h"
#include "Mux.h"
#include <iostream>
#include <nan.h>
#include <node.h>
#include <uv.h>
#include <v8.h>

namespace kcpuv_addons {
using namespace kcpuv;
using std::cout;
using v8::Boolean;
using v8::Context;
using v8::Exception;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Handle;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Number;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::Value;

class KcpuvSessBinding;
class KcpuvMuxBinding;
class KcpuvMuxConnBinding;

class KcpuvSessBinding : public Nan::ObjectWrap {
public:
  explicit KcpuvSessBinding() {
    // TODO: `passive` argument
    sess = new KcpuvSess;
    sess->data = this;
    sess->timeout = 0;
  }

  ~KcpuvSessBinding() {
    KcpuvSess *sess = reinterpret_cast<KcpuvSess *>(this->sess);
    delete sess;
  }

  static Persistent<Function> constructor;
  static void Create(const FunctionCallbackInfo<Value> &info);

  KcpuvSess *GetSess() { return sess; }

  Nan::CopyablePersistentTraits<Function>::CopyablePersistent listen_cb;
  Nan::CopyablePersistentTraits<Function>::CopyablePersistent close_cb;
  Nan::CopyablePersistentTraits<Function>::CopyablePersistent udp_send_cb;
  KcpuvSess *sess;
  int is_freed = 0;
};

Persistent<Function> KcpuvSessBinding::constructor;

// TODO: hide details
class KcpuvMuxConnBinding : public Nan::ObjectWrap {
public:
  explicit KcpuvMuxConnBinding() {}
  ~KcpuvMuxConnBinding() { delete conn; }

  static Persistent<Function> constructor;
  static void Create(const FunctionCallbackInfo<Value> &info);
  // void WrapObject(Local<Object> object) { this->Wrap(object); }
  void WrapConn(Conn *c) {
    conn = c;
    conn->data = this;
  }

  Nan::CopyablePersistentTraits<Function>::CopyablePersistent on_message;
  Nan::CopyablePersistentTraits<Function>::CopyablePersistent on_close;
  Conn *conn;
};

Persistent<Function> KcpuvMuxConnBinding::constructor;

class KcpuvMuxBinding : public Nan::ObjectWrap {
public:
  explicit KcpuvMuxBinding(unsigned int passive = 0) {
    // TODO: `passive` arg
    mux = new Mux();
  }
  ~KcpuvMuxBinding() {
    // TODO: What if it's deleted before closed?
    delete mux;
  }

  static Persistent<Function> constructor;
  static void Create(const FunctionCallbackInfo<Value> &info);
  Nan::CopyablePersistentTraits<Function>::CopyablePersistent on_close;
  Nan::CopyablePersistentTraits<Function>::CopyablePersistent on_connection;
  Mux *mux;
};

Persistent<Function> KcpuvMuxBinding::constructor;

void KcpuvMuxBinding::Create(const FunctionCallbackInfo<Value> &info) {
  Isolate *isolate = info.GetIsolate();

  if (info.IsConstructCall()) {
    KcpuvMuxBinding *binding = new KcpuvMuxBinding();
    binding->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    Local<Function> cons =
        Local<Function>::New(isolate, KcpuvMuxBinding::constructor);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> instance = cons->NewInstance(context, 0, 0).ToLocalChecked();
    info.GetReturnValue().Set(instance);
  }
}

// TODO: Check need mux now.
void KcpuvMuxConnBinding::Create(const FunctionCallbackInfo<Value> &info) {
  // TODO: Check it's mux.
  KcpuvMuxBinding *muxBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());

  Mux *mux = muxBinding->mux;

  Isolate *isolate = info.GetIsolate();

  if (info.IsConstructCall()) {
    Conn *c = mux->CreateConn();
    KcpuvMuxConnBinding *conn = new KcpuvMuxConnBinding();
    conn->WrapConn(c);
    conn->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    assert(0);
    // Local<Function> cons =
    //     Local<Function>::New(isolate, KcpuvMuxConnBinding::constructor);
    // Local<Context> context = isolate->GetCurrentContext();
    // Local<Object> instance = cons->NewInstance(context, 0,
    // 0).ToLocalChecked(); info.GetReturnValue().Set(instance);
  }
}

// void buffer_delete_callback(char *data, void *hint) { free(data); }

static void OnListenCb(KcpuvSess *sess, const char *data, unsigned int len) {
  KcpuvSessBinding *binding = static_cast<KcpuvSessBinding *>(sess->data);
  // NOTE: Create a scope for the allocation of v8 memories
  // as we are calling a js function outside the v8
  Nan::HandleScope scope;

  // NOTE: V8 will free these memories so that we have to
  // make a copy.
  char *buf_data = new char[len];
  memcpy(buf_data, data, len);

  const int argc = 1;
  Local<Value> info[argc] = {Nan::NewBuffer(buf_data, len).ToLocalChecked()};

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(binding->listen_cb), argc, info);
}

static void ClosingCb(KcpuvSess *sess) {
  KcpuvSessBinding *binding = static_cast<KcpuvSessBinding *>(sess->data);

  Nan::HandleScope scope;
  Isolate *isolate = Isolate::GetCurrent();
  // const char *error_msg = reinterpret_cast<const char *>(data);
  const int argc = 1;
  Local<Value> info[argc] = {Nan::Undefined()};

  // if (!isolate) {
  //   isolate = Isolate::New();
  //   isolate->Enter();
  // }

  // if (error_msg) {
  //   // TODO: memcpy
  //   info[0] = String::NewFromUtf8(isolate, error_msg);
  // }

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(binding->close_cb), argc, info);
}

static void OnUDPSend(KcpuvSess *sess, uv_buf_t *buf, int buf_count,
                      const struct sockaddr *addr) {
  KcpuvSessBinding *binding = static_cast<KcpuvSessBinding *>(sess->data);
  Nan::HandleScope scope;
  Isolate *isolate = Isolate::GetCurrent();

  char *buf_data = new char[buf->len];
  memcpy(buf_data, buf->base, buf->len);

  char address[16];
  uv_ip4_name(reinterpret_cast<const struct sockaddr_in *>(addr), address, 16);
  int port =
      ntohs((reinterpret_cast<const struct sockaddr_in *>(addr))->sin_port);

  Local<Value> js_address = String::NewFromUtf8(isolate, address);
  Local<Value> js_port = Number::New(isolate, port);

  const int argc = 3;
  Local<Value> info[argc] = {
      Nan::NewBuffer(buf_data, buf->len).ToLocalChecked(), js_address, js_port};

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(binding->udp_send_cb), argc, info);
}

void KcpuvSessBinding::Create(const FunctionCallbackInfo<Value> &info) {
  Isolate *isolate = info.GetIsolate();

  if (info.IsConstructCall()) {
    KcpuvSessBinding *binding = new KcpuvSessBinding();
    binding->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    Local<Function> cons =
        Local<Function>::New(isolate, KcpuvSessBinding::constructor);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> instance = cons->NewInstance(context, 0, 0).ToLocalChecked();
    info.GetReturnValue().Set(instance);
  }
}

static NAN_METHOD(UseDefaultLoop) {
  bool use = info[0]->BooleanValue();
  int val = 0;
  if (use) {
    val = 1;
  }

  Loop::KcpuvUseDefaultLoop(val);
}

static NAN_METHOD(Initialize) { KcpuvSess::KcpuvInitialize(); }

static NAN_METHOD(Destruct) { KcpuvSess::KcpuvDestruct(); }

static NAN_METHOD(InitCryptor) {
  Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  String::Utf8Value utf8Str(info[1]->ToString());
  const char *key = *utf8Str;
  int keylen = info[2]->ToNumber(isolate)->Int32Value();

  obj->GetSess()->InitCryptor(key, keylen);
}

static NAN_METHOD(SetWndSize) {
  Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  int snd_wnd = info[1]->ToNumber(isolate)->Int32Value();
  int rcv_wnd = info[2]->ToNumber(isolate)->Int32Value();

  ikcp_wndsize(obj->GetSess()->kcp, snd_wnd, rcv_wnd);
}

static NAN_METHOD(SetNoDelay) {
  Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  int nodelay = info[1]->ToNumber(isolate)->Int32Value();
  int interval = info[2]->ToNumber(isolate)->Int32Value();
  int resend = info[3]->ToNumber(isolate)->Int32Value();
  int nc = info[4]->ToNumber(isolate)->Int32Value();

  ikcp_nodelay(obj->GetSess()->kcp, nodelay, interval, resend, nc);
}

// TODO: Try to remove this.
static NAN_METHOD(Free) {
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  delete obj;
}

// TODO: need to free the object wrap or causing memory leaks
// static NAN_METHOD(MarkFree) {
//   KcpuvSessBinding *obj =
//       Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());
//
//   kcpuv_set_state(obj->GetSess(), KCPUV_STATE_WAIT_FREE);
// }

// TODO: check this
static NAN_METHOD(Input) {
  // Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  char *js_content = node::Buffer::Data(info[1]->ToObject());
  unsigned int content_size = info[2]->Uint32Value();
  char *content = new char[content_size];
  memcpy(content, js_content, content_size);

  String::Utf8Value utf8Str(info[3]->ToString());
  char *ip = *utf8Str;
  unsigned int port = info[4]->Uint32Value();

  struct sockaddr addr;
  uv_ip4_addr(reinterpret_cast<const char *>(ip), port,
              reinterpret_cast<struct sockaddr_in *>(&addr));

  obj->GetSess()->KcpInput(&addr, content, content_size);
}

static NAN_METHOD(Listen) {
  Isolate *isolate = info.GetIsolate();

  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());
  int port = info[1]->ToNumber(isolate)->Int32Value();

  if (info[2]->IsFunction()) {
    obj->listen_cb.Reset(isolate, info[2].As<Function>());
  }

  // else {
  // isolate->ThrowException(Exception::Error(
  //     String::NewFromUtf8(isolate, "`Listen` expect a callback
  //     function")));
  // return;
  // }

  KcpuvSess *sess = obj->GetSess();
  int rval = sess->Listen(port, OnListenCb);

  if (rval != 0) {
    return info.GetReturnValue().Set(
        String::NewFromUtf8(isolate, uv_strerror(rval)));
  }
}

static NAN_METHOD(BindListen) {
  Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  if (!info[1]->IsFunction()) {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(
        isolate, "`BindListen` expect a callback function")));
    return;
  }

  obj->listen_cb.Reset(isolate, info[1].As<Function>());
}

static NAN_METHOD(GetPort) {
  Isolate *isolate = info.GetIsolate();
  char addr[17];
  int namelen = 0;
  int port = 0;

  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  obj->GetSess()->GetAddressPort((char *)&addr, &namelen, &port);

  Local<Number> port_value = Number::New(isolate, port);
  info.GetReturnValue().Set(port_value);
}

// static NAN_METHOD(StopListen) {
//   KcpuvSessBinding *obj =
//       Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());
//
//   kcpuv_stop_listen(obj->GetSess());
// }

static NAN_METHOD(BindClose) {
  Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  if (!info[1]->IsFunction()) {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(
        isolate, "`BindClose` expect a callback function")));
    return;
  }

  obj->close_cb.Reset(isolate, info[1].As<Function>());

  obj->GetSess()->BindClose(ClosingCb);
}

// static NAN_METHOD(BindUdpSend) {
//   Isolate *isolate = info.GetIsolate();
//   KcpuvSessBinding *obj =
//       Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());
//
//   if (!info[1]->IsFunction()) {
//     isolate->ThrowException(Exception::Error(String::NewFromUtf8(
//         isolate, "`BindUdpSend` expect a callback function")));
//     return;
//   }
//
//   obj->udp_send_cb.Reset(isolate, info[1].As<Function>());
//   kcpuv_bind_udp_send(obj->GetSess(), OnUDPSend);
// }

static NAN_METHOD(Close) {
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  obj->GetSess()->Close();
}

static NAN_METHOD(InitSend) {
  Isolate *isolate = info.GetIsolate();
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());
  String::Utf8Value utf8Str(info[1]->ToString());
  // TODO: Do we need to free strings from js?
  char *addr = *utf8Str;
  int port = static_cast<double>(info[2]->ToNumber(isolate)->Value());

  obj->GetSess()->sessUDP->SetSendAddr(addr, port);
}

static NAN_METHOD(Send) {
  KcpuvSessBinding *obj =
      Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[0]->ToObject());

  unsigned char *buf = (unsigned char *)node::Buffer::Data(info[1]->ToObject());
  unsigned int size = info[2]->Uint32Value();

  obj->GetSess()->Send(reinterpret_cast<char *>(buf), size);
}

static NAN_METHOD(StartLoop) { Loop::KcpuvStartLoop_(Mux::UpdateMux); }

static NAN_METHOD(StopLoop) { Loop::KcpuvStopUpdaterTimer(); }

// static NAN_METHOD(MuxInit) {
//   // Isolate *isolate = info.GetIsolate();
//   KcpuvMuxBinding *muxBinding =
//       Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());
//   KcpuvSessBinding *sessObj =
//       Nan::ObjectWrap::Unwrap<KcpuvSessBinding>(info[1]->ToObject());
//
//   kcpuv_mux_init(&muxBinding->mux, sessObj->sess);
// }

// TODO:
// static NAN_METHOD(MuxFree) {
//   KcpuvMuxBinding *muxBinding =
//       Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());
//
//   kcpuv_mux_free(&muxBinding->mux);
//   delete muxBinding;
// }

// TODO: Add mux_close

static void MuxBindingOnCloseCb(Mux *mux, const char *error_msg) {
  KcpuvMuxBinding *muxBinding = static_cast<KcpuvMuxBinding *>(mux->data);
  Nan::HandleScope scope;
  Isolate *isolate = Isolate::GetCurrent();

  const int argc = 1;
  Local<Value> argv[argc] = {};

  if (error_msg) {
    argv[0] = String::NewFromUtf8(isolate, error_msg);
  } else {
    argv[0] = Nan::Undefined();
  }

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(muxBinding->on_close), argc, argv);
}

static NAN_METHOD(MuxBindClose) {
  Isolate *isolate = info.GetIsolate();
  KcpuvMuxBinding *muxBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());

  if (!info[1]->IsFunction()) {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(
        isolate, "`MuxBindClose` expect a callback function")));
    return;
  }

  muxBinding->on_close.Reset(isolate, info[1].As<Function>());
  muxBinding->mux->BindClose(MuxBindingOnCloseCb);
}

// TODO: We still need wrapping method.
static void MuxBindingOnConnectionCb(Conn *conn) {
  Mux *mux = static_cast<Mux *>(conn->mux);
  KcpuvMuxBinding *muxBinding = static_cast<KcpuvMuxBinding *>(mux->data);

  Nan::HandleScope scope;
  Isolate *isolate = Isolate::GetCurrent();
  Local<Context> context = isolate->GetCurrentContext();

  Local<Function> cons =
      Local<Function>::New(isolate, KcpuvMuxConnBinding::constructor);
  Local<Object> conn_instance =
      cons->NewInstance(context, 0, 0).ToLocalChecked();
  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(conn_instance->ToObject());

  // TODO: Check this.
  connBinding->WrapConn(conn);

  const int argc = 1;
  Local<Value> argv[argc] = {conn_instance};

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(muxBinding->on_connection), argc, argv);
}

static NAN_METHOD(MuxBindConnection) {
  Isolate *isolate = info.GetIsolate();
  KcpuvMuxBinding *muxBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());

  if (!info[1]->IsFunction()) {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(
        isolate, "`MuxBindConnection` expect a callback function")));
    return;
  }

  muxBinding->on_connection.Reset(isolate, info[1].As<Function>());
  muxBinding->mux->BindConnection(MuxBindingOnConnectionCb);
}

// static NAN_METHOD(ConnInit) {
//   KcpuvMuxBinding *muxBinding =
//       Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());
//   KcpuvMuxConnBinding *connBinding =
//       Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[1]->ToObject());
//
//   kcpuv_mux_conn_init(&muxBinding->mux, connBinding->conn);
// }

// static NAN_METHOD(ConnFree) {
//   KcpuvMuxConnBinding *connBinding =
//       Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());
//
//   kcpuv_mux_conn_free(connBinding->conn, NULL);
//   delete connBinding;
// }

// TODO: Add conn close

static NAN_METHOD(ConnSend) {
  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());

  unsigned char *buf = (unsigned char *)node::Buffer::Data(info[1]->ToObject());
  unsigned int size = info[2]->Uint32Value();

  connBinding->conn->Send(reinterpret_cast<char *>(buf), size, 0);
}

static NAN_METHOD(ConnSendClose) {
  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());

  connBinding->conn->SendClose();
}

static void ConnBindingOnMessage(Conn *conn, const char *data, int len) {
  KcpuvMuxConnBinding *connBinding =
      static_cast<KcpuvMuxConnBinding *>(conn->data);
  Nan::HandleScope scope;

  // NOTE: V8 will free these memories so that we have to
  // make a copy.
  char *buf_data = new char[len];
  memcpy(buf_data, data, len);

  const int argc = 1;
  Local<Value> info[argc] = {Nan::NewBuffer(buf_data, len).ToLocalChecked()};

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(connBinding->on_message), argc, info);
}

// TODO: Change name to `ConnBindMsg`
static NAN_METHOD(ConnListen) {
  Isolate *isolate = info.GetIsolate();

  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());

  if (!info[1]->IsFunction()) {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(
        isolate, "`ConnListen` expect a callback function")));
    return;
  }

  connBinding->on_message.Reset(isolate, info[1].As<Function>());
  connBinding->conn->BindMsg(ConnBindingOnMessage);
}

static void ConnBindingOnClose(Conn *conn, const char *error_msg) {
  KcpuvMuxConnBinding *connBinding =
      static_cast<KcpuvMuxConnBinding *>(conn->data);
  Nan::HandleScope scope;
  Isolate *isolate = Isolate::GetCurrent();

  const int argc = 1;
  Local<Value> argv[argc] = {};

  if (error_msg) {
    argv[0] = String::NewFromUtf8(isolate, error_msg);
  } else {
    argv[0] = Nan::Undefined();
  }

  Nan::MakeCallback(Nan::GetCurrentContext()->Global(),
                    Nan::New(connBinding->on_close), argc, argv);
}

static NAN_METHOD(ConnBindClose) {
  Isolate *isolate = info.GetIsolate();
  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());

  if (!info[1]->IsFunction()) {
    isolate->ThrowException(Exception::Error(String::NewFromUtf8(
        isolate, "`ConnBindClose` expect a callback function")));
    return;
  }

  connBinding->on_close.Reset(isolate, info[1].As<Function>());
  connBinding->conn->BindClose(ConnBindingOnClose);
}

static NAN_METHOD(ConnSetTimeout) {
  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());

  unsigned int timeout = info[1]->Uint32Value();
  connBinding->conn->SetTimeout(timeout);
}

static NAN_METHOD(ConnClose) {
  KcpuvMuxConnBinding *connBinding =
      Nan::ObjectWrap::Unwrap<KcpuvMuxConnBinding>(info[0]->ToObject());

  connBinding->conn->Close();
}

// TODO: Add StopSending
// static NAN_METHOD(MuxStopAll) {
//   KcpuvMuxBinding *muxBinding =
//       Nan::ObjectWrap::Unwrap<KcpuvMuxBinding>(info[0]->ToObject());
//   Mux *mux = &muxBinding->mux;
//   // 1. stop all listeners of mux and prevent it from sending
//   kcpuv_mux_stop(mux);
//   // // 2. trigger close msg
//   // kcpuv_close(mux->sess);
// }

static NAN_MODULE_INIT(Init) {
  Isolate *isolate = target->GetIsolate();

  Local<FunctionTemplate> tpl =
      FunctionTemplate::New(isolate, KcpuvSessBinding::Create);
  tpl->SetClassName(String::NewFromUtf8(isolate, "KcpuvSessBinding"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  KcpuvSessBinding::constructor.Reset(isolate, tpl->GetFunction());
  Nan::Set(target, String::NewFromUtf8(isolate, "create"), tpl->GetFunction());

  tpl = FunctionTemplate::New(isolate, KcpuvMuxConnBinding::Create);
  tpl->SetClassName(String::NewFromUtf8(isolate, "KcpuvMuxConnBinding"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  KcpuvMuxConnBinding::constructor.Reset(isolate, tpl->GetFunction());
  Nan::Set(target, String::NewFromUtf8(isolate, "createMuxConn"),
           tpl->GetFunction());

  tpl = FunctionTemplate::New(isolate, KcpuvMuxBinding::Create);
  tpl->SetClassName(String::NewFromUtf8(isolate, "KcpuvMuxBinding"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  KcpuvMuxBinding::constructor.Reset(isolate, tpl->GetFunction());
  Nan::Set(target, String::NewFromUtf8(isolate, "createMux"),
           tpl->GetFunction());

  // sess method
  Nan::SetMethod(target, "initCryptor", InitCryptor);
  Nan::SetMethod(target, "setWndSize", SetWndSize);
  Nan::SetMethod(target, "setNoDelay", SetNoDelay);
  Nan::SetMethod(target, "useDefaultLoop", UseDefaultLoop);
  Nan::SetMethod(target, "free", Free);
  // Nan::SetMethod(target, "markFree", MarkFree);
  Nan::SetMethod(target, "input", Input);
  Nan::SetMethod(target, "listen", Listen);
  Nan::SetMethod(target, "getPort", GetPort);
  // Nan::SetMethod(target, "stopListen", StopListen);
  Nan::SetMethod(target, "initSend", InitSend);
  Nan::SetMethod(target, "send", Send);
  Nan::SetMethod(target, "bindClose", BindClose);
  Nan::SetMethod(target, "bindListen", BindListen);
  // Nan::SetMethod(target, "bindUdpSend", BindUdpSend);
  Nan::SetMethod(target, "close", Close);
  Nan::SetMethod(target, "initialize", Initialize);
  Nan::SetMethod(target, "destruct", Destruct);
  Nan::SetMethod(target, "startLoop", StartLoop);
  Nan::SetMethod(target, "stopLoop", StopLoop);

  // mux method
  // Nan::SetMethod(target, "muxInit", MuxInit);
  // Nan::SetMethod(target, "muxFree", MuxFree);
  Nan::SetMethod(target, "muxBindClose", MuxBindClose);
  Nan::SetMethod(target, "muxBindConnection", MuxBindConnection);

  // conn method
  // Nan::SetMethod(target, "connInit", ConnInit);
  // Nan::SetMethod(target, "connFree", ConnFree);
  Nan::SetMethod(target, "connSend", ConnSend);
  Nan::SetMethod(target, "connSendClose", ConnSendClose);
  Nan::SetMethod(target, "connListen", ConnListen);
  Nan::SetMethod(target, "connBindClose", ConnBindClose);
  Nan::SetMethod(target, "connSetTimeout", ConnSetTimeout);
  Nan::SetMethod(target, "connEmitClose", ConnClose);

  // Nan::SetMethod(target, "muxStopAll", MuxStopAll);
}

NODE_MODULE(binding, Init)
} // namespace kcpuv_addons
