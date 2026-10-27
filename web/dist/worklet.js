// Audio thread: runs the WebAssembly engine, plays an uploaded file as input,
// and reports looper state to the page about 30 times a second.
class GrEngineProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ready = false;
    this.file = null;
    this.filePos = 0;
    this.filePlay = false;
    // Per channel: 'live' (system input routed by the page), 'fileL', 'fileR' or 'none'.
    this.mode = ['none', 'none'];
    this.sinceReport = 0;
    this.lastVersion = [-1, -1];
    this.lastOverview = [0, 0];
    this.clock = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async init(bytes) {
    try {
      const stubs = {};
      const wasi = new Proxy(stubs, { get: () => () => 0 });
      const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi });
      const e = instance.exports;
      if (e._initialize) e._initialize();
      e.gr_init();
      this.e = e;
      this.views();
      this.ready = true;
      this.port.postMessage({ t: 'ready' });
    } catch (err) {
      this.port.postMessage({ t: 'error', message: String(err && err.message || err) });
    }
  }

  views() {
    const e = this.e;
    this.buf = e.memory.buffer;
    const f = (p, n) => new Float32Array(this.buf, p, n);
    this.inA = f(e.gr_in_a(), 128);
    this.inB = f(e.gr_in_b(), 128);
    this.outL = f(e.gr_out_l(), 128);
    this.outR = f(e.gr_out_r(), 128);
    this.grainBuf = f(e.gr_grain_buf(), 96);
    this.overviewBuf = f(e.gr_overview_buf(), 1024);
    const ns = e.gr_control_source_count(), nd = e.gr_control_dest_count();
    this.destValues = f(e.gr_dest_values(), nd);
    this.sourceValues = f(e.gr_source_values(), ns);
    this.pulses = new Uint32Array(this.buf, e.gr_source_pulses(), ns);
    this.fires = new Uint32Array(this.buf, e.gr_trigger_fires(), e.gr_trigger_count());
  }

  onMessage(m) {
    if (m.t === 'init') { this.init(m.bytes); return; }
    if (!this.ready) return;
    const e = this.e;
    switch (m.t) {
      case 'param': e.gr_param(m.m, m.i, m.v); break;
      case 'apin': e.gr_audio_pin(m.s, m.d, m.v); break;
      case 'cpin': e.gr_control_pin(m.s, m.d, m.v); break;
      case 'clearPins': e.gr_clear_pins(); break;
      case 'trim': e.gr_control_trim(m.s, m.v); break;
      case 'box': e.gr_box_type(m.slot, m.type); break;
      case 'rec': e.gr_rec(m.ch, m.p ? 1 : 0); break;
      case 'recSync': e.gr_rec_sync(m.ch, m.on ? 1 : 0); break;
      case 'loopGet': {  // a copy of the loop, for saving
        const n = e.gr_loop_samples(m.ch);
        const data = n ? new Float32Array(this.e.memory.buffer, e.gr_loop_ptr(m.ch), n).slice() : null;
        this.port.postMessage({ t: 'loop', ch: m.ch, data }, data ? [data.buffer] : []);
        break;
      }
      case 'loopSet': {  // a saved loop comes back
        const n = Math.min(m.data.length, e.gr_loop_capacity(m.ch));
        if (this.e.memory.buffer !== this.buf) this.views();
        new Float32Array(this.e.memory.buffer, e.gr_loop_ptr(m.ch), n).set(m.data.subarray(0, n));
        e.gr_loop_restore(m.ch, n);
        this.lastVersion[m.ch] = -1;
        break;
      }
      case 'undo': e.gr_undo(m.ch); this.lastVersion[m.ch] = -1; break;
      case 'clear': e.gr_clear(m.ch); this.lastVersion[m.ch] = -1; break;
      case 'file': this.file = { l: m.l, r: m.r }; this.filePos = 0; break;
      case 'filePlay': this.filePlay = !!m.on; if (m.restart) this.filePos = 0; break;
      case 'inputMode': this.mode[m.ch] = m.mode; break;
    }
  }

  report() {
    const e = this.e;
    const channels = [];
    for (let ch = 0; ch < 2; ch++) {
      const n = e.gr_grains(ch);
      const info = {
        state: e.gr_state(ch),
        armed: e.gr_rec_armed(ch) === 1,
        live: e.gr_live(ch) === 1,
        version: e.gr_version(ch),
        length: e.gr_length(ch),
        playhead: e.gr_playhead(ch),
        grains: this.grainBuf.slice(0, n * 3),
      };
      const v = e.gr_version(ch);
      if (v !== this.lastVersion[ch] && this.clock - this.lastOverview[ch] > 4800) {
        e.gr_overview(ch, 320);
        info.overview = this.overviewBuf.slice(0, 320);
        this.lastVersion[ch] = v;
        this.lastOverview[ch] = this.clock;
      }
      channels.push(info);
    }
    this.port.postMessage({
      t: 'state', channels,
      dest: this.destValues.slice(), source: this.sourceValues.slice(),
      pulses: this.pulses.slice(), fires: this.fires.slice(),
    });
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const n = out[0].length;
    if (!this.ready) return true;
    if (this.e.memory.buffer !== this.buf) this.views();
    const f = this.file, live = inputs[0] || [];
    const playing = f && this.filePlay;
    for (let ch = 0; ch < 2; ch++) {
      const dst = ch ? this.inB : this.inA, mode = this.mode[ch];
      if (mode === 'live' && live[ch]) dst.set(live[ch].subarray(0, n));
      else if (playing && (mode === 'fileL' || mode === 'fileR')) {
        const src = mode === 'fileL' ? f.l : f.r, len = src.length;
        for (let i = 0; i < n; i++) dst[i] = src[(this.filePos + i) % len];
      } else dst.fill(0, 0, n);
    }
    if (playing) this.filePos = (this.filePos + n) % f.l.length;
    this.e.gr_process(n);
    out[0].set(this.outL.subarray(0, n));
    if (out[1]) out[1].set(this.outR.subarray(0, n));
    this.clock += n;
    this.sinceReport += n;
    if (this.sinceReport >= 1536) { this.sinceReport = 0; this.report(); }
    return true;
  }
}

registerProcessor('gr-engine', GrEngineProcessor);
