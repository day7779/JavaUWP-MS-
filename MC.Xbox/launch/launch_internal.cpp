#include "launch_internal.h"

#include "launcher_common.h"

#include <algorithm>
#include <string>
#include <vector>

bool CheckAndLogJavaException(JNIEnv* env, const wchar_t* stage) {
    if (!env->ExceptionCheck()) return false;
    WriteLogF(L"Java exception during %s", stage);

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!throwable) {
        WriteLog(L"Java exception object was null after ExceptionOccurred");
        return true;
    }

    jclass stringWriterClass = env->FindClass("java/io/StringWriter");
    jclass printWriterClass = env->FindClass("java/io/PrintWriter");
    jclass throwableClass = env->FindClass("java/lang/Throwable");
    if (!stringWriterClass || !printWriterClass || !throwableClass) {
        WriteLog(L"Unable to load Java exception formatting classes");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        return true;
    }

    jmethodID stringWriterCtor = env->GetMethodID(stringWriterClass, "<init>", "()V");
    jmethodID printWriterCtor = env->GetMethodID(printWriterClass, "<init>", "(Ljava/io/Writer;)V");
    jmethodID printStackTrace = env->GetMethodID(throwableClass, "printStackTrace", "(Ljava/io/PrintWriter;)V");
    jmethodID toString = env->GetMethodID(stringWriterClass, "toString", "()Ljava/lang/String;");
    if (!stringWriterCtor || !printWriterCtor || !printStackTrace || !toString || env->ExceptionCheck()) {
        WriteLog(L"Unable to resolve Java exception formatting methods");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        env->DeleteLocalRef(stringWriterClass);
        env->DeleteLocalRef(printWriterClass);
        env->DeleteLocalRef(throwableClass);
        return true;
    }

    jobject stringWriter = env->NewObject(stringWriterClass, stringWriterCtor);
    jobject printWriter = stringWriter ? env->NewObject(printWriterClass, printWriterCtor, stringWriter) : nullptr;
    if (!stringWriter || !printWriter || env->ExceptionCheck()) {
        WriteLog(L"Unable to create Java exception formatter");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        env->DeleteLocalRef(stringWriterClass);
        env->DeleteLocalRef(printWriterClass);
        env->DeleteLocalRef(throwableClass);
        return true;
    }

    env->CallVoidMethod(throwable, printStackTrace, printWriter);
    jstring trace = static_cast<jstring>(env->CallObjectMethod(stringWriter, toString));
    if (trace && !env->ExceptionCheck()) {
        const char* utf8 = env->GetStringUTFChars(trace, nullptr);
        if (utf8) {
            WriteLogF(L"Java exception stack:\n%s", a2w(utf8).c_str());
            env->ReleaseStringUTFChars(trace, utf8);
        }
    } else {
        WriteLog(L"Unable to stringify Java exception stack");
        env->ExceptionClear();
    }

    if (trace) env->DeleteLocalRef(trace);
    env->DeleteLocalRef(printWriter);
    env->DeleteLocalRef(stringWriter);
    env->DeleteLocalRef(throwable);
    env->DeleteLocalRef(stringWriterClass);
    env->DeleteLocalRef(printWriterClass);
    env->DeleteLocalRef(throwableClass);
    return true;
}

bool LaunchInvokeJavaMain(JNIEnv* env, const std::wstring& className, const std::vector<std::string>& args) {
    std::wstring classPath = className;
    std::replace(classPath.begin(), classPath.end(), L'.', L'/');

    jclass mainClass = env->FindClass(w2a(classPath).c_str());
    if (!mainClass || CheckAndLogJavaException(env, (L"FindClass(" + className + L")").c_str())) {
        return false;
    }

    jmethodID mainMethod = env->GetStaticMethodID(mainClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod || CheckAndLogJavaException(env, (L"GetStaticMethodID(" + className + L".main)").c_str())) {
        env->DeleteLocalRef(mainClass);
        return false;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass || CheckAndLogJavaException(env, L"FindClass(String)")) {
        env->DeleteLocalRef(mainClass);
        return false;
    }

    jobjectArray argv = env->NewObjectArray(static_cast<jsize>(args.size()), stringClass, nullptr);
    if (!argv || CheckAndLogJavaException(env, (L"NewObjectArray(" + className + L")").c_str())) {
        env->DeleteLocalRef(stringClass);
        env->DeleteLocalRef(mainClass);
        return false;
    }

    for (jsize i = 0; i < static_cast<jsize>(args.size()); ++i) {
        jstring value = env->NewStringUTF(args[i].c_str());
        if (!value || CheckAndLogJavaException(env, (L"NewStringUTF(" + className + L")").c_str())) {
            env->DeleteLocalRef(argv);
            env->DeleteLocalRef(stringClass);
            env->DeleteLocalRef(mainClass);
            return false;
        }
        env->SetObjectArrayElement(argv, i, value);
        env->DeleteLocalRef(value);
        if (CheckAndLogJavaException(env, (L"SetObjectArrayElement(" + className + L")").c_str())) {
            env->DeleteLocalRef(argv);
            env->DeleteLocalRef(stringClass);
            env->DeleteLocalRef(mainClass);
            return false;
        }
    }

    WriteLogF(L"Invoking NeoForge prep tool %s", className.c_str());
    env->CallStaticVoidMethod(mainClass, mainMethod, argv);
    const bool failed = CheckAndLogJavaException(env, (L"CallStaticVoidMethod(" + className + L".main)").c_str());
    env->DeleteLocalRef(argv);
    env->DeleteLocalRef(stringClass);
    env->DeleteLocalRef(mainClass);
    return !failed;
}

void LaunchSetJavaSystemProperty(JNIEnv* env, const std::wstring& key, const std::wstring& value) {
    jclass sys = env->FindClass("java/lang/System");
    if (!sys || CheckAndLogJavaException(env, L"FindClass(System)")) return;
    jmethodID setProp = env->GetStaticMethodID(sys, "setProperty",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!setProp || CheckAndLogJavaException(env, L"GetStaticMethodID(System.setProperty)")) {
        env->DeleteLocalRef(sys);
        return;
    }
    jstring k = env->NewStringUTF(w2a(key).c_str());
    jstring v = env->NewStringUTF(w2a(value).c_str());
    jobject prev = env->CallStaticObjectMethod(sys, setProp, k, v);
    CheckAndLogJavaException(env, L"System.setProperty");
    if (prev) env->DeleteLocalRef(prev);
    env->DeleteLocalRef(k);
    env->DeleteLocalRef(v);
    env->DeleteLocalRef(sys);
}
