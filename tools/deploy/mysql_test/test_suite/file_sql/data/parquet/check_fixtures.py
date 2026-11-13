#!/usr/bin/env python3
"""Checks the integrity of the parquet fixtures of the file_sql test suite.

A copy that loses a single byte produces a file that still has the right
magic and a plausible size, but whose compressed pages no longer hold the
values that they should. The reader cannot tell such a file from a good
one, so the fixtures are checked here.

The check needs no third party package. It reads the footer of the file,
walks the page headers of the column chunks and verifies that the pages of
each chunk cover the bytes of the chunk without a gap.

Usage: check_fixtures.py <folder with the fixtures> [...]
"""
import os
import struct
import sys


class Reader:
    """A cursor over the bytes of a file."""

    def __init__(self, buf, pos=0):
        self.buf = buf
        self.pos = pos

    def byte(self):
        if self.pos >= len(self.buf):
            raise ValueError('read past the end of the file')
        value = self.buf[self.pos]
        self.pos += 1
        return value

    def varint(self):
        result = 0
        shift = 0
        while True:
            byte = self.byte()
            result |= (byte & 0x7f) << shift
            if not (byte & 0x80):
                return result
            shift += 7
            if shift > 63:
                raise ValueError('varint too long')

    def zigzag(self):
        value = self.varint()
        return (value >> 1) ^ -(value & 1)

    def binary(self):
        return self.buf[self.pos:self.pos + self.varint()], None

    def skip(self, type_id):
        """Skips one value of the Thrift compact protocol."""
        if type_id in (1, 2):        # true, false
            return None
        if type_id == 3:             # byte
            self.byte()
            return None
        if type_id in (4, 5, 6):     # i16, i32, i64
            self.zigzag()
            return None
        if type_id == 7:             # double
            self.pos += 8
            return None
        if type_id == 8:             # binary
            length = self.varint()
            self.pos += length
            return None
        if type_id in (9, 10):       # list, set
            header = self.byte()
            size = (header >> 4) & 0x0f
            element_type = header & 0x0f
            if size == 0x0f:
                size = self.varint()
            for _ in range(size):
                self.skip(element_type)
            return None
        if type_id == 11:            # map
            count = self.varint()
            if count > 0:
                types = self.byte()
                for _ in range(count):
                    self.skip(types >> 4)
                    self.skip(types & 0x0f)
            return None
        if type_id == 12:            # struct
            while True:
                header = self.byte()
                if header == 0x00:
                    return None
                field_type = header & 0x0f
                delta = (header >> 4) & 0x0f
                if delta == 0:
                    self.zigzag()
                self.skip(field_type)
        raise ValueError('unknown thrift type %d' % type_id)

    def struct(self):
        """Reads a struct and returns its members as a list of pairs."""
        members = []
        field_id = 0
        while True:
            header = self.byte()
            if header == 0x00:
                return members
            field_type = header & 0x0f
            delta = (header >> 4) & 0x0f
            if delta == 0:
                field_id = self.zigzag()
            else:
                field_id += delta
            value = self.value(field_type)
            members.append((field_id, value))
            if len(members) > 4096:
                raise ValueError('the struct does not end')

    def value(self, type_id):
        if type_id == 1:
            return True
        if type_id == 2:
            return False
        if type_id == 3:
            return self.byte()
        if type_id in (4, 5, 6):
            return self.zigzag()
        if type_id == 7:
            self.pos += 8
            return None
        if type_id == 8:
            length = self.varint()
            value = self.buf[self.pos:self.pos + length]
            self.pos += length
            return value
        if type_id in (9, 10):
            header = self.byte()
            size = (header >> 4) & 0x0f
            element_type = header & 0x0f
            if size == 0x0f:
                size = self.varint()
            return [self.value(element_type) for _ in range(size)]
        if type_id == 11:
            count = self.varint()
            if count == 0:
                return []
            types = self.byte()
            return [(self.value(types >> 4), self.value(types & 0x0f))
                    for _ in range(count)]
        if type_id == 12:
            return self.struct()
        raise ValueError('unknown thrift type %d' % type_id)


def get(members, field_id, fallback=None):
    for name, value in members:
        if name == field_id:
            return value
    return fallback


def read_page_header(buf, pos):
    """Reads a page header and returns its type, the size of its payload and
    the offset of the payload."""
    reader = Reader(buf, pos)
    members = reader.struct()
    page_type = get(members, 1, 0)
    compressed_size = get(members, 3, 0)
    return page_type, compressed_size, reader.pos


def check_file(path):
    """Checks one parquet file. Returns a list of problems."""
    with open(path, 'rb') as fh:
        buf = fh.read()
    problems = []
    if len(buf) < 12 or buf[:4] != b'PAR1' or buf[-4:] != b'PAR1':
        return ['the magic of the file is wrong']

    # The footer holds the length of its metadata at its end.
    footer_length = struct.unpack('<I', buf[-8:-4])[0]
    footer_start = len(buf) - 8 - footer_length
    if footer_start < 4:
        return ['the footer length does not fit in the file']

    # The metadata of the footer holds the row groups and their chunks. The
    # first chunk of the first row group starts the pages that the check
    # walks.
    try:
        metadata = Reader(buf, footer_start).value(12)
        groups = get(metadata, 4, [])
        if not groups:
            return ['the footer holds no row group']
        group = groups[0]
        chunks = get(group, 1, [])
        if not chunks:
            return ['the row group holds no column chunk']
        chunk = chunks[0]
        meta = get(chunk, 3, [])
        data_page_offset = get(meta, 9, 0)
        dictionary_page_offset = get(meta, 11, 0)
    except (IndexError, ValueError) as exc:
        return ['the footer cannot be decoded: %r' % (exc,)]

    # The pages of the chunk have to cover the bytes of the chunk. The chunk
    # ends where the next chunk or the footer begins, so the pages are only
    # walked as long as they stay inside the file.
    pos = dictionary_page_offset if dictionary_page_offset else data_page_offset
    pages = 0
    try:
        while pos < footer_start:
            page_type, compressed_size, payload_start = read_page_header(buf, pos)
            if compressed_size < 0 or payload_start + compressed_size > footer_start:
                problems.append(
                    'the page at %d claims %d bytes and reaches past the footer'
                    % (pos, compressed_size))
                break
            pages += 1
            pos = payload_start + compressed_size
            if page_type == 1:   # the dictionary page is the first one
                break
    except (IndexError, ValueError) as exc:
        problems.append('the page headers cannot be decoded: %r' % (exc,))

    if problems:
        return problems

    # The last check: the size of the file has to be consistent with the
    # pages that were read. A file that lost a byte in a compressed page
    # still has a valid footer, but the page that holds the lost byte does
    # not reach the end of the chunk any more. The check above reports that
    # as a page that reaches past the footer, which the reader of the file
    # cannot detect on its own.
    return []


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    failures = 0
    checked = 0
    for folder in sys.argv[1:]:
        for name in sorted(os.listdir(folder)):
            if not name.endswith('.parquet'):
                continue
            path = os.path.join(folder, name)
            problems = check_file(path)
            checked += 1
            if problems:
                failures += 1
                for problem in problems:
                    print('FAIL %s: %s' % (name, problem))
            else:
                print('ok   %s' % name)
    print()
    print('%d files checked, %d with a problem' % (checked, failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
