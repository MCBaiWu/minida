package com.baiwu.imgui;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * GLSurfaceView + Renderer for ImGui rendering.
 * Adds soft keyboard input support via a hidden EditText,
 * physical key / DPAD support, and clipboard sync.
 */
public class Surface extends GLSurfaceView implements GLSurfaceView.Renderer {

    private static final String TAG = "minida";
    private static Surface instance;
    private EditText hiddenInput;
    private FrameLayout container;
    private String lastText = "";
    private boolean keyboardVisible = false;
    private ClipboardManager clipboardManager;

    static {
        System.loadLibrary("imgui");
    }

    public Surface(Context context) {
        super(context);
        instance = this;
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        setRenderer(this);
        setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);

        // Enable keyboard input
        setFocusable(true);
        setFocusableInTouchMode(true);

        clipboardManager = (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);

        createHiddenEditText(context);
    }

    private void createHiddenEditText(Context context) {
        container = new FrameLayout(context);

        hiddenInput = new EditText(context);
        hiddenInput.setLayoutParams(new FrameLayout.LayoutParams(1, 1));
        hiddenInput.setAlpha(0f);
        hiddenInput.setFocusable(true);
        hiddenInput.setFocusableInTouchMode(true);

        hiddenInput.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                String newText = s.toString();
                if (newText.length() > lastText.length()) {
                    String added = newText.substring(lastText.length());
                    for (int i = 0; i < added.length(); i++) {
                        char c = added.charAt(i);
                        CharEvent((int) c);
                    }
                }
                lastText = newText;
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        hiddenInput.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
                if (actionId == EditorInfo.IME_ACTION_DONE
                        || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                    KeyEvent(KeyEvent.KEYCODE_ENTER, true);
                    KeyEvent(KeyEvent.KEYCODE_ENTER, false);
                    return true;
                }
                return false;
            }
        });

        hiddenInput.setOnKeyListener(new OnKeyListener() {
            @Override
            public boolean onKey(View v, int keyCode, KeyEvent event) {
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    int mappedKey = mapAndroidKeycode(keyCode);
                    KeyEvent(mappedKey, true);
                    if (keyCode == KeyEvent.KEYCODE_DEL && lastText.isEmpty()) {
                        return true;
                    }
                } else if (event.getAction() == KeyEvent.ACTION_UP) {
                    int mappedKey = mapAndroidKeycode(keyCode);
                    KeyEvent(mappedKey, false);
                }
                return false;
            }
        });

        container.addView(hiddenInput);
    }

    // Android KeyCode -> custom key id (must match AndroidKeyToImGuiKey in native)
    private static int mapAndroidKeycode(int androidKeycode) {
        switch (androidKeycode) {
            case KeyEvent.KEYCODE_TAB:         return 0;
            case KeyEvent.KEYCODE_DPAD_LEFT:   return 1;
            case KeyEvent.KEYCODE_DPAD_RIGHT:  return 2;
            case KeyEvent.KEYCODE_DPAD_DOWN:   return 3;
            case KeyEvent.KEYCODE_DPAD_UP:     return 4;
            case KeyEvent.KEYCODE_SHIFT_LEFT:  return 5;
            case KeyEvent.KEYCODE_SHIFT_RIGHT: return 6;
            case KeyEvent.KEYCODE_CTRL_LEFT:   return 7;
            case KeyEvent.KEYCODE_CTRL_RIGHT:  return 8;
            case KeyEvent.KEYCODE_ALT_LEFT:    return 9;
            case KeyEvent.KEYCODE_ALT_RIGHT:   return 10;
            case KeyEvent.KEYCODE_CAPS_LOCK:   return 11;
            case KeyEvent.KEYCODE_SCROLL_LOCK: return 12;
            case KeyEvent.KEYCODE_NUM_LOCK:    return 13;
            case KeyEvent.KEYCODE_SYSRQ:       return 14;
            case KeyEvent.KEYCODE_BREAK:       return 15;
            case KeyEvent.KEYCODE_INSERT:      return 16;
            case KeyEvent.KEYCODE_FORWARD_DEL: return 23;
            case KeyEvent.KEYCODE_A: return 29;
            case KeyEvent.KEYCODE_B: return 30;
            case KeyEvent.KEYCODE_C: return 31;
            case KeyEvent.KEYCODE_D: return 32;
            case KeyEvent.KEYCODE_E: return 33;
            case KeyEvent.KEYCODE_F: return 34;
            case KeyEvent.KEYCODE_G: return 35;
            case KeyEvent.KEYCODE_H: return 36;
            case KeyEvent.KEYCODE_I: return 37;
            case KeyEvent.KEYCODE_J: return 38;
            case KeyEvent.KEYCODE_K: return 39;
            case KeyEvent.KEYCODE_L: return 40;
            case KeyEvent.KEYCODE_M: return 41;
            case KeyEvent.KEYCODE_N: return 42;
            case KeyEvent.KEYCODE_O: return 43;
            case KeyEvent.KEYCODE_P: return 44;
            case KeyEvent.KEYCODE_Q: return 45;
            case KeyEvent.KEYCODE_R: return 46;
            case KeyEvent.KEYCODE_S: return 47;
            case KeyEvent.KEYCODE_T: return 48;
            case KeyEvent.KEYCODE_U: return 49;
            case KeyEvent.KEYCODE_V: return 50;
            case KeyEvent.KEYCODE_W: return 51;
            case KeyEvent.KEYCODE_X: return 52;
            case KeyEvent.KEYCODE_Y: return 53;
            case KeyEvent.KEYCODE_Z: return 54;
            case KeyEvent.KEYCODE_COMMA:  return 55;
            case KeyEvent.KEYCODE_PERIOD: return 56;
            case KeyEvent.KEYCODE_SPACE:  return 59;
            case KeyEvent.KEYCODE_ENTER:  return 61;
            case KeyEvent.KEYCODE_DEL:    return 67;
            case KeyEvent.KEYCODE_ESCAPE: return 111;
            default: return androidKeycode;
        }
    }

    public FrameLayout getInputContainer() {
        return container;
    }

    // ==================== JNI declarations ====================
    private native void DrawFrame();
    private native void ShutDown();
    private native void SurfaceCreated();
    private native void SurfaceChanged(int width, int height);
    public static native String getWindowRect();
    public static native void MotionEventClick(boolean isActionUp, float rawX, float rawY, int action);
    public static native void KeyEvent(int key, boolean down);
    public static native void CharEvent(int c);
    public static native void TextInput(String text);
    public static native boolean WantTextInput();
    public static native void SetClipboardText(String text);
    public static native String GetClipboardText();

    @SuppressWarnings("unused")
    public static void nativeSetClipboard(String text) {
        if (instance != null && instance.clipboardManager != null) {
            ClipData clip = ClipData.newPlainText("minida", text);
            instance.clipboardManager.setPrimaryClip(clip);
        }
    }

    private void syncClipboardToNative() {
        if (clipboardManager != null && clipboardManager.hasPrimaryClip()) {
            ClipData clip = clipboardManager.getPrimaryClip();
            if (clip != null && clip.getItemCount() > 0) {
                CharSequence text = clip.getItemAt(0).getText();
                if (text != null) {
                    SetClipboardText(text.toString());
                }
            }
        }
    }

    // ==================== Touch handling ====================
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX();
        float y = e.getY();
        int action = e.getAction();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_UP:
                MotionEventClick(action != MotionEvent.ACTION_UP, x, y, action);
                break;
        }
        return true;
    }

    // ==================== Keyboard handling ====================
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        int mappedKey = mapAndroidKeycode(keyCode);
        KeyEvent(mappedKey, true);
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        int mappedKey = mapAndroidKeycode(keyCode);
        KeyEvent(mappedKey, false);
        return super.onKeyUp(keyCode, event);
    }

    public void showSoftKeyboard() {
        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(new Runnable() {
            @Override
            public void run() {
                if (hiddenInput != null) {
                    hiddenInput.requestFocus();
                    hiddenInput.setText("");
                    lastText = "";
                    InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (imm != null) {
                        imm.showSoftInput(hiddenInput, InputMethodManager.SHOW_FORCED);
                    }
                    keyboardVisible = true;
                }
            }
        });
    }

    public void hideSoftKeyboard() {
        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(new Runnable() {
            @Override
            public void run() {
                InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.hideSoftInputFromWindow(getWindowToken(), 0);
                }
                if (hiddenInput != null) {
                    hiddenInput.clearFocus();
                }
                keyboardVisible = false;
            }
        });
    }

    public void pollTextInput() {
        boolean want = WantTextInput();
        if (want && !keyboardVisible) {
            syncClipboardToNative();
            showSoftKeyboard();
        } else if (!want && keyboardVisible) {
            hideSoftKeyboard();
        }
    }

    public static void requestKeyboard() {
        if (instance != null) instance.showSoftKeyboard();
    }

    public static void hideKeyboard() {
        if (instance != null) instance.hideSoftKeyboard();
    }

    // ==================== GLSurfaceView.Renderer ====================
    @Override
    public void onSurfaceCreated(GL10 glUnused, EGLConfig config) {
        GLES30.glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        try {
            SurfaceCreated();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "SurfaceCreated JNI not linked", e);
        }
    }

    @Override
    public void onDrawFrame(GL10 glUnused) {
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
        try {
            DrawFrame();
            pollTextInput();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "DrawFrame JNI not linked", e);
        }
    }

    @Override
    public void onSurfaceChanged(GL10 glUnused, int width, int height) {
        GLES30.glViewport(0, 0, width, height);
        try {
            SurfaceChanged(width, height);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "SurfaceChanged JNI not linked", e);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        try {
            ShutDown();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "ShutDown JNI not linked", e);
        }
    }
}
