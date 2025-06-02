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

###### PROTOCOL

def printKey(bytes, len):
    if len == 4:
        return "Key "+printInts(bytes, len)
    if len == 0:
        return "<Request>"
    return printInts(bytes, len)

def printInts(bytes, len, max = 10, start=0):
    max = 10
    count = min((len-start) // 4, max)
    ints = [
        str(int.from_bytes(bytes[start+i*4:start+(i+1)*4], byteorder='little', signed=False))
        for i in range(count)
    ]
    result = ' '.join(ints)
    if (len-start) // 4 > max:
        result += ' ...'
    return result

def printBytes(bytes, len, max=20, start=0):
    max = 20
    count = min(len-start, max)
    if count<1:
        return "_"
    result = f"{' '.join(f'{b:02x}' for b in bytes[start:start+count])}"
    if count>max:
        result+= " ..."
    return result

def stringOrFunc(bytes, len, func, start=0):
    result = []
    for b in bytes[start:start + len]:
        if 32 <= b <= 126:  
            result.append(chr(b))
        else:
            return func(bytes, len, start=start)
    return ''.join(result)

def printStr0(bytes, len, start=0):
    return ''.join((chr(b) if (32 <= b <= 126 or b == 0) else '.') for b in bytes[start:start + len])

def printFile(bytes, len):
    len1 = int.from_bytes(bytes[0:4], 'little')
    result = printStr0(bytes, len1, 4)
    len2 = int.from_bytes(bytes[4+len1:8+len1], 'little')
    if len2 % 4 == 0:
        result = result+":"+stringOrFunc(bytes, len, printInts, start=len1+8)
    else:
        result = result+":"+stringOrFunc(bytes, len, printBytes, start=len1+8)
    return result

def cmd8(bytes, len):
    if len != 4:
        return printBytes(bytes, len)
    cmd = int.from_bytes(bytes[0:4], 'little')
    res = f"[{cmd}] "
    match cmd:
        case 1: return res+"Start audio recording"
        case 2: return res+"Stop audio recording"
        case 7: return res+"Mic type car"
        case 15: return res+"Mic type dongle"
        case 21: return res+"Mic type phone"
        case 24: return res+"Wifi 2.4Ghz"
        case 25: return res+"Wifi 5Ghz"
        case 22: return res+"Audio transfer bluetooth"
        case 23: return res+"Audio transfer dongle"               
        case 500: return res+"Request video focus"
        case 501: return res+"Release video focus"
        case 1003: return res+"Scanning devices"
        case 1004: return res+"Device found"
        case 1005: return res+"Device not found"
        case 1006: return res+"Connect device failed"
        case 1007: return res+"Dongle bluetooth connected"
        case 1008: return res+"Dongle bluetooth disconnected"
        case 1009: return res+"Dongle wifi connected"
        case 1010: return res+"Dongle wifi disconnected"
        case 1011: return res+"Bluetooth pairing started"

    return res+""

    
    

def getCmd(id):
    match id:
        case 1: return "Open", printInts
        case 2: return "Plugged", printInts
        case 3: return "State", None
        case 4: return "Unplugged", printInts
        case 5: return "Touch", None
        case 6: return "Video", printInts
        case 7: return "Audio", printInts
        case 8: return "Control", cmd8
        case 10: return "App info", None
        case 13: return "Bt info", printStr0
        case 14: return "Wifi info", printStr0
        case 15: return "Disconnect", None        
        case 18: return "Devices", printStr0
        case 20: return "Mfr name", None
        case 22: return "Camera", None        
        case 25: return "Json Ctl", None
        case 42: return "Media inf", None
        case 136: return "App log", None        
        case 153: return "Send file", printFile
        case 162: return "Daynight", None
        case 170: return "Heartbeat", None
        case 204: return "Version", printStr0
        case 206: return "Reboot", None
        case 240: return "Encrypt", printInts
    return "", None

###### ENCRYPTION

def build_iv_le(f5604m):
    iv = bytearray(16)
    iv[1]  = (f5604m >> 0) & 0xFF
    iv[4]  = (f5604m >> 8) & 0xFF
    iv[9]  = (f5604m >> 16) & 0xFF
    iv[12] = (f5604m >> 24) & 0xFF
    return bytes(iv)

def generate_key(f5604m):
    base = "SkBRDy3gmrw1ieH0"
    key_bytes = bytearray(16)
    for i in range(16):
        key_bytes[i] = ord(base[(f5604m + i) % 16])
    return bytes(key_bytes)

def decrypt_hex_ciphertext(hex_ciphertext, intkey, iv_builder):
    key = generate_key(intkey)
    iv = iv_builder(intkey)
    cipher = AES.new(key, AES.MODE_CFB, iv=iv, segment_size=128)
    ciphertext = bytes.fromhex(hex_ciphertext)
    plaintext = cipher.decrypt(ciphertext)
    return plaintext

def decrypt_hex_cipher(ciphertext, intkey, iv_builder):
    key = generate_key(intkey)
    iv = iv_builder(intkey)
    cipher = AES.new(key, AES.MODE_CFB, iv=iv, segment_size=128)
    plaintext = cipher.decrypt(ciphertext)
    return plaintext

###### HELPERS

def bytes_to_printable_ascii(byte_data):
    return ''.join((chr(b) if 32 <= b <= 126 else '.') for b in byte_data)

def bytes_to_int_list_le(byte_data):
    # Pad if length is not a multiple of 4
    length = len(byte_data)
    padded_length = (length + 3) // 4 * 4
    padded = byte_data.ljust(padded_length, b'\x00')
    # Unpack as little-endian unsigned ints
    return list(struct.unpack('<' + 'I' * (padded_length // 4), padded))

###### DECODING

def check_types(capdata: bytes) -> tuple[bool, bool]:
    is_aa = capdata.startswith(bytes.fromhex('aa55aa55'))
    is_bb = capdata.startswith(bytes.fromhex('bb55bb55'))
    if is_aa or is_bb:
        if len(capdata) >= 12:  # 4 bytes prefix + 8 bytes data
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

    #if cmd == 170 or cmd == 7 or cmd == 5 or cmd == 6:
    #    return
    
    cmdName, cmdProc = getCmd(cmd)
    cmdstr = f"{cmdName} [{cmd}]"
    res = f"{frame:>5} {cmdstr:>15} {'>' if not out else '<'}{' ' if not enc else '*'} "
    str = ""
    if cmdProc:
        str = cmdProc(capdata, size)
    else:
        if len(capdata)<1000:
            str = stringOrFunc(capdata, size, printBytes)
        else:
            str = f"{' '.join(f'{b:02x}' for b in capdata[:40])}"

    if size == 0:
        str = "_"

    print(f"{'\033[92m' if not out else '\033[93m'}{res}{str}\033[0m")
    

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
