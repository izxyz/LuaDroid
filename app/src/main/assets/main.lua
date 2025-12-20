---@diagnostic disable: undefined-global
log = java.log

log("begin")

local act = ...

local luathread = require("thread")


java.import("java.lang.Thread")
java.import("java.lang.Runnable")
java.import("com.xyz.luadroid.app.Foo")
java.import("java.lang.String")
java.import("com.xyz.luadroid.ScriptContext")

java.import("android.app.Dialog$ListenersHandler")
java.import("java.lang.System")
java.import("java.lang.Math")


local mutex = luathread:newmutex()
local newcond = luathread:newcond()



local test0 = function()
    local runnable1 = java.proxy({
        super = Runnable,
        methods = {
            run = function()
                for i = 1, 10, 1 do
                    log('runnable 1 run', Foo.add(0, 1))
                    log("runnable 1 end", Foo.add(0, 1))
                end
            end
        }
    })


    local runnable2 = java.proxy({
        super = Runnable,
        methods = {
            run = function()
                for i = 1, 10, 1 do
                    log('runnable 2 run', Foo.add(0, 2))
                    log("runnable 2 end", Foo.add(0, 2))
                end
            end
        }
    })

    local runnable3 = java.proxy({
        super = Runnable,
        methods = {
            run = function()
                for i = 1, 10, 1 do
                    log('runnable 3 run', Foo.add(0, 3))
                    log("runnable 3 end", Foo.add(0, 3))
                end
            end
        }
    })

    local size = 0

    Thread(runnable2).start()
    Thread(runnable1).start()
    Thread(runnable3).start()
    Thread(runnable1).start()
    Thread(runnable3).start()
    Thread(runnable2).start()
    Thread(runnable2).start()
    Thread(runnable1).start()
    Thread(runnable3).start()
    Thread(runnable1).start()
    Thread(runnable3).start()
    Thread(runnable2).start()

    for i = 1, 1000, 1 do
        Thread(runnable2).start()
        Thread(runnable1).start()
        Thread(runnable3).start()
        Thread(runnable1).start()
        Thread(runnable3).start()
        Thread(runnable2).start()
        -- Thread(runnable2).start()
        -- Thread(runnable1).start()
        -- Thread(runnable3).start()
        -- Thread(runnable1).start()
        -- Thread(runnable3).start()
        -- Thread(runnable2).start()

        size = size + 6
        log("size ", size)
        Thread.sleep(500)
    end
end

local test1 = function()
    local fun1 = function()
        log('runnable 1 run', Foo.add(0, 1))
        log("runnable 1 end", Foo.add(0, 1))
    end

    local fun2 = function()
        log('runnable 2 run', Foo.add(0, 2))
        log("runnable 2 end", Foo.add(0, 2))
    end

    local fun3 = function()
        log('runnable 3 run', Foo.add(0, 3))
        log("runnable 3 end", Foo.add(0, 3))
    end

    luathread.newthread(fun2, {})
    luathread.newthread(fun1, {})
    luathread.newthread(fun1, {})
    luathread.newthread(fun2, {})
    luathread.newthread(fun3, {})
    luathread.newthread(fun2, {})
    luathread.newthread(fun3, {})
    luathread.newthread(fun2, {})
end

luathread.newthread(function()
    log("hello-->")
--     java.import("java.util.Map")
--     log("byte----", java.type("byte[]"))
--     log("map", java.type("java.util.Map[]"))
end,{})

local socket = require("socket.core")
local mime = require("mime.core")
local ssl = require("ssl.core")

local openssl = require("openssl")
local md = openssl.digest.get('sha256')
local hash = md:digest('hello world')


local cjson  = require("cjson")
local t = {name= "张三",age = 18}

local encode = openssl.base64("张三")
log("base64编码", encode)
local decode = openssl.base64(encode,false)
log("base64解码",decode)

local lfs = require("lfs")
log("lfs",lfs)

local zlib = require("zlib")

for k, v in pairs(zlib) do
    log(k,v)
end

log("end-->")
