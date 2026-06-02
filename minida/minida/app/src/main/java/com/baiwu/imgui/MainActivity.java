package com.baiwu.imgui;
 




import android.app.Activity;
import android.os.Bundle;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.view.MotionEvent;

public class MainActivity extends Activity {

    
    private Surface surfaceView;
    private long nativeHandle = 0;
    private Thread renderThread;
    private volatile boolean rendering = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 设置全屏
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );

        // 创建 SurfaceView 用于 OpenGL 渲染
        surfaceView = new Surface(this);
       setFullScreenImmersive();
        
        setContentView(surfaceView);
    }
   private void setFullScreenImmersive() {
        // 获取 DecorView
        View decorView = getWindow().getDecorView();

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.KITKAT) {
            // 隐藏状态栏和导航栏，并且启用沉浸式（滑动边缘可临时显示，之后自动隐藏）
            int uiOptions = View.SYSTEM_UI_FLAG_FULLSCREEN          // 隐藏状态栏
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION         // 隐藏导航栏
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY        // 沉浸式（滑动边缘时临时显示，几秒后自动隐藏）
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN       // 布局延伸到状态栏区域
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION  // 布局延伸到导航栏区域
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;          // 保持布局稳定

            decorView.setSystemUiVisibility(uiOptions);
        } else {
            // 低版本使用传统全屏
            requestWindowFeature(Window.FEATURE_NO_TITLE);
            getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                                 WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }
    }
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getAction();

        switch (action) {
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_UP:
                surfaceView.MotionEventClick(action != MotionEvent.ACTION_UP, event.getX(), event.getY(),action);
             
                break;
            default:
                break;
        }
        return super.onTouchEvent(event);
    }


}

