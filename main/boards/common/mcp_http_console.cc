#include "mcp_http_console.h"

#include <esp_http_server.h>
#include <esp_log.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "board.h"
#include "mcp_server.h"

#define TAG "McpHttp"

namespace {

httpd_handle_t server = nullptr;

// JSON-RPC requests are small; tool results (base64 images) are not.
constexpr size_t kMaxRpcBody = 4096;
constexpr size_t kMaxSnapshotBytes = 256 * 1024;
constexpr size_t kMaxJobs = 32;

// A tools/call runs on the main task through Application::Schedule. Keep the
// response in a job so the HTTP task returns immediately: self.screen.snapshot
// posts back to this same server while its tool is still running, and a
// synchronous bridge would deadlock the single HTTP task.
struct RpcJob {
    std::mutex mutex;
    bool done = false;
    std::string payload;
};

std::mutex jobs_mutex;
std::map<int, std::shared_ptr<RpcJob>> jobs;

std::mutex& SnapshotMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string& SnapshotData() {
    static std::string data;
    return data;
}

esp_err_t SendJson(httpd_req_t* req, const char* status, const std::string& body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.c_str(), body.size());
}

bool ReadBody(httpd_req_t* req, std::string& body, size_t max_bytes) {
    if (req->content_len == 0 || req->content_len > max_bytes) {
        return false;
    }
    body.resize(req->content_len);
    size_t received = 0;
    while (received < body.size()) {
        int ret = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return false;
        }
        received += static_cast<size_t>(ret);
    }
    return true;
}

int QueryInt(httpd_req_t* req, const char* key, int fallback) {
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len > 255) {
        return fallback;
    }
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return fallback;
    }
    char value[16];
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return fallback;
    }
    return std::atoi(value);
}

// Accept only the shape McpServer::ParseMessage handles. Without this the
// handler would wait for a response that can never come.
bool IsValidRpc(const std::string& body, int& id) {
    CJsonUniquePtr json(cJSON_Parse(body.c_str()));
    if (json == nullptr) {
        return false;
    }
    auto version = cJSON_GetObjectItem(json.get(), "jsonrpc");
    auto method = cJSON_GetObjectItem(json.get(), "method");
    auto id_item = cJSON_GetObjectItem(json.get(), "id");
    if (!cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        return false;
    }
    if (!cJSON_IsString(method) || !cJSON_IsNumber(id_item)) {
        return false;
    }
    id = id_item->valueint;
    return true;
}

std::shared_ptr<RpcJob> AddJob(int id) {
    auto job = std::make_shared<RpcJob>();
    std::lock_guard<std::mutex> lock(jobs_mutex);
    if (jobs.size() >= kMaxJobs) {
        jobs.erase(jobs.begin());
    }
    jobs[id] = job;
    return job;
}

void RemoveJob(int id, const std::shared_ptr<RpcJob>& job) {
    std::lock_guard<std::mutex> lock(jobs_mutex);
    auto it = jobs.find(id);
    if (it != jobs.end() && it->second == job) {
        jobs.erase(it);
    }
}

esp_err_t McpHandler(httpd_req_t* req) {
    std::string body;
    if (!ReadBody(req, body, kMaxRpcBody)) {
        return SendJson(req, "413 Payload Too Large",
                        "{\"error\":\"request body must be 1..4096 bytes\"}");
    }

    int id = 0;
    if (!IsValidRpc(body, id)) {
        return SendJson(req, "400 Bad Request",
                        "{\"error\":\"expected JSON-RPC 2.0 with method and numeric id\"}");
    }

    auto job = AddJob(id);
    McpServer::GetInstance().ParseMessage(body, [job](const std::string& payload) {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->payload = payload;
        job->done = true;
    });

    std::string payload;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->done) {
            payload = std::move(job->payload);
        }
    }
    if (payload.empty()) {
        // The tool is running on the main task; the page polls for the result.
        return SendJson(req, "202 Accepted", "{\"pending\":true}");
    }
    RemoveJob(id, job);
    return SendJson(req, "200 OK", payload);
}

esp_err_t ResultHandler(httpd_req_t* req) {
    int id = QueryInt(req, "id", -1);
    if (id < 0) {
        return SendJson(req, "400 Bad Request", "{\"error\":\"missing numeric id\"}");
    }

    std::shared_ptr<RpcJob> job;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(id);
        if (it == jobs.end()) {
            return SendJson(req, "404 Not Found", "{\"error\":\"unknown or expired id\"}");
        }
        job = it->second;
    }

    std::string payload;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (!job->done) {
            httpd_resp_set_status(req, "204 No Content");
            httpd_resp_set_hdr(req, "Cache-Control", "no-store");
            return httpd_resp_send(req, "", 0);
        }
        payload = std::move(job->payload);
    }
    RemoveJob(id, job);
    return SendJson(req, "200 OK", payload);
}

// self.screen.snapshot uploads multipart/form-data to a URL it takes as an
// argument. Pointing that argument here makes the round trip testable from the
// console page: the JPEG part is stored and served by /snapshot.jpg.
esp_err_t SnapshotUploadHandler(httpd_req_t* req) {
    std::string body;
    if (!ReadBody(req, body, kMaxSnapshotBytes)) {
        return SendJson(req, "413 Payload Too Large",
                        "{\"error\":\"snapshot body missing or larger than 256 KiB\"}");
    }

    std::string jpeg;
    auto start = body.find("\xFF\xD8\xFF");
    auto end = body.rfind("\xFF\xD9");
    if (start != std::string::npos && end != std::string::npos && end >= start + 2) {
        jpeg.assign(body, start, end + 2 - start);
    } else if (!body.empty()) {
        jpeg = std::move(body);
    } else {
        return SendJson(req, "400 Bad Request", "{\"error\":\"empty snapshot\"}");
    }

    size_t bytes = jpeg.size();
    {
        std::lock_guard<std::mutex> lock(SnapshotMutex());
        SnapshotData() = std::move(jpeg);
    }
    ESP_LOGI(TAG, "Stored screen snapshot: %u bytes", (unsigned)bytes);
    return SendJson(req, "200 OK", "{\"ok\":true}");
}

esp_err_t SnapshotGetHandler(httpd_req_t* req) {
    std::string copy;
    {
        std::lock_guard<std::mutex> lock(SnapshotMutex());
        copy = SnapshotData();
    }
    if (copy.empty()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No snapshot uploaded yet");
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, copy.data(), copy.size());
}

// The page may be opened by hostname; the robot itself may not resolve that
// name. Report the numeric address so snapshot URLs can point at it directly.
esp_err_t AddressHandler(httpd_req_t* req) {
    std::string ip;
    CJsonUniquePtr board_json(cJSON_Parse(Board::GetInstance().GetBoardJson().c_str()));
    if (board_json != nullptr) {
        auto item = cJSON_GetObjectItem(board_json.get(), "ip");
        if (cJSON_IsString(item)) {
            ip = item->valuestring;
        }
    }

    CJsonUniquePtr json(cJSON_CreateObject());
    if (json == nullptr) {
        return SendJson(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    std::string origin = ip.empty() ? "" : "http://" + ip + ":81";
    cJSON_AddStringToObject(json.get(), "ip", ip.c_str());
    cJSON_AddStringToObject(json.get(), "origin", origin.c_str());
    CJsonStringUniquePtr text(cJSON_PrintUnformatted(json.get()));
    if (text == nullptr) {
        return SendJson(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    return SendJson(req, "200 OK", text.get());
}

const char kConsoleHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Desk Bot 01 · MCP console</title>
<style>
 :root { color-scheme: dark; }
 * { box-sizing: border-box; }
 body { margin: 0; font-family: system-ui, -apple-system, sans-serif; background: #0f1115;
        color: #e6e9ef; }
 header { position: sticky; top: 0; z-index: 10; display: flex; gap: .75rem; align-items: center;
          flex-wrap: wrap; padding: .7rem 1rem; background: #161a22ee; backdrop-filter: blur(6px);
          border-bottom: 1px solid #262c38; }
 h1 { font-size: 1rem; margin: 0; font-weight: 600; }
 #status { font-size: .78rem; color: #9aa4b2; }
 #status.err { color: #fca5a5; }
 .spacer { flex: 1; }
 button { font: inherit; font-size: .85rem; padding: .45rem .8rem; border-radius: 8px;
          border: 1px solid #303848; background: #1e2530; color: #e6e9ef; cursor: pointer; }
 button:hover { background: #273040; }
 button:disabled { opacity: .45; cursor: default; }
 button.primary { background: #3b82f6; border-color: #3b82f6; color: #fff; }
 button.primary:hover { background: #2f6fda; }
 button.danger { background: #7f1d1d; border-color: #991b1b; color: #fee2e2; }
 label.chk { display: flex; gap: .35rem; align-items: center; font-size: .78rem; color: #9aa4b2; }
 main { padding: 1rem; display: grid; gap: 1rem; align-items: start;
        grid-template-columns: repeat(auto-fill, minmax(22rem, 1fr)); }
 h2.sec { grid-column: 1 / -1; font-size: .75rem; text-transform: uppercase;
          letter-spacing: .08em; color: #7c8798; margin: .6rem 0 -.5rem; }
 .card { background: #161a22; border: 1px solid #262c38; border-radius: 12px; padding: .85rem;
         display: flex; flex-direction: column; gap: .6rem; min-width: 0; }
 .card h3 { margin: 0; font-size: .9rem; font-weight: 600; display: flex; gap: .5rem;
            align-items: center; flex-wrap: wrap; }
 .card .name { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: .7rem;
               color: #8b96a8; font-weight: 400; }
 .badge { font-size: .6rem; padding: .1rem .4rem; border-radius: 999px; background: #3f2d12;
          color: #fbbf24; border: 1px solid #5a3f16; }
 .desc { font-size: .74rem; color: #9aa4b2; white-space: pre-line; display: -webkit-box;
         -webkit-line-clamp: 3; -webkit-box-orient: vertical; overflow: hidden; }
 .controls { display: flex; flex-direction: column; gap: .45rem; }
 .row { display: flex; align-items: center; gap: .5rem; }
 .row > label { flex: 0 0 6.5rem; font-size: .74rem; color: #9aa4b2; overflow: hidden;
                text-overflow: ellipsis; }
 .row input[type=range] { flex: 1; min-width: 0; accent-color: #3b82f6; }
 .row input[type=number], .row input[type=text], .row select, .row input[type=color] {
   flex: 1; min-width: 0; font: inherit; font-size: .8rem; padding: .35rem .5rem;
   border-radius: 7px; border: 1px solid #303848; background: #0f1115; color: #e6e9ef; }
 .row output { min-width: 3.2rem; text-align: right; font-variant-numeric: tabular-nums;
               font-size: .78rem; color: #cbd5e1; }
 .actions { display: flex; gap: .5rem; align-items: center; flex-wrap: wrap; }
 .result { font-size: .74rem; color: #cbd5e1; display: flex; flex-direction: column; gap: .4rem; }
 .result pre { margin: 0; white-space: pre-wrap; word-break: break-word; font-size: .7rem;
               background: #0f1115; border: 1px solid #262c38; border-radius: 8px; padding: .5rem;
               max-height: 14rem; overflow: auto; }
 .result img { max-width: 100%; border-radius: 8px; border: 1px solid #262c38; }
 .err { color: #fca5a5; }
 .ok { color: #86efac; }
 .mono { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
 .joy { height: 9rem; border-radius: 12px; position: relative; touch-action: none;
        user-select: none; border: 1px solid #303848;
        background: radial-gradient(circle at 50% 50%, #1b2230, #12161d); }
 .joy .knob { position: absolute; width: 2.6rem; height: 2.6rem; margin: -1.3rem 0 0 -1.3rem;
              border-radius: 50%; background: #3b82f6; box-shadow: 0 2px 10px #0008;
              left: 50%; top: 50%; pointer-events: none; }
 .joy .hint { position: absolute; inset: auto 0 .35rem 0; text-align: center;
              font-size: .64rem; color: #5f6b7c; pointer-events: none; }
 #stopFab { position: fixed; right: 1rem; bottom: 1rem; z-index: 20; padding: .8rem 1.1rem;
            border-radius: 999px; font-weight: 700; box-shadow: 0 6px 18px #000a; }
 #history { grid-column: 1 / -1; background: #161a22; border: 1px solid #262c38;
            border-radius: 12px; padding: .85rem; }
 #history h2 { margin: 0 0 .4rem; }
 #history .item { border-top: 1px solid #262c38; padding: .4rem 0; font-size: .73rem;
                  color: #9aa4b2; word-break: break-word; }
 dialog { background: #161a22; color: #e6e9ef; border: 1px solid #303848; border-radius: 12px;
          width: min(46rem, 92vw); }
 dialog::backdrop { background: #000a; }
 dialog textarea { width: 100%; min-height: 14rem; font-family: ui-monospace, Menlo, monospace;
                   font-size: .75rem; background: #0f1115; color: #e6e9ef;
                   border: 1px solid #303848; border-radius: 8px; padding: .5rem; }
 dialog pre { max-height: 16rem; overflow: auto; font-size: .72rem; background: #0f1115;
              border: 1px solid #262c38; border-radius: 8px; padding: .5rem; }
</style>
</head>
<body>
<header>
  <h1>Desk Bot 01 · MCP console</h1>
  <span id="status">loading tools…</span>
  <span class="spacer"></span>
  <label class="chk"><input type="checkbox" id="userTools" checked> user-only tools</label>
  <button id="refresh">Refresh</button>
  <button id="rawBtn">Raw JSON-RPC</button>
</header>
<main id="tools"></main>
<button id="stopFab" class="danger" title="self.motor.stop (Esc)">STOP</button>
<dialog id="rawDlg">
  <form method="dialog" style="display:flex;align-items:center;gap:.5rem;margin-bottom:.6rem">
    <b style="font-size:.9rem">Raw JSON-RPC</b><span class="spacer"></span><button>Close</button>
  </form>
  <textarea id="rawIn" spellcheck="false"></textarea>
  <div class="actions" style="margin:.6rem 0">
    <button id="rawSend" class="primary">Send</button>
  </div>
  <pre id="rawOut"></pre>
</dialog>
<script>
'use strict';
const $ = (sel, root = document) => root.querySelector(sel);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const state = { id: 1, tools: [], history: [], statusText: '',
                deviceOrigin: location.origin };
const GROUP_ORDER = ['Motion', 'Lights', 'Audio', 'Screen', 'Camera', 'Status', 'User only', 'Other'];
const DANGER = new Set(['self.reboot', 'self.upgrade_firmware']);

const els = {
  status: $('#status'), tools: $('#tools'), refresh: $('#refresh'), userTools: $('#userTools'),
  stop: $('#stopFab'), rawBtn: $('#rawBtn'), rawDlg: $('#rawDlg'), rawIn: $('#rawIn'),
  rawOut: $('#rawOut'), rawSend: $('#rawSend'),
};

let statusTimer = null;
function setStatus(text, ok = true, transient = false) {
  els.status.textContent = text;
  els.status.className = ok ? '' : 'err';
  clearTimeout(statusTimer);
  if (transient) {
    statusTimer = setTimeout(() => {
      els.status.textContent = state.statusText;
      els.status.className = '';
    }, 2500);
  }
}

function unwrap(json) {
  if (json.error) throw new Error(json.error.message + ' (code ' + json.error.code + ')');
  return json.result;
}

async function poll(id) {
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const res = await fetch('/mcp/result?id=' + id);
    if (res.status === 200) return unwrap(await res.json());
    if (res.status !== 204) {
      const text = await res.text();
      throw new Error('result for id ' + id + ' expired: ' + text.slice(0, 120));
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error('no result after 60 s (the tool may still be running on the device)');
}

async function rpc(method, params) {
  const id = state.id++;
  const msg = { jsonrpc: '2.0', id, method };
  if (params !== undefined) msg.params = params;
  const res = await fetch('/mcp', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(msg),
  });
  if (res.status === 200) return unwrap(await res.json());
  if (res.status === 202) return poll(id);
  const text = await res.text();
  throw new Error('HTTP ' + res.status + ': ' + text.slice(0, 160));
}

function pretty(text) {
  try { return JSON.stringify(JSON.parse(text), null, 2); } catch { return text; }
}

function groupOf(tool) {
  const name = tool.name;
  if ((tool.annotations && (tool.annotations.audience || []).includes('user')) ||
      tool.userOnly) return 'User only';
  if (name.startsWith('self.motor.') || name.startsWith('self.car.')) return 'Motion';
  if (name.startsWith('self.lamp.') || name.startsWith('self.rgb_led.')) return 'Lights';
  if (name.startsWith('self.audio')) return 'Audio';
  if (name.startsWith('self.screen')) return 'Screen';
  if (name.startsWith('self.camera')) return 'Camera';
  if (name.startsWith('self.get') || name.startsWith('self.assets')) return 'Status';
  return 'Other';
}

function titleOf(name) {
  const last = name.split('.').pop().replace(/_/g, ' ');
  return last.charAt(0).toUpperCase() + last.slice(1);
}

function defaultString(toolName, name) {
  if (name !== 'url') return '';
  if (toolName === 'self.screen.snapshot') return state.deviceOrigin + '/snapshot';
  if (toolName === 'self.screen.preview_image') return state.deviceOrigin + '/snapshot.jpg';
  return '';
}

async function loadAddress() {
  try {
    const res = await fetch('/address');
    if (!res.ok) return;
    const info = await res.json();
    if (info.origin) state.deviceOrigin = info.origin;
  } catch { /* keep location.origin */ }
}

function selectField(options, value) {
  const el = document.createElement('select');
  for (const option of options) {
    const item = document.createElement('option');
    item.value = option;
    item.textContent = option;
    el.append(item);
  }
  el.value = value;
  return el;
}

function makeField(toolName, name, prop, required) {
  const row = document.createElement('div');
  row.className = 'row';
  const label = document.createElement('label');
  label.textContent = name + (required ? ' *' : '');
  label.title = name;
  row.append(label);

  let get;
  let set;
  if (prop.type === 'boolean') {
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = prop.default === true;
    get = () => box.checked;
    set = (v) => { box.checked = !!v; };
    row.append(box);
  } else if (prop.type === 'integer' && prop.minimum !== undefined && prop.maximum !== undefined) {
    const range = document.createElement('input');
    range.type = 'range';
    range.min = prop.minimum;
    range.max = prop.maximum;
    range.step = 1;
    range.value = prop.default ?? prop.minimum;
    const out = document.createElement('output');
    out.textContent = range.value;
    range.oninput = () => { out.textContent = range.value; };
    get = () => parseInt(range.value, 10);
    set = (v) => {
      range.value = String(Math.min(prop.maximum, Math.max(prop.minimum, v)));
      out.textContent = range.value;
    };
    row.append(range, out);
  } else if (prop.type === 'integer') {
    const number = document.createElement('input');
    number.type = 'number';
    number.step = '1';
    if (prop.minimum !== undefined) number.min = prop.minimum;
    if (prop.maximum !== undefined) number.max = prop.maximum;
    if (prop.default !== undefined) number.value = prop.default;
    get = () => number.value === '' ? undefined : parseInt(number.value, 10);
    set = (v) => { number.value = v; };
    row.append(number);
  } else {
    let el;
    if (toolName === 'self.car.act' && name === 'action') {
      el = selectField(['forward', 'backward', 'turn_left', 'turn_right', 'spin', 'stop'],
                       prop.default ?? 'forward');
    } else if (toolName === 'self.screen.set_theme' && name === 'theme') {
      el = selectField(['dark', 'light'], prop.default ?? 'dark');
    } else {
      el = document.createElement('input');
      el.type = 'text';
      if (prop.maxLength) el.maxLength = prop.maxLength;
      el.value = prop.default ?? defaultString(toolName, name);
      el.placeholder = name === 'url' ? 'http://…' : '';
    }
    get = () => el.value;
    set = (v) => { el.value = v; };
    row.append(el);
  }
  return { name, required, row, get, set };
}

function addColorPicker(controls, fields) {
  const byName = Object.fromEntries(fields.map((field) => [field.name, field]));
  const row = document.createElement('div');
  row.className = 'row';
  const label = document.createElement('label');
  label.textContent = 'color';
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.value = '#ffffff';
  const hex = document.createElement('span');
  hex.className = 'mono';
  hex.textContent = '#ffffff';
  hex.style.fontSize = '.72rem';
  hex.style.color = '#9aa4b2';
  picker.oninput = () => {
    const value = parseInt(picker.value.slice(1), 16);
    byName.r?.set((value >> 16) & 255);
    byName.g?.set((value >> 8) & 255);
    byName.b?.set(value & 255);
    hex.textContent = picker.value;
  };
  row.append(label, picker, hex);
  controls.prepend(row);
}

function addJoystick(controls, fields) {
  const byName = Object.fromEntries(fields.map((field) => [field.name, field]));
  const pad = document.createElement('div');
  pad.className = 'joy';
  const knob = document.createElement('div');
  knob.className = 'knob';
  const hint = document.createElement('div');
  hint.className = 'hint';
  hint.textContent = 'drag to drive · release to stop';
  pad.append(knob, hint);

  const row = document.createElement('label');
  row.className = 'chk';
  const stopOnRelease = document.createElement('input');
  stopOnRelease.type = 'checkbox';
  stopOnRelease.checked = true;
  row.append(stopOnRelease, document.createTextNode(' stop on release'));

  let timer = null;
  let latest = { steer: 0, throttle: 0 };
  let inFlight = false;
  let dragId = null;

  const send = (steer, throttle) => {
    if (inFlight) return;
    inFlight = true;
    rpc('tools/call', { name: 'self.motor.drive',
                        arguments: { steer, throttle, duration_ms: 0 } })
      .catch(() => {})
      .finally(() => { inFlight = false; });
  };
  const stop = () => {
    rpc('tools/call', { name: 'self.motor.stop', arguments: {} }).catch(() => {});
  };
  const reset = () => {
    knob.style.left = '50%';
    knob.style.top = '50%';
    latest = { steer: 0, throttle: 0 };
    byName.steer?.set(0);
    byName.throttle?.set(0);
  };
  const move = (event) => {
    const rect = pad.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const radius = Math.min(rect.width, rect.height) / 2 - 14;
    let dx = (event.clientX - cx) / radius;
    let dy = (event.clientY - cy) / radius;
    const length = Math.hypot(dx, dy);
    if (length > 1) { dx /= length; dy /= length; }
    knob.style.left = (50 + dx * 46) + '%';
    knob.style.top = (50 + dy * 46) + '%';
    latest = { steer: Math.round(dx * 100), throttle: Math.round(-dy * 100) };
    byName.steer?.set(latest.steer);
    byName.throttle?.set(latest.throttle);
  };

  pad.addEventListener('pointerdown', (event) => {
    dragId = event.pointerId;
    try { pad.setPointerCapture(dragId); } catch { /* synthetic or older pointer */ }
    move(event);
    clearInterval(timer);
    timer = setInterval(() => send(latest.steer, latest.throttle), 150);
    send(latest.steer, latest.throttle);
  });
  pad.addEventListener('pointermove', (event) => {
    if (dragId === event.pointerId) move(event);
  });
  const end = (event) => {
    if (dragId !== event.pointerId) return;
    dragId = null;
    clearInterval(timer);
    timer = null;
    if (stopOnRelease.checked) {
      reset();
      stop();
    }
  };
  pad.addEventListener('pointerup', end);
  pad.addEventListener('pointercancel', end);

  controls.prepend(pad);
  controls.insertBefore(row, controls.children[1] || null);
}

function summaryOf(result) {
  const content = (result && result.content) || [];
  const part = content[0];
  if (!part) return '(empty)';
  if (part.type === 'image') return 'image ' + (part.mimeType || '');
  const text = part.text || '';
  return text.length > 120 ? text.slice(0, 117) + '…' : text;
}

function compactArgs(args) {
  const text = JSON.stringify(args);
  return text.length > 100 ? text.slice(0, 97) + '…' : text;
}

function addHistory(item) {
  state.history.unshift(item);
  state.history = state.history.slice(0, 20);
  renderHistory();
}

function renderHistory() {
  const old = document.getElementById('history');
  if (old) old.remove();
  if (!state.history.length) return;
  const section = document.createElement('section');
  section.id = 'history';
  const heading = document.createElement('h2');
  heading.className = 'sec';
  heading.textContent = 'History';
  section.append(heading);
  for (const item of state.history) {
    const row = document.createElement('div');
    row.className = 'item';
    if (item.error) row.classList.add('err');
    row.innerHTML = '<span class="mono">' + esc(item.name) + '</span> ' + esc(item.args) +
                    ' → ' + esc(item.error || item.summary) + ' (' + item.ms + ' ms)';
    section.append(row);
  }
  els.tools.append(section);
}

function showContent(out, result, toolName) {
  const content = (result && result.content) || [];
  for (const part of content) {
    if (part.type === 'text') {
      const pre = document.createElement('pre');
      pre.textContent = pretty(part.text);
      out.append(pre);
    } else if (part.type === 'image') {
      const img = document.createElement('img');
      img.loading = 'lazy';
      img.src = 'data:' + (part.mimeType || 'image/jpeg') + ';base64,' + part.data;
      out.append(img);
    }
  }
  if (toolName === 'self.screen.snapshot') {
    const img = document.createElement('img');
    img.src = '/snapshot.jpg?t=' + Date.now();
    img.alt = 'screen snapshot';
    out.append(img);
  }
}

async function invoke(tool, args, button, out) {
  button.disabled = true;
  out.className = 'result';
  out.textContent = 'running…';
  const started = performance.now();
  try {
    const result = await rpc('tools/call', { name: tool.name, arguments: args });
    const ms = Math.round(performance.now() - started);
    out.className = 'result';
    out.textContent = '';
    const meta = document.createElement('div');
    meta.className = 'ok';
    meta.textContent = 'ok · ' + ms + ' ms';
    out.append(meta);
    showContent(out, result, tool.name);
    addHistory({ name: tool.name, args: compactArgs(args), summary: summaryOf(result), ms });
  } catch (error) {
    const ms = Math.round(performance.now() - started);
    out.className = 'result err';
    out.textContent = error.message;
    addHistory({ name: tool.name, args: compactArgs(args), error: error.message, ms });
  } finally {
    button.disabled = false;
  }
}

function makeCard(tool) {
  const card = document.createElement('div');
  card.className = 'card';
  card.dataset.tool = tool.name;

  const heading = document.createElement('h3');
  const title = document.createElement('span');
  title.textContent = titleOf(tool.name);
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = tool.name;
  heading.append(title, name);
  if (groupOf(tool) === 'User only') {
    const badge = document.createElement('span');
    badge.className = 'badge';
    badge.textContent = 'user';
    heading.append(badge);
  }

  const desc = document.createElement('div');
  desc.className = 'desc';
  desc.textContent = tool.description || '';
  desc.title = tool.description || '';

  const controls = document.createElement('div');
  controls.className = 'controls';
  const schema = tool.inputSchema || {};
  const required = schema.required || [];
  const fields = Object.entries(schema.properties || {}).map(([key, prop]) =>
    makeField(tool.name, key, prop, required.includes(key)));
  for (const field of fields) controls.append(field.row);
  if (tool.name === 'self.rgb_led.set_color') addColorPicker(controls, fields);
  if (tool.name === 'self.motor.drive') addJoystick(controls, fields);

  const args = () => {
    const result = {};
    for (const field of fields) {
      const value = field.get();
      if (value === undefined || value === null) continue;
      if (value === '' && !field.required) continue;
      result[field.name] = value;
    }
    return result;
  };

  const out = document.createElement('div');
  out.className = 'result';

  const actions = document.createElement('div');
  actions.className = 'actions';
  const run = document.createElement('button');
  run.className = DANGER.has(tool.name) ? 'danger' : 'primary';
  run.textContent = 'Run';
  run.onclick = () => {
    if (DANGER.has(tool.name) && !confirm('Run ' + tool.name + '?')) return;
    invoke(tool, args(), run, out);
  };
  actions.append(run);

  card.append(heading, desc, controls, actions, out);
  return card;
}

function renderTools(tools) {
  els.tools.textContent = '';
  const groups = new Map();
  for (const tool of tools) {
    const group = groupOf(tool);
    if (!groups.has(group)) groups.set(group, []);
    groups.get(group).push(tool);
  }
  const order = GROUP_ORDER.filter((group) => groups.has(group));
  for (const group of groups.keys()) if (!order.includes(group)) order.push(group);
  for (const group of order) {
    const heading = document.createElement('h2');
    heading.className = 'sec';
    heading.textContent = group + ' · ' + groups.get(group).length;
    els.tools.append(heading);
    for (const tool of groups.get(group)) els.tools.append(makeCard(tool));
  }
  renderHistory();
}

async function refreshTools() {
  setStatus('loading tools…');
  try {
    const tools = [];
    let cursor = null;
    do {
      const params = { withUserTools: els.userTools.checked };
      if (cursor) params.cursor = cursor;
      const page = await rpc('tools/list', params);
      tools.push(...(page.tools || []));
      cursor = page.nextCursor || null;
    } while (cursor);
    state.tools = tools;
    renderTools(tools);
    state.statusText = tools.length + ' tools · ' + location.host;
    setStatus(state.statusText);
  } catch (error) {
    setStatus('error: ' + error.message, false);
  }
}

async function stopMotors() {
  try {
    await rpc('tools/call', { name: 'self.motor.stop', arguments: {} });
    setStatus('motors stopped', true, true);
  } catch (error) {
    setStatus('stop failed: ' + error.message, false, true);
  }
}

els.refresh.onclick = refreshTools;
els.userTools.onchange = refreshTools;
els.stop.onclick = stopMotors;
window.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape') return;
  const tag = (event.target.tagName || '').toLowerCase();
  if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
  if (els.rawDlg.open) return;
  stopMotors();
});
window.addEventListener('pagehide', () => {
  navigator.sendBeacon('/mcp', JSON.stringify({
    jsonrpc: '2.0', id: 2000000000, method: 'tools/call',
    params: { name: 'self.motor.stop', arguments: {} },
  }));
});

els.rawBtn.onclick = () => {
  if (!els.rawIn.value) {
    els.rawIn.value = JSON.stringify({
      jsonrpc: '2.0', id: 1, method: 'tools/list', params: { withUserTools: true },
    }, null, 2);
  }
  els.rawOut.textContent = '';
  els.rawDlg.showModal();
};
els.rawSend.onclick = async (event) => {
  event.preventDefault();
  els.rawOut.textContent = 'sending…';
  try {
    const msg = JSON.parse(els.rawIn.value);
    const res = await fetch('/mcp', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(msg),
    });
    if (res.status === 202) {
      els.rawOut.textContent = 'pending · polling id ' + msg.id + '…';
      els.rawOut.textContent = JSON.stringify(await poll(msg.id), null, 2);
    } else {
      const text = await res.text();
      if (res.status !== 200) throw new Error('HTTP ' + res.status + ': ' + text.slice(0, 160));
      const json = JSON.parse(text);
      els.rawOut.textContent = JSON.stringify(json, null, 2);
    }
  } catch (error) {
    els.rawOut.textContent = 'error: ' + error.message;
  }
};

loadAddress().finally(refreshTools);
</script>
</body>
</html>
)HTML";

esp_err_t IndexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kConsoleHtml, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool McpHttpConsole::Start(int port) {
    if (server != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = static_cast<uint16_t>(port);
    config.ctrl_port = 32770;  // 32768 is used by the camera HTTP server
    config.max_uri_handlers = 7;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MCP console on port %d", port);
        server = nullptr;
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = IndexHandler,
        .user_ctx = nullptr,
    };
    httpd_uri_t mcp_uri = {
        .uri = "/mcp",
        .method = HTTP_POST,
        .handler = McpHandler,
        .user_ctx = nullptr,
    };
    httpd_uri_t result_uri = {
        .uri = "/mcp/result",
        .method = HTTP_GET,
        .handler = ResultHandler,
        .user_ctx = nullptr,
    };
    httpd_uri_t snapshot_uri = {
        .uri = "/snapshot",
        .method = HTTP_POST,
        .handler = SnapshotUploadHandler,
        .user_ctx = nullptr,
    };
    httpd_uri_t snapshot_jpeg_uri = {
        .uri = "/snapshot.jpg",
        .method = HTTP_GET,
        .handler = SnapshotGetHandler,
        .user_ctx = nullptr,
    };
    httpd_uri_t address_uri = {
        .uri = "/address",
        .method = HTTP_GET,
        .handler = AddressHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &mcp_uri);
    httpd_register_uri_handler(server, &result_uri);
    httpd_register_uri_handler(server, &snapshot_uri);
    httpd_register_uri_handler(server, &snapshot_jpeg_uri);
    httpd_register_uri_handler(server, &address_uri);

    ESP_LOGI(TAG, "MCP console ready: http://<device-ip>:%d/", port);
    return true;
}
