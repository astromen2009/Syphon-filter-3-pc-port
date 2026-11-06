# Recursos del port (imagenes de los titulos, sonidos de los menus, fondo del
# lanzador y dibujo del mando) en un solo archivo comprimido: recursos.bin.
#
#   python3 recursos.py crear SALIDA.bin ARCHIVO_O_HPP...   (crea el paquete)
#   python3 recursos.py inc ENTRADA.bin SALIDA.inc          (lo pasa a C++ para compilar)
#
# Formato: "SF3RES01", u32 n, n x {u8 largo, nombre, u32 tamano, u32 comprimido,
# datos LZSS}. LZSS: byte de banderas (bit 1 = literal), referencia de 2 bytes
# = desplazamiento (12 bits, 1..4096) y largo (4 bits, 3..18).
import os, re, struct, sys


def lzss(data):
    out = bytearray(); i = 0; n = len(data)
    head = {}
    while i < n:
        flags_at = len(out); out.append(0); flags = 0
        for bit in range(8):
            if i >= n: break
            best_len = 0; best_off = 0
            if i + 3 <= n:
                key = data[i:i + 3]
                cands = head.get(key, [])
                for p in reversed(cands[-64:]):
                    off = i - p
                    if off > 4096: break
                    l = 3
                    while l < 18 and i + l < n and data[p + l] == data[i + l]: l += 1
                    if l > best_len: best_len, best_off = l, off
                        # (los candidatos van de cerca a lejos)
                    if l == 18: break
            if best_len >= 3:
                v = ((best_off - 1) << 4) | (best_len - 3)
                out += struct.pack('<H', v)
                for k in range(best_len):
                    if i + 3 <= n: head.setdefault(data[i:i + 3], []).append(i)
                    i += 1
            else:
                flags |= 1 << bit; out.append(data[i])
                if i + 3 <= n: head.setdefault(data[i:i + 3], []).append(i)
                i += 1
        out[flags_at] = flags
    return bytes(out)


def unlzss(comp, size):
    out = bytearray(); i = 0
    while len(out) < size:
        flags = comp[i]; i += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (1 << bit): out.append(comp[i]); i += 1
            else:
                v = comp[i] | (comp[i + 1] << 8); i += 2
                off = (v >> 4) + 1; l = (v & 15) + 3
                for _ in range(l): out.append(out[-off])
    return bytes(out)


def arrays_from_hpp(path):
    """Saca los arreglos 'inline constexpr TIPO NOMBRE[...] = {...};' de un .hpp."""
    text = open(path, encoding='utf-8').read()
    res = []
    for m in re.finditer(r'inline constexpr std::(u?int16_t)\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S):
        typ, name, body = m.groups()
        vals = [int(v, 0) for v in re.findall(r'-?(?:0x[0-9a-fA-F]+|\d+)', body)]
        fmt = '<%d%s' % (len(vals), 'H' if typ == 'uint16_t' else 'h')
        res.append((name, struct.pack(fmt, *vals)))
    return res


def crear(out, inputs):
    items = []
    for p in inputs:
        if p.endswith('.hpp'): items += arrays_from_hpp(p)
        else: items.append((os.path.basename(p), open(p, 'rb').read()))
    blob = bytearray(b'SF3RES01' + struct.pack('<I', len(items)))
    for name, data in items:
        c = lzss(data)
        assert unlzss(c, len(data)) == data, name
        nb = name.encode()
        blob += struct.pack('<B', len(nb)) + nb + struct.pack('<II', len(data), len(c)) + c
        print('%-24s %8d -> %8d' % (name, len(data), len(c)))
    open(out, 'wb').write(blob)
    print(out, len(blob), 'bytes')


def inc(src, out):
    d = open(src, 'rb').read()
    with open(out, 'w') as f:
        f.write('// generado por tools/recursos.py desde recursos.bin\n')
        for i in range(0, len(d), 32): f.write(','.join(str(b) for b in d[i:i + 32]) + ',\n')


if __name__ == '__main__':
    if sys.argv[1] == 'crear': crear(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == 'inc': inc(sys.argv[2], sys.argv[3])
