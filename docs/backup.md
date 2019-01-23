## Webview API documentation
Functions and for creating and controlling webviews to show html pages or
evaluate javascript. These API:s only exist on mobile platforms.

### `webview.create`
Creates a webview instance. It can show HTML pages as well as evaluate
Javascript. The view remains hidden until the first call. There can exist a
maximum of 4 webviews at the same time.

[icon:ios] On iOS, the callback will never get a
`webview.CALLBACK_RESULT_EVAL_ERROR,` due to the iOS SDK implementation.

@param callback [type:function(self, webview_id, request_id, type, data)]
A callback which receives info about finished requests taking the following parameters

`self`
: [type:object] The calling script

`webview_id`
: [type:number] The webview id

`request_id`
: [type:number] The request id

`type`
: [type:number] The type of the callback. Can be one of these:

- `webview.CALLBACK_RESULT_URL_OK`
- `webview.CALLBACK_RESULT_URL_ERROR`
- `webview.CALLBACK_RESULT_URL_LOADING`
- `webview.CALLBACK_RESULT_EVAL_OK`
- `webview.CALLBACK_RESULT_EVAL_ERROR`

`data`
: [type:table] A table holding the data. The table has these fields:

- [type:string] `url`: The url used in the webview.open() call. `nil` otherwise.
- [type:string] `result`: Holds the result of either: a failed url open, a successful eval request or a failed eval. `nil` otherwise

@return id [type:number] The id number of the webview

@examples

```lua
local function webview_callback(self, webview_id, request_id, type, data)
    if type == webview.CALLBACK_RESULT_URL_OK then
        -- the page is now loaded, let's show it
        webview.set_visible(webview_id, 1)
    elseif type == webview.CALLBACK_RESULT_URL_ERROR then
        print("Failed to load url: " .. data["url"])
        print("Error: " .. data["error"])
    elseif type == webview.CALLBACK_RESULT_URL_LOADING then
        -- a page is loading
        -- return false to prevent it from loading
        -- return true or nil to continue loading the page
        if data.url ~= "https://www.defold.com/" then
            return false
        end
    elseif type == webview.CALLBACK_RESULT_EVAL_OK then
        print("Eval ok. Result: " .. data['result'])
    elseif type == webview.CALLBACK_RESULT_EVAL_ERROR then
        print("Eval not ok. Request # " .. request_id)
    end
end

local webview_id = webview.create(webview_callback)
```

###
