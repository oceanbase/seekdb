/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OB_JIT_MEMORY_MANAGER_H
#define OB_JIT_MEMORY_MANAGER_H

#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/page_arena.h"
#include "core/ob_jit_allocator.h"

namespace oceanbase {
namespace jit {
namespace core {
class ObJitAllocator;

class ObJitMemoryManager : public llvm::RTDyldMemoryManager
{
  explicit ObJitMemoryManager(const ObJitMemoryManager&);
  void operator=(const ObJitMemoryManager&);
public:
  explicit ObJitMemoryManager(ObJitAllocator &allocator)
      : allocator_(allocator),
        code_section_addr_(nullptr),
        gcc_except_tab_addr_(nullptr),
        gcc_except_tab_size_(0)
  {}
  virtual ~ObJitMemoryManager() {}

  virtual uint8_t *allocateCodeSection(
      uintptr_t Size, unsigned Alignment, unsigned SectionID,
      llvm::StringRef SectionName)
  {
    uint8_t *ptr = reinterpret_cast<uint8_t*>(allocator_.alloc(JMT_RWE, Size, Alignment));
#if defined(__APPLE__)
    // Track __text section address for .eh_frame pc_begin fixup.
    if (SectionName == "__text") {
      code_section_addr_ = ptr;
    }
#endif
    return ptr;
  }

  virtual uint8_t *allocateDataSection(
      uintptr_t Size, unsigned Alignment, unsigned SectionID,
      llvm::StringRef SectionName, bool IsReadOnly){
    uint8_t *ptr = reinterpret_cast<uint8_t*>(allocator_.alloc(JMT_RO, Size, Alignment));
#if defined(__APPLE__)
    // Track __gcc_except_tab section address for .eh_frame LSDA fixup.
    // On macOS ARM64, RuntimeDyld doesn't properly relocate the LSDA pointer
    // in .eh_frame (ARM64_RELOC_SUBTRACTOR+UNSIGNED pair), so we fix it up
    // manually in registerEHFrames.
    if (SectionName == "__gcc_except_tab") {
      gcc_except_tab_addr_ = ptr;
      gcc_except_tab_size_ = Size;
    }
#endif
    return ptr;
  }

  virtual void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr, size_t Size) {
#if defined(__APPLE__)
    // Fix up LSDA pointers in .eh_frame before registration.
    // RuntimeDyld on MachO ARM64 does not correctly apply the
    // ARM64_RELOC_SUBTRACTOR + ARM64_RELOC_UNSIGNED relocation pair
    // within __eh_frame, leaving the LSDA pointer unrelocated and pointing
    // to garbage memory. We parse the CIE/FDE and patch the LSDA pointer
    // to correctly reference __gcc_except_tab.
    if (Size > 0) {
      fixupEHFrameRelocations(Addr, Size);
    }
#endif
    llvm::RTDyldMemoryManager::registerEHFrames(Addr, LoadAddr, Size);
    // Reset for next module
    code_section_addr_ = nullptr;
    gcc_except_tab_addr_ = nullptr;
    gcc_except_tab_size_ = 0;
  }

  virtual void deregisterEHFrames() {
    llvm::RTDyldMemoryManager::deregisterEHFrames();
  }

private:
#if defined(__APPLE__)
  // Read a ULEB128 value, advancing the pointer
  static uint64_t readULEB128(const uint8_t **p) {
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
      byte = **p; (*p)++;
      result |= ((uint64_t)(byte & 0x7f)) << shift;
      shift += 7;
    } while (byte & 0x80);
    return result;
  }

  // Get byte size of an encoded pointer given the DWARF encoding
  static size_t getEncodedPtrSize(uint8_t enc) {
    if (enc == 0xFF) return 0; // DW_EH_PE_omit
    switch (enc & 0x0F) {
      case 0x00: return sizeof(uintptr_t); // DW_EH_PE_absptr
      case 0x02: return 2;  // DW_EH_PE_udata2
      case 0x03: return 4;  // DW_EH_PE_udata4
      case 0x04: return 8;  // DW_EH_PE_udata8
      case 0x09: return 2;  // DW_EH_PE_sdata2
      case 0x0A: return 4;  // DW_EH_PE_sdata4 (note: some refs use 0x0B)
      case 0x0B: return 4;  // DW_EH_PE_sdata4
      case 0x0C: return 8;  // DW_EH_PE_sdata8
      default:   return sizeof(uintptr_t);
    }
  }

  // Fix up pc_begin and LSDA pointers in .eh_frame FDEs.
  // RuntimeDyld on MachO ARM64 does not correctly apply
  // ARM64_RELOC_SUBTRACTOR + ARM64_RELOC_UNSIGNED pairs in __eh_frame,
  // so both pc_begin (function start) and LSDA pointer end up wrong.
  void fixupEHFrameRelocations(uint8_t *ehFrame, size_t size) {
    const uint8_t *p = ehFrame;
    const uint8_t *end = ehFrame + size;

    // --- Parse CIE ---
    if (p + 4 > end) return;
    uint32_t cie_length = *(const uint32_t *)p; p += 4;
    if (cie_length == 0 || p + cie_length > end) return;
    const uint8_t *cie_end = p + cie_length;

    if (p + 4 > cie_end) return;
    uint32_t cie_id = *(const uint32_t *)p; p += 4;
    if (cie_id != 0) return; // not a CIE

    if (p >= cie_end) return;
    uint8_t version = *p++;

    const char *aug_str = (const char *)p;
    while (p < cie_end && *p) p++;
    if (p >= cie_end) return;
    p++; // skip null terminator

    bool has_z = false, has_L = false, has_P = false, has_R = false;
    for (const char *a = aug_str; *a; a++) {
      switch (*a) {
        case 'z': has_z = true; break;
        case 'L': has_L = true; break;
        case 'P': has_P = true; break;
        case 'R': has_R = true; break;
      }
    }

    readULEB128(&p); // code_alignment_factor
    { uint8_t b; do { b = *p++; } while (b & 0x80); } // data_alignment_factor (SLEB128)
    if (version >= 3) { readULEB128(&p); } else { p++; } // return_address_register

    uint8_t lsda_encoding = 0xFF;
    uint8_t fde_encoding = 0x00;

    if (has_z) {
      readULEB128(&p); // augmentation_data_length
      for (const char *a = aug_str; *a; a++) {
        if (*a == 'z') continue;
        if (*a == 'P') {
          uint8_t penc = *p++;
          p += getEncodedPtrSize(penc);
        } else if (*a == 'L') {
          lsda_encoding = *p++;
        } else if (*a == 'R') {
          fde_encoding = *p++;
        }
      }
    }

    // --- Parse FDE(s) ---
    p = cie_end;
    while (p + 4 <= end) {
      uint32_t fde_length = *(const uint32_t *)p; p += 4;
      if (fde_length == 0) break;
      if (p + fde_length > end) break;
      const uint8_t *fde_end = p + fde_length;

      uint32_t cie_ptr = *(const uint32_t *)p; p += 4;
      if (cie_ptr == 0) { p = fde_end; continue; } // another CIE

      // --- Fix pc_begin ---
      size_t fde_ptr_sz = getEncodedPtrSize(fde_encoding);
      bool fde_pcrel = ((fde_encoding & 0x70) == 0x10);
      if (code_section_addr_ != nullptr && fde_pcrel) {
        uint8_t *pc_begin_field = const_cast<uint8_t *>(p);
        if (fde_ptr_sz == 8) {
          int64_t correct = (int64_t)code_section_addr_ - (int64_t)pc_begin_field;
          *(int64_t *)pc_begin_field = correct;
        } else if (fde_ptr_sz == 4) {
          int32_t correct = (int32_t)((int64_t)code_section_addr_ - (int64_t)pc_begin_field);
          *(int32_t *)pc_begin_field = correct;
        }
      }
      p += fde_ptr_sz; // skip pc_begin
      p += fde_ptr_sz; // skip pc_range (not relocated, leave as-is)

      // --- Fix LSDA pointer ---
      if (has_z) {
        readULEB128(&p); // augmentation_data_length
        if (has_L && lsda_encoding != 0xFF && gcc_except_tab_addr_ != nullptr) {
          uint8_t *lsda_field = const_cast<uint8_t *>(p);
          size_t lsda_ptr_sz = getEncodedPtrSize(lsda_encoding);
          bool lsda_pcrel = ((lsda_encoding & 0x70) == 0x10);

          if (lsda_pcrel && lsda_ptr_sz == 8) {
            int64_t correct = (int64_t)gcc_except_tab_addr_ - (int64_t)lsda_field;
            *(int64_t *)lsda_field = correct;
          } else if (lsda_pcrel && lsda_ptr_sz == 4) {
            int32_t correct = (int32_t)((int64_t)gcc_except_tab_addr_ - (int64_t)lsda_field);
            *(int32_t *)lsda_field = correct;
          }
        }
      }
      p = fde_end;
    }
  }
#endif

  /// This method is called when object loading is complete and section page
  /// permissions can be applied.  It is up to the memory manager implementation
  /// to decide whether or not to act on this method.  The memory manager will
  /// typically allocate all sections as read-write and then apply specific
  /// permissions when this method is called.  Code sections cannot be executed
  /// until this function has been called.  In addition, any cache coherency
  /// operations needed to reliably use the memory are also performed.
  ///
  /// Returns true if an error occurred, false otherwise.
  virtual bool finalizeMemory(std::string *ErrMsg = 0) {
    return allocator_.finalize();
  }

#if defined(__aarch64__)
  /// Inform the memory manager about the total amount of memory required to
  /// allocate all sections to be loaded:
  /// \p CodeSize - the total size of all code sections
  /// \p DataSizeRO - the total size of all read-only data sections
  /// \p DataSizeRW - the total size of all read-write data sections
  ///
  /// Note that by default the callback is disabled. To enable it
  /// redefine the method needsToReserveAllocationSpace to return true.
  virtual void reserveAllocationSpace(uintptr_t CodeSize, uint32_t CodeAlign,
                                      uintptr_t RODataSize,
                                      uint32_t RODataAlign,
                                      uintptr_t RWDataSize,
                                      uint32_t RWDataAlign)
  {
    int64_t sz = CodeSize + CodeAlign + RODataSize + RODataAlign + RWDataSize + RWDataAlign;
    int64_t align = MAX3(CodeAlign, RODataAlign, RWDataAlign);
    allocator_.reserve(JMT_RWE, sz, align);
  }

  /// Override to return true to enable the reserveAllocationSpace callback.
  virtual bool needsToReserveAllocationSpace() { return true; }
#endif

private:
  uint8_t *alloc(uintptr_t Size, unsigned Alignment);

private:
  ObJitAllocator &allocator_;
  uint8_t *code_section_addr_;
  uint8_t *gcc_except_tab_addr_;
  size_t gcc_except_tab_size_;
};

}  // core
}  // jit
}  // oceanbase

#endif /* OB_JIT_MEMORY_MANAGER_H */
