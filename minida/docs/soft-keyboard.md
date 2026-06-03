# 软键盘输入学习笔记 —— 来自 Uranalysis 项目

> 项目：Uranalysis（`com.uranalysis.app`）
> 核心文件：`Surface.java`（Java 层）、`main.cpp`（JNI 层）
> 学习目的：把软键盘 / 剪贴板 / 物理键盘的能力迁移到 **MiniDA**

---

## 整体架构

```
┌────────────────────────────────────────────────────────────┐
│  Activity (MainActivity)                                    │
│  ├─ FrameLayout rootLayout                                  │
│  │   ├─ Surface (GLSurfaceView + ImGui 渲染)                │
│  │   └─ inputContainer (FrameLayout, 放一个 1×1 的 EditText)│
└────────────────────────────────────────────────────────────┘
        │ 触摸事件             │ 键盘事件
        ▼                      ▼
┌────────────────────────────────────────────────────────────┐
│  Surface  (Java 端)                                          │
│  ├─ hiddenInput (1×1 EditText, alpha=0)                     │
│  │   ├─ TextWatcher  → CharEvent  → native  io.AddInputChar│
│  │   ├─ OnEditorAction (Enter)    → KeyEvent(Enter)         │
│  │   └─ OnKeyListener (Backspace) → KeyEvent(Backspace)     │
│  ├─ clipboardManager  ↔  nativeSetClipboard() / GetClip...()│
│  └─ onTouchEvent / onKeyDown / onKeyUp                     │
└────────────────────────────────────────────────────────────┘
        │ JNI
        ▼
┌────────────────────────────────────────────────────────────┐
│  Native (main.cpp)                                           │
│  ├─ ImGuiGetClipboardText / ImGuiSetClipboardText           │
│  ├─ AndroidKeyToImGuiKey(key)  把自定义 key id 映射到        │
│  │                            ImGuiKey_*                     │
│  ├─ KeyEvent   → io.AddKeyEvent(imguiKey, down)             │
│  ├─ CharEvent  → io.AddInputCharacter(c)                    │
│  ├─ TextInput  → 逐字符 io.AddInputCharacter                 │
│  └─ WantTextInput → 返回 g_wantTextInput，控制软键盘显示     │
└────────────────────────────────────────────────────────────┘
```

---

## 核心实现拆解

### 1. 软键盘 —— "1×1 透明 EditText" 桥接

**为什么用一个不可见的 EditText？**
- Android 软键盘（IME）只对可获得焦点的 `View` 触发
- ImGui 自己绘制的输入框是 GL 像素，**收不到 IME 输入**
- 解决方案：放一个 `1×1`、透明度 `0` 的 `EditText` 用来"喂"IME，文本变化时把内容回灌到 ImGui

```java
// Surface.java
hiddenInput = new EditText(context);
hiddenInput.setLayoutParams(new FrameLayout.LayoutParams(1, 1));
hiddenInput.setAlpha(0f);
hiddenInput.setFocusable(true);
hiddenInput.setFocusableInTouchMode(true);
```

**通过 TextWatcher 捕获字符输入：**
```java
hiddenInput.addTextChangedListener(new TextWatcher() {
    @Override
    public void onTextChanged(CharSequence s, int start, int before, int count) {
        String newText = s.toString();
        if (newText.length() > lastText.length()) {
            String added = newText.substring(lastText.length());
            for (int i = 0; i < added.length(); i++) {
                CharEvent((int) added.charAt(i));   // ← 喂给 ImGui
            }
        }
        lastText = newText;
    }
    ...
});
```

### 2. Enter / Backspace 等"非字符键" —— OnKeyListener

TextWatcher 只能拿到"插入的字符"，拿不到 Backspace / Enter 这种**编辑操作**，所以额外挂 `OnKeyListener`：

```java
hiddenInput.setOnKeyListener((v, keyCode, event) -> {
    if (event.getAction() == KeyEvent.ACTION_DOWN) {
        KeyEvent(mapAndroidKeycode(keyCode), true);
    } else if (event.getAction() == KeyEvent.ACTION_UP) {
        KeyEvent(mapAndroidKeycode(keyCode), false);
    }
    return false;
});
```

Enter 单独走 `OnEditorActionListener`：
```java
hiddenInput.setOnEditorActionListener((v, actionId, event) -> {
    if (actionId == EditorInfo.IME_ACTION_DONE
        || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
        KeyEvent(KeyEvent.KEYCODE_ENTER, true);
        KeyEvent(KeyEvent.KEYCODE_ENTER, false);
        return true;
    }
    return false;
});
```

### 3. Keycode 映射表 —— 两层映射

**第一层：Android KeyCode → 自定义 key id（Java）**
```java
case KeyEvent.KEYCODE_TAB:        return 0;
case KeyEvent.KEYCODE_DPAD_LEFT:  return 1;
...
case KeyEvent.KEYCODE_A:          return 29;
...
case KeyEvent.KEYCODE_DEL:        return 67;  // Backspace
```

**第二层：自定义 key id → ImGuiKey（Native C++）**
```cpp
static ImGuiKey AndroidKeyToImGuiKey(int androidKey) {
    switch (androidKey) {
        case 0:  return ImGuiKey_Tab;
        case 1:  return ImGuiKey_LeftArrow;
        ...
        case 29: return ImGuiKey_A;
        case 67: return ImGuiKey_Backspace;
        ...
    }
}
```

> **为什么分两层？** ① 把 Java 的 enum 与 native 的 ImGui 版本解耦；② 同一份映射表能复用于物理键盘和软键盘。

### 4. 软键盘的"按需显示" —— WantTextInput 轮询

ImGui 每帧渲染后知道当前是否需要文本输入（`io.WantTextInput`）。把它通过 JNI 暴露给 Java：

```cpp
// native
JNIEXPORT jboolean JNICALL Java_..._WantTextInput(JNIEnv*, jclass) {
    return g_wantTextInput ? JNI_TRUE : JNI_FALSE;
}
```

```java
// java
public void pollTextInput() {
    boolean want = WantTextInput();
    if (want && !keyboardVisible) {
        syncClipboardToNative();
        showSoftKeyboard();
    } else if (!want && keyboardVisible) {
        hideSoftKeyboard();
    }
}
```

在 `onDrawFrame` 末尾调用一次：
```java
@Override
public void onDrawFrame(GL10 glUnused) {
    ...
    DrawFrame();
    pollTextInput();   // ← 每帧轮询，自动弹/收键盘
}
```

> 关键细节：**显示键盘前先同步 Android 剪贴板到 native**（用于 Ctrl+V 粘贴时直接拿到内容）

### 5. 软键盘显示 / 隐藏 —— 主线程 Handler

`InputMethodManager` 必须在主线程调用：

```java
public void showSoftKeyboard() {
    Handler handler = new Handler(Looper.getMainLooper());
    handler.post(() -> {
        hiddenInput.requestFocus();
        hiddenInput.setText("");   // 清空，避免下次触发 TextWatcher
        lastText = "";
        InputMethodManager imm = (InputMethodManager)
            getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        imm.showSoftInput(hiddenInput, InputMethodManager.SHOW_FORCED);
        keyboardVisible = true;
    });
}
```

> `SHOW_FORCED` 强制显示，避免有些设备在非 EditText 上下文拒绝弹键盘。

### 6. 物理键盘 / DPAD —— onKeyDown / onKeyUp

外接键盘、DPAD 摇杆走 `GLSurfaceView.onKeyDown/Up`，**不依赖 IME**：

```java
@Override
public boolean onKeyDown(int keyCode, KeyEvent event) {
    int mappedKey = mapAndroidKeycode(keyCode);
    KeyEvent(mappedKey, true);
    return super.onKeyDown(keyCode, event);
}
```

并加 `setFocusable(true) + setFocusableInTouchMode(true)` 让 Surface 自身能拿到焦点。

### 7. 剪贴板 —— 双向同步

**ImGui → Android：**
```cpp
static void ImGuiSetClipboardText(void*, const char* text) {
    // 缓存到 native（GetClipboardText 直接返回）
    snprintf(g_clipboardBuf, sizeof(g_clipboardBuf), "%s", text);
    g_clipboardText = text;
    
    // 同时回写 Android 系统剪贴板
    if (g_jvm) {
        JNIEnv* env;
        g_jvm->AttachCurrentThread(&env, nullptr);
        jclass cls = env->FindClass("com/uranalysis/app/Surface");
        jmethodID mid = env->GetStaticMethodID(cls, "nativeSetClipboard", "(Ljava/lang/String;)V");
        env->CallStaticVoidMethod(cls, mid, env->NewStringUTF(text));
    }
}
```

**Android → ImGui（弹出键盘前同步）：**
```java
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
```

注册到 ImGui：
```cpp
io.SetClipboardTextFn = ImGuiSetClipboardText;
io.GetClipboardTextFn  = ImGuiGetClipboardText;
```

### 8. 触摸事件 —— 一行转发

```java
@Override
public boolean onTouchEvent(MotionEvent e) {
    MotionEventClick(action != MotionEvent.ACTION_UP, e.getX(), e.getY(), e.getAction());
    return true;
}
```

```cpp
JNIEXPORT void JNICALL Java_..._MotionEventClick(
    JNIEnv*, jclass, jboolean down, jfloat x, jfloat y, jint action) {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[0] = down;
    io.MousePos = ImVec2(x, y);
}
```

---

## 可直接迁移到 MiniDA 的部分

| 模块 | MiniDA 现状 | 是否要迁移 | 改造点 |
|---|---|---|---|
| 软键盘 | 没有 | ✅ 必加 | 复制 `hiddenInput` + TextWatcher + OnKeyListener |
| 物理键 / DPAD | `MotionEventClick` | ✅ 补全 | 加上 `onKeyDown/Up` → `KeyEvent` |
| 剪贴板 | 没有 | ✅ 建议 | 接入 `ClipboardManager` + `io.SetClipboardTextFn` |
| Keycode 映射 | `MotionEventClick` 中 | ✅ 重构 | 拆成 `AndroidKey → ID → ImGuiKey` 两层 |
| WantTextInput 轮询 | 无 | ✅ 必加 | 暴露 JNI，每帧 `pollTextInput()` |
| 焦点 / Touch 转 ImGui | 有 | ⚠️ 小改 | `setFocusable(true)` 以接收键盘事件 |

---

## MiniDA 落地清单（建议）

1. **Java 层（`Surface.java`）**
   - 增加 `hiddenInput` + `container` 字段
   - 增加 `createHiddenEditText()`、`mapAndroidKeycode()`、`showSoftKeyboard()` / `hideSoftKeyboard()`
   - 增加 `pollTextInput()`，在 `onDrawFrame` 末尾调用
   - 实现 `nativeSetClipboard()`、`syncClipboardToNative()`
   - 实现 `onKeyDown()` / `onKeyUp()`
   - 加载库名从 `"imgui"` 改为 `"minida"`

2. **MainActivity（`MainActivity.java`）**
   - 创建 `FrameLayout` root，挂 Surface + `inputContainer`
   - 切换到全屏沉浸模式（同 Uranalysis）

3. **Native 层（`Main.cpp`）**
   - 新增 JNI 函数：
     - `Java_..._KeyEvent`
     - `Java_..._CharEvent`
     - `Java_..._TextInput`
     - `Java_..._WantTextInput`
     - `Java_..._SetClipboardText`
     - `Java_..._GetClipboardText`
   - 新增 `AndroidKeyToImGuiKey()`
   - 在 `ImGui_ImplOpenGL3_Init` 后挂接 `io.SetClipboardTextFn / GetClipboardTextFn`
   - 每帧末 `g_wantTextInput = io.WantTextInput;`

4. **CMake / `Android.mk`**
   - 保持现状，无需修改

---

## 关键经验 & 坑

1. **不能在子线程弹/收软键盘**，必须 `Handler(Looper.getMainLooper()).post(...)`。
2. **`hiddenInput` 必须 add 到 `Activity` 的 `View` 树**（`rootLayout.addView(inputContainer)`），否则 IME 收不到焦点。
3. **TextWatcher 只关心"新增"字符**——比较 `newText.length()` 与 `lastText.length()`，不然 `setText("")` 也会当成一次"删除全部"误报。
4. **回车键** 走 `OnEditorActionListener`，**不要**用 KeyListener（部分设备 IME 的回车不触发 KeyEvent）。
5. **Backspace 也要走 KeyEvent**——TextWatcher 拿不到"删除"语义。
6. **同步剪贴板要发生在 `showSoftKeyboard` 之前**，否则 Ctrl+V 粘贴的是旧值。
7. **`SHOW_FORCED`** 而不是 `SHOW_IMPLICIT`——ImGui 焦点不在原生 View 上，必须强制。
8. **JavaVM 保存到 global**：跨线程访问需要 `AttachCurrentThread` / `DetachCurrentThread`。
9. **JNI 找不到类的两种可能**：包名写错、ProGuard 优化掉了 `nativeSetClipboard` —— 加 `-keepclasseswithmembernames` 规则。
10. **JVM attach 必须配对 detach**，否则线程泄漏；本例是在 ImGui SetClipboard 回调里做，要小心频次。

---

## 一句话总结

> Uranalysis 用 **"1×1 透明 EditText" + 两层 Keycode 映射 + WantTextInput 轮询** 三件套，把 Android IME 桥接进 ImGui，是目前 Android 上 ImGui 输入法最成熟的开源方案。
