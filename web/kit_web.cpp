// One kit machine behind a small C API for the browser (built by web/build.sh with emscripten).
// The machine is chosen at link time: the build adds a file defining kw_machine_type().
//
// The page uses one instance on the main thread (the parameter table, ROM recognition) and one in
// the AudioWorklet (boot, parameters, audio, readouts). Everything runs on the calling thread: the
// kit's Host and its worker thread are not used, so set_params() runs between audio blocks.
//
// Strings come back as pointers to static buffers, valid until the next call of the same function.
#include "../shared/core/machine.hpp"
#include <emscripten/emscripten.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

const kit::MachineType& kw_machine_type();

namespace {

constexpr int kMaxBlock = 1024;

struct State {
    std::vector<kit::RomFile> files;
    std::unique_ptr<kit::Machine> m;
    std::string err;
    std::vector<float> params;
    float buf[4][kMaxBlock] = {};   // in L, in R, out L, out R
};
State& st() {
    static State s;
    return s;
}

std::string q(const std::string& s) {   // a JSON string
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else o += char(c);
        }
    }
    return o + "\"";
}
std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
std::string strs(const std::vector<std::string>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); i++) o += (i ? "," : "") + q(v[i]);
    return o + "]";
}

}  // namespace

extern "C" {

// {name, about, inputs, tail, params:[{id,name,kind,min,max,def,choices,unit}], roms:[{role,sizes,known,hints}]}
EMSCRIPTEN_KEEPALIVE const char* kw_info() {
    static std::string o;
    const kit::Info& i = kw_machine_type().info;
    static const char* kinds[] = {"int", "choice", "bool", "float"};
    o = "{\"name\":" + q(i.name) + ",\"about\":" + q(i.about) + ",\"tag\":" + q(i.state_tag) +
        ",\"inputs\":" + num(i.inputs) + ",\"tail\":" + num(i.tail_seconds) + ",\"params\":[";
    for (size_t k = 0; k < i.params.size(); k++) {
        const auto& p = i.params[k];
        o += (k ? "," : "") + std::string("{\"id\":") + q(p.id) + ",\"name\":" + q(p.name) + ",\"kind\":\"" +
             kinds[p.kind] + "\",\"min\":" + num(p.min) + ",\"max\":" + num(p.max) + ",\"def\":" + num(p.def) +
             ",\"choices\":" + strs(p.choices) + ",\"unit\":" + q(p.unit) + "}";
    }
    o += "],\"roms\":[";
    for (size_t k = 0; k < i.roms.size(); k++) {
        const auto& r = i.roms[k];
        o += (k ? "," : "") + std::string("{\"role\":") + q(r.role) + ",\"sizes\":[";
        for (size_t j = 0; j < r.sizes.size(); j++) o += (j ? "," : "") + num(double(r.sizes[j]));
        o += "],\"known\":[";
        for (size_t j = 0; j < r.known.size(); j++) o += (j ? "," : "") + q(r.known[j].label);
        o += "],\"hints\":" + strs(r.name_hints) + "}";
    }
    return (o += "]}").c_str();
}

// true if a file of this size could fill some role (the page skips other files)
EMSCRIPTEN_KEEPALIVE int kw_size_fits(int len) { return kit::any_size_fits(kw_machine_type().info.roms, size_t(len)); }

EMSCRIPTEN_KEEPALIVE void kw_files_clear() { st().files.clear(); }
EMSCRIPTEN_KEEPALIVE void kw_file_add(const char* name, const uint8_t* data, int len) {
    st().files.push_back({name, name, std::vector<uint8_t>(data, data + len)});
}

// {complete, describe, slots:[{role, found, known, label, file}]}
EMSCRIPTEN_KEEPALIVE const char* kw_recognise() {
    static std::string o;
    const auto& specs = kw_machine_type().info.roms;
    kit::RomSet set = kit::recognise(specs, st().files);
    o = std::string("{\"complete\":") + (set.complete() ? "true" : "false") + ",\"describe\":" + q(set.describe(specs)) +
        ",\"slots\":[";
    for (size_t k = 0; k < set.slots.size(); k++) {
        const auto& s = set.slots[k];
        o += (k ? "," : "") + std::string("{\"role\":") + q(specs[k].role) + ",\"found\":" + (s.found ? "true" : "false") +
             ",\"known\":" + (s.known ? "true" : "false") + ",\"label\":" + q(s.label) + ",\"file\":" + q(s.file.name) + "}";
    }
    return (o += "]}").c_str();
}

// Boot from the files added; 1 = running. The files are dropped afterwards.
EMSCRIPTEN_KEEPALIVE int kw_boot(const char* state) {
    State& s = st();
    const auto& t = kw_machine_type();
    kit::RomSet set = kit::recognise(t.info.roms, s.files);
    s.m.reset();
    s.err.clear();
    if (!set.complete()) {
        s.err = "missing: " + set.describe(t.info.roms);
    } else {
        s.m = t.boot(set, state ? state : "", &s.err);
    }
    s.files.clear();
    s.files.shrink_to_fit();
    s.params.clear();
    for (auto& p : t.info.params) s.params.push_back(p.def);
    if (s.m)   // the panel inputs at their defaults, as kit::Host does on install
        for (auto& in : t.info.panel.inputs) s.m->panel_input(in.id, in.def);
    return s.m ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE const char* kw_error() { return st().err.c_str(); }

EMSCRIPTEN_KEEPALIVE void kw_prepare(int rate) {
    if (st().m) st().m->prepare(rate, kMaxBlock);
}
EMSCRIPTEN_KEEPALIVE int kw_latency() { return st().m ? int(st().m->latency()) : 0; }
EMSCRIPTEN_KEEPALIVE int kw_live_readouts() { return st().m && st().m->live_readouts() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int kw_max_block() { return kMaxBlock; }

// The parameter values (plain, table order): write them here, then call kw_set_params().
EMSCRIPTEN_KEEPALIVE float* kw_params() {
    State& s = st();
    s.params.resize(kw_machine_type().info.params.size());
    return s.params.data();
}
EMSCRIPTEN_KEEPALIVE void kw_set_params() {
    if (st().m) st().m->set_params(st().params);
}

// 0, 1 = inputs L/R, 2, 3 = outputs L/R; kMaxBlock floats each.
EMSCRIPTEN_KEEPALIVE float* kw_buf(int i) { return st().buf[i & 3]; }
EMSCRIPTEN_KEEPALIVE void kw_process(int n) {
    State& s = st();
    if (!s.m || n <= 0 || n > kMaxBlock) return;
    if (kw_machine_type().info.inputs == 1)
        for (int i = 0; i < n; i++) s.buf[0][i] = 0.5f * (s.buf[0][i] + s.buf[1][i]);
    const float* in[2] = {s.buf[0], s.buf[1]};
    s.m->process(in, s.buf[2], s.buf[3], n);
}

// {param:[...], panel, status}
EMSCRIPTEN_KEEPALIVE const char* kw_readouts() {
    static std::string o;
    if (!st().m) return "{\"param\":[],\"panel\":\"\",\"status\":\"\"}";
    kit::Readouts r = st().m->readouts();
    o = "{\"param\":" + strs(r.param) + ",\"panel\":" + q(r.panel) + ",\"status\":" + q(r.status) + "}";
    return o.c_str();
}

// [{name, values:[...]}]
EMSCRIPTEN_KEEPALIVE const char* kw_presets() {
    static std::string o;
    o = "[";
    if (st().m) {
        auto ps = st().m->presets();
        for (size_t k = 0; k < ps.size(); k++) {
            o += (k ? "," : "") + std::string("{\"name\":") + q(ps[k].name) + ",\"values\":[";
            for (size_t j = 0; j < ps[k].values.size(); j++) o += (j ? "," : "") + num(ps[k].values[j]);
            o += "]}";
        }
    }
    return (o += "]").c_str();
}

// ---- hardware panel (core/panel.hpp) ----
// {width, height, face, ink, leds, digits, alpha, notes, inputs:[{id,name,min,max,def}],
//  elements:[{k, x, y, w, h, id, param, text, colour, size, style, count, hotkey}]}; colours as ARGB
// numbers; empty elements = no hardware panel
EMSCRIPTEN_KEEPALIVE const char* kw_panel_layout() {
    static std::string o;
    const kit::PanelLayout& L = kw_machine_type().info.panel;
    static const char* kinds[] = {"frame", "window", "text", "key", "led", "digit", "alpha", "pot", "switch", "hole", "path"};
    o = "{\"width\":" + num(L.width) + ",\"height\":" + num(L.height) + ",\"face\":" + num(L.face) + ",\"ink\":" + num(L.ink) + ",\"weight\":" + num(L.legend_weight) +
        ",\"leds\":" + num(L.leds) + ",\"digits\":" + num(L.digits) + ",\"alpha\":" + num(L.alpha) + ",\"notes\":" + q(L.notes) +
        ",\"inputs\":[";
    for (size_t k = 0; k < L.inputs.size(); k++) {
        const auto& in = L.inputs[k];
        o += (k ? "," : "") + std::string("{\"id\":") + num(in.id) + ",\"name\":" + q(in.name) + ",\"min\":" + num(in.min) +
             ",\"max\":" + num(in.max) + ",\"def\":" + num(in.def) + "}";
    }
    o += "],\"elements\":[";
    for (size_t k = 0; k < L.elements.size(); k++) {
        const auto& e = L.elements[k];
        o += (k ? "," : "") + std::string("{\"k\":\"") + kinds[e.kind] + "\",\"x\":" + num(e.x) + ",\"y\":" + num(e.y) +
             ",\"w\":" + num(e.w) + ",\"h\":" + num(e.h) + ",\"id\":" + num(e.id) + ",\"param\":" + q(e.param) +
             ",\"text\":" + q(e.text) + ",\"colour\":" + num(e.colour) + ",\"size\":" + num(e.size) + ",\"style\":" + num(e.style) +
             ",\"count\":" + num(e.count) + ",\"hotkey\":" + q(e.hotkey) + "}";
    }
    return (o += "]}").c_str();
}
// A panel key down/up, or a panel input's value; between audio blocks (the machine queues it).
EMSCRIPTEN_KEEPALIVE void kw_panel_key(int id, int down) {
    if (st().m) st().m->panel_key(id, down != 0);
}
EMSCRIPTEN_KEEPALIVE void kw_panel_input(int id, float v) {
    if (st().m) st().m->panel_input(id, v);
}
// {valid, digits:[], leds:[], alpha, held:[], adopt:[] (null = leave), serial}
EMSCRIPTEN_KEEPALIVE const char* kw_panel() {
    static std::string o;
    if (!st().m) return "{\"valid\":false}";
    kit::PanelState p = st().m->panel();
    auto ints = [](const auto& v) {
        std::string r = "[";
        for (size_t i = 0; i < v.size(); i++) r += (i ? "," : "") + std::to_string(int(v[i]));
        return r + "]";
    };
    o = std::string("{\"valid\":") + (p.valid ? "true" : "false") + ",\"digits\":" + ints(p.digits) + ",\"leds\":" + ints(p.leds) +
        ",\"alpha\":" + q(p.alpha) + ",\"held\":" + ints(p.held) + ",\"adopt\":[";
    for (size_t i = 0; i < p.adopt.size(); i++) o += (i ? "," : "") + (std::isnan(p.adopt[i]) ? std::string("null") : num(p.adopt[i]));
    return (o += "],\"serial\":" + std::to_string(p.adopt_serial) + "}").c_str();
}

}  // extern "C"
