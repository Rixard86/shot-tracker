import json
import struct
import sys
import zlib

AXES = 6
BLOCK_SAMPLES = 32
MODE_BITS = 2
K_BITS = 5
MAX_K = 17
RAW_BITS = 16
ESCAPE_Q = 24
ESCAPE_BITS = 18
MODE_DELTA, MODE_SLOPE, MODE_RAW = 0, 1, 2
FORMAT_FULL, FORMAT_EXTENDED, FORMAT_ULTRA = 0, 1, 2
FULL_RATE_PRE = 480
MEDIUM_RATE_PRE = 2880
GROUP = 4
SLOW_GROUP = 8
INT16_LOW, INT16_HIGH = -32768, 32767
WORD_RANGE = 65536
BITS_PER_BYTE = 8
TOP_BIT = 7
HEADER_FIELDS = "<IHHI"
SAMPLE_FIELDS = "<6h"


class Shape:
    def __init__(self, vector):
        self.n_samples = vector["n_samples"]
        self.trigger_index = vector["trigger_index"]
        self.fmt = vector["format"]

    def aim(self):
        averaged = self.fmt != FORMAT_FULL and self.trigger_index > FULL_RATE_PRE
        return self.trigger_index - FULL_RATE_PRE if averaged else 0

    def slow(self):
        if self.fmt != FORMAT_ULTRA or self.trigger_index <= MEDIUM_RATE_PRE:
            return 0
        return (self.trigger_index - MEDIUM_RATE_PRE) // SLOW_GROUP * SLOW_GROUP

    def stored_count(self):
        aim, slow = self.aim(), self.slow()
        return slow // SLOW_GROUP + (aim - slow + GROUP - 1) // GROUP + self.n_samples - aim

    def group_at(self, k):
        if k < self.slow():
            return SLOW_GROUP
        if k < self.aim():
            return (self.aim() - k) % GROUP or GROUP
        return 1


class Decoder:
    def __init__(self, payload):
        self.data = payload
        self.pos = 0
        self.last = [0] * AXES
        self.before_last = [0] * AXES
        self.modes = [0, 0, 0]
        self.escapes = 0

    def bit(self):
        if self.pos >= len(self.data) * BITS_PER_BYTE:
            raise ValueError("payload ends early")
        value = (self.data[self.pos // BITS_PER_BYTE] >> (TOP_BIT - self.pos % BITS_PER_BYTE)) & 1
        self.pos += 1
        return value

    def bits(self, count):
        value = 0
        for _ in range(count):
            value = (value << 1) | self.bit()
        return value

    def rice(self, k):
        q = 0
        while self.bit():
            q += 1
            if q == ESCAPE_Q:
                self.escapes += 1
                return self.bits(ESCAPE_BITS)
        return (q << k) | self.bits(k)

    def value(self, plan):
        axis, mode, k = plan
        if mode == MODE_RAW:
            x = self.bits(RAW_BITS)
            return x - WORD_RANGE if x > INT16_HIGH else x
        code = self.rice(k)
        residual = -(code >> 1) - 1 if code & 1 else code >> 1
        last = self.last[axis]
        predicted = last if mode == MODE_DELTA else 2 * last - self.before_last[axis]
        x = predicted + residual
        if not INT16_LOW <= x <= INT16_HIGH:
            raise ValueError("sample out of range")
        return x

    def axis(self, axis, count):
        mode = self.bits(MODE_BITS)
        if mode > MODE_RAW:
            raise ValueError("bad block mode")
        k = 0 if mode == MODE_RAW else self.bits(K_BITS)
        if k > MAX_K:
            raise ValueError("bad Rice parameter")
        self.modes[mode] += 1
        values = []
        for _ in range(count):
            x = self.value((axis, mode, k))
            self.before_last[axis], self.last[axis] = self.last[axis], x
            values.append(x)
        return values

    def samples(self, count):
        out = []
        while len(out) < count:
            n = min(BLOCK_SAMPLES, count - len(out))
            columns = [self.axis(axis, n) for axis in range(AXES)]
            out.extend([list(row) for row in zip(*columns)])
        return out


def rebuild(stored, shape):
    out = []
    source = iter(stored)
    while len(out) < shape.n_samples:
        out.extend([next(source)] * shape.group_at(len(out)))
    return out


def record_check(vector, payload):
    fields = struct.pack(HEADER_FIELDS, vector["seq"], vector["n_samples"], vector["trigger_index"], len(payload))
    return zlib.crc32(payload + fields)


def samples_crc(samples):
    return zlib.crc32(b"".join(struct.pack(SAMPLE_FIELDS, *s) for s in samples))


def check_vector(vector, decoder):
    shape = Shape(vector)
    stored = decoder.samples(shape.stored_count())
    problems = []
    if len(decoder.data) != vector["payload_len"] or record_check(vector, decoder.data) != vector["check"]:
        problems.append("check value")
    if stored != vector["stored"]:
        problems.append("stored samples")
    if samples_crc(rebuild(stored, shape)) != vector["rebuilt_crc32"]:
        problems.append("rebuilt samples")
    return problems


def main(path):
    with open(path, encoding="utf-8") as f:
        vectors = json.load(f)["vectors"]
    failed = 0
    for vector in vectors:
        decoder = Decoder(bytes.fromhex(vector["payload_hex"]))
        problems = check_vector(vector, decoder)
        failed += bool(problems)
        print(f"{vector['name']:22s} format {vector['format']} {vector['payload_len']:6d} B  "
              f"delta/slope/raw axis-blocks {decoder.modes}  escapes {decoder.escapes}  "
              f"{'FAIL: ' + ', '.join(problems) if problems else 'ok'}")
    print(f"CAPREC DECODER: {'FAIL' if failed else 'PASS'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
