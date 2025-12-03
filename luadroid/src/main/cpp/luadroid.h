
#include <jni.h>
#include <android/log.h>
#include "common.h"
#include "myarray.h"
#include "jtype.h"
#include "lua_object.h"
#include "func_info.h"
#include "lua.hpp"
#include "TJNIEnv.h"
#include "tls.h"
#include "logger.h"

#include "env.h"

#ifndef LUADROID_LUADROID_H
#define LUADROID_LUADROID_H

#define HOLD_JAVA_EXCEPTION(context, code)({\
if(unlikely(env->ExceptionCheck())){\
    jthrowable t=env->ExceptionOccurred();\
    env->ExceptionClear();\
    context->setPendingException(t);\
    env->DeleteLocalRef(t);\
    code\
}})

#define INVALID_OBJECT reinterpret_cast<jobject >(-1)

inline void cleanArgs(jvalue *args, int argSize, Vector<ValidLuaObject> &arr, JNIEnv *env) {
    for (int i = argSize; i--;) {
        if (arr[i].shouldRelease) {
            jobject ref = args[i].l;
            if (ref != INVALID_OBJECT)
                env->DeleteLocalRef(ref);
            else break;
        }
    }
}

inline void cleanArg(JNIEnv *env, const jobject j, bool should) {
    if (should) {
        env->DeleteLocalRef(j);
    }
}

enum class ContextStorage {
    PARSED_TABLE,
    LEN
};

class JavaType;
class Member;

class ScriptContext;

struct ThreadContext {
    ScriptContext *scriptContext = nullptr;
    int pushedCount = 0;
private:
    Import *import = nullptr;
    jthrowable pendingJavaError = nullptr;
    void *storage[(int) ContextStorage::LEN] = {nullptr};

    inline JClass getTypeNoCheck(TJNIEnv *env,const String &className) const;

    inline JavaType *ensureArrayType(TJNIEnv *env,const char *typeName);

public:

    template<typename T>
    T *getValue(ContextStorage index) {
        return (T *) storage[(int) index];
    }

    void setValue(ContextStorage index, void *value) {
        storage[(int) index] = value;
    }

    void setPendingException(const String &msg);

    void setPendingException(const char *msg) {
        FakeString m(msg);
        const String &k = m;
        setPendingException(k);
    }

    void setPendingException(jthrowable throwable) {
        if (throwable != nullptr) {
            TJNIEnv *env = getThreadEnv();
            if (pendingJavaError != nullptr) {
                if (env->IsSameObject(throwable, pendingJavaError)) return;
                env->DeleteLocalRef(pendingJavaError);
            }
            pendingJavaError = (jthrowable) env->NewLocalRef(throwable);
        }
    }

    jthrowable transferJavaError() {
        auto ret = pendingJavaError;
        pendingJavaError = nullptr;
        return ret;
    }

    void restore(Import *oldImport) {
        changeImport(oldImport);
        if (hasErrorPending())
            throwToJava();
    }

    void throwToJava() {
        TJNIEnv *env = getThreadEnv();
        jthrowable p = (jthrowable) pendingJavaError;
        pendingJavaError = nullptr;
        env->Throw(p);
        env->DeleteLocalRef(p);
    }

    Import *changeImport(Import *newImport) {
        Import *old = import;
        import = newImport;
        return old;
    }

    Import *getImport() {
        return import;
    }

    bool hasErrorPending() {
        return pendingJavaError != nullptr;
    }

    JClass findClass(TJNIEnv *env,String &&str) {
        return findClass(env,str);
    }

    JClass findClass(TJNIEnv *env,String &str);

    JavaType *ensureType(TJNIEnv *env,const String &typeName);

    jobject proxy(TJNIEnv *env,JavaType *main, Vector<JavaType *> *interfaces,
                  const Vector<JObject> &principal, Vector<std::unique_ptr<BaseFunction>> &proxy,
                  BaseFunction *defaultFunc = nullptr,
                  bool shared = false, long nativeInfo = 0, jobject superObject = nullptr);

    jvalue luaObjectToJValue(TJNIEnv *env,ValidLuaObject &luaObject, JavaType *type, jobject realType = nullptr);

    jobject luaObjectToJObject(TJNIEnv *env,ValidLuaObject &luaObject);

    JavaType *MapType(TJNIEnv *env);

    JavaType *FunctionType(TJNIEnv *env);

    JavaType *ArrayType(TJNIEnv *env);

    ~ThreadContext();
};

class ScriptContext {

    struct hashJClass {
        int operator()(const jclass c) const noexcept {
            return getThreadEnv()->CallIntMethod(c, objectHash);
        }
    };

    struct equalJClass {
        bool operator()(const jclass &c1, const jclass &c2) const noexcept {//must not null both
            if (c1 == nullptr) return false;
            if (c2 == nullptr) return false;
            return c1 == c2 || getThreadEnv()->IsSameObject(c1, c2);
        }
    };

    friend class ThreadContext;

    typedef Map<jclass, JavaType *, hashJClass, equalJClass> TypeMap;
    TypeMap typeMap;

    JavaType *HashMapClass = nullptr;
    JavaType *FunctionClass = nullptr;
    JavaType *ArrayClass = nullptr;

    JavaType *getVoidClass(TJNIEnv *env);

    JavaType *const byteClass;
    JavaType *const shortClass;
    JavaType *const intClass;
    JavaType *const longClass;
    JavaType *const booleanClass;
    JavaType *const charClass;
    JavaType *const floatClass;
    JavaType *const doubleClass;
    JavaType *const voidClass;
    
public:
    pthread_mutex_t stateMutex;  // 保护主状态机的互斥锁
    lua_State *const state;
    JavaType *const ByteClass;
    JavaType *const ShortClass;
    JavaType *const IntegerClass;
    JavaType *const LongClass;
    JavaType *const BooleanClass;
    JavaType *const CharacterClass;
    JavaType *const FloatClass;
    JavaType *const DoubleClass;
    JavaType *const ObjectClass;
    jobject const javaRef;

    ThreadContext * const threadContext;

    JavaType *ensureType(TJNIEnv *env, jclass type);

    ScriptContext(TJNIEnv *env, jobject con);

    ~ScriptContext();
private:
    void config(lua_State *L);

    static void init(TJNIEnv *env, jobject const javaObject);

};


#endif //LUADROID_LUADROID_H
