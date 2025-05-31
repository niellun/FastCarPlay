from Crypto.Cipher import AES
import struct
import json

inEnc = False
inSize = 0
inCmd = 0
inSet = False

outEnc = False
outCmd = 0
outSize = 0
outSet = False

encKey = 0

def build_iv_le(key):
    iv = bytearray(16)
    iv[1]  = (key >> 0) & 0xFF
    iv[4]  = (key >> 8) & 0xFF
    iv[9]  = (key >> 16) & 0xFF
    iv[12] = (key >> 24) & 0xFF
    return bytes(iv)

def generate_key(key):
    base = "SkBRDy3gmrw1ieH0"
    key_bytes = bytearray(16)
    for i in range(16):
        key_bytes[i] = ord(base[(key + i) % 16])
    return bytes(key_bytes)

def decrypt_hex_cipher(ciphertext, intkey, iv_builder):
    key = generate_key(intkey)
    iv = iv_builder(intkey)
    cipher = AES.new(key, AES.MODE_CFB, iv=iv, segment_size=128)
    plaintext = cipher.decrypt(ciphertext)
    return plaintext

def bytes_to_printable_ascii(byte_data):
    return ''.join((chr(b) if 32 <= b <= 126 else '.') for b in byte_data)

def bytes_to_int_list_le(byte_data):
    length = len(byte_data)
    padded_length = (length + 3) // 4 * 4
    padded = byte_data.ljust(padded_length, b'\x00')
    return list(struct.unpack('<' + 'I' * (padded_length // 4), padded))

def check_types(capdata: bytes) -> tuple[bool, bool]:
    is_aa = capdata.startswith(bytes.fromhex('aa55aa55'))
    is_bb = capdata.startswith(bytes.fromhex('bb55bb55'))
    if is_aa or is_bb:
        if len(capdata) >= 12:
            size = int.from_bytes(capdata[4:8], 'little')
            cmd = int.from_bytes(capdata[8:12], 'little')
            return True, is_bb, size, cmd    
    return False, False, None, None

def processPacket(frame, out, capdata, cmd, size, enc):
    global encKey
    if enc and encKey != 0 and size > 0:
        capdata = decrypt_hex_cipher(capdata, encKey, build_iv_le)
    if out and cmd == 240 and size == 4 and len(capdata) >= 4:
        encKey = int.from_bytes(capdata[0:4], 'little')
    if cmd == 170:
        return
    res = f"{frame:>5}{cmd:>5} {'<' if not out else '>'}{' ' if not enc else '*'} {size:<5} : "
    str = ""
    if len(capdata)<1000:
        str = bytes_to_printable_ascii(capdata)
    else:
        bts = ""
        for i in range(0, min(len(capdata), 20), 4):
            chunk = capdata[i:i+4]
            val = int.from_bytes(chunk, 'little')
            bts = bts+f"{val} "
    if size == 4:
        str = int.from_bytes(capdata[0:4], 'little')
    print(f"{'\033[92m' if not out else '\033[93m'}{res}{str}\033[0m")
    print(f"                    {' '.join(f'{b:02x}' for b in capdata[:20])}")
    

def processInc(frame, capdata):
    global inEnc, inSize, inCmd, inSet
    header, enc, size, cmd = check_types(capdata)
    if header:
        inEnc = enc
        inSize = size
        inCmd = cmd
        inSet = header        
        if inSize == 0:
            processPacket(frame, False, bytearray(), inCmd, inSize, inEnc)
        return

    if inSet:
        processPacket(frame, False, capdata, inCmd, inSize, inEnc)
    inSet = header

def processOut(frame, capdata):
    global outEnc, outSize, outCmd, outSet
    header, enc, size, cmd = check_types(capdata)
    if header:
        outEnc = enc
        outSize = size
        outCmd = cmd
        outSet = header        
        if outSize == 0:
            processPacket(frame, True, bytearray(), outCmd, outSize, outEnc)
        return

    if outSet:
        processPacket(frame, True, capdata, outCmd, outSize, outEnc)
    outSet = header


def processFile(file_path):
    with open(file_path) as f:
        data = json.load(f)

    for entry in data:
        layers = entry['_source']['layers']
        usb = layers.get('usb', {})
        frame_number = layers.get('frame', {}).get('frame.number')
        direction = usb.get('usb.endpoint_address_tree', {}).get('usb.endpoint_address.direction')
        urb_type = usb.get('usb.urb_type')
        capdata = entry['_source']['layers'].get('usb.capdata')

        if capdata:
            byte_array = bytes(int(b, 16) for b in capdata.split(':'))
            if (direction == "0" and urb_type == "'S'"):
                processOut(frame_number, byte_array)
            if(direction == "1" and urb_type == "'C'"):
                processInc(frame_number, byte_array)

print("This tool decode carlinkit protocol")
print("It requred JSON export of USB events filtered by device")
file_path = input("Path to JSON export from PCAPNG: ").strip().strip("'")
processFile(file_path)
