'use strict';
// __CONFIG__ is replaced by the local launcher after file/identity verification.
const cfg = __CONFIG__;
const ids = cfg.identity.map(x => ptr(x));
const voice = Process.getModuleByName('VoipEngine.dll');
const conf = Process.getModuleByName('ConfSdk.dll');
const controlAddress = voice.base.add(0x51cf60);
const originalControl = new NativeFunction(controlAddress, 'int', ['pointer', 'int', 'int']);
let enabled = false;
let setterCalls = 0;
let overriddenCalls = 0;
let lastRequestedBps = null;
let stoppedReason = null;
let listener = null;
let safetyTimer = null;

function sameCall() {
  try {
    return conf.base.add(0x12c9d8).readPointer().equals(ids[0]) &&
      ids[0].add(8).readPointer().equals(ids[1]) &&
      ids[1].readPointer().equals(conf.base.add(0xf4f90)) &&
      ids[1].add(0x980).readPointer().equals(ids[2]) &&
      ids[2].readPointer().equals(voice.base.add(0xf7cc70)) &&
      ids[2].add(0x210).readPointer().equals(ids[3]) &&
      ids[3].readPointer().equals(voice.base.add(0xfa11d0)) &&
      ids[3].add(0x20).readPointer().equals(ids[4]) &&
      ids[3].add(0x28).readS32() > cfg.channel_index &&
      ids[3].add(0x28).readS32() <= 64 &&
      ids[4].add(cfg.channel_index * 0x58).readPointer().equals(ids[5]) &&
      ids[5].add(0x10).readS32() === cfg.channel_id &&
      ids[5].add(0x5240).readS32() === 11 &&
      ids[5].add(0x5230).readPointer().equals(ids[6]) &&
      ids[6].readPointer().equals(ids[7]) &&
      ids[7].readPointer().equals(voice.base.add(0xfc2b08)) &&
      ids[7].add(8).readS32() === 11 &&
      ids[7].add(0x10).readPointer().equals(ids[8]);
  } catch (_) { return false; }
}

function snapshot() {
  const current = sameCall();
  return {
    enabled, same_call: current, setter_calls: setterCalls,
    overridden_calls: overriddenCalls, last_requested_bps: lastRequestedBps,
    stopped_reason: stoppedReason,
    wrapper_bps: current ? ids[7].add(0x24).readS32() : null,
    opus_requested_bps: current ? ids[8].add(0xa4).readS32() : null,
    opus_current_bps: current ? ids[8].add(0xa0).readS32() : null
  };
}

function stop(reason) {
  enabled = false;
  stoppedReason = stoppedReason || reason;
  if (safetyTimer !== null) { clearInterval(safetyTimer); safetyTimer = null; }
  if (listener !== null) { listener.detach(); listener = null; }
  Interceptor.flush();
  let restoreResult = null;
  if (sameCall()) {
    // Upstream wrapper retains the application's value throughout this method.
    // Restore to its latest value, so a legitimate network adaptation is retained.
    const restoreBps = ids[7].add(0x24).readS32();
    if (restoreBps >= 16000 && restoreBps <= 128000) {
      restoreResult = originalControl(ids[8], 4002, restoreBps);
    }
  }
  return { ...snapshot(), restore_result: restoreResult };
}

rpc.exports = {
  start() {
    if (enabled) throw new Error('Already running');
    if (!sameCall()) throw new Error('Call identity changed');
    if (cfg.target !== 128000) throw new Error('Only the validated 128 kbps target is allowed');
    const expected = '488bc48950104c8940184c894820535541564881eca00000';
    const observed = Array.from(new Uint8Array(controlAddress.readByteArray(24)),
      x => x.toString(16).padStart(2, '0')).join('');
    if (observed !== expected) throw new Error('Encoder control code changed');
    listener = Interceptor.attach(controlAddress, {
      onEnter(args) {
        if (!enabled || !args[0].equals(ids[8]) || args[1].toInt32() !== 4002) return;
        if (!sameCall()) { enabled = false; stoppedReason = 'call_changed'; return; }
        setterCalls++;
        lastRequestedBps = args[2].toInt32();
        if (lastRequestedBps !== cfg.target) overriddenCalls++;
        args[2] = ptr(cfg.target);
      }
    });
    enabled = true;
    Interceptor.flush();
    // Apply once. Subsequent changes happen only inside the application's calls.
    const result = originalControl(ids[8], 4002, cfg.target);
    if (result !== 0) { stop('initial_set_failed'); throw new Error('Native bitrate setter rejected target'); }
    safetyTimer = setInterval(() => {
      if (!sameCall()) stop('call_changed');
    }, 250);
    return snapshot();
  },
  stats: snapshot,
  stop() { return stop('stopped'); }
};
