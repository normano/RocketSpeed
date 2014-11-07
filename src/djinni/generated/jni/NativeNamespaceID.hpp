// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#pragma once

#include "NamespaceID.hpp"
#include "djinni_support.hpp"

namespace djinni_generated {

class NativeNamespaceID final {
public:
    using CppType = ::rocketglue::NamespaceID;
    using JniType = jobject;

    static jobject toJava(JNIEnv*, ::rocketglue::NamespaceID);
    static ::rocketglue::NamespaceID fromJava(JNIEnv*, jobject);

    const djinni::GlobalRef<jclass> clazz { djinni::jniFindClass("org/rocketspeed/NamespaceID") };
    const jmethodID jconstructor { djinni::jniGetMethodID(clazz.get(), "<init>", "(I)V") };
    const jfieldID field_mNsid { djinni::jniGetFieldID(clazz.get(), "mNsid", "I") };

private:
    NativeNamespaceID() {}
    friend class djinni::JniClass<::djinni_generated::NativeNamespaceID>;
};

}  // namespace djinni_generated