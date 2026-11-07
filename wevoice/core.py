"""Version-gated numeric audio inspection. No audio or chat buffers are read."""
import hashlib
from dataclasses import dataclass
from pathlib import Path

HASHES = {
    'weixin.dll': 'dcd309bf5a0cee9219d12ee7308f706b77ad798393c9230f904db5a4ce861c11',
    'voipengine.dll': 'ec7cfdbc32842111be3287753832559c0403a79a3042a3a9ae57dd3908bc225c',
    'confsdk.dll': 'fd3bfa52b84a19726f30273e6bf52f6e99a513077707bf1ee5cf87ca24f98c2a',
}
SUPPORTED_VERSION = '4.1.15.56 x64'


class VoiceError(Exception):
    def __init__(self, message, kind='error'):
        super().__init__(message)
        self.kind = kind


def verify_files(loaded):
    for name, expected in HASHES.items():
        if name not in loaded:
            raise VoiceError('尚未检测到语音引擎，请先接通微信语音。', 'no_call')
        try:
            with Path(loaded[name]['path']).open('rb') as stream:
                observed = hashlib.file_digest(stream, 'sha256').hexdigest()
        except OSError as exc:
            raise VoiceError('无法读取微信引擎文件，请重新选择微信。') from exc
        if observed != expected:
            raise VoiceError('当前微信引擎未适配；仅支持 4.1.15.56 x64 的已验证版本。', 'unsupported')


@dataclass(frozen=True)
class Call:
    identity: tuple
    index: int
    channel_id: int


def require(value, message='请先接通电脑微信语音，再读取状态。', kind='no_call'):
    if not value:
        raise VoiceError(message, kind)
    return value


def locate(reader, loaded):
    c, v = loaded['confsdk.dll']['base'], loaded['voipengine.dll']['base']
    pointer = lambda address: require(reader.ptr(address))
    service = pointer(c + 0x12c9d8)
    backend = pointer(service + 8)
    require(reader.ptr(backend) == c + 0xf4f90, '通话结构未适配。', 'unsupported')
    engine = pointer(backend + 0x980)
    require(reader.ptr(engine) == v + 0xf7cc70, '语音引擎结构未适配。', 'unsupported')
    xve = pointer(engine + 0x210)
    require(reader.ptr(xve) == v + 0xfa11d0, '音频引擎结构未适配。', 'unsupported')
    table = pointer(xve + 0x20)
    count = reader.i32(xve + 0x28)
    require(0 < count <= 64)
    calls = []
    for i in range(count):
        channel = reader.ptr(table + i * 0x58)
        if not channel:
            continue
        channel_id = reader.i32(channel + 0x10)
        if channel_id < 0:
            continue
        coder = reader.ptr(channel + 0x5230)
        if not coder:
            continue
        codec = pointer(coder)
        require(reader.ptr(codec) == v + 0xfc2b08, '当前音频编码器未适配。', 'unsupported')
        require(reader.i32(channel + 0x5240) == reader.i32(codec + 8) == 11,
                '当前通话不是已适配的 Opus 编码。', 'unsupported')
        opus = pointer(codec + 0x10)
        calls.append(Call((service, backend, engine, xve, table, channel, coder, codec, opus), i, channel_id))
    return select_call(calls)


def select_call(calls):
    require(calls)
    require(len(calls) == 1, '检测到多个通话实例，请仅保留一个电脑微信语音通话。', 'ambiguous')
    return calls[0]


def parameters(reader, call):
    channel, _, codec, opus = call.identity[5:]
    values = {
        'target_bps': reader.i32(opus + 0xa4),
        'current_bps': reader.i32(opus + 0xa0),
        'default_bps': reader.i32(codec + 0x24),
        'sample_rate': reader.i32(opus + 0x90),
        'channels': reader.i32(opus + 0x70),
        'codec': 'Opus',
        'frame_ms': reader.i32(channel + 0x5254),
    }
    require(6000 <= values['target_bps'] <= 510000 and
            values['sample_rate'] in (8000, 12000, 16000, 24000, 48000) and
            values['channels'] in (1, 2), '音频参数不符合已验证的结构，已停止操作。', 'unsupported')
    return values
