package com.xyz.luadroid.app;

import android.util.Log;

public class Foo {

    final static String TAG = "xiyuezhen";

    public Foo(){

    }

    public static void run(Runnable runnable) {
        new Thread(runnable).start();
    }

    public static void testChar(char p) {
        Log.d(TAG, "testChar: " + p);
    }

    public String test0(String p) {
        if (p == null) {
            Log.d(TAG, "test0: p==null");
        }
        return p + "--<";
    }

    public static String test1() {
        return "a";
    }

    public String say(){
        return "汤姆";
    }

    public static int add( int a, int b) {
        return a+b;
    }

     public class Child {

         public Child() {

        }

         public String say() {
            return "hello word";
        }

         public    String say1() {
            return "hello static";
        }
    }
}
