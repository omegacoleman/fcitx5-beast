# fcitx5-webserver

A plugin for interacting with fcitx5 via HTTP and Websocket

## usage

### 1. invoke controller methods

Supported controller methods:

* `current_input_method`

e.g.
```bash
curl -sS --unix-socket /tmp/fcitx5.sock http://fcitx/controller/current_input_method | jq
```
```json
{
  "input_method": "keyboard-us"
}
```

### 2. subscribe specific events

Subscribe and listen for specific events via `/subscribe`

This is a websocket interface, so you need tools like [websocat](https://github.com/vi/websocat)

Use a `'+'` splitted section for events to subscribe, e.g.

```bash
websocat \
  --ws-c-uri=ws:/fcitx/subscribe/input_context_focus_in+input_context_focus_out \
  --text \
  ws-c:unix:/tmp/fcitx5.sock -
```

Now move your cursor between input boxes, it will output:

```json
{"event":"input_context_focus_in","params":{"frontend":"xim","program":"xterm","uuid":"643ef6e072f04e6982af435f3d968852"}}
{"event":"input_context_focus_out","params":{"frontend":"xim","program":"xterm","uuid":"643ef6e072f04e6982af435f3d968852"}}
```

Supported events:

* `input_context_focus_in`
* `input_context_focus_out`
* `input_context_focus_switch_input_method`

### 3. get/set config via HTTP

#### Get config

* `GET /config/global` for global config
* `GET /config/addon/...` for addon config
* `GET /config/inputmethod/...` for IM config

e.g.
```
curl -sS --unix-socket /tmp/fcitx5.sock http://fcitx/config/addon/webserver | jq
```

#### Set config

Change method to POST for setting config.

e.g.
```
curl -sS --unix-socket /tmp/fcitx5.sock http://fcitx/config/addon/webserver \
  -X POST\
  -d '{"Tcp": {"Port": 12345}}'
```

## roadmap

1. Add unit tests
2. Support more controller methods
3. Support more fcitx events
4. Document more details about json format
5. Implement input contexts backed by websocket
6. Supplement po files

## misc

Powered by[Boost.Beast](https://github.com/boostorg/beast)

Forked from [fcitx5-beast](https://github.com/fcitx-contrib/fcitx5-beast)

