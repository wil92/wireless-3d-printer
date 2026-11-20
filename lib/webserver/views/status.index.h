
#ifndef STATUS_INDEX_HTML_H
#define STATUS_INDEX_HTML_H

const char status_index_html[] PROGMEM = R"PAGE(
<!doctype html><html lang=en><meta charset=utf-8><meta content="width=device-width,initial-scale=1" name=viewport><title>Remote SD</title><style>:root{color:#1e293b;color-scheme:light;background:#f8fafc;font-family:system-ui,sans-serif}body{margin:0;padding:1.5rem}main{box-sizing:border-box;background:#fff;border-radius:.6rem;max-width:26rem;margin:2rem auto;padding:1.5rem;box-shadow:0 4px 16px #0002}h1{margin-top:0;font-size:1.5rem}h2{font-size:1.15rem}label{margin-top:1rem;font-weight:600;display:block}input{box-sizing:border-box;width:100%;font:inherit;border:1px solid #94a3b8;border-radius:.25rem;margin-top:.3rem;padding:.6rem}button{color:#fff;font:inherit;cursor:pointer;background:#059669;border:0;border-radius:.25rem;padding:.6rem .8rem}button:disabled{opacity:.6;cursor:wait}.network{align-items:end;gap:.5rem;display:flex}.network input{flex:1;min-width:0}.action{margin-top:1.5rem}.status{color:#475569;min-height:1.5em}.status.error{color:#b91c1c}ul{padding:0;list-style:none}li{overflow-wrap:anywhere;justify-content:space-between;align-items:center;gap:.5rem;margin:.5rem 0;display:flex}li button{flex-shrink:0}.upload-overlay[hidden]{display:none}.upload-overlay{z-index:1;background:#000a;place-items:center;padding:1.5rem;display:grid;position:fixed;inset:0}.upload-dialog{box-sizing:border-box;background:#fff;border-radius:.6rem;width:min(100%,26rem);padding:1.5rem}.upload-dialog progress{width:100%}</style><body><main><h1>Remote SD</h1><p>File operations are unavailable. Printer USB integration is not implemented.</main>
)PAGE";

#endif //STATUS_INDEX_HTML_H
