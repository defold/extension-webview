// Exercise the production event queue and delegate without creating a browser window.
#include <assert.h>
#include <stdio.h>
#include <functional>
#include <atomic>
#include <thread>
#include "../webview/src/webview_darwin.mm"

static int g_Delivered;
static std::function<void()> g_OnCallback;

namespace dmGraphics {
id GetNativeOSXNSWindow() { return nil; }
id GetNativeOSXNSView() { return nil; }
}

namespace dmWebView {
void ClearWebViewInfo(WebViewInfo* info) { info->m_Callback = 0; }
void RunCallback(CallbackInfo* event)
{
    assert(event->m_WebViewID == 0);
    assert(event->m_Generation == g_WebView.m_Info[0].m_Generation);
    assert(strcmp(event->m_Result, "payload") == 0);
    ++g_Delivered;
    if (g_OnCallback) g_OnCallback();
}
}

static uint64_t NewView()
{
    static uint64_t generation = 0;
    g_WebView.m_Info[0].m_Callback = (dmScript::LuaCallbackInfo*)1;
    g_WebView.m_Info[0].m_Generation = ++generation;
    return generation;
}

static void QueueEvent(uint64_t generation, int view_id = 0)
{
    Command command;
    command.m_Type = CMD_EVAL_OK;
    command.m_Generation = generation;
    command.m_WebViewID = view_id;
    command.m_Url = strdup("https://example.invalid/");
    command.m_Data = strdup("payload");
    QueueCommand(&command);
}

// Verifies the queue rejects destroyed/reused slots and malformed IDs before dispatch.
static void TestQueuedEvents()
{
    uint64_t old_generation = NewView();
    QueueEvent(old_generation);
    dmWebView::ClearWebViewInfo(&g_WebView.m_Info[0]);
    dmWebView::Platform_Update(0);
    assert(g_Delivered == 0);

    uint64_t generation = NewView();
    QueueEvent(old_generation);
    QueueEvent(generation, -1);
    QueueEvent(generation, dmWebView::MAX_NUM_WEBVIEWS);
    QueueEvent(generation);
    dmWebView::Platform_Update(0);
    assert(g_Delivered == 1);

    // The first callback invalidates commands already swapped into the update's local batch.
    g_Delivered = 0;
    g_OnCallback = [] { NewView(); };
    QueueEvent(generation);
    QueueEvent(generation);
    dmWebView::Platform_Update(0);
    assert(g_Delivered == 1);
    g_OnCallback = {};
}

// Verifies stale direct delegates are ignored and their navigation decision is cancelled.
static void TestStaleDelegate()
{
    uint64_t old_generation = NewView();
    WebViewDelegate* delegate = [[WebViewDelegate alloc] init];
    delegate->m_WebViewID = 0;
    delegate->m_Generation = old_generation;
    NewView();
    g_Delivered = 0;
    // Stale delegates must return before accessing these browser/action arguments.
    WKWebView* unused_view = nil;
    WKNavigationAction* unused_action = nil;
    NSError* unused_error = nil;
    [delegate webView:unused_view didFinishNavigation:nil];
    [delegate webView:unused_view didFailNavigation:nil withError:unused_error];
    assert(g_Delivered == 0);

    __block int decisions = 0;
    [delegate webView:unused_view decidePolicyForNavigationAction:unused_action decisionHandler:^(WKNavigationActionPolicy policy) {
        assert(policy == WKNavigationActionPolicyCancel);
        ++decisions;
    }];
    assert(decisions == 1);
    [delegate release];
}

// Verifies destroying a view resolves its pending decision exactly once.
static void TestPendingDecision()
{
    uint64_t generation = NewView();
    WebViewDelegate* delegate = [[WebViewDelegate alloc] init];
    delegate->m_WebViewID = 0;
    delegate->m_Generation = generation;
    delegate->m_PendingUrl = [@"https://example.invalid/" copy];
    __block int decisions = 0;
    delegate->m_DecisionHandler = [^(WKNavigationActionPolicy policy) {
        assert(policy == WKNavigationActionPolicyCancel);
        ++decisions;
    } copy];
    g_WebView.m_WebViewDelegates[0] = delegate;
    assert(dmWebView::Platform_Destroy(0, 0) == 0);
    assert(decisions == 1);
    assert(!g_WebView.m_Info[0].m_Callback && !g_WebView.m_WebViewDelegates[0]);
}

// Verifies queue reads and writes remain synchronized while update drains events.
static void TestConcurrentUpdate()
{
    uint64_t generation = NewView();
    std::atomic<bool> finished(false);
    g_Delivered = 0;
    std::thread producer([&] {
        for (int i = 0; i < 1000; ++i) QueueEvent(generation);
        finished.store(true);
    });
    while (!finished.load()) dmWebView::Platform_Update(0);
    producer.join();
    dmWebView::Platform_Update(0);
    assert(g_Delivered == 1000);
}

// Verifies pending and concurrently arriving events are discarded during shutdown.
static void TestShutdown()
{
    uint64_t generation = NewView();
    QueueEvent(generation);
    std::atomic<bool> start(false);
    std::thread producer([&] {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 1000; ++i) QueueEvent(generation);
    });
    start.store(true);
    dmWebView::Platform_Finalize(0);
    producer.join();
    assert(g_WebView.m_CmdQueue.Empty());
    assert(!g_WebView.m_AcceptCommands);

    dmMutex::HMutex mutex = g_WebView.m_Mutex;
    dmWebView::Platform_AppFinalize(0);
    QueueEvent(generation);
    assert(g_WebView.m_Mutex == mutex);
    assert(g_WebView.m_CmdQueue.Empty());

    // Reopening the queue must still reject results from the previous lifetime.
    dmWebView::Platform_AppInitialize(0);
    dmWebView::Platform_Initialize(0);
    assert(g_WebView.m_Mutex == mutex);
    uint64_t current = NewView();
    g_Delivered = 0;
    QueueEvent(generation);
    QueueEvent(current);
    dmWebView::Platform_Update(0);
    assert(g_Delivered == 1);
    dmWebView::ClearWebViewInfo(&g_WebView.m_Info[0]);
}

int main()
{
    @autoreleasepool
    {
        QueueEvent(1);
        assert(g_WebView.m_CmdQueue.Empty());
        dmWebView::Platform_AppInitialize(0);
        dmWebView::Platform_Initialize(0);
        TestQueuedEvents();
        TestStaleDelegate();
        TestPendingDecision();
        TestConcurrentUpdate();
        TestShutdown();
        dmWebView::Platform_Finalize(0);
        dmWebView::Platform_AppFinalize(0);
    }
    puts("Darwin event queue and delegate lifecycle tests passed.");
    return 0;
}
