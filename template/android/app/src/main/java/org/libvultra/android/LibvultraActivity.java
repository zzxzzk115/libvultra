package org.libvultra.android;

import android.os.Bundle;
import android.view.WindowManager;
import com.google.androidgamesdk.GameActivity;

public class LibvultraActivity extends GameActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    static {
        System.loadLibrary("libvultra_android");
    }
}
