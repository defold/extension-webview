package com.defold.webview;

import android.app.Activity;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewGroup.LayoutParams;
import android.view.ViewGroup.MarginLayoutParams;
import android.view.Window;
import android.view.WindowManager;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.LinearLayout;
import android.webkit.ConsoleMessage;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebSettings;

import android.graphics.PixelFormat;
import android.graphics.Color;

import android.util.Log;


public class WebViewJNI {

    public static final String PREFERENCES_FILE = "webview";
    public static final String TAG = "defold.webview";
    public static final String JS_NAMESPACE = "defold";

    private Activity activity;
    private static WebViewInfo[] infos;
    private boolean immersiveMode = false;
    private boolean displayCutout = false;

    public native void onPageLoading(String url, int webview_id, int request_id);
    public native void onPageFinished(String url, int webview_id, int request_id);
    public native void onReceivedError(String url, int webview_id, int request_id, String errorMessage);
    public native void onEvalFinished(String result, int webview_id, int request_id);
    public native void onEvalFailed(String errorMessage, int webview_id, int request_id);

    private static class CustomWebViewClient extends WebViewClient {
        public Activity activity;
        public int webviewID;
        public int requestID;
        private String continueLoadingUrl;
        private WebViewJNI webviewJNI;
        private String PACKAGE_NAME;

        // This is a hack to counter for the case where the load() experiences an error
        // In that case, the onReceivedError will be called, and onPageFinished will be called TWICE
        // This guard variable helps to avoid propagating the onPageFinished calls in that case
        private boolean hasError;

        public CustomWebViewClient(Activity activity, WebViewJNI webviewJNI, int webview_id) {
            super();
            this.activity = activity;
            this.webviewJNI = webviewJNI;
            this.webviewID = webview_id;
            PACKAGE_NAME = activity.getApplicationContext().getPackageName();
            reset(-1);
        }

        private String trimTrailingSlash(String url) {
            if (url.endsWith("/")) {
                url = url.substring(0, url.length() - 1);
            }
            return url;
        }

        public void reset(int request_id)
        {
            this.requestID = request_id;
            this.hasError = false;
            this.continueLoadingUrl = null;
        }

        // store a url that should be allowed to load
        public void setContinueLoadingUrl(String url) {
            this.continueLoadingUrl = url;
        }

        // check if a URL has been set as ok to load by the user
        // (by returning true in the CALLBACK_RESULT_URL_LOADING callback)
        // note that we trim trailing slashes since a call to load https://www.google.com
        // will end up as a https://www.google.com/ in the methods we overload below
        private boolean shouldContinueLoadingUrl(String url) {
            if (continueLoadingUrl == null) return false;
            return trimTrailingSlash(continueLoadingUrl).equals(trimTrailingSlash(url));
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, String url) {
            if( url.startsWith(PACKAGE_NAME) )
            {
                // Try to find an app that can open the url scheme,
                // otherwise continue as usual error.
                Intent intent = new Intent(Intent.ACTION_VIEW);
                intent.setData(Uri.parse(url));
                PackageManager packageManager = activity.getPackageManager();
                if (intent.resolveActivity(packageManager) != null) {
                    activity.startActivity(intent);
                    return true;
                }
            }

            // should we continue to load this page?
            // the continueLoadingUrl value is set as a result of a call to
            // webviewJNI.onPageLoading (see below) which will let the client
            // either allow or block the page from loading
            if( shouldContinueLoadingUrl(url) ) {
                return false;
            }
            // block the page from loading and ask the client if it should load
            // or not
            webviewJNI.onPageLoading(url, webviewID, requestID);
            return true;
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            // NOTE! this callback will be called TWICE for errors, see comment above
            // NOTE! this callback will be called once when initially blocked in
            // shouldOverrideUrlLoading and then once more if allowed to load
            if (!this.hasError && shouldContinueLoadingUrl(url)) {
                continueLoadingUrl = null;
                webviewJNI.onPageFinished(url, webviewID, requestID);
            }
        }

        @Override
        public void onReceivedError(WebView view, int errorCode, String description, String failingUrl) {
            if (errorCode == WebViewClient.ERROR_UNSUPPORTED_SCHEME) {
                // Try to find an app that can open the url scheme,
                // otherwise continue as usual error.
                Intent intent = new Intent(Intent.ACTION_VIEW);
                intent.setData(Uri.parse(failingUrl));
                PackageManager packageManager = activity.getPackageManager();
                if (intent.resolveActivity(packageManager) != null) {
                    activity.startActivity(intent);
                    return;
                }
            }

            if (!this.hasError) {
                this.hasError = true;
                webviewJNI.onReceivedError(failingUrl, webviewID, requestID, description);
            }
        }

        @JavascriptInterface
        public void returnResultToJava(String result) {
            webviewJNI.onEvalFinished(result, webviewID, requestID);
        }
    }

    private static class CustomWebChromeClient extends WebChromeClient {
        private WebViewJNI webviewJNI;
        private int webviewID;
        private int requestID;

        public CustomWebChromeClient(WebViewJNI webviewJNI, int webview_id) {
            this.webviewJNI = webviewJNI;
            this.webviewID = webview_id;
            reset(-1);
        }

        public void reset(int request_id)
        {
            this.requestID = request_id;
        }

        @Override
        public boolean onConsoleMessage(ConsoleMessage msg) {
            if( msg.messageLevel() == ConsoleMessage.MessageLevel.ERROR )
            {
                webviewJNI.onEvalFailed(String.format("js:%d: %s", msg.lineNumber(), msg.message()), webviewID, requestID);
                return true;
            }
            return false;
        }
    }

    private class WebViewInfo
    {
        WebView                     webview;
        CustomWebViewClient         webviewClient;
        CustomWebChromeClient       webviewChromeClient;
        LinearLayout                layout;
        WindowManager.LayoutParams  windowParams;
        int                         first;
        int                         webviewID;
    };

    public WebViewJNI(Activity activity, int maxnumviews, boolean immersiveMode, boolean displayCutout) {
        this.activity = activity;
        this.infos = new WebViewInfo[maxnumviews];
        this.immersiveMode = immersiveMode;
        this.displayCutout = displayCutout;
    }

    private WebViewInfo createView(Activity activity, int webview_id)
    {
        WebViewInfo info = new WebViewInfo();
        info.webviewID = webview_id;
        info.webview = new WebView(activity);
        info.webview.setVisibility(View.GONE);

        MarginLayoutParams params = new MarginLayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.MATCH_PARENT);
        params.setMargins(0, 0, 0, 0);

        LinearLayout layout = new LinearLayout(activity);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.addView(info.webview, params);

        //Fixes 2.3 bug where the keyboard is not being shown when the user focus on an input/textarea.
        info.webview.setFocusable(true);
        info.webview.setFocusableInTouchMode(true);
        info.webview.setClickable(true);
        info.webview.requestFocus(View.FOCUS_DOWN);

        info.webview.setHapticFeedbackEnabled(true);

        info.webview.requestFocusFromTouch();

        info.webviewChromeClient = new CustomWebChromeClient(WebViewJNI.this, webview_id);
        info.webview.setWebChromeClient(info.webviewChromeClient);

        info.webviewClient = new CustomWebViewClient(activity, WebViewJNI.this, webview_id);
        info.webview.setWebViewClient(info.webviewClient);

        WebSettings webSettings = info.webview.getSettings();
        webSettings.setJavaScriptEnabled(true);
        webSettings.setAllowFileAccessFromFileURLs(true);

        info.webview.addJavascriptInterface(info.webviewClient, JS_NAMESPACE);

        info.layout = layout;
        info.first = 1;

        info.windowParams = new WindowManager.LayoutParams();
        info.windowParams.gravity = Gravity.TOP | Gravity.LEFT;
        info.windowParams.x = WindowManager.LayoutParams.MATCH_PARENT;
        info.windowParams.y = WindowManager.LayoutParams.MATCH_PARENT;
        info.windowParams.width = WindowManager.LayoutParams.MATCH_PARENT;
        info.windowParams.height = WindowManager.LayoutParams.MATCH_PARENT;
        info.windowParams.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE; // Fix navigation bar visible briefly when hiding/showing
        info.windowParams.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING;
        info.windowParams.format = PixelFormat.TRANSLUCENT; // To be able to make webview transparent
        if (Build.VERSION.SDK_INT < 30) {
            if (immersiveMode) {
                info.windowParams.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                                  | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                                  | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                                  | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                                  | View.SYSTEM_UI_FLAG_FULLSCREEN
                                                  | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            } else {
                info.windowParams.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                                  // | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                                  | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                                  // | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                                  | View.SYSTEM_UI_FLAG_FULLSCREEN
                                                  | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            }
        }
        if (displayCutout && Build.VERSION.SDK_INT >= 28) {
            info.windowParams.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        info.layout.setLayoutParams(info.windowParams);
        return info;
    }

    private void setTransparentInternal(WebViewInfo info, final int transparent)
    {
        info.webview.setBackgroundColor((transparent == 1) ? Color.TRANSPARENT : Color.WHITE);
    }

    private void setVisibleInternal(WebViewInfo info, int visible)
    {
        info.webview.setVisibility((visible != 0) ? View.VISIBLE : View.GONE);
        info.layout.setVisibility((visible != 0) ? View.VISIBLE : View.GONE);
        if( visible != 0 && info.first == 1 )
        {
            info.first = 0;
            WindowManager wm = activity.getWindowManager();
            wm.addView(info.layout, info.windowParams);

            // Fix navigation bar visible briefly when hiding/showing
            info.windowParams.flags = WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
            wm.updateViewLayout(info.layout, info.windowParams);

            if (Build.VERSION.SDK_INT >= 30) {
                WindowInsetsController windowInsetsController = info.layout.getWindowInsetsController();
                windowInsetsController.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                if (immersiveMode) {
                    windowInsetsController.hide(WindowInsets.Type.systemBars());
                } else {
                    windowInsetsController.hide(WindowInsets.Type.statusBars());
                }
            }
        }
    }

    private void setPositionInternal(WebViewInfo info, int x, int y, int width, int height)
    {
        info.windowParams.x = x;
        info.windowParams.y = y;
        info.windowParams.width = width >= 0 ? width : WindowManager.LayoutParams.MATCH_PARENT;
        info.windowParams.height = height >= 0 ? height : WindowManager.LayoutParams.MATCH_PARENT;

        if (info.webview.getVisibility() == View.VISIBLE) {
            WindowManager wm = activity.getWindowManager();
            wm.updateViewLayout(info.layout, info.windowParams);
        }
    }

    public void create(final int webview_id) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                WebViewJNI.this.infos[webview_id] = createView(WebViewJNI.this.activity, webview_id);
            }
        });
    }

    public void destroy(final int webview_id) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if( WebViewJNI.this.infos[webview_id] != null )
                {
                    if( WebViewJNI.this.infos[webview_id].layout != null )
                    {
                        WindowManager wm = activity.getWindowManager();
                        wm.removeView(WebViewJNI.this.infos[webview_id].layout);
                    }
                    WebViewJNI.this.infos[webview_id].layout = null;
                    WebViewJNI.this.infos[webview_id].webview = null;
                    WebViewJNI.this.infos[webview_id] = null;
                }
            }
        });
    }

    public void loadRaw(final String html, final int webview_id, final int request_id, final int hidden, final int transparent) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                WebViewJNI.this.infos[webview_id].webviewClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewChromeClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewClient.setContinueLoadingUrl("file:///android_res/");
                WebViewJNI.this.infos[webview_id].webview.loadDataWithBaseURL("file:///android_res/", html, "text/html", "utf-8", null);
                setVisibleInternal(WebViewJNI.this.infos[webview_id], hidden != 0 ? 0 : 1);
                setTransparentInternal(WebViewJNI.this.infos[webview_id], transparent);
            }
        });
    }

    public void load(final String url, final int webview_id, final int request_id, final int hidden, final int transparent) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                WebViewJNI.this.infos[webview_id].webviewClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewChromeClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewClient.setContinueLoadingUrl(url);
                WebViewJNI.this.infos[webview_id].webview.loadUrl(url);
                setVisibleInternal(WebViewJNI.this.infos[webview_id], hidden != 0 ? 0 : 1);
                setTransparentInternal(WebViewJNI.this.infos[webview_id], transparent);
            }
        });
    }

    public void continueLoading(final String url, final int webview_id, final int request_id) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                WebViewJNI.this.infos[webview_id].webviewClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewChromeClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewClient.setContinueLoadingUrl(url);
                WebViewJNI.this.infos[webview_id].webview.loadUrl(url);
            }
        });
    }

    public void eval(final String code, final int webview_id, final int request_id) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                WebViewJNI.this.infos[webview_id].webviewClient.reset(request_id);
                WebViewJNI.this.infos[webview_id].webviewChromeClient.reset(request_id);
                String javascript = String.format("javascript:%s.returnResultToJava(eval(\"%s\"))", JS_NAMESPACE, JSONValue.escape(code));
                WebViewJNI.this.infos[webview_id].webview.loadUrl(javascript);
            }
        });
    }

    public void setVisible(final int webview_id, final int visible) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                setVisibleInternal(WebViewJNI.this.infos[webview_id], visible);
            }
        });
    }

    public int isVisible(final int webview_id) {
        int visible = this.infos[webview_id].webview.isShown() ? 1 : 0;
        return visible;
    }

    public void setPosition(final int webview_id, final int x, final int y, final int width, final int height) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                setPositionInternal(WebViewJNI.this.infos[webview_id], x, y, width, height);
            }
        });
    }

    public void setTransparent(final int webview_id, final int transparent) {
        this.activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                setTransparentInternal(WebViewJNI.this.infos[webview_id], transparent);
            }
        });
    }
}
