cmake_minimum_required(VERSION 3.22.1)

project(luaopenssl)

# 设置 C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LUAOPENSSL_SOURCES
        asn1.c
        bio.c
        callback.c
        cipher.c
        cms.c
        compat.c
        crl.c
        csr.c
        dh.c
        digest.c
        dsa.c
        ec.c
        engine.c
        hmac.c
        kdf.c
        lbn.c
        lhash.c
        mac.c
        misc.c
        ocsp.c
        openssl.c
        ots.c
        param.c
        pkcs7.c
        pkcs12.c
        pkey.c
        rsa.c
        srp.c
        ssl.c
        th-lock.c
        util.c
        x509.c
        xalgor.c
        xattrs.c
        xexts.c
        xname.c
        xstore.c

        auxiliar.c
        subsidiar.c
)

add_library(luaopenssl SHARED
        ${LUAOPENSSL_SOURCES}
)

target_include_directories(luaopenssl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../lua
)
target_link_libraries(luaopenssl
        lua
        m
        ${CMAKE_CURRENT_SOURCE_DIR}/../../jniLibs/${ANDROID_ABI}/libcrypto.so
        ${CMAKE_CURRENT_SOURCE_DIR}/../../jniLibs/${ANDROID_ABI}/libssl.so
)