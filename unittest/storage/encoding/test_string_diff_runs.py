"""Compile the actual inline codec and batch helper with ASAN/UBSAN.

No server or scoring script is replaced. The small harness compares the new
batch helper with the existing scalar decoder and independently packed bytes.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
ENC = ROOT / 'src/storage/blocksstable/encoding'


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


hex_header = (ENC / 'ob_hex_string_encoder.h').read_text()
diff_header = (ENC / 'ob_string_diff_encoder.h').read_text()
decoder = (ENC / 'ob_string_diff_decoder.cpp').read_text()
unpacker = hex_header[hex_header.index('class ObHexStringUnpacker'):hex_header.index('OB_INLINE void ObHexStringPacker::pack')]
unpack_one = function(hex_header, 'OB_INLINE unsigned char ObHexStringUnpacker::unpack()')
unpack_many = function(hex_header, 'OB_INLINE void ObHexStringUnpacker::unpack(unsigned char *str')
metadata = diff_header[diff_header.index('struct ObStringDiffHeader'):diff_header.index('// fix length string diff encoder')]
copy_helpers = diff_header[diff_header.index('template <typename Copy, typename IsDiff, typename PartStr, typename FullStr>\nOB_INLINE void ObStringDiffHeader::copy_string'):diff_header.index('} // end namespace blocksstable')]
batch = function(decoder, 'void unpack_diff_runs(')
common_batch = function(decoder, 'void copy_common_string_batch(')
source = r'''
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <new>
#include <random>
#include <vector>
#define OB_INLINE inline
#define MEMCPY std::memcpy
#define TO_STRING_KV(...)
'''+unpacker+unpack_one+unpack_many+metadata+copy_helpers+batch+common_batch+r'''
int main() {
  std::mt19937 rng(20261101);
  for (int trial=0; trial<20000; ++trial) {
    const int runs=1+rng()%(trial%2 ? 4 : 64);
    const int alphabet_size=1+rng()%16;
    std::vector<unsigned char> header_bytes(sizeof(ObStringDiffHeader)+runs+16+8192);
    auto *h=new (header_bytes.data()) ObStringDiffHeader;
    h->diff_desc_cnt_=runs;
    h->hex_char_array_size_=alphabet_size;
    auto *alphabet=h->hex_char_array();
    for (int i=0; i<alphabet_size; ++i) alphabet[i]=(i*17+trial)%256;
    int length=0, nibble_count=0;
    for (int i=0; i<runs; ++i) {
      auto &d=h->diff_descs_[i];
      d.count_=1+rng()%(trial%2 ? 16 : 127);
      d.diff_=trial%4==0 ? 0 : trial%4==1 ? 1 : rng()%2;
      length+=d.count_;
      if (d.diff_) nibble_count+=d.count_;
    }
    h->string_size_=length;
    // Pack independently: high nibble first, including odd total lengths.
    std::vector<unsigned char> packed((nibble_count+1)/2);
    std::vector<char> expected(length+32, '\x5a'), reference=expected, actual=expected;
    int pos=0, packed_pos=0, common_pos=0;
    for (int i=0; i<runs; ++i) {
      const auto &d=h->diff_descs_[i];
      for (int j=0; j<d.count_; ++j,++pos) {
        if (d.diff_) {
          const int value=rng()%alphabet_size;
          packed[packed_pos/2] |= value << (packed_pos%2 ? 0 : 4);
          ++packed_pos;
          expected[pos]=alphabet[value];
        } else {
          const char value=rng()%256;
          const_cast<char*>(h->common_data())[common_pos++]=value;
          expected[pos]=value;
        }
      }
    }
    h->copy_string(ObStringDiffHeader::LeftToRight(),std::logical_not<uint8_t>(),
                   h->common_data(),reference.data());
    ObHexStringUnpacker scalar(alphabet,packed.data());
    h->copy_hex_string(scalar,
      static_cast<void(ObHexStringUnpacker::*)(unsigned char&)>(&ObHexStringUnpacker::unpack),
      reinterpret_cast<unsigned char*>(reference.data()));
    const int stride=length+32;
    std::vector<char> rows(stride*5,'\x5a');
    copy_common_string_batch(*h,rows.data(),stride,5);
    for (int row=0; row<5; ++row) {
      unpack_diff_runs(*h,packed.data(),rows.data()+row*stride);
      std::memcpy(actual.data(),rows.data()+row*stride,stride);
      assert(actual==expected && reference==expected);
    }
  }
  std::cout << "20000 randomized codec cases PASS (odd/even runs, binary alphabets, guards)\n";
}
'''
with tempfile.TemporaryDirectory(prefix='string-diff-runs-') as directory:
    cpp = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    cpp.write_text(source)
    subprocess.run(['g++', '-std=c++17', '-O2', '-g', '-fno-omit-frame-pointer',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
