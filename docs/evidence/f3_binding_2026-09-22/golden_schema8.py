"""Independent schema-8 framing oracle; does not import/call the C++ codec."""
import hashlib
import struct


def digest(marker):
    return bytes([marker]) + bytes(31)


payload = (struct.pack('<QQ', 12, 42) + digest(0x44)
           + struct.pack('<QQQ', 10, 2, 3) + digest(0x55))
preimage = struct.pack('<HH', 8, 8) + payload
frame = b'V2KC' + struct.pack('<I', len(payload)) + preimage + hashlib.sha256(preimage).digest()
print('command_preimage', hashlib.sha256(preimage).hexdigest())
print('command_frame', hashlib.sha256(frame).hexdigest())

payload = struct.pack('<BBQQBQBQQBQQQQBBBBHHB',
                      16, 0, 17, 2, 3, 99, 4, 4, 8, 1, 10, 2, 3, 21, 16, 2, 1, 1, 4, 5, 0)
preimage = struct.pack('<H', 8) + payload
frame = b'V2KR' + struct.pack('<I', len(payload)) + preimage + hashlib.sha256(preimage).digest()
print('result_preimage', hashlib.sha256(preimage).hexdigest())
print('result_frame', hashlib.sha256(frame).hexdigest())

payload = (struct.pack('<Q', 0) + struct.pack('<B', 5)
           + struct.pack('<QQQQ', 0, 0, 0, 0)
           + struct.pack('<HHBB', 0, 0, 0, 0)
           + struct.pack('<HHHHHHHH', 0, 0, 0, 0, 0, 0, 0, 0) + bytes(32))
assert len(payload) == 95
print('empty_snapshot', hashlib.sha256(struct.pack('<H', 8) + payload).hexdigest())
