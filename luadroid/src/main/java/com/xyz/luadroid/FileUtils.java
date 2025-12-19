package com.xyz.luadroid;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;

public class FileUtils {
    public static void copyAssetsToPath(AssetManager assets, String assetsPath, String targetPath) throws IOException {
        String[] files = assets.list(assetsPath);
        if (files == null) {
            return;
        }

        Files.createDirectories(Paths.get(targetPath));

        for (String filename : files) {
            String assetFilePath = assetsPath.isEmpty() ? filename : assetsPath + "/" + filename;
            Path targetFilePath = Paths.get(targetPath, filename);

            try {
                String[] subFiles = assets.list(assetFilePath);
                if (subFiles != null && subFiles.length > 0) {
                    copyAssetsToPath(assets, assetFilePath, targetFilePath.toString());
                } else {
                    copyAssetFile(assets, assetFilePath, targetFilePath);
                }
            } catch (IOException e) {
                copyAssetFile(assets, assetFilePath, targetFilePath);
            }
        }
    }

    private static void copyAssetFile(AssetManager assets, String assetPath, Path targetPath) throws IOException {
        try (InputStream in = assets.open(assetPath)) {
            Files.copy(in, targetPath, StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
