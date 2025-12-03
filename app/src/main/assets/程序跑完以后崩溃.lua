log = java.log

log("begin")

local act = ...

local luathread = require("luathread")


java.import("java.lang.Thread")
java.import("java.lang.Runnable")
java.import("com.xyz.luadroid.app.Foo")
java.import("java.lang.String")
java.import("com.xyz.luadroid.ScriptContext")

java.import("android.app.Dialog$ListenersHandler")
java.import("java.lang.System")
java.import("java.lang.Math")



local fooObj = Foo()

local mutex = luathread:newmutex()
local newcond = luathread:newcond()

local begin = System.currentTimeMillis()
for i = 1, 100000 do
    Math.abs(0)
end
act.toast(" 主线程耗时:" .. (System.currentTimeMillis() - begin) .. "ms")

java.proxy({
   super = Runnable,
   methods = {
       run = function ()
        --    print("-->")
       end
   }
})


luathread.newthread(function()
    log("子线程开始")
    local begin2 = System.currentTimeMillis()
    for i = 1, 100000 do
        Math.abs(0)
    end
    act.toast(" 子线程耗时:" .. (System.currentTimeMillis() - begin2) .. "ms")
    log(" 子线程耗时:" .. (System.currentTimeMillis() - begin2) .. "ms")
    newcond:broadcast()
end, {})

newcond:wait(mutex)

log("end")
