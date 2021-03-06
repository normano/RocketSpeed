// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#include "src-gen/djinni/cpp/NativeSubscribeCallback.hpp"  // my header
#include "Marshal.hpp"
#include "src-gen/djinni/cpp/NativeStatus.hpp"

namespace djinni_generated {

NativeSubscribeCallback::NativeSubscribeCallback() : ::djinni::JniInterface<::rocketspeed::djinni::SubscribeCallback, NativeSubscribeCallback>() {}

NativeSubscribeCallback::~NativeSubscribeCallback() = default;

NativeSubscribeCallback::JavaProxy::JavaProxy(JniType j) : JavaProxyCacheEntry(j) { }

NativeSubscribeCallback::JavaProxy::~JavaProxy() = default;

void NativeSubscribeCallback::JavaProxy::Call(int32_t tenant_id, std::string namespace_id, std::string topic_name, int64_t start_seqno, bool subscribed, ::rocketspeed::djinni::Status status) {
    auto jniEnv = ::djinni::jniGetThreadEnv();
    ::djinni::JniLocalScope jscope(jniEnv, 10);
    const auto& data = ::djinni::JniClass<::djinni_generated::NativeSubscribeCallback>::get();
    jniEnv->CallVoidMethod(getGlobalRef(), data.method_call,
                           ::djinni::I32::fromCpp(jniEnv, tenant_id),
                           ::djinni::String::fromCpp(jniEnv, namespace_id).get(),
                           ::djinni::String::fromCpp(jniEnv, topic_name).get(),
                           ::djinni::I64::fromCpp(jniEnv, start_seqno),
                           ::djinni::Bool::fromCpp(jniEnv, subscribed),
                           ::djinni_generated::NativeStatus::fromCpp(jniEnv, status).get());
    ::djinni::jniExceptionCheck(jniEnv);
}

}  // namespace djinni_generated
