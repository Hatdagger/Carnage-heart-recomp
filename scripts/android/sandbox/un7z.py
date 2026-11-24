#!/usr/bin/env python3
"""Minimal 7z extractor: LZMA / LZMA2 / copy coders (+ BCJ x86 not supported),
encoded or plain headers, empty files/dirs. Usage: un7z.py ARCHIVE OUTDIR"""
import lzma
import os
import struct
import sys
import zlib

K_END, K_HEADER, K_ARCHIVE_PROPS, K_ADD_STREAMS, K_MAIN_STREAMS, K_FILES_INFO = 0, 1, 2, 3, 4, 5
K_PACK_INFO, K_UNPACK_INFO, K_SUBSTREAMS_INFO, K_SIZE, K_CRC, K_FOLDER = 6, 7, 8, 9, 10, 11
K_CODERS_UNPACK_SIZE, K_NUM_UNPACK_STREAM, K_EMPTY_STREAM, K_EMPTY_FILE = 12, 13, 14, 15
K_NAME, K_ATTRIBUTES, K_ENCODED_HEADER, K_DUMMY = 17, 21, 23, 25


class R:
    def __init__(self, b):
        self.b, self.p = b, 0

    def byte(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def read(self, n):
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def num(self):
        first = self.byte()
        mask = 0x80
        val = 0
        for i in range(8):
            if not first & mask:
                high = first & (mask - 1)
                return val | (high << (8 * i))
            val |= self.byte() << (8 * i)
            mask >>= 1
        return val

    def u32(self):
        return struct.unpack('<I', self.read(4))[0]

    def bits(self, n):
        out, mask, cur = [], 0, 0
        for _ in range(n):
            if mask == 0:
                cur, mask = self.byte(), 0x80
            out.append(bool(cur & mask))
            mask >>= 1
        return out

    def defined_bits(self, n):
        alldef = self.byte()
        return [True] * n if alldef else self.bits(n)


def read_pack_info(r):
    pack_pos = r.num()
    n = r.num()
    sizes = []
    while True:
        t = r.byte()
        if t == K_END:
            break
        if t == K_SIZE:
            sizes = [r.num() for _ in range(n)]
        elif t == K_CRC:
            d = r.defined_bits(n)
            for x in d:
                if x:
                    r.u32()
    return pack_pos, sizes


def read_folder(r):
    ncoders = r.num()
    coders = []
    tot_out = 0
    for _ in range(ncoders):
        flags = r.byte()
        cid = r.read(flags & 0x0F)
        if flags & 0x10:
            nin, nout = r.num(), r.num()
        else:
            nin, nout = 1, 1
        props = r.read(r.num()) if flags & 0x20 else b''
        coders.append((cid, props, nin, nout))
        tot_out += nout
    for _ in range(tot_out - 1):
        r.num(); r.num()  # bind pairs
    tot_in = sum(c[2] for c in coders)
    npacked = tot_in - (tot_out - 1)
    if npacked > 1:
        for _ in range(npacked):
            r.num()
    return {'coders': coders, 'nout': tot_out}


def read_unpack_info(r):
    assert r.byte() == K_FOLDER
    n = r.num()
    assert r.byte() == 0, 'external folders unsupported'
    folders = [read_folder(r) for _ in range(n)]
    assert r.byte() == K_CODERS_UNPACK_SIZE
    for f in folders:
        f['sizes'] = [r.num() for _ in range(f['nout'])]
    while True:
        t = r.byte()
        if t == K_END:
            break
        if t == K_CRC:
            d = r.defined_bits(n)
            for x in d:
                if x:
                    r.u32()
    return folders


def read_substreams(r, folders):
    counts = [1] * len(folders)
    sizes = []
    t = r.byte()
    if t == K_NUM_UNPACK_STREAM:
        counts = [r.num() for _ in folders]
        t = r.byte()
    if t == K_SIZE:
        for f, c in zip(folders, counts):
            if c == 0:
                continue
            s = 0
            for _ in range(c - 1):
                v = r.num()
                sizes.append(v)
                s += v
            sizes.append(f['sizes'][-1] - s)
        t = r.byte()
    else:
        for f, c in zip(folders, counts):
            if c == 1:
                sizes.append(f['sizes'][-1])
    while t != K_END:
        if t == K_CRC:
            total = sum(counts)
            d = r.defined_bits(total)
            for x in d:
                if x:
                    r.u32()
        t = r.byte()
    return counts, sizes


def read_streams_info(r):
    pack_pos, pack_sizes, folders = 0, [], []
    counts, sizes = None, None
    while True:
        t = r.byte()
        if t == K_END:
            break
        if t == K_PACK_INFO:
            pack_pos, pack_sizes = read_pack_info(r)
        elif t == K_UNPACK_INFO:
            folders = read_unpack_info(r)
        elif t == K_SUBSTREAMS_INFO:
            counts, sizes = read_substreams(r, folders)
    if counts is None:
        counts = [1] * len(folders)
        sizes = [f['sizes'][-1] for f in folders]
    return pack_pos, pack_sizes, folders, counts, sizes


def decode_folder(data, folder):
    out = data
    # coders are listed outermost-first; apply in reverse (innermost packed first)
    for cid, props, nin, nout in reversed(folder['coders']):
        if cid == b'\x00':
            continue
        if cid == b'\x03\x01\x01':  # LZMA
            pb_lp_lc = props[0]
            lc, rem = pb_lp_lc % 9, pb_lp_lc // 9
            lp, pb = rem % 5, rem // 5
            dic = struct.unpack('<I', props[1:5])[0]
            f = {'id': lzma.FILTER_LZMA1, 'dict_size': dic, 'lc': lc, 'lp': lp, 'pb': pb}
            d = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[f])
            out = d.decompress(out, max_length=folder['sizes'][-1])
        elif cid == b'\x21':  # LZMA2
            p = props[0]
            dic = 0xFFFFFFFF if p == 40 else (2 | (p & 1)) << (p // 2 + 11)
            d = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA2, 'dict_size': dic}])
            out = d.decompress(out)
        elif cid == b'\x03\x03\x01\x03':  # BCJ x86
            d = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_X86}])
            out = d.decompress(out)
        else:
            raise SystemExit('unsupported coder ' + cid.hex())
    return out[:folder['sizes'][-1]]


def main():
    arc, outdir = sys.argv[1], sys.argv[2]
    f = open(arc, 'rb')
    sig = f.read(32)
    assert sig[:6] == b"7z\xbc\xaf'\x1c"
    nh_off, nh_size, _ = struct.unpack('<QQI', sig[12:32])
    f.seek(32 + nh_off)
    hdr = f.read(nh_size)
    r = R(hdr)
    t = r.byte()
    while t == K_ENCODED_HEADER:
        pack_pos, pack_sizes, folders, _, _ = read_streams_info(r)
        f.seek(32 + pack_pos)
        hdr = decode_folder(f.read(pack_sizes[0]), folders[0])
        r = R(hdr)
        t = r.byte()
    assert t == K_HEADER
    t = r.byte()
    if t == K_ARCHIVE_PROPS:
        raise SystemExit('archive props unsupported')
    if t == K_ADD_STREAMS:
        raise SystemExit('additional streams unsupported')
    streams = None
    if t == K_MAIN_STREAMS:
        streams = read_streams_info(r)
        t = r.byte()
    names, empty_stream, empty_file, nfiles = [], [], [], 0
    if t == K_FILES_INFO:
        nfiles = r.num()
        empty_stream = [False] * nfiles
        while True:
            pt = r.byte()
            if pt == K_END:
                break
            size = r.num()
            end = r.p + size
            if pt == K_NAME:
                assert r.byte() == 0
                raw = r.b[r.p:end]
                names = raw.decode('utf-16-le').split('\x00')[:nfiles]
            elif pt == K_EMPTY_STREAM:
                empty_stream = r.bits(nfiles)
            elif pt == K_EMPTY_FILE:
                n_empty = sum(empty_stream)
                empty_file = r.bits(n_empty)
            r.p = end
    os.makedirs(outdir, exist_ok=True)
    real = os.path.realpath(outdir)
    pack_pos, pack_sizes, folders, counts, sizes = streams
    # decode folder by folder, slicing substreams
    pos = 32 + pack_pos
    fi = 0
    si = 0
    data_iter = []
    for k, folder in enumerate(folders):
        f.seek(pos)
        blob = f.read(pack_sizes[k])
        pos += pack_sizes[k]
        out = decode_folder(blob, folder)
        off = 0
        for _ in range(counts[k]):
            data_iter.append(out[off:off + sizes[si]])
            off += sizes[si]
            si += 1
    di = 0
    ei = 0
    for i in range(nfiles):
        name = names[i].replace('\\', '/')
        dst = os.path.realpath(os.path.join(outdir, name))
        if not dst.startswith(real + os.sep):
            raise SystemExit('unsafe path ' + name)
        if empty_stream[i]:
            is_dir = not (empty_file[ei] if ei < len(empty_file) else False)
            ei += 1
            if is_dir:
                os.makedirs(dst, exist_ok=True)
            else:
                open(dst, 'wb').close()
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as o:
            o.write(data_iter[di])
        print(name, len(data_iter[di]))
        di += 1


main()
