
#include "luadroid.h"
#include "myarray.h"
#include "java_type.h"
#include "utf8.h"
#include "java_member.h"
#include "lua_object.h"
#include "FakeVector.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <csetjmp>
#include "lua.h"
#include <cassert>
#include <dlfcn.h>
#include <cctype>
#include "logger.h"
#include "lstate.h"

// TODO: @xyz Removed GNU statement expression syntax for C++ compatibility (2024-11-26)
#define TopErrorHandle(fmt, ...)  \
    do { lua_pushfstring(L,fmt,##__VA_ARGS__); \
    goto __ErrorHandle; } while(0)
#define ERROR(fmt, ...)  \
    do { lua_pushfstring(L,fmt,##__VA_ARGS__); \
    lua_error(L); } while(0)
#define SetErrorJMP() \
    if(false) { \
__ErrorHandle: \
        lua_error(L); \
        return 0; \
    }
#define luaL_isstring(L, i) (lua_type(L,i)==LUA_TSTRING)
#define max(a, b) ((a)>(b)?(a):(b))

inline FakeString::FakeString(lua_State *L, int i) : __cap_(1) {
    pointer = lua_tolstring(L, i, (size_t *) &__size_);
}

static int javaType(lua_State *L);

static int javaInstanceOf(lua_State *L);

static int javaNew(lua_State *L);

static int javaNewArray(lua_State *L);

static int javaImport(lua_State *L);

static int javaIterate(lua_State *L);

static int javaToJavaObject(lua_State *L);

static int javaProxy(lua_State *L);

static int javaUnBox(lua_State *L);

static int javaTypeOf(lua_State *L);

static int javaSuper(lua_State *L);

static int javaLog(lua_State *L);

static int javaBenchmark(lua_State *L);

static int javaEmptyMethod(lua_State *L);

static int concatString(lua_State *L);

static int objectEquals(lua_State *L);

static int getObjectLength(lua_State *L);

static int javaTypeToString(lua_State *L);

static int javaObjectToString(lua_State *L);

static int getClassMember(lua_State *L);

static int setFieldOrArray(lua_State *L);

static int setField(lua_State *L);

static int getField(lua_State *L);

static int callMethod(lua_State *L);

static int callInitializer(lua_State *L);

static ThreadContext *getContext(lua_State *L);

static bool parseLuaObject(lua_State *L, int idx,
                           ValidLuaObject &luaObject);

static inline void pushJavaType(lua_State *L, JavaType *type);

static void checkLuaType(TJNIEnv *env, lua_State *L, JavaType *expected, ValidLuaObject &luaObject);

static void
pushMember(ThreadContext *context, lua_State *L, const Member *member, int toIndex, bool isStatic,
           int fieldCount, bool isMethod);

static void
pushArrayElement(lua_State *L, TJNIEnv *env, ThreadContext *context, const JavaObject *obj,
                 JavaType *component);

static void
readArguments(lua_State *L, TJNIEnv *env, ThreadContext *context, FakeVector<JavaType *> &types,
              FakeVector<ValidLuaObject> &objects,
              int start, int end);

static void recordLuaError(ThreadContext *context, lua_State *L, int ret);

static LocalFunctionInfo *saveLocalFunction(lua_State *L, int i);

static bool
readProxyMethods(lua_State *L, TJNIEnv *env, ThreadContext *context, Vector<JavaType *> &interfaces,
                 JavaType *main, Vector<std::unique_ptr<BaseFunction>> &luaFuncs,
                 Vector<JObject> &agentMethods);

extern "C" {
jlong nativeOpen(TJNIEnv *env, jobject object);
void nativeClose(JNIEnv *env, jclass thisClass, jlong ptr);
void referFunc(JNIEnv *env, jclass thisClass, jlong ptr, jboolean deRefer);
jint getClassType(TJNIEnv *env, jclass, jlong ptr, jclass clz);
jboolean sameSigMethod(JNIEnv *env, jclass, jobject f, jobject s, jobject caller);
jobjectArray runScript(TJNIEnv *env, jclass thisClass, jlong ptr, jobject script,
                       jboolean isFile,
                       jobjectArray args);
jobject constructChild(TJNIEnv *env, jclass thisClass, jlong ptr, jclass target,
                       jlong nativeInfo);
jobject
invokeSuper(TJNIEnv *env, jclass c, jobject thiz, jobject method, jint id, jobjectArray args);
jobject
invokeLuaFunction(TJNIEnv *env, jclass clazz, jlong ptr, jlong funcRef, jboolean multiRet,
                  jobject proxy,
                  jstring methodName, jintArray argTypes, jobjectArray args);
}

static const luaL_Reg javaInterfaces[] =
        {{"type",       javaType},
         {"instanceof", javaInstanceOf},
         {"new",        javaNew},
         {"newArray",   javaNewArray},
         {"import",     javaImport},
         {"proxy",      javaProxy},
         {"object",     javaToJavaObject},
         {"unbox",      javaUnBox},
         {"super",      javaSuper},
         {"typeof",     javaTypeOf},
         {"log",        javaLog},
         {"benchmark",  javaBenchmark},
         {"em",         javaEmptyMethod},
         {nullptr,      nullptr}};

static const JNINativeMethod nativeMethods[] =
        {{"nativeOpen",        "()J",                                                                                  (void *) nativeOpen},
         {"nativeClose",       "(J)V",                                                                                 (void *) nativeClose},
         {"runScript",         "(JLjava/lang/Object;Z"
                               "[Ljava/lang/Object;"
                               ")[Ljava/lang/Object;",                                                                 (void *) runScript},
         {"constructChild",    "(JLjava/lang/Class;J)"
                               "Ljava/lang/Object;",                                                                   (void *) constructChild},
         {"referFunc",         "(JZ)V",                                                                                (void *) referFunc},
         {"getClassType",      "(JLjava/lang/Class;)I",                                                                (void *) getClassType},
         {"sameSigMethod",     "(Ljava/lang/reflect/Method;Ljava/lang/reflect/Method;Ljava/lang/reflect/Method;)Z",    (void *) sameSigMethod},
         {"invokeSuper",       "(Ljava/lang/Object;Ljava/lang/reflect/Method;I[Ljava/lang/Object;)Ljava/lang/Object;", (void *) invokeSuper},
         {"invokeLuaFunction", "(JJZLjava/lang/Object;Ljava/lang/String;"
                               "[I[Ljava/lang/Object;)"
                               "Ljava/lang/Object;",                                                                   (void *) invokeLuaFunction}};

JavaVM *vm;
jclass stringType;
jclass classType;
jclass throwableType;
jclass contextClass;
jmethodID objectHash;
jmethodID classGetName;
jmethodID objectToString;
static __thread jmp_buf errorJmp;

#define JAVA_OBJECT "java_object"
#define JAVA_TYPE "java_type"
#define JAVA_CONTEXT "java_context"
#define JAVA_MEMBER "java_member"

class RegisterKey {
};

static const RegisterKey *OBJECT_KEY = reinterpret_cast<const RegisterKey *>(javaInterfaces);
static const RegisterKey *TYPE_KEY = OBJECT_KEY + 1;
static const RegisterKey *MEMBER_KEY = OBJECT_KEY + 2;


static bool testType(lua_State *L, int objIndex, const RegisterKey *key) {
    if (!lua_getmetatable(L, objIndex)) return false;
    lua_rawgetp(L, LUA_REGISTRYINDEX, key);
    bool ret = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return ret;
}

static void *testUData(lua_State *L, int ud, const RegisterKey *key) {
    void *p = lua_touserdata(L, ud);
    if (p != nullptr) {  /* value is a userdata? */
        if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
            lua_rawgetp(L, LUA_REGISTRYINDEX, key);  /* get correct metatable */
            if (!lua_rawequal(L, -1, -2))  /* not the same? */
                p = nullptr;  /* value is a userdata with wrong metatable */
            lua_pop(L, 2);  /* remove both metatables */
            return p;
        }
    }
    return nullptr;  /* value is not a userdata with a metatable */
}

static bool isJavaTypeOrObject(lua_State *L, int index) {
    return lua_rawlen(L, index) == sizeof(void *);
}

static JavaObject *checkJavaObject(lua_State *L, int idx) {
    auto *ret = static_cast<JavaObject *>(testUData(L, idx, OBJECT_KEY));
    if (unlikely(ret == nullptr))
        ERROR("Expected " JAVA_OBJECT ",but got %s", luaL_tolstring(L, idx, NULL));
    return ret;
}

static JavaType *checkJavaType(lua_State *L, int idx) {
    auto **ret = static_cast<JavaType **>(testUData(L, idx, TYPE_KEY));
    if (unlikely(ret == nullptr))
        ERROR("Expected " JAVA_TYPE ",but got %s", luaL_tolstring(L, idx, NULL));
    return *ret;
}

static void setMetaTable(lua_State *L, const RegisterKey *key) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, key);
    lua_setmetatable(L, -2);
}

static bool newMetaTable(lua_State *L, const RegisterKey *key, const char *tname) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, key);
    if (!lua_isnil(L, -1)) return false;
    lua_pop(L, 1);
    lua_newtable(L);  /* create metatable */
    if (tname) {
        lua_pushstring(L, tname);
        lua_setfield(L, -2, "__name");  /* metatable.__name = tname */
    }
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, key);
    return true;
}

static int safeGC(lua_State *L) {
    lua_gc(L, LUA_GCCOLLECT, 0);
    return 1;
}

static inline void luaFullGC(lua_State *L) {
    do {
        lua_pushcfunction(L, safeGC);
    } while (lua_pcall(L, 0, 0, 0) != LUA_OK);
}

static inline void
pushJavaObject(lua_State *L, TJNIEnv *env, ThreadContext *context, jobject obj,
               JavaType *given = nullptr) {

    if (((++context->pushedCount) & 0x1FF) == 0) {// multi of 512
        luaFullGC(L);
    }

    if (context->pushedCount >> 13) {//>=8192
        lua_pushnil(L);
        return;
    }

    auto *objectRef = (JavaObject *) lua_newuserdata(L, sizeof(JavaObject));
    objectRef->object = env->NewGlobalRef(obj);

    objectRef->type = given ? given : context->scriptContext->ensureType(env,
                                                                         (JClass) env->GetObjectClass(
                                                                                 obj));
    
    // 检查 type 是否有效
    if (unlikely(objectRef->type == nullptr)) {
        // 清理已创建的全局引用
        env->DeleteGlobalRef(objectRef->object);
        // 弹出 userdata 并压入 nil
        lua_pop(L, 1);
        lua_pushnil(L);
        return;
    }
    
    setMetaTable(L, OBJECT_KEY);
}

static void appendInt(String &str, int i) {
    char tmp[8];
    snprintf(tmp, 7, "%d", i);
    str.append(tmp);
}

static String traceback(lua_State *L, int level) {
    lua_Debug ar;
    String ret;
    ret.reserve(1024);
    ret += "\nstack traceback:";
    bool isOk = false;
    while (lua_getstack(L, level++, &ar)) {
        lua_getinfo(L, "Slnt", &ar);
        if (ar.currentline > 0) {
            ret.append("\n\tat line ");
            appendInt(ret, ar.currentline)/*<<':'*/;
            if (*ar.namewhat != '\0')  /* is there a name from code? */
                ret.append(" in ").append(ar.namewhat).append(ar.name);  /* use it */
            else if (*ar.what == 'm')  /* main? */
                ret.append(" in main chunk");
            else if (*ar.what != 'C') {
                /* for Lua functions, use <file:line> */
                ret.append(" in function <").append(ar.short_src).push_back(':');
                appendInt(ret, ar.linedefined);
                ret.push_back('-');
                appendInt(ret, ar.lastlinedefined);
                ret.push_back('>');
            }
            if (ar.istailcall)
                ret.append("\n\t(...tail calls...)");
            isOk = true;
        }
    }
    if (isOk)
        return ret;
    return String();
}

static void pushJavaException(lua_State *L, ThreadContext *context) {
    context->setPendingException(traceback(L, 0));
    pushJavaObject(L, getThreadEnv(), context,
                   JObject(getThreadEnv(), context->transferJavaError()));
}

inline void throwJavaError(lua_State *L, ThreadContext *context) {
    pushJavaException(L, context);
    lua_error(L);
}

static inline bool isThrowableObject(lua_State *L, ThreadContext *context) {
    return testUData(L, -1, OBJECT_KEY) && static_cast<JavaObject *>(
            lua_touserdata(L, -1))->type->isThrowable(getThreadEnv());
}

static int luaPCallHandler(lua_State *L) {
    ThreadContext *context = getContext(L);
    if (isThrowableObject(L, context))
        return 1;
    if (context->hasErrorPending()) {
        pushJavaException(L, context);
        return 1;
    }
    const char *s = luaL_tolstring(L, -1, nullptr);
    luaL_traceback(L, L, s, 1);
    return 1;
}

static inline void pushErrorHandler(lua_State *L, ThreadContext *context) {
    lua_pushlightuserdata(L, context);
    lua_pushcclosure(L, luaPCallHandler, 1);
}

static int luaGetJava(lua_State *L) {
    int size = sizeof(javaInterfaces) / sizeof(luaL_Reg) - 1;
    lua_createtable(L, 0, size);
    lua_getfield(L, LUA_REGISTRYINDEX, JAVA_CONTEXT);
    luaL_setfuncs(L, javaInterfaces, 1);
    return 1;
}

static int luaPanic(lua_State *L) {
    const char *s = lua_tostring(L, -1);
    if (s) {
        luaL_traceback(L, L, s, 1);
        s = lua_tostring(L, -1);
    } else s = "Unexpected lua error";
    lua_getfield(L, LUA_REGISTRYINDEX, JAVA_CONTEXT);
    auto *context = static_cast<ThreadContext *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    context->setPendingException(s);
    _longjmp(errorJmp, -1);
    return 0;
}

void ScriptContext::config(lua_State *L) {
    lua_atpanic(L, luaPanic);
    ThreadContext *context = threadContext;
    lua_pushlightuserdata(L, context);//for panic and clean
    lua_setfield(L, LUA_REGISTRYINDEX, JAVA_CONTEXT);
    int top = lua_gettop(L);
    luaL_requiref(L, "java", luaGetJava,/*glb*/true);
    lua_settop(L, top);

    if (newMetaTable(L, TYPE_KEY, JAVA_TYPE)) {
        int index = lua_gettop(L);
        lua_pushstring(L, "Can't change java metatable");
        lua_setfield(L, index, "__metatable");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, setFieldOrArray, 1);
        lua_setfield(L, index, "__newindex");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, getClassMember, 1);
        lua_setfield(L, index, "__index");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, javaNew, 1);
        lua_setfield(L, index, "__call");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, javaTypeToString, 1);
        lua_setfield(L, index, "__tostring");
    }
    if (newMetaTable(L, OBJECT_KEY, JAVA_OBJECT)) {
        int index = lua_gettop(L);
        lua_pushstring(L, "Can't change java metatable");
        lua_setfield(L, index, "__metatable");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, setFieldOrArray, 1);
        lua_setfield(L, index, "__newindex");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, getClassMember, 1);
        lua_setfield(L, index, "__index");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, objectEquals, 1);
        lua_setfield(L, index, "__eq");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, javaObjectToString, 1);
        lua_setfield(L, index, "__tostring");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, javaIterate, 1);
        lua_setfield(L, index, "__pairs");
        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, getObjectLength, 1);
        lua_setfield(L, index, "__len");

        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, callInitializer, 1);
        lua_setfield(L, index, "__call");
        //concat is simple
        lua_pushcfunction(L, concatString);
        lua_setfield(L, index, "__concat");

        lua_pushlightuserdata(L, context);
        lua_pushcclosure(L, JavaObject::objectGc, 1);
        lua_setfield(L, index, "__gc");
    }
    luaL_openlibs(L);
}

int JavaObject::objectGc(lua_State *L) {
    auto context = (ThreadContext *) lua_touserdata(L, lua_upvalueindex(1));
    auto ref = (JavaObject *) lua_touserdata(L, -1);
    getThreadEnv()->DeleteGlobalRef(ref->object);
    context->pushedCount--;
    return 0;
}

ThreadContext::~ThreadContext() {
    LOGE("~ThreadContext pushedCount: %d", pushedCount);
    scriptContext = nullptr;
}

ScriptContext::~ScriptContext() {
    LOGE("~ScriptContext");
    for (auto &&pair: typeMap) {
        delete pair.second;
    }

    lua_getfield(state, LUA_REGISTRYINDEX, JAVA_CONTEXT);
    auto *context = (ThreadContext *) lua_touserdata(state, -1);
    context->scriptContext = nullptr;//to mark it as freed
    lua_close(state);

    getThreadEnv()->DeleteWeakGlobalRef(javaRef);
    delete threadContext;
    
    // 销毁主状态机互斥锁
    pthread_mutex_destroy(&stateMutex);
}

static inline void pushJavaType(lua_State *L, JavaType *type) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, type);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        *((JavaType **) lua_newuserdata(L, sizeof(JavaType *))) = type;
        setMetaTable(L, TYPE_KEY);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, type);
    }

}

static inline ThreadContext *getContext(lua_State *L) {
    return (ThreadContext *) lua_touserdata(L, lua_upvalueindex(1));
}

static inline MemberInfo *getMemberInfo(lua_State *L) {
    auto *ret = (MemberInfo *) lua_touserdata(L, FLAG_INDEX);
    return ret ? ret : (MemberInfo *) lua_touserdata(L, 1);
}

static bool parseLuaObject(lua_State *L, int idx,
                           ValidLuaObject &luaObject) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            luaObject.type = T_NIL;
            break;
        case LUA_TBOOLEAN: {
            luaObject.type = T_BOOLEAN;
            luaObject.isTrue = (jboolean) lua_toboolean(L, idx);
            break;
        }
        case LUA_TSTRING: {
            luaObject.type = T_STRING;
            luaObject.string = lua_tostring(L, idx);
            break;
        }
        case LUA_TFUNCTION: {
            luaObject.type = T_FUNCTION;
            luaObject.func = saveLocalFunction(L, idx);
            break;
        }
        case LUA_TNUMBER: {
            int isInt;
            lua_Integer intValue = lua_tointegerx(L, idx, &isInt);
            if (isInt) {
                luaObject.type = T_INTEGER;
                luaObject.integer = intValue;
            } else {
                luaObject.type = T_FLOAT;
                luaObject.number = lua_tonumber(L, idx);
            }
            break;
        }
        case LUA_TLIGHTUSERDATA: {
            luaObject.type = T_INTEGER;
            luaObject.userdata = lua_touserdata(L, idx);
            break;
        }
        case LUA_TUSERDATA:
            if (testUData(L, idx, OBJECT_KEY)) {
                luaObject.type = T_OBJECT;
                luaObject.objectRef = (JavaObject *) lua_touserdata(L, idx);
            } else {
                // userdata 不是 JavaObject，无法解析
                return false;
            }
            break;
        case LUA_TTABLE: {
            luaObject.type = T_TABLE;
            luaObject.lazyTable = new LazyTable(idx, L);
            break;
        }
        default:
            return false;
    }
    return true;
}

static bool checkLuaTypeNoThrow(TJNIEnv *env, lua_State *L, JavaType *expected,
                                ValidLuaObject &luaObject) {
    if (expected == nullptr) return false;
    if (expected->isInteger()) {
        if (luaObject.type != T_INTEGER) {
            if (luaObject.type == T_FLOAT) {
                luaObject.type = T_INTEGER;
                luaObject.integer = int64_t(luaObject.number);
            } else if (luaObject.type == T_OBJECT) {
                JavaType *type = luaObject.objectRef->type;
                if (!expected->canAcceptBoxedNumber(type)) goto bail;
                luaObject.type = T_INTEGER;
                luaObject.integer = type->isBoxedChar() ?
                                    (jlong) env->CallCharMethod(luaObject.objectRef->object,
                                                                charValue) :
                                    env->CallLongMethod(luaObject.objectRef->object, longValue);
            } else if (luaObject.type == T_STRING) {
                goto HANDLE_CHAR_STRING;
            } else goto bail;
        }
    } else if (expected->isFloat()) {
        if (luaObject.type != T_FLOAT) {
            if (luaObject.type == T_INTEGER) {
                luaObject.type = T_FLOAT;
                luaObject.number = luaObject.integer;
            } else if (luaObject.type == T_OBJECT) {
                JavaType *type = luaObject.objectRef->type;
                if (!expected->canAcceptBoxedNumber(type)) goto bail;
                luaObject.type = T_FLOAT;
                luaObject.number = type->isBoxedChar() ?
                                   (double) env->CallCharMethod(luaObject.objectRef->object,
                                                                charValue) :
                                   env->CallDoubleMethod(luaObject.objectRef->object, doubleValue);
            } else if (luaObject.type == T_STRING) {
                goto HANDLE_CHAR_STRING;
            } else
                goto bail;
        }
    } else if (expected->isChar()) {
        if (luaObject.type == T_STRING) {
            HANDLE_CHAR_STRING:
            if (strlen8to16(luaObject.string) != 1) {
                lua_pushstring(L, "String length for char type must be 1");
                return true;
            } else {
                luaObject.type = T_CHAR;
                strcpy8to16(&luaObject.character, luaObject.string, nullptr);
            }
        } else if (luaObject.type == T_INTEGER) {
            if (luaObject.integer < 0 || luaObject.integer > 65535) {
                lua_pushstring(L, "The integer is too large for char value");
                return true;
            } else luaObject.type = T_CHAR;
        } else if (luaObject.type == T_OBJECT && luaObject.objectRef->type->isBoxedChar()) {
            luaObject.type = T_CHAR;
            luaObject.character = env->CallCharMethod(luaObject.objectRef->object, charValue);
            return false;
        } else goto bail;

    } else if (expected->isBool()) {
        if (luaObject.type != T_BOOLEAN) {
            if (luaObject.type == T_OBJECT && luaObject.objectRef->type->isBoxedChar()) {
                luaObject.type = T_BOOLEAN;
                luaObject.isTrue = env->CallBooleanMethod(luaObject.objectRef->object,
                                                          booleanValue);
                return false;
            } else goto bail;
        }
    } else {
        if (luaObject.type == T_NIL) return false;
        if (luaObject.type == T_OBJECT) {
            if (luaObject.objectRef->type == expected ||
                env->IsInstanceOf(luaObject.objectRef->object, expected->getType()))
                return false;
            else {
                lua_pushfstring(L, "Incompatible object,expected:%s,received:%s",
                                expected->name(env).str(),
                                luaObject.objectRef->type->name(env).str());
                return true;
            }
        }
        if (expected->isStringAssignable(env) && luaObject.type == T_STRING) return false;
        if (luaObject.type == T_FUNCTION) {
            if (expected->isSingleInterface(env))return false;
            forceRelease(luaObject);
            lua_pushstring(L, "Can't convert function to a class that is not a single interface");
            return true;
        }
        if (luaObject.type == T_TABLE) {
            if (expected->isTableType(env) ||
                (expected->isInterface(env) && luaObject.lazyTable->isInterface()))
                return false;
        }
        if (luaObject.type == T_FLOAT) {
            if (expected->isBoxedFloat() || expected->isObjectClass() ||
                expected->isNumberClass()) {
                return false;
            }
        } else if (luaObject.type == T_INTEGER) {
            if (expected->isBoxedInteger() || expected->isBoxedFloat() ||
                expected->isObjectClass() || expected->isNumberClass())
                return false;

        }
        if (expected->isBoxedChar() && (luaObject.type == T_STRING || luaObject.type == T_INTEGER ||
                                        luaObject.type == T_FLOAT))
            return false;
        if (expected->isBoxedBool() && luaObject.type == T_BOOLEAN)
            return false;
        goto bail;
    }
    return false;
    bail:
    lua_pushfstring(L, "Expected type is %s,but receives %s",
                    JString(env->CallObjectMethod(expected->getType(), classGetName)).str(),
                    luaObject.typeName());
    return true;
}

void checkLuaType(TJNIEnv *env, lua_State *L, JavaType *expected, ValidLuaObject &luaObject) {
    if (likely(expected == nullptr)) return;
    if (checkLuaTypeNoThrow(env, L, expected, luaObject)) {
        forceRelease(luaObject);
        lua_error(L);
    }
}

void
readArguments(lua_State *L, TJNIEnv *env, ThreadContext *context, FakeVector<JavaType *> &types,
              FakeVector<ValidLuaObject> &objects,
              int start, int end) {
    for (int i = start; i <= end; ++i) {
        auto **typeRef = (JavaType **) testUData(L, i, TYPE_KEY);
        bool noType = typeRef == nullptr;
        JavaType *paramType = noType ? nullptr : *typeRef;
        types.asVector().push_back(paramType);
        if (!noType) i++;
        ValidLuaObject luaObject;
        if (!parseLuaObject(L, i, luaObject)) {
            forceRelease(types, objects);
            ERROR("Arg unexpected");
        }
        if (checkLuaTypeNoThrow(env, L, paramType, luaObject)) {
            forceRelease(luaObject);
            objects.release();
            lua_error(L);
        }
        objects.asVector().push_back(std::move(luaObject));
    }
}


void pushArrayElement(lua_State *L, TJNIEnv *env, ThreadContext *context, const JavaObject *obj,
                      JavaType *componentType) {
    int isnum;
    jlong index = lua_tointegerx(L, 2, &isnum);
    if (unlikely(!isnum)) ERROR("Invalid index for an array");
    if (unlikely(index > INT32_MAX))ERROR("Index out of range:%d", index);
    switch (componentType->getTypeID()) {
#define PushChar(c) ({ \
        char s[4];\
        strncpy16to8(s,(const char16_t *) &(c), 1);\
        lua_pushstring(L,s);\
        break;\
        })
#define ArrayGet(typeID, jtype, jname, TYPE)\
            case JavaType::typeID:{\
                j##jtype buf;\
                env->Get##jname##ArrayRegion((j##jtype##Array) obj->object, (jsize) index, 1, &buf);\
                lua_push##TYPE(L,buf);\
                break;\
            }
#define IntArrayGet(typeID, jtype, jname) ArrayGet(typeID,jtype,jname,integer)
#define LongArrayGet() IntArrayGet(LONG,long, Long)
#define FloatArrayGet(typeID, jtype, jname) ArrayGet(typeID,jtype,jname,number)
        IntArrayGet(BYTE, byte, Byte)
        IntArrayGet(SHORT, short, Short)
        IntArrayGet(INT, int, Int)
        LongArrayGet()
        FloatArrayGet(FLOAT, float, Float)
        FloatArrayGet(DOUBLE, double, Double)
        ArrayGet(BOOLEAN, boolean, Boolean, boolean)
        case JavaType::CHAR: {
            jchar buf;
            env->GetCharArrayRegion((jcharArray) obj->object, (jsize) index, 1, &buf);
            PushChar(buf);
        }
        default: {
            auto ele = env->GetObjectArrayElement((jobjectArray) obj->object, (jsize) index);
            if (ele == nullptr)
                lua_pushnil(L);
            else pushJavaObject(L, env, context, ele);
        }
    }
    HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
}

static inline void pushRawMethod(lua_State *L, int toIndex) {
    lua_pushvalue(L, toIndex);
    lua_pushcclosure(L, callMethod, 2);
}

void
pushMember(ThreadContext *context, lua_State *L, const Member *member, int tOIndex, bool isStatic,
           int fieldCount, bool isMethod) {
    auto *flag = (MemberInfo *) lua_newuserdata(L, sizeof(MemberInfo));
    if (isStatic) flag->type = *(JavaType **) lua_touserdata(L, tOIndex);
    else flag->object = (JavaObject *) lua_touserdata(L, tOIndex);
    flag->member = member;
    flag->context = context;
    flag->isStatic = isStatic;
    flag->isNotOnlyMethod = fieldCount != 0;
    flag->isDuplicatedField = fieldCount > 1;
    if (fieldCount == 0) {
        pushRawMethod(L, tOIndex);
        return;
    }
    lua_pushvalue(L, tOIndex);
    lua_setuservalue(L, -2);
    if (newMetaTable(L, MEMBER_KEY, JAVA_MEMBER)) {
        int tableIndex = lua_gettop(L);
        if (fieldCount > 0) {
            lua_pushstring(L, "__index");
            lua_pushcclosure(L, getField, 0);
            lua_rawset(L, tableIndex);

            lua_pushstring(L, "__newindex");
            lua_pushcclosure(L, setField, 0);
            lua_rawset(L, tableIndex);
        }

        if (isMethod) {
            lua_pushstring(L, "__call");
            lua_pushcclosure(L, callMethod, 0);
            lua_rawset(L, tableIndex);
        }
        lua_pushstring(L, "__metatable");
        lua_pushstring(L, "Can't change java metatable");
        lua_rawset(L, tableIndex);
    }
    lua_setmetatable(L, -2);
}

void recordLuaError(ThreadContext *context, lua_State *L, int ret) {
    switch (ret) {
        case LUA_ERRERR:
        case LUA_ERRRUN:
        case LUA_ERRGCMM:
        case LUA_ERRSYNTAX:
            if (!isThrowableObject(L, context))
                context->setPendingException(luaL_tolstring(L, -1, nullptr));
            else
                context->setPendingException(
                        (jthrowable) static_cast<JavaObject *>(lua_touserdata(L, -1))->object);
            lua_pop(L, 1);
            break;
        case LUA_ERRMEM:
            context->setPendingException("Lua memory error");
            break;
        case -1:
            context->setPendingException("Unknown error");
        default:
            break;
    }
}

int javaType(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        JavaType *type;
        if (luaL_isstring(L, i)) {
            type = context->ensureType(env, FakeString(L, i));
            if (type == nullptr)
                goto Error;
        } else {
            auto *objectRef = (JavaObject *) testUData(L, i, OBJECT_KEY);
            if (objectRef != nullptr) {
                if (env->IsInstanceOf(objectRef->object, classType)) {
                    type = context->scriptContext->ensureType(env, (jclass) objectRef->object);
                    goto Ok;
                } else if (env->IsInstanceOf(objectRef->object, stringType)) {
                    type = context->ensureType(env,
                                               FakeString(
                                                       JString(env, (jstring) objectRef->object)));
                    if (type != nullptr)
                        goto Ok;
                }
            }
            goto Error;
        }
        Ok:
        pushJavaType(L, type);
        continue;//never reach
        Error:
        ERROR("Invalid type=%s", luaL_tolstring(L, i, NULL));
    }
    return count;
}

static int javaProxy(lua_State *L) {
    if (!lua_istable(L, 1)) return 0;
    ThreadContext *context = getContext(L);
    SetErrorJMP();
    Vector<JavaType *> interfaces;
    lua_getfield(L, 1, "super");
    JavaType **typeRef;
    JavaType *main = nullptr;
    JavaObject *superObject = nullptr;
    if ((typeRef = (JavaType **) testUData(L, -1, TYPE_KEY)) != nullptr)
        main = *typeRef;
    else if ((superObject = (JavaObject *) testUData(L, -1, OBJECT_KEY)) != nullptr) {
        main = superObject->type;
    }
    lua_getfield(L, 1, "interfaces");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if ((typeRef = (JavaType **) testUData(L, -1, TYPE_KEY)) != nullptr)
                interfaces.push_back(*typeRef);
            lua_pop(L, 1);
        }
    }
    if (main == nullptr) {
        if (interfaces.size() == 0) {
            lua_pushstring(L, "No class or interfaces");
            goto __ErrorHandle;
        } else {
            main = *interfaces.begin();
            interfaces.erase(interfaces.begin());
        }
    }
    auto env = getThreadEnv();
    Vector<std::unique_ptr<BaseFunction>> luaFuncs;
    Vector<JObject> agentMethods;
    std::unique_ptr<BaseFunction> defaultFunc;
    lua_getfield(L, 1, "methods");
    if (lua_istable(L, -1)) {
        if (!readProxyMethods(L, env, context, interfaces, main, luaFuncs,
                              agentMethods)) {
            goto __ErrorHandle;
        }
    }
    lua_getfield(L, 1, "all");
    if (lua_isfunction(L, -1)) {
        // Use reset() for unique_ptr assignment
        defaultFunc.reset((BaseFunction *) saveLocalFunction(L, -1));
        defaultFunc->javaRefCount++;
    }
    lua_getfield(L, 1, "shared");
    bool shared;
    if (lua_isnil(L, -1)) shared = false;
    else if (lua_isboolean(L, -1)) shared = lua_toboolean(L, -1) != 0;
    else if (lua_isnumber(L, -1)) shared = lua_tointeger(L, -1) != 0;
    else shared = true;
    jobject proxy;
    if (superObject) {
        proxy = context->proxy(env, main, &interfaces, agentMethods, luaFuncs, defaultFunc.get(),
                               shared,
                               0, superObject->object);
    } else {
        Vector<ValidLuaObject> constructArgs;
        Vector<JavaType *> argTypes;
        lua_getfield(L, 1, "args");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            JavaType *type = nullptr;
            while (lua_next(L, -2)) {
                typeRef = (JavaType **) testUData(L, -1, TYPE_KEY);
                if (typeRef != nullptr) {
                    type = *typeRef;
                } else {
                    ValidLuaObject luaObject;
                    if (!parseLuaObject(L, -1, luaObject)) {
                        lua_pushstring(L, "Arg type not support");
                        goto __ErrorHandle;
                    }
                    argTypes.push_back(type);
                    constructArgs.push_back(std::move(luaObject));
                    type = nullptr;
                }

            }
        }
        void *constructInfo[] = {&constructArgs, &argTypes};
        proxy = context->proxy(env, main, &interfaces, agentMethods, luaFuncs, defaultFunc.get(),
                               shared,
                               (long) &constructInfo);
    }
    if (proxy == INVALID_OBJECT) {
        pushJavaException(L, context);
        goto __ErrorHandle;
    }
    for (auto &ptr: luaFuncs) {
        ptr.release();
    }
    defaultFunc.release();
    pushJavaObject(L, env, context, JObject(env, proxy));
    return 1;
}

bool
readProxyMethods(lua_State *L, TJNIEnv *env, ThreadContext *context, Vector<JavaType *> &interfaces,
                 JavaType *main, Vector<std::unique_ptr<BaseFunction>> &luaFuncs,
                 Vector<JObject> &agentMethods) {
    uint expectedLen = (uint) lua_rawlen(L, -1);
    luaFuncs.reserve(expectedLen);
    agentMethods.reserve(expectedLen);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (!luaL_isstring(L, -2)) {
            TopErrorHandle("Not a string for method name=%s", luaL_tolstring(L, -2, NULL));
        }
        FakeString methodName(L, -2);
        if (strcmp(methodName.data(), "<init>") == 0) {
            TopErrorHandle("Constructor can't be override");
        }
        if (lua_istable(L, -1)) {
            Vector<JavaType *> paramTypes;
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                JavaType **typeRef;
                if ((typeRef = (JavaType **) testUData(L, -1, TYPE_KEY)) != nullptr)
                    paramTypes.push_back(*typeRef);
                else if (lua_isfunction(L, -1)) {
                    JavaType *matchType = main;
                    auto info = main->findMethod(env, methodName, false, paramTypes,
                                                 nullptr);
                    if (info == nullptr) {
                        for (auto interface: interfaces) {
                            info = interface->findMethod(env, methodName, false,
                                                         paramTypes, nullptr);
                            if (info != nullptr) {
                                matchType = interface;
                                break;
                            }
                        }
                    }
                    if (info == nullptr) {
                        TopErrorHandle("Can't find matched method "
                                       "for the method:%s", methodName.c_str());
                    }
                    agentMethods.push_back(env->ToReflectedMethod(
                            matchType->getType(), info->id, JNI_FALSE));
                    BaseFunction *func = saveLocalFunction(L, -1);
                    func->javaRefCount++;
                    luaFuncs.emplace_back(func);
                    paramTypes.clear();
                }
                lua_pop(L, 1);
            }
        } else if (lua_isfunction(L, -1)) {//proxy for all methods
            BaseFunction *func = saveLocalFunction(L, -1);
            auto all = main->findAllObjectMethod(env, methodName);
            bool found = false;
            if (all) {
                for (auto &&info: *all) {
                    func->javaRefCount++;
                    agentMethods.push_back(env->ToReflectedMethod(
                            main->getType(), info.id, JNI_FALSE));
                    luaFuncs.emplace_back(func);
                    found = true;
                }
            }
            for (auto interface: interfaces) {
                all = main->findAllObjectMethod(env, methodName);
                if (!all) continue;
                for (auto &&info: *all) {
                    func->javaRefCount++;
                    agentMethods.push_back(env->ToReflectedMethod(
                            interface->getType(), info.id, JNI_FALSE));
                    luaFuncs.emplace_back(func);
                    found = true;
                }
            }
            if (!found) {
                TopErrorHandle("No method named %s", methodName);
            }
        }
        lua_pop(L, 1);
    }
    return true;
    __ErrorHandle:
    return false;
};

jobject constructChild(TJNIEnv *env, jclass, jlong ptr, jclass target,
                       jlong nativeInfo) {
    auto *context = (ScriptContext *) ptr;
    ThreadContext *threadContext = context->threadContext;
    JavaType *type = context->ensureType(env, target);
    void **constructInfo = (void **) nativeInfo;
    auto *constructArgs = (Vector<ValidLuaObject> *) constructInfo[0];
    auto *argTypes = (Vector<JavaType *> *) constructInfo[1];
    jobject ret = type->newObject(env, threadContext, *argTypes, *constructArgs);
    if (threadContext->hasErrorPending())
        threadContext->throwToJava();
    return ret;
}

/**
 * 保存 Lua 函数为可跨线程调用的回调
 * 
 * 实现细节：
 * 1. 创建 userdata 管理生命周期
 * 2. 创建独立 thread
 * 3. 将 thread 绑定到 userdata.uservalue
 * 4. 将函数移动到 thread 栈底
 * 5. 保存到注册表
 * 
 * @param L 主 Lua 状态机
 * @param i 函数在栈上的索引
 * @return LocalFunctionInfo 指针，用于后续调用
 */
LocalFunctionInfo *saveLocalFunction(lua_State *L, int i) {
    // 0. 首先将函数复制到栈顶，避免索引问题
    lua_pushvalue(L, i);
    int func_index = lua_gettop(L);

    // 1. 创建 userdata（管理生命周期）
    void **ud = (void **) lua_newuserdata(L, sizeof(void *));
    int ud_index = lua_gettop(L);

    // 2. 创建独立的 thread（协程）
    lua_State *thread = lua_newthread(L);

    // 3. 将 thread 绑定到 userdata 的 uservalue（防止 GC）
    lua_setuservalue(L, ud_index);

    // 4. 重新获取 thread（因为 setuservalue 会弹出栈顶）
    lua_getuservalue(L, ud_index);
    thread = lua_tothread(L, -1);
    lua_pop(L, 1);

    // 5. 将回调函数移动到 thread 的栈底
    // 使用 lua_xmove 将函数从主状态机移动到 thread
    lua_pushvalue(L, func_index);  // 复制函数到栈顶
    lua_xmove(L, thread, 1);  // 移动到 thread 栈
    // 现在 thread 栈: [function]

    // 6. 创建 LocalFunctionInfo 并存储到 userdata
    auto *info = new LocalFunctionInfo(thread);
    *ud = info;

    // 7. 将 userdata 保存到注册表，以 LocalFunctionInfo 指针为键
    lua_pushvalue(L, ud_index);  // 复制 userdata 到栈顶
    lua_rawsetp(L, LUA_REGISTRYINDEX, info);

    // 8. 清理栈，移除 userdata 和原始函数副本
    lua_pop(L, 2);  // pop userdata and function copy

    return info;
}

static bool qualifyJavaName(const String &name) {
    auto i = name.length();
    const char *s = &name[0];
    if (!isalpha(*s)) return false;
    for (; i != 0;) {
        auto c = s[--i];
        if (c != '_' && !isalnum(c)) {
            return false;
        }
    }
    return true;
}

bool verifyClassName(const char *name) {
    if (!name || !name[0]) return false;
    const char *p = name;
    bool can_accept_dot = false;
    bool is_first_char = true;
    while (*p) {
        char c = *p++;
        if (c == '.') {
            if (!can_accept_dot) return false;
            can_accept_dot = false;
            is_first_char = true;
            continue;
        }
        if (!(isalnum(c) || c == '_' || c == '$')) return false;
        if (is_first_char && isdigit(c)) return false;
        can_accept_dot = true;
        is_first_char = false;
    }
    return can_accept_dot;
}

int javaImport(lua_State *L) {
    ThreadContext *context = getContext(L);
    SetErrorJMP();
    auto env = getThreadEnv();
    if (!luaL_isstring(L, -1)) ERROR("Should pass a string for import");
    FakeString s(lua_tostring(L, -1));

    Import *import = context->getImport();

    if (s.c_str()[0] == '[') TopErrorHandle("Don't import array type!");
    uint separate = ((const String &) s).rfind('.');
    String pack;
    if (separate != String::npos) {
        auto len = separate + 1;
        pack.resize(uint(len));
        memcpy(&pack[0], s, len);
    }

    FakeString name(separate != String::npos ? &s.c_str()[separate + 1] : s.c_str());
    auto &&iter = import->stubbed.find(name);
    if (iter == nullptr) {
        if (!verifyClassName(s.c_str())) {
            TopErrorHandle("Invalid class name: %s", s.c_str());
        }
        JClass c(context->findClass(env, String((const String &) s)));
        if (c == nullptr) {
            TopErrorHandle("Type:%s not found", s.c_str());
        }
        auto type = context->scriptContext->ensureType(env, c);
        Import::TypeInfo info;
        info.type = type;
        info.pack = DeleteOrNotString(pack, true);
        import->stubbed[name] = std::move(info);
        pushJavaType(L, type);
    } else {
        const char *prevName = iter->second.pack;
        if (strcmp(prevName, pack.data()) != 0 && strcmp(prevName, "java.lang.") != 0) {
            TopErrorHandle("Only single import is allowed for name: %s"
                           ",with previous class: %s", name.c_str(), prevName);
        }
        pushJavaType(L, iter->second.type);
    }
    if (qualifyJavaName(name)) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, name.data());
    }
    return 1;
}

static int newArray(lua_State *L, TJNIEnv *env, int index, ThreadContext *context, JavaType *type,
                    JavaType *arrayType = nullptr) {
    if (type->isVoid()) {
        ERROR("Type Error:array for void.class can't be created");
    }

    int isnum;
    jlong size = lua_tointegerx(L, index++, &isnum);
    if (!isnum) {
        ERROR("Type Error: not a integer but %s", luaL_typename(L, index));
    }

    if (size > INT32_MAX || size < -1) {
        ERROR("integer overflowed");
    }
    int top = lua_gettop(L);
    if (size != -1 && top - index > size) {
        ERROR("%d elements is too many for an array of size %d", top - index, size);
    }
    Vector<ValidLuaObject> elements;
    elements.reserve(static_cast<uint >(top - index + 1));

    for (; index <= top; ++index) {
        ValidLuaObject object;
        if (!parseLuaObject(L, index, object)) {
            forceRelease(elements);
            ERROR("Arg unexpected for array");
        }
        if (checkLuaTypeNoThrow(env, L, type, object)) {
            forceRelease(elements, object);
            lua_error(L);
        }
        elements.push_back(std::move(object));
    }
    if (size == -1) size = elements.size();
    JArray ret(env, type->newArray(env, context, (jint) size, elements));
    if (ret == nullptr) {
        forceRelease(elements);
        throwJavaError(L, context);
    }
    pushJavaObject(L, env, context, ret, arrayType);
    return 1;
}


static void
unbox(lua_State *L, TJNIEnv *env, ThreadContext *context, int index, ValidLuaObject &ret) {
    auto *object = (JavaObject *) testUData(L, index, OBJECT_KEY);
    ret.type = T_NIL;
    if (object == nullptr) {
        switch (lua_type(L, index)) {
            case LUA_TNIL:
            case LUA_TFUNCTION:
            case LUA_TTABLE:
            case LUA_TTHREAD:
                break;
            default:
                if (!parseLuaObject(L, index, ret)) break;
                if (ret.type == T_STRING) {
                    if (strlen8to16(ret.string) == 1) {
                        strcpy8to16(&ret.character, ret.string, nullptr);
                        ret.type = T_CHAR;
                    } else {
                        ret.type = T_NIL;
                        break;
                    }
                }
                return;
        }
    } else {
        switch (object->type->getTypeID()) {
            case JavaType::BOX_BOOLEAN:
                ret.isTrue = env->CallBooleanMethod(object->object, booleanValue);
                ret.type = T_BOOLEAN;
                break;
            case JavaType::BOX_BYTE:
            case JavaType::BOX_SHORT:
            case JavaType::BOX_INT:
            case JavaType::BOX_LONG:
                ret.integer = env->CallLongMethod(object->object, longValue);
                ret.type = T_INTEGER;
                break;
            case JavaType::BOX_FLOAT:
            case JavaType::BOX_DOUBLE:
                ret.number = env->CallDoubleMethod(object->object, doubleValue);
                ret.type = T_FLOAT;
                break;
            case JavaType::BOX_CHAR: {
                jchar c = env->CallCharMethod(object->object, charValue);
                ret.character = c;
                ret.type = T_CHAR;
                break;
            }
            default:
                break;
        }
    }
}

int javaNew(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    JavaType *type = nullptr, *component = nullptr;
    if (luaL_isstring(L, 1)) {
        SetErrorJMP();
        FakeString des(L, 1);
        const String &desStr = des;
        uint arr_st = desStr.find('[');
        if (arr_st == String::npos) {
            type = context->ensureType(env, desStr);
            goto TypeNew;
        }
        String name = desStr.substr(0, arr_st);
        Vector<int> dims;
        do {
            uint close = desStr.find(']', arr_st);
            if (close == String::npos) {
                TopErrorHandle("Bad array type:%s", des.data());
            }
            char *s;
            for (++arr_st; isspace(des[arr_st]); ++arr_st);
            if (arr_st != close) {
                if (!isdigit(des[arr_st])) {
                    BadSizeString:
                    lua_pushlstring(L, &des.data()[arr_st], close - arr_st);
                    TopErrorHandle("Bad array size string:%s", lua_tostring(L, -1));
                }
                unsigned long l = strtoul(des.data() + arr_st, &s, 0);
                if (l > INT32_MAX) {
                    TopErrorHandle("Array size out of range:%lu", l);
                }
                if (s[0] != ']' && !isspace(s[0])) {
                    goto BadSizeString;
                }
                dims.push_back((int) l);
            } else {
                dims.push_back(-1);
            }

            for (arr_st = close + 1; isspace(des[arr_st]) && arr_st != des.size(); ++arr_st);
            if (des[arr_st] != '[') {
                if (arr_st == des.size()) {
                    break;
                }
                TopErrorHandle("Bad array type string:%s", des.data());
            }
        } while (true);
        int dimMax = -1;
        for (int i = dims.size(); i--;) {
            if (dims[i] != -1) {
                if (dimMax == -1)
                    dimMax = i;
            } else if (dimMax != -1)
                TopErrorHandle("Array dimensions must be continuous: %s", des.data());
        }

        if (dimMax == -1 || dimMax == 0) {
            int len = dimMax == -1 ? -1 : dims[0];
            lua_pushinteger(L, len);
            if (lua_gettop(L) != 2)
                lua_insert(L, 2);
            int squareCount = dims.size() - 1;
            name.reserve(name.length() + (squareCount) * 2);
            while (squareCount--) {
                name.append("[]");
            }
            component = context->ensureType(env, name);
            goto TypeNew;
        } else {
            static jmethodID multiArrayNew;
            if (multiArrayNew == nullptr) {
                jclass Array = context->ArrayType(env)->getType();
                multiArrayNew = env->GetStaticMethodID(Array, "newInstance",
                                                       "(Ljava/lang/Class;[I)Ljava/lang/Object;");
            }
            auto dimArray = env->NewIntArray(dimMax + 1);
            void *p = env->GetPrimitiveArrayCritical(dimArray, nullptr);
            memcpy(p, &dims[0], (dimMax + 1) * 4);
            env->ReleasePrimitiveArrayCritical(dimArray, p, 0);
            type = context->ensureType(env, name);
            auto ret = (JObjectArray) env->CallStaticObjectMethod(
                    context->ArrayType(env)->getType(),
                    multiArrayNew, type->getType(),
                    dimArray.get());
            forceRelease(dims);
            HOLD_JAVA_EXCEPTION(context, {
                throwJavaError(L, context);
            });
            type = context->scriptContext->ensureType(env, env->GetObjectClass(ret));
            component = type->getComponentType(env);
            int len = dims[0];
            int top = lua_gettop(L);
            if (len == -1) {
                len = top - 1;
            } else if (len < top - 1) {
                ERROR("%d elements is too many for an array of size %d", top - 1, len);
            }
            for (int i = 2, max = max(top, len + 1); i <= max; ++i) {
                ValidLuaObject object;
                if (!parseLuaObject(L, i, object)) {
                    TopErrorHandle("Arg unexpected for array");
                }
                if (checkLuaTypeNoThrow(env, L, component, object)) {
                    lua_error(L);
                }
                jvalue v = context->luaObjectToJValue(env, object, component);
                env->SetObjectArrayElement(ret, i - 2, v.l);
                cleanArg(env, v.l, object.shouldRelease);
            }
            pushJavaObject(L, env, context, ret, type);
        }
    } else {
        type = checkJavaType(L, 1);
        component = type->getComponentType(env);
        TypeNew:
        if (component != nullptr) {
            return newArray(L, env, 2, context, component, type);
        } else if (!type->isPrimitive()) {
            int top = lua_gettop(L);
            uint expectedSize = uint(top - 1);
            JavaType *_types[expectedSize];
            ValidLuaObject _objects[expectedSize];
            FakeVector<JavaType *> types(_types, expectedSize);
            FakeVector<ValidLuaObject> objects(_objects, expectedSize);
            readArguments(L, env, context, types, objects, 2, top);
            JObject obj = JObject(env, type->newObject(env, context, types, objects));
            if (context->hasErrorPending()) {
                forceRelease(obj);
                types.release();
                throwJavaError(L, context);
            }
            pushJavaObject(L, env, context, obj.get(), type);
        } else if (lua_gettop(L) == 2) {
            //extension for primitive type cast
            ValidLuaObject luaObject;
            unbox(L, env, context, 2, luaObject);
            if (luaObject.type == T_NIL) {
                ERROR("Invalid value cast to primitive type");
            }
            switch (type->getTypeID()) {
                case JavaType::BOOLEAN: {
                    if (luaObject.type == T_BOOLEAN)
                        lua_pushboolean(L, luaObject.isTrue);
                    else
                        ERROR("Invalid value as boolean value");
                    break;
                }
                case JavaType::BYTE: {
                    lua_pushinteger(L,
                                    luaObject.type == T_FLOAT ? int8_t(luaObject.number) : int8_t(
                                            luaObject.integer));
                    break;
                }
                case JavaType::SHORT: {
                    lua_pushinteger(L,
                                    luaObject.type == T_FLOAT ? int16_t(luaObject.number) : int16_t(
                                            luaObject.integer));
                    break;
                }
                case JavaType::CHAR: {
                    char16_t v = luaObject.type == T_FLOAT ? uint16_t(luaObject.number) : uint16_t(
                            luaObject.integer);
                    char tmp[4];
                    strncpy16to8(tmp, &v, 1);
                    lua_pushstring(L, tmp);
                    break;
                }
                case JavaType::INT: {
                    lua_pushinteger(L, luaObject.type == T_FLOAT ? int32_t(luaObject.number) :
                                       luaObject.type == T_CHAR ?
                                       luaObject.character : int32_t(luaObject.integer));
                    break;
                }
                case JavaType::LONG: {
                    lua_pushinteger(L, luaObject.type == T_FLOAT ? int64_t(luaObject.number) :
                                       luaObject.type == T_CHAR ?
                                       luaObject.character : int64_t(luaObject.integer));
                    break;
                }
                case JavaType::FLOAT: {
                    lua_pushnumber(L, luaObject.type == T_FLOAT ? float(luaObject.number) :
                                      luaObject.type == T_CHAR ?
                                      luaObject.character : float(luaObject.integer));
                    break;
                }
                case JavaType::DOUBLE: {
                    lua_pushnumber(L, luaObject.type == T_FLOAT ? luaObject.number :
                                      luaObject.type == T_CHAR ? luaObject.character
                                                               : luaObject.integer);
                    break;
                }
                default:
                    break;
            }
        } else {
            ERROR("Primitive type can't make a new instance");
        }
    }
    return 1;
}

int javaNewArray(lua_State *L) {
    ThreadContext *context = getContext(L);
    JavaType *type = checkJavaType(L, 1);
    return newArray(L, getThreadEnv(), 2, context, type);
}

static int javaUnBox(lua_State *L) {
    ThreadContext *context = getContext(L);
    int n = lua_gettop(L);
    ValidLuaObject luaObject;
    TJNIEnv *env = getThreadEnv();
    for (int i = 1; i <= n; ++i) {
        unbox(L, env, context, i, luaObject);
        switch (luaObject.type) {
            case T_BOOLEAN:
                lua_pushboolean(L, luaObject.isTrue);
                break;
            case T_INTEGER:
                lua_pushinteger(L, luaObject.integer);
                break;
            case T_FLOAT:
                lua_pushnumber(L, luaObject.number);
                break;
            case T_STRING: {
                jchar c = luaObject.character;
                char s[4];
                strncpy16to8(s, (const char16_t *) &c, 1);
                lua_pushstring(L, s);
                break;
            }
            default:
                lua_pushvalue(L, i);
        }
    }
    return n;
}

static int javaTypeOf(lua_State *L) {
    JavaObject *obj = checkJavaObject(L, 1);
    pushJavaType(L, obj->type);
    return 1;
}

static int javaSuper(lua_State *L) {
    JavaObject *obj = checkJavaObject(L, 1);
    ThreadContext *context = getContext(L);
    ScriptContext *scriptContext = context->scriptContext;
    TJNIEnv *env = getThreadEnv();
    pushJavaObject(L, env, context, obj->object,
                   scriptContext->ensureType(env, env->GetSuperclass(obj->type->getType())));
    return 1;
}

static int javaLog(lua_State *L) {
    int n = lua_gettop(L);
    lua_getglobal(L, "tostring");
    std::string log_message;
    for (int i = 1; i <= n; i++) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char *s = lua_tolstring(L, -1, NULL);
        if (s) {
            if (i > 1) log_message += "\t";
            log_message += s;
        }
        lua_pop(L, 1);
    }
    LOGE("%s", log_message.c_str());
    return 0;
}

static int javaBenchmark(lua_State *L) {
    getThreadEnv();
    return 0;
}

static int javaEmptyMethod(lua_State *L) {
    return 0;
}

static int javaNext(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto *object = static_cast<JavaObject *>(lua_touserdata(L, 1));
    TJNIEnv *env = getThreadEnv();
    static jmethodID hasNext;
    static jmethodID nextEntry;
    if (hasNext == nullptr) {
        JClass type(env->FindClass("com/xyz/luadroid/MapIterator"));
        hasNext = env->GetMethodID(type, "hasNext", "()Z");
        nextEntry = env->GetMethodID(type, "nextEntry", "()[Ljava/lang/Object;");
    }
    jboolean hasMore = env->CallBooleanMethod(object->object, hasNext);
    HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
    if (!hasMore) {
        lua_pushnil(L);
        return 1;
    }
    JObjectArray next(env->CallObjectMethod(object->object, nextEntry));
    HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
    int len = env->GetArrayLength(next);
    if (len == 1) {
        int64_t key = lua_tointeger(L, 2);
        JObject value = env->GetObjectArrayElement(next, 0);
        lua_pushinteger(L, key + 1);
        if (value == nullptr)lua_pushnil(L);
        else pushJavaObject(L, env, context, value);
    } else {
        JObject key = env->GetObjectArrayElement(next, 0);
        JObject value = env->GetObjectArrayElement(next, 1);
        if (key == nullptr)
            lua_pushboolean(L, 0);
        else pushJavaObject(L, env, context, key);
        if (value == nullptr)lua_pushnil(L);
        else pushJavaObject(L, env, context, value);
    }
    return 2;
}

int javaIterate(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    static jmethodID iterate = env->GetMethodID(contextClass, "iterate",
                                                "(Ljava/lang/Object;)Lcom/xyz/luadroid/MapIterator;");
    JavaObject *object = checkJavaObject(L, 1);
    JObject iterator = env->CallObjectMethod(context->scriptContext->javaRef, iterate,
                                             object->object);
    HOLD_JAVA_EXCEPTION(context, {});
    if (iterator == nullptr) {
        ERROR("Bad argument for iterator:%s", luaL_tolstring(L, 1, nullptr));
    }
    lua_pushlightuserdata(L, context);
    lua_pushcclosure(L, javaNext, 1);
    pushJavaObject(L, env, context, iterator);
    lua_pushinteger(L, -1);
    return 3;
}

int javaToJavaObject(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    uint expectedSize = (uint) lua_gettop(L);
    JavaType *_types[expectedSize];
    ValidLuaObject _objects[expectedSize];
    FakeVector<JavaType *> types(_types, expectedSize);
    FakeVector<ValidLuaObject> luaObjects(_objects, expectedSize);
    readArguments(L, env, context, types, luaObjects, 1, expectedSize);
    int len = luaObjects.asVector().size();
    int i;
    for (i = 0; i < len; ++i) {
        if (_objects[i].type == T_NIL) {
            lua_pushnil(L);
            continue;
        }
        JavaType *type = _types[i];
        if (type == nullptr) {
            jobject obj = context->luaObjectToJObject(env, _objects[i]);
            if (likely(obj != INVALID_OBJECT))
                pushJavaObject(L, env, context, JObject(env, obj));
            else {
                throwJavaError(L, context);
            }
            continue;
        } else {
            if (type->isPrimitive()) {
                type = type->toBoxedType();
            }
            jvalue v = context->luaObjectToJValue(env, _objects[i], type);
            if (likely(v.l != INVALID_OBJECT)) {
                pushJavaObject(L, env, context, v.l);
                cleanArg(env, v.l, _objects[i].shouldRelease);
            } else {
                throwJavaError(L, context);
            }
        }
    }
    return i;
}

int javaInstanceOf(lua_State *L) {
    JavaObject *objectRef = checkJavaObject(L, 1);
    JavaType *typeRef = checkJavaType(L, 2);
    lua_pushboolean(L, getThreadEnv()->IsInstanceOf(objectRef->object, typeRef->getType()));
    return 1;
}

static JString getMemberName(TJNIEnv *env, const JObject &member) {
    static jmethodID getName = env->GetMethodID(env->GetObjectClass(member), "getName",
                                                "()Ljava/lang/String;");
    return (JString) env->CallObjectMethod(member, getName);
}

static JString getMethodName(TJNIEnv *env, jclass c, jmethodID id, jboolean isStatic) {
    JObject member = env->ToReflectedMethod(c, id, isStatic);
    return getMemberName(env, member);
}

int callMethod(lua_State *L) {
    MemberInfo *memberInfo = getMemberInfo(L);
    ThreadContext *context = memberInfo->context;
    SetErrorJMP();
    bool isStatic = memberInfo->isStatic;
    JavaObject *objRef = isStatic ? nullptr : memberInfo->object;
    JavaType *type = isStatic ? memberInfo->type : objRef->type;
    int start = 1 + memberInfo->isNotOnlyMethod;
    int top = lua_gettop(L);
    uint expectedSize = uint(top - (memberInfo->isNotOnlyMethod));
    JavaType *_types[expectedSize];
    ValidLuaObject _objects[expectedSize];
    FakeVector<JavaType *> types(_types, expectedSize);
    FakeVector<ValidLuaObject> objects(_objects, expectedSize);
    auto env = getThreadEnv();
    readArguments(L, env, context, types, objects, start, top);
    auto &&array = memberInfo->member->methods;
    bool gotVarMethod;
    auto info = type->deductMethod(env, &array, types, &objects.asVector(), &gotVarMethod);
    if (unlikely(info == nullptr)) {
        TopErrorHandle("No matched found for the method %s;->%s", type->name(env).str(),
                       getMethodName(env, type->getType(), array[0].id, isStatic).str());
    }
    int argCount = info->params.size();
    jvalue args[argCount];
    for (int i = argCount - gotVarMethod; i-- != 0;) {
        ValidLuaObject &object = _objects[i];
        ParameterizedType &tp = info->params[i];
        args[i] = context->luaObjectToJValue(env, object, tp.rawType, tp.realType);
        if (!tp.rawType->isPrimitive() && args[i].l == INVALID_OBJECT) {
            cleanArgs(args, argCount, objects, env);
            pushJavaException(L, context);
            goto __ErrorHandle;
        }
    }
    if (gotVarMethod) {
        uint varCount = types.asVector().size() - argCount + 1;
        FakeVector<ValidLuaObject> varArgs(_objects + argCount - 1, varCount, varCount);
        jarray arr = info->varArgType.rawType->newArray(env, context, varCount, varArgs);
        if (arr == nullptr) {
            cleanArgs(args, argCount - 1, objects, env);
            pushJavaException(L, context);
            goto __ErrorHandle;
        }
        args[argCount - 1].l = arr;
    }
    int retCount = 1;
    auto returnType = info->returnType.rawType;
#define PushResult(jtype, jname, NAME)\
     case JavaType::jtype:{\
        lua_push##NAME(L,isStatic?env->CallStatic##jname##MethodA(type->getType(),info->id\
        ,args):env->CallNonvirtual##jname##MethodA(objRef->object,objRef->type->getType(),info->id,args));\
        break;}
#define PushFloatResult(jtype, jname) PushResult(jtype,jname,number)
#define PushIntegerResult(jtype, jname) PushResult(jtype,jname,integer)

    switch (returnType->getTypeID()) {
        PushResult(BOOLEAN, Boolean, boolean)
        PushIntegerResult(INT, Int)
        PushIntegerResult(LONG, Long)
        PushFloatResult(DOUBLE, Double)
        PushFloatResult(FLOAT, Float)
        PushIntegerResult(BYTE, Byte)
        PushIntegerResult(SHORT, Short)
        case JavaType::CHAR: {
            jchar buf;
            if (isStatic) buf = env->CallStaticCharMethodA(type->getType(), info->id, args);
            else
                buf = env->CallNonvirtualCharMethodA(objRef->object, objRef->type->getType(),
                                                     info->id, args);
            char s[4];
            strncpy16to8(s, (const char16_t *) &buf, 1);
            lua_pushstring(L, s);
            break;
        }
        case JavaType::VOID:
            if (isStatic) env->CallStaticVoidMethodA(type->getType(), info->id, args);
            else
                env->CallNonvirtualVoidMethodA(objRef->object, objRef->type->getType(), info->id,
                                               args);
            retCount = 0;
            break;
        default:
            JObject object = isStatic ? env->CallStaticObjectMethodA(type->getType(), info->id,
                                                                     args) :
                             env->CallNonvirtualObjectMethodA(objRef->object,
                                                              objRef->type->getType(),
                                                              info->id, args);
            if (object == nullptr) lua_pushnil(L); else pushJavaObject(L, env, context, object);
            break;
    }
    HOLD_JAVA_EXCEPTION(context, {
        pushJavaException(L, context);
        goto __ErrorHandle;
    });
    cleanArgs(args, argCount - gotVarMethod, objects, env);
    if (gotVarMethod) env->DeleteLocalRef(args[argCount - 1].l);
    return retCount;
}

static int pushMapValue(lua_State *L, ThreadContext *context, TJNIEnv *env, jobject obj) {
    static jmethodID sGet = env->GetMethodID(contextClass, "at",
                                             "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    ValidLuaObject object;
    parseLuaObject(L, 2, object);
    JObject v(env, context->luaObjectToJObject(env, object));
    auto &&retObj = env->CallObjectMethod(context->scriptContext->javaRef, sGet, obj, v.get());
    HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
    if (retObj == nullptr)
        lua_pushnil(L);
    else pushJavaObject(L, env, context, retObj);
    return 1;
}

static int setMapValue(lua_State *L, ThreadContext *context, TJNIEnv *env, jobject obj) {
    static jmethodID sSet = env->GetMethodID(contextClass, "set",
                                             "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V");
    ValidLuaObject key;
    parseLuaObject(L, 2, key);
    ValidLuaObject value;
    parseLuaObject(L, 3, value);
    JObject k(env, context->luaObjectToJObject(env, key));
    jobject v = context->luaObjectToJObject(env, value);
    if (v == INVALID_OBJECT) {
        forceRelease(key, value, k);
        ERROR("invalid map value");
    }
    env->CallVoidMethod(context->scriptContext->javaRef, sSet, obj, k.get(), JObject(env, v).get());
    HOLD_JAVA_EXCEPTION(context, {
        throwJavaError(L, context);
    });
    return 0;
}

static int isInstanceOfCall(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto *object = (JavaObject *) lua_touserdata(L, lua_upvalueindex(2));
    JavaType *type = checkJavaType(L, 1);
    lua_pushboolean(L, getThreadEnv()->IsInstanceOf(object->object, type->getType()));
    return 1;
}

static int isAssignableFromCall(lua_State *L) {
    ThreadContext *context = getContext(L);
    auto *sub = (JavaType *) lua_touserdata(L, lua_upvalueindex(2));
    JavaType *type = checkJavaType(L, 1);
    lua_pushboolean(L, getThreadEnv()->IsAssignableFrom(sub->getType(), type->getType()));
    return 1;
}

static int newCall(lua_State *L) {
    int len = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, len, 1);//fast call the type;
    return 1;
}

static int newInnerClassCall(lua_State *L) {
    int len = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 2);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushcclosure(L, javaNew, 1);
    lua_insert(L, 1);
    lua_call(L, len + 1, 1);
    return 1;
}

static int callExtendingMethod(lua_State *L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, 1);
    return 1;
}

static inline void saveExistedMember(lua_State *L, bool force) {
    lua_getuservalue(L, 1);
    if (lua_isnil(L, -1)) {
        if (!force)
            return;
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setuservalue(L, 1);

    }
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}


static inline uintptr_t tryExistedMember(lua_State *L, JavaType *type, bool isStatic) {
    lua_getuservalue(L, 1);

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        goto Handle_Object;
    }

    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    switch (lua_type(L, -1)) {
        case LUA_TNIL:
            lua_pop(L, 2);//nil and table
            goto Handle_Object;
        case LUA_TLIGHTUSERDATA:
            return (uintptr_t) lua_touserdata(L, -1);
        default:
            return 1;

    }
    Handle_Object:
    if (isStatic)
        return 0;
    //resolving user added member
    lua_rawgetp(L, LUA_REGISTRYINDEX, reinterpret_cast<char *>(type) + 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);//nil
        return 0;
    }
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);//nil and table
        return 0;
    }
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, callExtendingMethod, 2);
    lua_remove(L, -2);
    saveExistedMember(L, true);
    return 1;
}

static inline int pushMockMember(lua_State *L, ThreadContext *context, const Member *getter) {
    pushMember(context, L, getter, 1, false, 0, true);
    lua_call(L, 0, 1);
    return 1;
}

int getClassMember(lua_State *L) {
    bool isStatic = isJavaTypeOrObject(L, 1);
    JavaObject *obj = isStatic ? nullptr : (JavaObject *) lua_touserdata(L, 1);
    JavaType *type = isStatic ? *(JavaType **) lua_touserdata(L, 1) : obj->type;
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    if (!isStatic) {
        auto component = obj->type->getComponentType(env);
        if (component != nullptr) {
            if (luaL_isstring(L, 2)) {
                const char *s = lua_tostring(L, 2);
                if (unlikely(strcmp(s, "length") != 0)) {
                    goto MemberStart;
                } else {
                    lua_pushinteger(L, env->GetArrayLength((jarray) obj->object));
                }
            } else {
                pushArrayElement(L, env, context, obj, component);
            }
            return 1;
        }
    }
    if (unlikely(!luaL_isstring(L, 2))) {
        if (!isStatic) {
            return pushMapValue(L, context, env, obj->object);
        }
        ERROR("Invaild type to get a field or method:%s", luaL_typename(L, 2));
    }
    MemberStart:
    {
        uintptr_t existed = tryExistedMember(L, type, isStatic);
        if (existed) {
            if (existed != 1) {
                pushMockMember(L, context, reinterpret_cast<const Member *>(existed));
            } else return 1;
        }
    };

    FakeString name(L, 2);
    auto member = type->ensureMember(env, (const String &) name, isStatic);
    FieldArray *fieldArr = nullptr;
    bool isMethod = false;
    int fieldCount = 0;
    if (member) {
        fieldArr = &member->fields;
        isMethod = member->methods.size() != 0;
        fieldCount = fieldArr->size();
    }
    if (fieldCount == 1 && !isMethod) {
        auto &&info = fieldArr->begin();
        JavaType *fieldType = info->type.rawType;
#define GetField(typeID, jtype, jname, TYPE)\
        case JavaType::typeID:{\
            lua_push##TYPE(L,isStatic?env->GetStatic##jname##Field(type->getType()\
                    ,info->id):env->Get##jname##Field(obj->object,info->id));\
            break;\
        }
#define GetIntegerField(typeID, jtype, jname) GetField(typeID,jtype,jname,integer)
#define GetFloatField(typeID, jtype, jname) GetField(typeID,jtype,jname,number)
#define GetInteger64Field() GetIntegerField(LONG,long,Long)
#define PushField()\
        switch(fieldType->getTypeID()){\
            GetIntegerField(INT,int,Int)\
            GetIntegerField(BYTE,byte,Byte)\
            GetInteger64Field()\
            GetIntegerField(SHORT,short,Short)\
            GetFloatField(FLOAT,float,Float)\
            GetFloatField(DOUBLE,double,Double)\
            GetField(BOOLEAN,boolean,Boolean,boolean)\
            case JavaType::CHAR:{\
            jchar c=isStatic?env->GetStaticCharField(type->getType(),info->id):env->GetCharField(obj->object,info->id);\
            PushChar(c);\
            }\
            default:{\
            JObject object=isStatic?env->GetStaticObjectField(type->getType(),info->id):env->GetObjectField(obj->object,info->id);\
            if(object==nullptr) lua_pushnil(L);else pushJavaObject(L,env,context,object);break;\
            }}
        PushField();
    } else {
        bool force = true;
        if (!isMethod && unlikely(fieldCount == 0)) {
            if (strcmp(name, "class") == 0) {
                pushJavaObject(L, env, context, type->getType());
                goto SAVE_AND_EXIT;
            } else if (!isStatic) {
                if (strcmp(name, "instanceof") == 0) {
                    lua_pushlightuserdata(L, context);
                    lua_pushvalue(L, 1);
                    lua_pushcclosure(L, isInstanceOfCall, 2);
                    force = false;
                    goto SAVE_AND_EXIT;
                } else if (strcmp(name, "super") == 0) {
                    pushJavaObject(L, env, context, obj->object,
                                   context->scriptContext->ensureType(env, env->GetSuperclass(
                                           type->getType())));
                    goto SAVE_AND_EXIT;
                } else if (strcmp(name, "new") == 0) {
                    lua_pushlightuserdata(L, context);
                    lua_pushlightuserdata(L, obj);
                    lua_pushcclosure(L, newInnerClassCall, 2);
                    goto SAVE_AND_EXIT;
                }

            } else {
                if (strcmp(name, "new") == 0) {
                    lua_pushvalue(L, 1);
                    lua_pushcclosure(L, newCall, 1);
                    goto SAVE_AND_EXIT;
                } else if (strcmp(name, "assignableFrom") == 0) {
                    lua_pushlightuserdata(L, context);
                    lua_pushlightuserdata(L, type);
                    lua_pushcclosure(L, isAssignableFromCall, 2);
                    goto SAVE_AND_EXIT;
                }
            }
            JavaType *innerClass = type->ensureInnerClass(env, context, name);
            if (innerClass) {
                pushJavaType(L, innerClass);
                goto SAVE_AND_EXIT;
            }
            if (isStatic)
                ERROR("No static member is named %s in class %s", name.data(),
                      type->name(env).str());
            else {
                auto getter = type->findMockMember(env, name, true);
                if (getter) {
                    lua_pushlightuserdata(L, (void *) getter);
                    saveExistedMember(L, force);
                    lua_pop(L, 1);
                    return pushMockMember(L, context, getter);
                } else
                    return pushMapValue(L, context, env, obj->object);
            }
            return 0;
        }
        pushMember(context, L, member, 1, isStatic, fieldCount, isMethod);
        SAVE_AND_EXIT:
        saveExistedMember(L, force);
    }
    return 1;
}

int getObjectLength(lua_State *L) {
    auto *objRef = (JavaObject *) (lua_touserdata(L, 1));
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    if (objRef->type->isArray(env))
        lua_pushinteger(L, env->GetArrayLength((jarray) objRef->object));
    else {
        static jmethodID sLength = env->GetMethodID(contextClass, "length",
                                                    "(Ljava/lang/Object;)I");
        int len = env->CallIntMethod(context->scriptContext->javaRef, sLength, objRef->object);
        HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
        lua_pushinteger(L, len);
    }
    return 1;
}

int getField(lua_State *L) {
    MemberInfo *memberInfo = getMemberInfo(L);
    SetErrorJMP();
#ifndef NDEBUG
    if (unlikely(!memberInfo->isField)) {
        LOGE("Not a field");
    }
#endif
    bool isStatic = memberInfo->isStatic;
    JavaObject *obj = isStatic ? nullptr : memberInfo->object;
    JavaType *type = isStatic ? memberInfo->type : obj->type;

    auto env = getThreadEnv();
    JavaType **fieldTypeRef;
    if (unlikely(memberInfo->isDuplicatedField) &&
        (fieldTypeRef = (JavaType **) testUData(L, 2, TYPE_KEY)) ==
        nullptr) {
        TopErrorHandle("The class has duplicated field named %s while no type specified",
                       getMemberName(env, env->
                               ToReflectedField(type->getType(), memberInfo->member->fields[0].id,
                                                isStatic)).str());
    }
    auto &&info = type->deductField(&memberInfo->member->fields,
                                    memberInfo->isDuplicatedField ? *fieldTypeRef : nullptr);
    if (info == nullptr) {
        TopErrorHandle("The class doesn't have a field name %s with type %s",
                       getMemberName(env, env->
                               ToReflectedField(type->getType(), memberInfo->member->fields[0].id,
                                                isStatic)).str(), (*fieldTypeRef)->name(env).str());
    }
    JavaType *fieldType = info->type.rawType;
    auto context = memberInfo->context;
    PushField();
    return 1;
}

int setField(lua_State *L) {
    MemberInfo *memberInfo = getMemberInfo(L);
    ThreadContext *context = memberInfo->context;
    SetErrorJMP();
#ifndef NDEBUG
    if (unlikely(!memberInfo->isField)) {
        LOGE("Not a field");
    }
#endif
    bool isStatic = memberInfo->isStatic;
    JavaObject *objRef = isStatic ? nullptr : memberInfo->object;
    JavaType *type = isStatic ? memberInfo->type : objRef->type;

    auto env = getThreadEnv();
    JavaType **fieldTypeRef;
    if (unlikely(memberInfo->isDuplicatedField) && (fieldTypeRef = (JavaType **)
            testUData(L, 2, TYPE_KEY)) == nullptr) {
        TopErrorHandle("The class has duplicated field named %s while no type specified",
                       getMemberName(env, env->
                               ToReflectedField(type->getType(), memberInfo->member->fields[0].id,
                                                isStatic)).str());
    }
    auto info = type->deductField(&memberInfo->member->fields,
                                  memberInfo->isDuplicatedField ? *fieldTypeRef : nullptr);
    if (info == nullptr) {
        TopErrorHandle("The class doesn't have a field name %s with type %s",
                       getMemberName(env, env->
                               ToReflectedField(type->getType(), memberInfo->member->fields[0].id,
                                                isStatic)).str(), (*fieldTypeRef)->name(env).str());
    }
    JavaType *fieldType = info->type.rawType;
    ValidLuaObject luaObject;
    if (unlikely(!parseLuaObject(L, 3, luaObject))) {
        ERROR("Invalid value passed to java as a field with type:%s", luaL_typename(L, 3));
    }
    checkLuaType(env, L, fieldType, luaObject);
#define RawSetField(jname, NAME)({\
        if(isStatic) env->SetStatic##jname##Field(type->getType(),info->id,NAME);\
        else env->Set##jname##Field(objRef->object,info->id,NAME);})
#define SetField(typeID, jtype, jname, NAME)\
    case JavaType::typeID:{\
        RawSetField(jname,(luaObject.NAME));\
        break;\
    }
#define SetIntegerField(typeID, jtype, jname) SetField(typeID,jtype,jname,integer)
#define SetFloatField(typeID, jtype, jname) SetField(typeID,jtype,jname,number)
#define SET_FIELD()\
    switch(fieldType->getTypeID()){\
        SetIntegerField(INT,int,Int)\
        SetField(BOOLEAN,boolean,Boolean,isTrue)\
        SetIntegerField(LONG,long,Long)\
        SetFloatField(FLOAT,float,Float)\
        SetFloatField(DOUBLE,double,Double)\
        SetIntegerField(BYTE,byte,Byte)\
        SetIntegerField(SHORT,short,Short)\
        case JavaType::CHAR:{\
            char16_t  s;\
            strcpy8to16(&s,luaObject.string, nullptr);\
            RawSetField(Char,s);\
        }\
        default:{\
            jvalue v= context->luaObjectToJValue(env,luaObject,fieldType,info->type.realType);\
            RawSetField(Object,v.l);\
            if(luaObject.type==T_FUNCTION||luaObject.type==T_STRING) env->DeleteLocalRef(v.l);\
        }}/*\
        HOLD_JAVA_EXCEPTION(context,{throwJavaError(L,context);});// jni doesn't seem to throw on mismatched type*/

    SET_FIELD()
    return 0;
}

int setFieldOrArray(lua_State *L) {
    bool isStatic = isJavaTypeOrObject(L, 1);
    JavaObject *objRef = isStatic ? nullptr : (JavaObject *) lua_touserdata(L, 1);
    JavaType *type = isStatic ? *(JavaType **) lua_touserdata(L, 1) : objRef->type;
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    if (type->isPrimitive())
        ERROR("Primitive type is not allowed to be set");
    if (!isStatic) {
        JavaType *component = objRef->type->getComponentType(env);
        if (component != nullptr) {
            type = component;
            int isnum;
            jlong index = lua_tointegerx(L, 2, &isnum);
            if (unlikely(!isnum))
                ERROR("Invalid Value to set a array:%s", luaL_tolstring(L, 2, nullptr));
            if (index < 0 || index > INT32_MAX) ERROR("Index out of range:%lld", index);
            ValidLuaObject luaObject;
            parseLuaObject(L, 3, luaObject);
            checkLuaType(env, L, type, luaObject);

#define RAW_SET_ARR(jtype, jname, Ref) env->Set##jname##ArrayRegion((j##jtype##Array) objRef->object, (jsize) index, 1,Ref)
#define SET_ARR(typeID, jtype, jname, NAME) \
            case JavaType::typeID:{ j##jtype* ref=(j##jtype*) &luaObject.NAME;RAW_SET_ARR(jtype,jname,ref);break;}
#define SET_INTEGER_ARR(typeID, jtype, jname) SET_ARR(typeID,jtype,jname,integer)
#define SET_FLOAT_ARR(typeID, jtype, jname) SET_ARR(typeID,jtype,jname,number)
            switch (type->getTypeID()) {
                SET_INTEGER_ARR(BYTE, byte, Byte)
                SET_INTEGER_ARR(SHORT, short, Short)
                SET_INTEGER_ARR(INT, int, Int)
                SET_INTEGER_ARR(LONG, long, Long)
                SET_FLOAT_ARR(FLOAT, float, Float)
                SET_FLOAT_ARR(DOUBLE, double, Double)
                SET_ARR(BOOLEAN, boolean, Boolean, isTrue)
                case JavaType::CHAR: {
                    char16_t s[strlen8to16(luaObject.string)];
                    strcpy8to16(s, luaObject.string, nullptr);
                    RAW_SET_ARR(char, Char, (jchar *) s);
                }
                default: {
                    jobject v = context->luaObjectToJValue(env, luaObject, type).l;
                    if (unlikely(v == INVALID_OBJECT)) lua_error(L);
                    env->SetObjectArrayElement((jobjectArray) objRef->object, (jsize) index, v);
                    cleanArg(env, v, luaObject.shouldRelease);
                }
            }
            HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
            return 0;
        }
    }
    if (unlikely(!luaL_isstring(L, 2))) {
        if (!isStatic && (lua_isnumber(L, 2) || testType(L, 2, OBJECT_KEY))) {
            setMapValue(L, context, env, objRef->object);
            return 0;
        }
        ERROR("Invalid index for a field member:%s",
              luaL_tolstring(L, 2, NULL));
    }

    const FakeString name(L, 2);
    auto arr = type->ensureField(env, name, isStatic);
    if (arr == nullptr) {
        if (!isStatic) {
            auto setter = type->findMockMember(env, name, false);
            if (setter) {
                pushMember(context, L, setter, 1, false, 0, true);
                lua_pushvalue(L, 3);
                lua_call(L, 1, 0);
                return 0;
            }
            setMapValue(L, context, env, objRef->object);
            return 0;
        } else if (lua_isfunction(L, 3) && type->ensureMethod(env, name, false) == nullptr) {
            char *key = reinterpret_cast<char *>(type) + 1;
            lua_rawgetp(L, LUA_REGISTRYINDEX, key);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_rawsetp(L, LUA_REGISTRYINDEX, key);
            }
            lua_replace(L, 1);
            lua_rawset(L, 1);
            return 0;
        }
        ERROR("No such field");
    }
    if (arr->size() > 1) ERROR("The name %s represents not only one field", name.data());
    auto &&info = arr->begin();
    JavaType *fieldType = info->type.rawType;
    ValidLuaObject luaObject;
    if (unlikely(!parseLuaObject(L, 3, luaObject))) {
        ERROR("Invalid value passed to java as a field with type:%s",
              luaL_tolstring(L, 3, NULL));
    }
    checkLuaType(env, L, fieldType, luaObject);

    SET_FIELD();
    return 0;


}

int objectEquals(lua_State *L) {
    auto *ob1 = (JavaObject *) testUData(L, 1, OBJECT_KEY);
    auto *ob2 = (JavaObject *) testUData(L, 2, OBJECT_KEY);
    if (ob1 == nullptr || ob2 == nullptr) {
        lua_pushboolean(L, false);
    } else lua_pushboolean(L, getThreadEnv()->IsSameObject(ob1->object, ob2->object));
    return 1;
}

int concatString(lua_State *L) {
    size_t len1 = 0;
    size_t len2 = 0;
    const char *s1 = luaL_tolstring(L, 1, &len1);
    const char *s2 = luaL_tolstring(L, 2, &len2);
    lua_pushfstring(L, "%s%s", s1, s2);
    return 1;
}

int javaTypeToString(lua_State *L) {
    JavaType *type = *(JavaType **) lua_touserdata(L, 1);
    lua_pushfstring(L, "Java Type:%s", type->name(getThreadEnv()).str());
    return 1;
}

int javaObjectToString(lua_State *L) {
    auto *ob = (JavaObject *) lua_touserdata(L, 1);
    ThreadContext *context = getContext(L);
    auto env = getThreadEnv();
    JString str = env->CallObjectMethod(ob->object, objectToString);
    HOLD_JAVA_EXCEPTION(context, { throwJavaError(L, context); });
    lua_pushlstring(L, str, strlen(str.str()));
    return 1;
}

static int callInitializer(lua_State *L) {
    if (lua_gettop(L) != 2) return 0;
    if (!lua_istable(L, 2)) return 0;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_settable(L, 1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 1;
}

jobject LazyTable::asInterface(TJNIEnv *env, ThreadContext *context, JavaType *main) {
    Vector<JavaType *> interfaces;
    Vector<std::unique_ptr<BaseFunction>> luaFuncs;
    Vector<JObject> agentMethods;
    if (!readProxyMethods(L, env, context, interfaces, main, luaFuncs, agentMethods))
        return INVALID_OBJECT;
    return context->proxy(env, main, nullptr, agentMethods, luaFuncs);
}

LuaTable<ValidLuaObject> *LazyTable::getTable() {
    if (table != nullptr)
        return table;
    lua_pushvalue(L, index);
    lua_rawget(L, LUA_REGISTRYINDEX);
    LuaTable<ValidLuaObject> *luaTable;
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        luaTable = new LuaTable<ValidLuaObject>();
        lua_pushvalue(L, index);
        lua_pushlightuserdata(L, luaTable);
        lua_rawset(L, LUA_REGISTRYINDEX);
        lua_pushnil(L);
        while (lua_next(L, index)) {
            ValidLuaObject key;
            ValidLuaObject value;
            bool ok = parseLuaObject(L, -2, key) &&
                      parseLuaObject(L, -1, value);
            lua_pop(L, 1);
            if (ok)
                luaTable->get().push_back({std::move(key), std::move(value)});
            else {
                lua_pushvalue(L, index);
                lua_pushnil(L);
                lua_rawset(L, LUA_REGISTRYINDEX);
                delete luaTable;
                return nullptr;
            }
        }
        lua_pushvalue(L, index);
        lua_pushnil(L);
        lua_rawset(L, LUA_REGISTRYINDEX);
    } else {
        luaTable = static_cast<LuaTable<ValidLuaObject> *>(lua_touserdata(L, -1));
        lua_pop(L, -1);
    }
    table = luaTable;
    return luaTable;
}

static void registerNativeMethods(JNIEnv *env) {
    jclass scriptClass = env->FindClass("com/xyz/luadroid/ScriptContext");
    env->RegisterNatives(scriptClass, nativeMethods,
                         sizeof(nativeMethods) / sizeof(JNINativeMethod));

    env->DeleteLocalRef(scriptClass);
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    ::vm = vm;
    JNIEnv *env;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_4) != JNI_OK) {
        return -1;
    }
    registerNativeMethods(env);
    return JNI_VERSION_1_4;
}

jlong nativeOpen(TJNIEnv *env, jobject object) {
    auto *context = new ScriptContext(env, object);
    return reinterpret_cast<long>(context);
}

void nativeClose(JNIEnv *, jclass, jlong ptr) {
    auto *context = (ScriptContext *) ptr;
    delete context;
}

/**
 * 管理 LocalFunctionInfo 的引用计数和生命周期
 * 
 * 当 deRefer=true 且引用计数为 0 时：
 * 1. 从注册表移除 userdata
 * 2. 允许 Lua GC 回收 userdata 和 thread
 * 3. 删除 LocalFunctionInfo 对象
 * 
 * @param env JNI 环境
 * @param thisClass Java 类
 * @param ptr LocalFunctionInfo 指针
 * @param deRefer true 表示减少引用，false 表示增加引用
 */
void referFunc(JNIEnv *, jclass, jlong ptr, jboolean deRefer) {
    LOGE("referFunc");
    auto *info = (BaseFunction *) ptr;
    if (!deRefer) {
        // 增加引用计数
        info->javaRefCount++;
    } else if (--info->javaRefCount == 0) {
        // 引用计数为 0，需要清理资源

        // 对于 LocalFunctionInfo，需要从注册表移除 userdata
        // 这样 Lua GC 就可以回收 userdata 和关联的 thread
        auto *localInfo = dynamic_cast<LocalFunctionInfo *>(info);
        if (localInfo != nullptr) {
            // 获取 Lua 状态机（从 thread 的父状态机）
            lua_State *thread = localInfo->getThread();
            if (thread != nullptr) {
                // 获取主状态机（thread 的父状态机）
                lua_State *L = G(thread)->mainthread;

                // 从注册表移除 userdata（使用 LocalFunctionInfo 指针作为键）
                // 将键设置为 nil，这样 userdata 就不再被注册表引用
                lua_pushnil(L);
                lua_rawsetp(L, LUA_REGISTRYINDEX, info);

                // 现在 userdata 不再被注册表引用，Lua GC 可以回收它
                // 当 userdata 被回收时，它的 uservalue（thread）也会被回收
            }
        }

        // 删除 C++ 对象
        delete info;
    }
}

jboolean sameSigMethod(JNIEnv *env, jclass, jobject f, jobject s, jobject caller) {
    static jmethodID mid = env->FromReflectedMethod(caller);
    return env->CallBooleanMethod(f, mid, s);
}

jint getClassType(TJNIEnv *env, jclass, jlong ptr, jclass clz) {
    auto *context = (ScriptContext *) ptr;
    return context->ensureType(env, clz)->getTypeID();
}

jobject
invokeSuper(TJNIEnv *env, jclass, jobject thiz, jobject method, jint id, jobjectArray args) {
    jmethodID mid = env->FromReflectedMethod(method);
    JClass superclass(env->GetSuperclass(env->GetObjectClass(thiz)));
    switch (id) {
        case 1://toString
            return env->asJNIEnv()->CallNonvirtualObjectMethod(thiz, superclass, mid);
        case 2://hashCode
        {
            jintArray ret = env->asJNIEnv()->NewIntArray(1);
            int retVal = env->CallNonvirtualIntMethod(thiz, superclass, mid);
            env->SetIntArrayRegion(ret, 0, 1, &retVal);
            return ret;
        }
        case 3://equals
        {
            jbooleanArray ret = env->asJNIEnv()->NewBooleanArray(1);
            jboolean retVal = env->CallNonvirtualBooleanMethod(thiz, superclass, mid,
                                                               env->GetObjectArrayElement(args,
                                                                                          0).get());
            env->SetBooleanArrayRegion(ret, 0, 1, &retVal);
            return ret;
        }
        default:
            return nullptr;
    }
}

jobjectArray runScript(TJNIEnv *env, jclass, jlong ptr, jobject script, jboolean isFile,
                       jobjectArray args) {
    auto *scriptContext = (ScriptContext *) ptr;
    ThreadContext *context = scriptContext->threadContext;
    Import *oldImport = nullptr;
    if (_setjmp(errorJmp)) {
        context->restore(oldImport);
        return nullptr;
    }
    Import myIMport;
    oldImport = context->changeImport(&myIMport);
    auto L = scriptContext->state;
    auto top = lua_gettop(L);
    int argCount;
    jobjectArray result = nullptr;
    pushErrorHandler(L, context);
    int handlerIndex = lua_gettop(L);
    JString s(env, static_cast<jstring>(script));
    int ret = isFile ? luaL_loadfile(L, s) : luaL_loadstring(L, s);
    s.invalidate();
    if (unlikely(ret != LUA_OK)) {
        context->setPendingException("Failed to load");
        recordLuaError(context, L, ret);
        goto over;
    }
    argCount = args ? env->GetArrayLength(args) : 0;
    for (int i = 0; i < argCount; ++i) {
        pushJavaObject(L, env, context, env->GetObjectArrayElement(args, i));
    }

    ret = lua_pcall(L, argCount, LUA_MULTRET, handlerIndex);
    if (unlikely(ret != LUA_OK)) {
        recordLuaError(context, L, ret);
        goto over;
    }
    {
        int resultSize = lua_gettop(L) - handlerIndex;
        if (resultSize > 0) {
            result = env->asJNIEnv()->NewObjectArray(resultSize,
                                                     scriptContext->ObjectClass->getType(),
                                                     nullptr);
            for (int i = resultSize; i--;) {
                ValidLuaObject object;
                parseLuaObject(L, handlerIndex + i + 1, object);
                jobject value = context->luaObjectToJObject(env, object);
                if (value != INVALID_OBJECT)
                    env->SetObjectArrayElement(result, i, JObject(env, value).get());
            }
        }
    }
    over:
    lua_settop(L, top);
    context->restore(oldImport);
    return result;
}

/*
 * 从Java调用Lua函数->主动调用->Java层的回调方法
 */
jobject
invokeLuaFunction(TJNIEnv *env, jclass clazz, jlong ptr, jlong funcRef, jboolean multiRet,
                  jobject proxy,
                  jstring methodName,
                  jintArray argTypes, jobjectArray args) {
    auto *scriptContext = (ScriptContext *) ptr;
    ThreadContext *context = scriptContext->threadContext;
    Import *oldImport = nullptr;
    if (_setjmp(errorJmp)) {
        context->restore(oldImport);
        return nullptr;
    }
    auto L = scriptContext->state;
    oldImport = context->getImport();

    // 1. 获取函数信息（锁定会在后面进行）
    auto *info = reinterpret_cast<LocalFunctionInfo *>(funcRef);
    

    // 2. 直接从 LocalFunctionInfo 获取 thread
    lua_State *thread = info->getThread();

    // 3. 验证 thread 有效性
    if (unlikely(thread == nullptr)) {
        context->setPendingException("Invalid thread in callback: thread object is corrupted");
        context->restore(oldImport);
        context->throwToJava();
        return nullptr;
    }

    // 4. 锁定互斥锁，保护多线程并发访问（必须在任何 Lua 栈操作之前）
    info->lock();

    // 5. 保存 thread 栈状态（用于清理）
    int threadTop = lua_gettop(thread);

    // 5. 在 thread 栈上复制函数到栈顶
    // thread 栈底应该已经有函数了，我们复制它到栈顶准备调用
    lua_pushvalue(thread, 1);  // 复制栈底的函数到栈顶

    // 6. 转换并压入 Java 参数到 thread 栈
    pushJavaObject(thread, env, context, proxy);
    JString name(env, methodName);
    lua_pushstring(thread, name.str());
    name.invalidate();

    int len = env->GetArrayLength(argTypes);
    jint *arr = env->GetIntArrayElements(argTypes, nullptr);
    for (int i = 0; i < len; ++i) {
        switch (arr[i]) {
            case 0: {//char
                JObject character = env->GetObjectArrayElement(args, i);
                char16_t c = env->CallCharMethod(character, charValue);
                char charStr[4];
                strncpy16to8(charStr, &c, 1);
                lua_pushstring(thread, charStr);
                break;
            }
            case 1: {//boolean
                JObject boolean = env->GetObjectArrayElement(args, i);
                jboolean b = env->CallBooleanMethod(boolean, booleanValue);
                lua_pushboolean(thread, b);
                break;
            }
            case 2: {//integer
                JObject integer = env->GetObjectArrayElement(args, i);
                jlong intValue = env->CallLongMethod(integer, longValue);
                lua_pushinteger(thread, intValue);
                break;
            }
            case 3: {//double
                JObject box = env->GetObjectArrayElement(args, i);
                jdouble floatValue = env->CallDoubleMethod(box, doubleValue);
                lua_pushnumber(thread, floatValue);
                break;
            }
            default: {//object
                JObject obj = env->GetObjectArrayElement(args, i);
                if (obj == nullptr) {
                    lua_pushnil(thread);
                } else pushJavaObject(thread, env, context, obj);
                break;
            }
        }
    }
    env->ReleaseIntArrayElements(argTypes, arr, JNI_ABORT);
    len += 2;  // proxy + methodName + args

    // 7. 使用 lua_pcall 调用函数（带错误处理）
    int err = lua_pcall(thread, len, LUA_MULTRET, 0);

    jobject ret = nullptr;
    int retCount;

    // 8. 处理返回值或错误
    if (err != LUA_OK) {
        // 生成详细的错误堆栈跟踪
        const char *errMsg = lua_tostring(thread, -1);
        luaL_traceback(thread, thread, errMsg, 1);
        const char *fullMsg = lua_tostring(thread, -1);

        // 清理 thread 栈，保持函数在栈底
        lua_settop(thread, threadTop);

        // 解锁互斥锁
        info->unlock();

        context->setPendingException(fullMsg);
        context->restore(oldImport);
        context->throwToJava();
        return nullptr;
    } else if ((retCount = lua_gettop(thread) - threadTop) != 0) {
        if (multiRet) {
            JObjectArray result(
                    env->NewObjectArray(retCount, scriptContext->ObjectClass->getType(), nullptr));
            for (int i = threadTop + 1, top = lua_gettop(thread), j = 0; i <= top; ++i, ++j) {
                ValidLuaObject object;
                parseLuaObject(thread, i, object);
                jobject value = context->luaObjectToJObject(env, object);
                if (value != INVALID_OBJECT)
                    env->SetObjectArrayElement(result, j, JObject(env, value));
            }
            ret = result.invalidate();
        } else {
            ValidLuaObject object;
            parseLuaObject(thread, threadTop + 1, object);//only return the first result
            ret = context->luaObjectToJObject(env, object);
            if (ret == INVALID_OBJECT) ret = nullptr;
        }
    }

    // 9. 清理 thread 栈，保持函数在栈底
    lua_settop(thread, threadTop);

    // 10. 解锁互斥锁
    info->unlock();

    context->restore(oldImport);
    return ret;
}
