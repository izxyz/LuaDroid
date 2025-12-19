package com.xyz.luadroid.app;

import android.app.Dialog;
import android.content.Context;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.widget.Toast;

import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;


import com.xyz.luadroid.ScriptContext;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Member;
import java.lang.reflect.Proxy;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.SecureRandom;

public class MainActivity extends AppCompatActivity {

    final static String TAG = "xiyuezhen";

    ScriptContext context;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return insets;
        });
        System.loadLibrary("luadroid");

        runLua("main.lua",false);
    }

    void runLua(String filename, boolean newThread) {
        Runnable runnable = () -> {
            try {
                context = new ScriptContext(this);
                context.run(copy(filename), MainActivity.this);
            } catch (Throwable throwable) {
                Log.e(TAG, filename, throwable);
            }
        };
        if (newThread) {
            new Thread(runnable).start();
        }else {
            runnable.run();
        }
    }



    public File copy(String filename) {
//        File output = new File(getFilesDir(), filename);
//        try (InputStream is = getAssets().open(filename)) {
//            Files.copy(is, output.toPath(), StandardCopyOption.REPLACE_EXISTING);
//        } catch (IOException e) {
//            Log.e(TAG, "copy: ", e);
//            return null;
//        }
//        return output;

        File output = new File(getDir("luadroid",0), filename);
        try (InputStream is = getAssets().open(filename)) {
            Files.copy(is, output.toPath(), StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            Log.e(TAG, "copy: ", e);
            return null;
        }
        return output;
    }

    private  void toast(String msg) {
        runOnUiThread(() -> Toast.makeText(MainActivity.this, msg, Toast.LENGTH_LONG).show());
    }

    public static class Child {
        public Child() {

        }
    }
}