
#include "luadroid.h"
#include "jtype.h"
#include "java_type.h"
#include "utf8.h"
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <assert.h>

static jmethodID sProxy;
jmethodID charValue;
jmethodID booleanValue;
jmethodID longValue;
jmethodID doubleValue;

#define BOX_INIT(Type) Type##Class(ensureType(env,env->FindClass("java/lang/"#Type)))

ScriptContext::ScriptContext(TJNIEnv *env, jobject javaObject) :
        state(luaL_newstate()),
        byteClass((init(env, javaObject), ensureType(env, JavaType::getComponentType(env,
                                                                                     env->FindClass(
                                                                                             "[B"))))),
        shortClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[S")))),
        intClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[I")))),
        longClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[J")))),
        booleanClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[Z")))),
        charClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[C")))),
        floatClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[F")))),
        doubleClass(ensureType(env, JavaType::getComponentType(env, env->FindClass("[D")))),
        voidClass(getVoidClass(env)),
        ObjectClass(ensureType(env, env->GetSuperclass(stringType))),
        javaRef(env->NewWeakGlobalRef(javaObject)),
        BOX_INIT(Byte),
        BOX_INIT(Character),
        BOX_INIT(Boolean),
        BOX_INIT(Integer),
        BOX_INIT(Long),
        BOX_INIT(Short),
        BOX_INIT(Float),
        BOX_INIT(Double),
        threadContext(new ThreadContext()) {

    // 初始化主状态机互斥锁
    pthread_mutex_init(&stateMutex, nullptr);

    JavaType *StringClass = ensureType(env, stringType);
    StringClass->_isString = true;
    booleanClass->primitive = true;
    charClass->primitive = true;
    byteClass->_isInteger = true;
    byteClass->primitive = true;
    shortClass->_isInteger = true;
    shortClass->primitive = true;
    intClass->primitive = true;
    intClass->_isInteger = true;
    longClass->primitive = true;
    longClass->_isInteger = true;
    floatClass->primitive = true;
    doubleClass->primitive = true;
    floatClass->_isFloat = true;
    doubleClass->_isFloat = true;
    voidClass->primitive = true;
    ByteClass->_isBox = true;
    ShortClass->_isBox = true;
    IntegerClass->_isBox = true;
    LongClass->_isBox = true;
    FloatClass->_isBox = true;
    DoubleClass->_isBox = true;
    CharacterClass->_isBox = true;
    BooleanClass->_isBox = true;
    ByteClass->_isInteger = true;
    ShortClass->_isInteger = true;
    IntegerClass->_isInteger = true;
    LongClass->_isInteger = true;
    FloatClass->_isFloat = true;
    DoubleClass->_isFloat = true;
    byteClass->typeID = JavaType::BYTE;
    shortClass->typeID = JavaType::SHORT;
    intClass->typeID = JavaType::INT;
    longClass->typeID = JavaType::LONG;
    floatClass->typeID = JavaType::FLOAT;
    doubleClass->typeID = JavaType::DOUBLE;
    charClass->typeID = JavaType::CHAR;
    booleanClass->typeID = JavaType::BOOLEAN;
    voidClass->typeID = JavaType::VOID;
    ByteClass->typeID = JavaType::BOX_BYTE;
    ShortClass->typeID = JavaType::BOX_SHORT;
    IntegerClass->typeID = JavaType::BOX_INT;
    LongClass->typeID = JavaType::BOX_LONG;
    FloatClass->typeID = JavaType::BOX_FLOAT;
    DoubleClass->typeID = JavaType::BOX_DOUBLE;
    CharacterClass->typeID = JavaType::BOX_CHAR;
    BooleanClass->typeID = JavaType::BOX_BOOLEAN;
    ObjectClass->typeID = JavaType::BOX_OBJECT;
    ensureType(env, env->FindClass("java/lang/Number"))->typeID = JavaType::BOX_NUMBER;

    threadContext->scriptContext = this;
    config(state);
}


void ScriptContext::init(TJNIEnv *env, const jobject javaObject) {
    if (stringType == nullptr) {
        JClass cStr = env->FindClass("java/lang/String");
        stringType = (jclass) env->NewGlobalRef(cStr);
    }
    if (throwableType == nullptr) {
        JClass cThr = env->FindClass("java/lang/Throwable");
        throwableType = (jclass) env->NewGlobalRef(cThr);
    }
    if (classType == nullptr) {
        JClass cClass = env->GetObjectClass(stringType);
        classType = (jclass) env->NewGlobalRef(cClass);
        classGetName = env->GetMethodID(classType, "getName", "()Ljava/lang/String;");
        JavaType::sGetComponentType = env->GetMethodID(classType, "getComponentType",
                                                       "()Ljava/lang/Class;");
        JavaType::sIsInterface = env->GetMethodID(classType, "isInterface", "()Z");
    }
    if (sProxy == nullptr) {
        JClass cl = env->GetObjectClass(javaObject);
        contextClass = (jclass) env->NewGlobalRef(cl);
        sProxy = env->GetMethodID(cl, "proxy", "(Ljava/lang/Class;[Ljava/lang/Class;"
                                               "[Ljava/lang/reflect/Method;[JJZJLjava/lang/Object;)Ljava/lang/Object;");
        JavaType::sFindMembers = env->GetStaticMethodID(cl, "findMembers",
                                                        "(Ljava/lang/Class;Ljava/lang/String;ZZ)[Ljava/lang/Object;");

        JavaType::sFindMockName = env->GetStaticMethodID(cl, "findMockName",
                                                         "(Ljava/lang/Class;Ljava/lang/String;)[Ljava/lang/String;");

        JavaType::sGetSingleInterface = env->GetStaticMethodID(cl, "getSingleInterface",
                                                               "(Ljava/lang/Class;)Ljava/lang/reflect/Method;");
        JavaType::sIsTableType = env->GetMethodID(cl, "isTableType", "(Ljava/lang/Class;)Z");
        JavaType::sTableConvert = env->GetMethodID(cl, "convertTable", "(Lcom/xyz/luadroid/DataMap;"
                                                                       "Ljava/lang/Class;Ljava/lang/reflect/Type;)Ljava/lang/Object;");
    }
    if (charValue == nullptr) {
        JClass cChar = env->FindClass("java/lang/Character");
        charValue = env->GetMethodID(cChar, "charValue", "()C");

        JClass cBoolean = env->FindClass("java/lang/Boolean");
        booleanValue = env->GetMethodID(cBoolean, "booleanValue", "()Z");

        JClass cNumber = env->FindClass("java/lang/Number");
        longValue = env->GetMethodID(cNumber, "longValue", "()J");
        doubleValue = env->GetMethodID(cNumber, "doubleValue", "()D");
    }
    if (objectHash == nullptr) {
        JClass cObject = env->FindClass("java/lang/Object");
        objectHash = env->GetMethodID(cObject, "hashCode", "()I");
        objectToString = env->GetMethodID(cObject, "toString", "()Ljava/lang/String;");
    }
}


JavaType *ScriptContext::ensureType(TJNIEnv *env, jclass type) {
    const auto &iter = typeMap.find(type);
    if (iter != nullptr) {
        return iter->second;
    }
    JavaType *ret = new JavaType(env, type, this);
    typeMap.emplace(ret->type, ret);
    return ret;
}

JavaType *ScriptContext::getVoidClass(TJNIEnv *env) {
    JClass Void = env->FindClass("java/lang/Void");
    jfieldID mid = env->GetStaticFieldID(Void, "TYPE", "Ljava/lang/Class;");
    return ensureType(env, (JClass) env->GetStaticObjectField(Void, mid));
}

inline JClass ThreadContext::getTypeNoCheck(TJNIEnv *env, const String &className) const {
    JClass type = env->FindClass(className.c_str());
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    return type;
}

inline JavaType *ThreadContext::ensureArrayType(TJNIEnv *env, const char *typeName) {
    auto sq_st = strchr(typeName, '[');
    if (unlikely(sq_st == NULL)) {
        return nullptr;
    }
    uint32_t nameLen = uint32_t(sq_st - typeName);
    do {
        if (sq_st[0] != '[' || sq_st[1] != ']') {
            return nullptr;
        }
        sq_st += 2;
    } while (*sq_st != 0);
    String trueType;
#define liteq(literal, str, code)  if(memcmp(str,literal "",sizeof(literal "")-1)==0){code}
    switch (nameLen) {
        case 3:
            liteq("int", typeName, {
                trueType = "I";
                goto Type_End;
            })
            break;
        case 4:
            liteq("byte", typeName, {
                trueType = "B";
                goto Type_End;
            })
            liteq("char", typeName, {
                trueType = "C";
                goto Type_End;
            })
            liteq("long", typeName, {
                trueType = "J";
                goto Type_End;
            })
            break;
        case 5:
            liteq("float", typeName, {
                trueType = "F";
                goto Type_End;
            })
            liteq("short", typeName, {
                trueType = "S";
                goto Type_End;
            })
        case 6:
            liteq("double", typeName, {
                trueType = "D";
                goto Type_End;
            })
            break;
        case 7:
            liteq("boolean", typeName, {
                trueType = "Z";
                goto Type_End;
            })
        default:
            break;
    }
    trueType.append(typeName, nameLen);
    Type_End:
    uint32_t arrDepth = (uint32_t(strlen(typeName)) - nameLen) >> 1;
    String legalName(trueType.size() + arrDepth, '[');
    memcpy(&legalName[arrDepth], trueType.data(), trueType.length() + 1);
    JClass type = getTypeNoCheck(env, legalName);
    if (type == nullptr) {
        return nullptr;
    }
    JavaType *ret = scriptContext->ensureType(env, type);
    getImport()->stubbed[typeName] = {ret, DeleteOrNotString()};
    return ret;
}

JClass ThreadContext::findClass(TJNIEnv *env, String &className) {
    replaceAll<'.', '/'>(className);
    return getTypeNoCheck(env, className);
}

JavaType *ThreadContext::ensureType(TJNIEnv *env, const String &typeStr) {
    const char *typeName = typeStr.data();
#define MatchPrimitive(type)\
    ({if(memcmp(typeName,#type,sizeof(#type)-1)==0){\
        return scriptContext->type##Class;\
    }})
    switch (typeStr.size()) {
        case 3:
            MatchPrimitive(int);
            break;
        case 4:
            MatchPrimitive(byte);
            MatchPrimitive(long);
            MatchPrimitive(char);
            MatchPrimitive(void);
            break;
        case 5:
            MatchPrimitive(float);
            MatchPrimitive(short);
            break;
        case 6:
            MatchPrimitive(double);
            break;
        case 7:
            MatchPrimitive(boolean);
        default:
            break;
    }

    JClass type;
    if (typeName[typeStr.length() - 1] == ']') {
        return ensureArrayType(env, typeName);
    }
    if (strchr(typeName, '.') == nullptr && typeName[0] != '[') {
        if (strchr(typeName, '/') != nullptr) return nullptr;
        Import *import = getImport();
        auto &&iter = import->stubbed.find(typeStr);
        if (iter != nullptr) {
            return iter->second.type;
        }
    }
    String qul(typeStr);
    type = findClass(env, qul);
    if (type == nullptr) {
        return nullptr;
    }
    JavaType *ret = scriptContext->ensureType(env, type);
    return ret;
}

void ThreadContext::setPendingException(const String &msg) {
    TJNIEnv *env = getThreadEnv();
    if (pendingJavaError == nullptr) {
        static jclass luaExceptionType = (jclass) env->NewGlobalRef(
                env->FindClass("com/xyz/luadroid/LuaException"));
        static jmethodID con = env->GetMethodID(luaExceptionType, "<init>",
                                                "(Ljava/lang/String;)V");
        pendingJavaError = (jthrowable) env->NewObject(luaExceptionType, con,
                                                       env->NewStringUTF(msg.data()).get());
    } else {
        static jfieldID id = env->GetFieldID(throwableType, "detailMessage", "Ljava/lang/String;");
        JString oldMsg = (JString) env->GetObjectField(pendingJavaError, id);
        JString jmsg = env->NewStringUTF(
                oldMsg.str() != nullptr && strlen(oldMsg) > 0 ? (oldMsg.str() + ('\t' + msg)).data()
                                                              : msg.data());
        env->SetObjectField(pendingJavaError, id, jmsg.get());
    }
}

jvalue
ThreadContext::luaObjectToJValue(TJNIEnv *env, ValidLuaObject &luaObject, JavaType *type,
                                 jobject realType) {
    jvalue ret;
    auto typeId = type->getTypeID();
    if (type->isInteger()) {
        if (luaObject.type == T_INTEGER) {
            ret.j = luaObject.integer;
        } else if (unlikely(luaObject.type == T_OBJECT)) {
            ret.j = luaObject.objectRef->type->isBoxedChar() ? jlong(
                    env->CallCharMethod(luaObject.objectRef->object, charValue)) :
                    env->CallLongMethod(luaObject.objectRef->object, longValue);
        } else if (luaObject.type == T_CHAR) {
            ret.j = luaObject.character;
        } else /*if(luaObject.type==T_STRING)*/{
            strcpy8to16((char16_t *) &ret.c, luaObject.string, nullptr);
            ret.j = (jlong) ret.c;
        }
    } else if (typeId == JavaType::BOOLEAN) {
        if (unlikely(luaObject.type == T_OBJECT)) {
            ret.z = env->CallBooleanMethod(luaObject.objectRef->object, booleanValue);
        } else ret.z = luaObject.isTrue;
    } else if (typeId == JavaType::FLOAT) {
        if (luaObject.type == T_INTEGER) {
            ret.f = (float) luaObject.integer;
        } else if (luaObject.type == T_FLOAT) {
            ret.f = (float) luaObject.number;
        } else if (unlikely(luaObject.type == T_OBJECT)) {
            ret.f = luaObject.objectRef->type->isBoxedChar() ? float(
                    env->CallCharMethod(luaObject.objectRef->object, charValue)) :
                    float(env->CallDoubleMethod(luaObject.objectRef->object, doubleValue));
        } else if (luaObject.type == T_CHAR) {
            ret.f = (float) luaObject.character;
        } else /*if(luaObject.type==T_STRING)*/{
            strcpy8to16((char16_t *) &ret.c, luaObject.string, nullptr);
            ret.f = (float) ret.c;
        }
    } else if (typeId == JavaType::DOUBLE) {
        if (luaObject.type == T_INTEGER) {
            ret.d = luaObject.integer;
        } else if (luaObject.type == T_FLOAT) {
            ret.d = luaObject.number;
        } else if (unlikely(luaObject.type == T_OBJECT)) {
            ret.d = luaObject.objectRef->type->isBoxedChar() ? double(
                    env->CallCharMethod(luaObject.objectRef->object, charValue)) :
                    env->CallDoubleMethod(luaObject.objectRef->object, doubleValue);
        } else if (luaObject.type == T_CHAR) {
            ret.d = luaObject.character;
        } else /*if(luaObject.type==T_STRING)*/{
            strcpy8to16((char16_t *) &ret.c, luaObject.string, nullptr);
            ret.d = ret.c;
        }
    } else if (typeId == JavaType::CHAR) {
        if (unlikely(luaObject.type == T_OBJECT)) {
            ret.c = env->CallCharMethod(luaObject.objectRef->object, charValue);
        } else if (luaObject.type == T_CHAR)
            ret.c = luaObject.character;
        else strcpy8to16((char16_t *) &ret.c, luaObject.string, nullptr);
    } else if (luaObject.type == T_FUNCTION) {
        luaObject.shouldRelease = true;
        JObject method = type->getSingleInterface(env);
        Vector<JObject> methods;
        methods.push_back(std::move(method));
        BaseFunction *info = luaObject.func;
        luaObject.func = nullptr;//avoid
        Vector<std::unique_ptr<BaseFunction>> func;
        func.emplace_back(info);
        info->javaRefCount++;
        ret.l = proxy(env, type, nullptr, methods, func);

        if (ret.l != INVALID_OBJECT) {
            func.begin()->release();
        }
    } else if (luaObject.type == T_TABLE) {
        luaObject.shouldRelease = true;
        if (type->isTableType(env)) {
            typedef Map<LazyTable *, jobject> PTable;
            bool isOwner = false;
            PTable *current = getValue<PTable>(ContextStorage::PARSED_TABLE);
            if (current == nullptr) {
                current = new PTable();
                setValue(ContextStorage::PARSED_TABLE, current);
                isOwner = true;
            }
            auto &&iter = current->find(luaObject.lazyTable);
            if (iter == nullptr) {
                JavaType *mapType = MapType(env);
                auto &&table = luaObject.lazyTable->getTable()->get();
                static jmethodID initMap = env->GetMethodID(mapType->type, "<init>", "(I)V");
                static jmethodID mapPut = env->GetMethodID(mapType->type, "put",
                                                           "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
                JObject map = JObject(env, env->NewObject(mapType->type, initMap, table.size()));
                current->emplace(luaObject.lazyTable, map.get());
                for (auto &&pair: table) {
                    jobject key = luaObjectToJObject(env, pair.first);
                    if (key == INVALID_OBJECT) goto ERROR_HANDLE;
                    jobject value = luaObjectToJObject(env, pair.second);
                    if (value == INVALID_OBJECT) {
                        env->DeleteLocalRef(key);
                        goto ERROR_HANDLE;
                    }
                    env->CallObjectMethod(map, mapPut, JObject(env, key).get(),
                                          JObject(env, value).get());
                }
                current->erase(luaObject.lazyTable);
                ret.l = type->convertTable(env, map, realType);
            } else ret.l = type->convertTable(env, iter->second, realType);
            if (isOwner) {
                delete current;
                setValue(ContextStorage::PARSED_TABLE, nullptr);
            }
        } else if (type->isInterface(env)) {
            ret.l = luaObject.lazyTable->asInterface(env, this, type);
        } else ret.l = nullptr;
        HOLD_JAVA_EXCEPTION(this, {
            goto ERROR_HANDLE;
        });
    } else if (luaObject.type == T_NIL) {
        ret.l = nullptr;
    } else if (luaObject.type == T_STRING) {
        luaObject.shouldRelease = true;
        ret.l = env->NewStringUTF(luaObject.string).invalidate();
    } else if (luaObject.type == T_OBJECT) {
        ret.l = luaObject.objectRef->object;
    } else {
        luaObject.shouldRelease = true;
        switch (typeId) {
            case JavaType::BOX_CHAR:
                if (luaObject.type == T_STRING)
                    strcpy8to16(&luaObject.character, luaObject.string, NULL);
                else if (luaObject.type == T_FLOAT)
                    luaObject.character = (char16_t) luaObject.number;
                ret.l = env->CallStaticObjectMethod(type->getType(), type->
                        getBoxMethodForBoxType(env), luaObject.character).invalidate();
                break;
            case JavaType::BOX_BOOLEAN:
                ret.l = env->CallStaticObjectMethod(type->getType(), type->
                        getBoxMethodForBoxType(env), luaObject.isTrue).invalidate();
                break;
            case JavaType::BOX_DOUBLE:
                ret.l = env->CallStaticObjectMethod(type->getType(),
                                                    type->getBoxMethodForBoxType(env),
                                                    luaObject.type == T_INTEGER
                                                    ? (double) luaObject.integer :
                                                    luaObject.number).invalidate();
                break;
            case JavaType::BOX_FLOAT:
                ret.l = env->CallStaticObjectMethod(type->getType(),
                                                    type->getBoxMethodForBoxType(env),
                                                    luaObject.type == T_INTEGER
                                                    ? (float) luaObject.integer :
                                                    (float) luaObject.number).invalidate();
                break;
            case JavaType::BOX_NUMBER:
            case JavaType::BOX_OBJECT:
                ret.l = luaObjectToJObject(env, luaObject);
                break;
            default:
                if (likely(type->_isBox)) {
                    ret.l = env->CallStaticObjectMethod(type->getType(), type->
                            getBoxMethodForBoxType(env), luaObject.integer).invalidate();
                } else {
                    luaObject.shouldRelease = false;
                    ret.l = INVALID_OBJECT;
                }
                break;
        }
    }
    return ret;
    ERROR_HANDLE:
    ret.l = INVALID_OBJECT;
    return ret;
}

JavaType *ThreadContext::MapType(TJNIEnv *env) {
    if (scriptContext->HashMapClass == nullptr) {
        scriptContext->HashMapClass = scriptContext->
                ensureType(env, env->FindClass("com/xyz/luadroid/DataMap"));
    }
    return scriptContext->HashMapClass;
}

JavaType *ThreadContext::ArrayType(TJNIEnv *env) {
    if (scriptContext->ArrayClass == nullptr) {
        scriptContext->ArrayClass = scriptContext->
                ensureType(env, env->FindClass("java/lang/reflect/Array"));
    }
    return scriptContext->ArrayClass;
}

JavaType *ThreadContext::FunctionType(TJNIEnv *env) {
    if (scriptContext->FunctionClass == nullptr) {
        scriptContext->FunctionClass = scriptContext->
                ensureType(env, env->FindClass("com/xyz/luadroid/LuaFunction"));
    }
    return scriptContext->FunctionClass;
}

jobject ThreadContext::luaObjectToJObject(TJNIEnv *env, ValidLuaObject &luaObject) {
    switch (luaObject.type) {
        case T_NIL:
            return nullptr;
        case T_INTEGER: {
            return env->CallStaticObjectMethod(scriptContext->LongClass->getType(),
                                               scriptContext->LongClass->
                                                       getBoxMethodForBoxType(env),
                                               luaObject.integer).invalidate();
        }
        case T_BOOLEAN: {
            return env->CallStaticObjectMethod(scriptContext->BooleanClass->getType(),
                                               scriptContext->BooleanClass->
                                                       getBoxMethodForBoxType(env),
                                               luaObject.isTrue).invalidate();
        }
        case T_FLOAT: {
            return env->CallStaticObjectMethod(scriptContext->DoubleClass->getType(),
                                               scriptContext->DoubleClass->
                                                       getBoxMethodForBoxType(env),
                                               luaObject.number).invalidate();
        }
        case T_OBJECT: {
            jvalue v = luaObjectToJValue(env, luaObject, MapType(env));
            v.l = env->NewLocalRef(v.l);
            return v.l;
        }
        case T_TABLE: {
            jvalue v = luaObjectToJValue(env, luaObject, MapType(env));
            return v.l;
        }
        case T_STRING:
        case T_FUNCTION: {
            jvalue v = luaObjectToJValue(env, luaObject, FunctionType(env));
            return v.l;
        }
        case T_CHAR: {
            return env->CallStaticObjectMethod(scriptContext->CharacterClass->getType(),
                                               scriptContext->CharacterClass->
                                                       getBoxMethodForBoxType(env),
                                               luaObject.integer).invalidate();
        }
    }
    return nullptr;
}

jobject ThreadContext::proxy(TJNIEnv *env, JavaType *main, Vector<JavaType *> *interfaces,
                             const Vector<JObject> &principal,
                             Vector<std::unique_ptr<BaseFunction>> &proxy,
                             BaseFunction *defaultFunc,
                             bool shared, long nativeInfo, jobject superObject) {
    uint interfaceCount;
    JObjectArray interfaceArray;
    if (interfaces != nullptr && (interfaceCount = interfaces->size())) {
        interfaceArray = env->NewObjectArray(interfaceCount, classType, nullptr);
        for (uint i = interfaceCount; i--;) {
            env->SetObjectArrayElement(interfaceArray, i,
                                       interfaces->at(i)->getType());
        }
    }
    uint principalCount = principal.size();
    static jclass sMethodClass = (jclass) env->NewGlobalRef(
            env->GetObjectClass(env->ToReflectedMethod(contextClass, sProxy, 0)));
    JObjectArray principalArray = env->NewObjectArray(principalCount, sMethodClass, nullptr);
    if (principalCount) {
        for (uint i = principalCount; i--;) {
            env->SetObjectArrayElement(principalArray, i, principal[i]);
        }
    }
    int proxyCount = proxy.size();
    jlong buf[proxyCount];
    for (jsize i = proxyCount; i--;) {
        buf[i] = (jlong) proxy[i].get();
    }
    JType<jlongArray> proxyArray = env->NewLongArray(proxyCount);
    env->SetLongArrayRegion(proxyArray, 0, proxyCount, buf);
    jvalue args[] = {
            {.l=main->getType()},
            {.l=interfaceArray},
            {.l=principalArray},
            {.l=proxyArray},
            {.j=(jlong) defaultFunc},
            {.z=(jboolean) shared},
            {.j=nativeInfo},
            {.l=superObject}
    };
    jobject ret = env->asJNIEnv()->CallObjectMethodA(
            scriptContext->javaRef, sProxy, args);//use jvalue to avoid stack limit in 32 bit mode
    HOLD_JAVA_EXCEPTION(this, {
        return INVALID_OBJECT;
    });
    return ret;
}


