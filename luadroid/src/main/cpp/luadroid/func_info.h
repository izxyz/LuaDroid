

#ifndef LUADROID_FUNC_INFO_H
#define LUADROID_FUNC_INFO_H

#include "common.h"
#include "myarray.h"
#include "base_func.h"
#include "Vector.h"

class JavaType;

class DeleteOrNotString {
    const char *s;
    bool shouldDel;
public:
    DeleteOrNotString() : s(nullptr), shouldDel(false) {}

    explicit DeleteOrNotString(const String &str, bool del = false) : shouldDel(del) {
        if (del) {
            size_t length = str.length() + 1;
            char *tmp = new char[length];
            memcpy(tmp, str.data(), length);
            s = tmp;
        } else {
            s = str.data();
        }
    }

    explicit DeleteOrNotString(const char *str, bool del = false) : shouldDel(del) {
        if (del) {
            size_t len = strlen(str) + 1;
            char *tmp = new char[len];
            memcpy(tmp, str, len);
            s = tmp;
        } else {
            s = str;
        }
    }

    DeleteOrNotString(DeleteOrNotString &&other) : s(other.s), shouldDel(other.shouldDel) {
        other.s = nullptr;
        other.shouldDel = false;
    }

    DeleteOrNotString(const DeleteOrNotString &) = delete;

    DeleteOrNotString &operator=(const DeleteOrNotString &) = delete;

    DeleteOrNotString &operator=(DeleteOrNotString &&other) {
        this->~DeleteOrNotString();
        s = other.s;
        shouldDel = other.shouldDel;
        other.s = nullptr;
        other.shouldDel = false;
        return *this;
    }

    operator const char *() {
        return s;
    }

    const char *get() {
        return s;
    }

    bool cached() {
        return !shouldDel;
    }

    ~DeleteOrNotString() {
//        LOGE("~DeleteOrNotString");
        if (shouldDel) {
            delete[] s;
        }

    }
};

struct Import {
    struct TypeInfo {
        JavaType *type;
        DeleteOrNotString pack;

        TypeInfo() : type(nullptr), pack() {}

        TypeInfo(JavaType *type, DeleteOrNotString &&string) : type(type),
                                                               pack(std::move(string)) {}

        TypeInfo(TypeInfo &&o) : type(o.type), pack(std::move(o.pack)) {}

        TypeInfo &operator=(TypeInfo &&o) {
            type = o.type;
            pack = std::move(o.pack);
            return *this;
        }

        TypeInfo &operator=(const TypeInfo &o) = delete;

        TypeInfo(const TypeInfo &) = delete;
    };

    typedef Map<String, TypeInfo> TypeCache;
    TypeCache stubbed;

    Import() {
    }

    Import(Import &&other) :
            stubbed(std::move(other.stubbed)) {
    }

    ~Import() {
//        LOGE("~Import");
    }
};

class LocalFunctionInfo : public BaseFunction {
private:
    lua_State *thread;  // 独立的 thread，持有回调函数
    pthread_mutex_t mutex;  // 互斥锁，保护多线程并发访问
public:
    /**
     * 构造函数
     * @param threadL 独立的 thread，持有回调函数
     */
    explicit LocalFunctionInfo(lua_State *threadL) : thread(threadL) {
        pthread_mutex_init(&mutex, nullptr);
    }
    
    /**
     * 获取持有回调函数的 thread
     * @return thread 指针
     */
    lua_State* getThread() const { return thread; }
    
    /**
     * 锁定互斥锁，用于保护多线程并发访问
     */
    void lock() {
        pthread_mutex_lock(&mutex);
    }
    
    /**
     * 解锁互斥锁
     */
    void unlock() {
        pthread_mutex_unlock(&mutex);
    }
    
    virtual ~LocalFunctionInfo() {
        LOGE("~LocalFunctionInfo");
        pthread_mutex_destroy(&mutex);
    }
};

#endif //LUADROID_FUNCINFO_H

