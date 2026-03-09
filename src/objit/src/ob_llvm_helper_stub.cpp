/*
 * Stub implementations for ObLLVMHelper and related classes on Android.
 * LLVM JIT is not available on Android; all methods return OB_NOT_SUPPORTED.
 */
#ifdef __ANDROID__

#include "objit/ob_llvm_helper.h"
#include "lib/ob_errno.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace jit {

// ObLLVMType
int ObLLVMType::get_pointer_to(ObLLVMType &) { return OB_NOT_SUPPORTED; }
int ObLLVMType::get_pointee_type(ObLLVMType &) { return OB_NOT_SUPPORTED; }
int ObLLVMType::same_as(ObLLVMType &, bool &same) { same = false; return OB_NOT_SUPPORTED; }
int64_t ObLLVMType::get_id() const { return 0; }
int64_t ObLLVMType::get_width() const { return 0; }
int64_t ObLLVMType::get_num_child() const { return 0; }
ObLLVMType ObLLVMType::get_child(int64_t) const { return ObLLVMType(); }

// ObLLVMValue
int ObLLVMValue::get_type(ObLLVMType &) const { return OB_NOT_SUPPORTED; }
int ObLLVMValue::set_name(const ObString &) { return OB_NOT_SUPPORTED; }
ObLLVMType ObLLVMValue::get_type() const { return ObLLVMType(); }
int64_t ObLLVMValue::get_type_id() const { return 0; }
int ObLLVMValue::get_pointee_type(ObLLVMType &) { return OB_NOT_SUPPORTED; }

// ObLLVMArrayType
int ObLLVMArrayType::get(const ObLLVMType &, uint64_t, ObLLVMType &) { return OB_NOT_SUPPORTED; }

// ObLLVMConstant
int ObLLVMConstant::get_null_value(const ObLLVMType &, ObLLVMConstant &) { return OB_NOT_SUPPORTED; }

// ObLLVMConstantStruct
int ObLLVMConstantStruct::get(ObLLVMStructType &, ObIArray<ObLLVMConstant> &, ObLLVMConstant &) { return OB_NOT_SUPPORTED; }

// ObLLVMGlobalVariable
int ObLLVMGlobalVariable::set_constant() { return OB_NOT_SUPPORTED; }
int ObLLVMGlobalVariable::set_initializer(ObLLVMConstant &) { return OB_NOT_SUPPORTED; }

// ObLLVMFunctionType
int ObLLVMFunctionType::get(const ObLLVMType &, ObIArray<ObLLVMType> &, ObLLVMFunctionType &) { return OB_NOT_SUPPORTED; }

// ObLLVMFunction
int ObLLVMFunction::set_personality(ObLLVMFunction &) { return OB_NOT_SUPPORTED; }
int ObLLVMFunction::get_argument_size(int64_t &size) { size = 0; return OB_NOT_SUPPORTED; }
int ObLLVMFunction::get_argument(int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// ObLLVMBasicBlock
bool ObLLVMBasicBlock::is_terminated() { return false; }

// ObLLVMLandingPad
int ObLLVMLandingPad::set_cleanup() { return OB_NOT_SUPPORTED; }
int ObLLVMLandingPad::add_clause(ObLLVMConstant &) { return OB_NOT_SUPPORTED; }

// ObLLVMSwitch
int ObLLVMSwitch::add_case(const ObLLVMValue &, ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }

// ObDWARFHelper
ObDWARFHelper::~ObDWARFHelper() {}
int ObDWARFHelper::dump(char*, int64_t) { return OB_NOT_SUPPORTED; }

// ObLLVMHelper
ObLLVMHelper::~ObLLVMHelper() {}
int ObLLVMHelper::init() { return OB_NOT_SUPPORTED; }
void ObLLVMHelper::final() {}
int ObLLVMHelper::initialize() { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::compile_module(ObPLOptLevel) { return OB_NOT_SUPPORTED; }
void ObLLVMHelper::dump_module() {}
int ObLLVMHelper::verify_module() { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_function_address(const ObString &, uint64_t &) { return OB_NOT_SUPPORTED; }
void ObLLVMHelper::add_symbol(const ObString &, void *) {}
ObDIRawData ObLLVMHelper::get_debug_info() const { return ObDIRawData(); }
const ObString &ObLLVMHelper::get_compiled_object() { static ObString s; return s; }
int ObLLVMHelper::add_compiled_object(size_t, const char *) { return OB_NOT_SUPPORTED; }

// Control flow
int ObLLVMHelper::create_br(const ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_cond_br(ObLLVMValue &, ObLLVMBasicBlock &, ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_call(const ObString &, ObLLVMFunction &, ObIArray<ObLLVMValue> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_call(const ObString &, ObLLVMFunction &, ObIArray<ObLLVMValue> &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_call(const ObString &, ObLLVMFunction &, const ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_invoke(const ObString &, ObLLVMFunction &, ObIArray<ObLLVMValue> &, const ObLLVMBasicBlock &, const ObLLVMBasicBlock &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_invoke(const ObString &, ObLLVMFunction &, ObIArray<ObLLVMValue> &, const ObLLVMBasicBlock &, const ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_invoke(const ObString &, ObLLVMFunction &, ObLLVMValue &, const ObLLVMBasicBlock &, const ObLLVMBasicBlock &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_invoke(const ObString &, ObLLVMFunction &, ObLLVMValue &, const ObLLVMBasicBlock &, const ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }

// Memory
int ObLLVMHelper::create_alloca(const ObString &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_store(const ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_load(const ObString &, ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_ialloca(const ObString &, ObObjType, int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_istore(int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// Comparisons
int ObLLVMHelper::create_icmp_eq(ObLLVMValue &, int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_icmp(ObLLVMValue &, int64_t, CMPTYPE, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_icmp(ObLLVMValue &, ObLLVMValue &, CMPTYPE, ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// Arithmetic
int ObLLVMHelper::create_inc(ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_dec(ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_add(ObLLVMValue &, ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_add(ObLLVMValue &, int64_t &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_sub(ObLLVMValue &, ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_sub(ObLLVMValue &, int64_t &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_ret(ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// GEP
int ObLLVMHelper::create_gep(const ObString &, ObLLVMValue &, ObIArray<int64_t> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_gep(const ObString &, ObLLVMValue &, ObIArray<ObLLVMValue> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_gep(const ObString &, ObLLVMValue &, int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_gep(const ObString &, ObLLVMValue &, ObLLVMValue &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_extract_value(const ObString &, ObLLVMValue &, uint64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_const_gep1_64(const ObString &, ObLLVMValue &, uint64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// Casts
int ObLLVMHelper::create_ptr_to_int(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_int_to_ptr(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_bit_cast(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_pointer_cast(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_addr_space_cast(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_sext(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_sext_or_bitcast(const ObString &, const ObLLVMValue &, const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// Exception handling
int ObLLVMHelper::create_landingpad(const ObString &, ObLLVMType &, ObLLVMLandingPad &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_switch(ObLLVMValue &, ObLLVMBasicBlock &, ObLLVMSwitch &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_resume(ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_unreachable() { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_phi(const ObString &, ObLLVMType &, ObIArray<std::pair<ObLLVMValue, ObLLVMBasicBlock>> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_global_string(const ObString &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }

// Block/module management
int ObLLVMHelper::set_insert_point(const ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::set_insert_point(ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_or_insert_global(const ObString &, ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_null_const(const ObLLVMType &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_array_type(const ObLLVMType &, uint64_t, ObLLVMType &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_uint64_array(const ObIArray<uint64_t> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_int8_array(const ObIArray<int8_t> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_const_struct(ObLLVMType &, ObIArray<ObLLVMValue> &, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_function_type(ObLLVMType &, ObIArray<ObLLVMType> &, ObLLVMType &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_function(const ObString &, ObLLVMFunctionType &, ObLLVMFunction &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_block(const ObString &, ObLLVMFunction &, ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::create_struct_type(const ObString &, ObIArray<ObLLVMType> &, ObLLVMType &) { return OB_NOT_SUPPORTED; }

// Type helpers
int ObLLVMHelper::get_llvm_type(ObObjType, ObLLVMType &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_void_type(ObLLVMType &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_int8(int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_int16(int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_int32(int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_int64(int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_int_value(const ObLLVMType &, int64_t, ObLLVMValue &) { return OB_NOT_SUPPORTED; }
int ObLLVMHelper::get_insert_block(ObLLVMBasicBlock &) { return OB_NOT_SUPPORTED; }
int64 ObLLVMHelper::get_integer_type_id() { return 0; }
int64 ObLLVMHelper::get_pointer_type_id() { return 0; }
int64 ObLLVMHelper::get_struct_type_id() { return 0; }
int ObLLVMHelper::get_compiled_stack_size(uint64_t &) { return OB_NOT_SUPPORTED; }

} // namespace jit
} // namespace oceanbase

#endif // __ANDROID__
