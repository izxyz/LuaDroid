package com.xyz.luadroid;

/**
 * Mapping for lua functions.
 */
public interface LuaFunction {
    Object[] invoke(Object... args);
}
